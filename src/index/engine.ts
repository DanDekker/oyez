import { appendFile, mkdir } from "node:fs/promises";
import { join } from "node:path";
import { stripHtmlToText } from "../parser/xml.ts";
import type {
  Article,
  EngineStats,
  FacetCounts,
  FeedConfig,
  ParsedFeedItem,
  ScoredArticle,
  SearchQuery,
  SearchResult,
  StoryCluster,
} from "../types.ts";
import { extractArticleEntities } from "./entities.ts";
import { extractWeightedTermFrequencies, tokenize } from "./tokenizer.ts";

interface WalEntry {
  op: "upsert_article" | "delete_article" | "delete_feed_articles";
  article?: Article;
  articleId?: string;
  feedId?: string;
  ts: number;
}

interface SnapshotData {
  version: 1;
  savedAt: number;
  feeds: FeedConfig[];
  articles: Article[];
}

export function computeCanonicalHash(url: string, guid: string): string {
  let normalized = (url || guid).trim();
  try {
    if (normalized.startsWith("http://") || normalized.startsWith("https://")) {
      const u = new URL(normalized);
      u.hash = "";
      const trackingParams = [
        "utm_source",
        "utm_medium",
        "utm_campaign",
        "utm_term",
        "utm_content",
        "fbclid",
        "gclid",
        "mc_cid",
        "mc_eid",
        "ref",
      ];
      for (const p of trackingParams) {
        u.searchParams.delete(p);
      }
      normalized = u.toString().replace(/\/$/, "");
    }
  } catch {
    // Fallback to raw string
  }
  return Bun.hash(normalized).toString(36);
}

export function extractDomain(url: string): string {
  if (!url) return "unknown";
  try {
    const u = new URL(url);
    return u.hostname.replace(/^www\./i, "").toLowerCase();
  } catch {
    return "unknown";
  }
}

function addToSetMap(map: Map<string, Set<string>>, key: string, id: string): void {
  const clean = key.trim().toLowerCase();
  if (!clean) return;
  let set = map.get(clean);
  if (!set) {
    set = new Set();
    map.set(clean, set);
  }
  set.add(id);
}

function removeFromSetMap(
  map: Map<string, Set<string>>,
  key: string,
  id: string
): void {
  const clean = key.trim().toLowerCase();
  if (!clean) return;
  const set = map.get(clean);
  if (set) {
    set.delete(id);
    if (set.size === 0) map.delete(clean);
  }
}

function setMapToCounts(
  map: Map<string, Set<string>>,
  upperCaseKeys = false,
  maxEntries?: number
): Record<string, number> {
  const entries: Array<[string, number]> = [];
  for (const [k, set] of map.entries()) {
    if (set.size > 0) {
      entries.push([upperCaseKeys ? k.toUpperCase() : k, set.size]);
    }
  }
  entries.sort((a, b) => b[1] - a[1] || a[0].localeCompare(b[0]));
  const sliced = maxEntries ? entries.slice(0, maxEntries) : entries;
  return Object.fromEntries(sliced);
}

export class OyezIndexEngine {
  private readonly dataDir: string;
  private readonly feedsPath: string;
  private readonly walPath: string;
  private readonly snapshotPath: string;
  private readonly startedAt = Date.now();
  private lastSnapshotAt?: number;

  // Primary stores
  private feeds = new Map<string, FeedConfig>();
  private articles = new Map<string, Article>();
  private canonicalToArticleId = new Map<string, string>();

  // Proprietary Inverted Index: term -> Map<articleId, weightedTermFrequency>
  private invertedIndex = new Map<string, Map<string, number>>();
  private docLengths = new Map<string, number>();
  private totalDocLengthSum = 0;

  // Deep multi-dimensional secondary indexes
  private byFeed = new Map<string, Set<string>>();
  private byCategory = new Map<string, Set<string>>();
  private byRegion = new Map<string, Set<string>>();
  private byCountry = new Map<string, Set<string>>();
  private byMentionedCountry = new Map<string, Set<string>>();
  private byLanguage = new Map<string, Set<string>>();
  private byTopic = new Map<string, Set<string>>();
  private byEntity = new Map<string, Set<string>>();
  private byDomain = new Map<string, Set<string>>();
  private timeSortedIds: string[] = [];

  private walOpsSinceSnapshot = 0;

  constructor(dataDir = "./data") {
    this.dataDir = dataDir;
    this.feedsPath = join(dataDir, "feeds.json");
    this.walPath = join(dataDir, "articles.wal.jsonl");
    this.snapshotPath = join(dataDir, "snapshot.oyez.json");
  }

  async init(): Promise<void> {
    await mkdir(this.dataDir, { recursive: true });

    const snapshotFile = Bun.file(this.snapshotPath);
    if (await snapshotFile.exists()) {
      try {
        const snapshot = (await snapshotFile.json()) as SnapshotData;
        this.lastSnapshotAt = snapshot.savedAt;
        if (Array.isArray(snapshot.feeds)) {
          for (const f of snapshot.feeds) {
            this.feeds.set(f.id, this.normalizeFeedConfig(f));
          }
        }
        if (Array.isArray(snapshot.articles)) {
          for (const a of snapshot.articles) {
            this.indexArticleInMemory(this.normalizeArticle(a));
          }
        }
      } catch (err) {
        console.warn("[OyezEngine] Warning loading snapshot:", err);
      }
    }

    const feedsFile = Bun.file(this.feedsPath);
    if (await feedsFile.exists()) {
      try {
        const feedsList = (await feedsFile.json()) as FeedConfig[];
        if (Array.isArray(feedsList)) {
          this.feeds.clear();
          for (const f of feedsList) {
            this.feeds.set(f.id, this.normalizeFeedConfig(f));
          }
        }
      } catch (err) {
        console.warn("[OyezEngine] Warning loading feeds.json:", err);
      }
    }

    const walFile = Bun.file(this.walPath);
    if (await walFile.exists()) {
      try {
        const walText = await walFile.text();
        const lines = walText.split("\n");
        let replayed = 0;
        for (const line of lines) {
          const trimmed = line.trim();
          if (!trimmed) continue;
          const entry = JSON.parse(trimmed) as WalEntry;
          if (entry.op === "upsert_article" && entry.article) {
            this.indexArticleInMemory(this.normalizeArticle(entry.article));
            replayed++;
          } else if (entry.op === "delete_article" && entry.articleId) {
            this.removeArticleFromMemory(entry.articleId);
            replayed++;
          } else if (entry.op === "delete_feed_articles" && entry.feedId) {
            this.removeFeedArticlesFromMemory(entry.feedId);
            replayed++;
          }
        }
        if (replayed > 0) {
          await this.compactSnapshot();
        }
      } catch (err) {
        console.warn("[OyezEngine] Warning replaying WAL:", err);
      }
    }
  }

  private normalizeFeedConfig(feed: Partial<FeedConfig> & { id: string; url: string }): FeedConfig {
    const now = Date.now();
    return {
      id: feed.id,
      url: feed.url,
      title: feed.title || feed.url,
      description: feed.description,
      siteUrl: feed.siteUrl,
      region: feed.region ?? "global",
      country: (feed.country ?? "INT").toUpperCase(),
      language: (feed.language ?? "en").toLowerCase().slice(0, 2),
      category: (feed.category ?? "general").toLowerCase(),
      tags: Array.isArray(feed.tags) ? feed.tags : [],
      enabled: feed.enabled ?? true,
      pollIntervalSeconds: feed.pollIntervalSeconds ?? 900,
      scrapeFullContent: feed.scrapeFullContent ?? false,
      etag: feed.etag,
      lastModified: feed.lastModified,
      lastPolledAt: feed.lastPolledAt,
      lastSuccessAt: feed.lastSuccessAt,
      nextPollAt: feed.nextPollAt ?? now,
      consecutiveFailures: feed.consecutiveFailures ?? 0,
      lastError: feed.lastError,
      createdAt: feed.createdAt ?? now,
      updatedAt: feed.updatedAt ?? now,
    };
  }

  private normalizeArticle(article: Article): Article {
    const region = article.region ?? "global";
    const country = (article.country ?? "INT").toUpperCase();
    const language = (article.language ?? "en").toLowerCase().slice(0, 2);
    const entities =
      article.entities ??
      extractArticleEntities(
        article.title,
        article.summary,
        article.contentText,
        country
      );
    return {
      ...article,
      region,
      country,
      language,
      entities,
    };
  }

  private insertIntoTimeSorted(articleId: string, publishedAt: number): void {
    let low = 0;
    let high = this.timeSortedIds.length;
    while (low < high) {
      const mid = (low + high) >>> 1;
      const midArticle = this.articles.get(this.timeSortedIds[mid]);
      const midTime = midArticle?.publishedAt ?? 0;
      if (midTime > publishedAt) {
        low = mid + 1;
      } else {
        high = mid;
      }
    }
    this.timeSortedIds.splice(low, 0, articleId);
  }

  private removeFromTimeSorted(articleId: string): void {
    const idx = this.timeSortedIds.indexOf(articleId);
    if (idx !== -1) {
      this.timeSortedIds.splice(idx, 1);
    }
  }

  private getArticleWeightedFields(article: Article) {
    return [
      { text: article.title, weight: 3.5 },
      {
        text: [
          ...article.entities.organizationsAndPeople,
          ...article.entities.topics,
        ].join(" "),
        weight: 3.0,
      },
      { text: article.categories.join(" "), weight: 2.5 },
      { text: article.author ?? "", weight: 2.0 },
      { text: article.summary, weight: 1.5 },
      { text: article.contentText, weight: 1.0 },
    ];
  }

  private indexArticleInMemory(article: Article): void {
    if (this.articles.has(article.id)) {
      this.removeArticleFromMemory(article.id);
    }

    this.articles.set(article.id, article);
    this.canonicalToArticleId.set(article.canonicalHash, article.id);

    addToSetMap(this.byFeed, article.feedId, article.id);
    addToSetMap(this.byRegion, article.region, article.id);
    addToSetMap(this.byCountry, article.country, article.id);
    addToSetMap(this.byLanguage, article.language, article.id);
    if (article.domain) {
      addToSetMap(this.byDomain, article.domain, article.id);
    }

    for (const cat of article.categories) {
      addToSetMap(this.byCategory, cat, article.id);
    }
    for (const mc of article.entities.mentionedCountries) {
      addToSetMap(this.byMentionedCountry, mc, article.id);
    }
    for (const topic of article.entities.topics) {
      addToSetMap(this.byTopic, topic, article.id);
    }
    for (const ent of article.entities.organizationsAndPeople) {
      addToSetMap(this.byEntity, ent, article.id);
    }

    this.insertIntoTimeSorted(article.id, article.publishedAt);

    const { termFreqs, totalWeightedLength } = extractWeightedTermFrequencies(
      this.getArticleWeightedFields(article)
    );

    this.docLengths.set(article.id, totalWeightedLength);
    this.totalDocLengthSum += totalWeightedLength;

    for (const [term, weightedTf] of termFreqs.entries()) {
      let postings = this.invertedIndex.get(term);
      if (!postings) {
        postings = new Map();
        this.invertedIndex.set(term, postings);
      }
      postings.set(article.id, weightedTf);
    }
  }

  private removeArticleFromMemory(articleId: string): boolean {
    const existing = this.articles.get(articleId);
    if (!existing) return false;

    this.articles.delete(articleId);
    this.canonicalToArticleId.delete(existing.canonicalHash);

    removeFromSetMap(this.byFeed, existing.feedId, articleId);
    removeFromSetMap(this.byRegion, existing.region, articleId);
    removeFromSetMap(this.byCountry, existing.country, articleId);
    removeFromSetMap(this.byLanguage, existing.language, articleId);
    if (existing.domain) {
      removeFromSetMap(this.byDomain, existing.domain, articleId);
    }

    for (const cat of existing.categories) {
      removeFromSetMap(this.byCategory, cat, articleId);
    }
    for (const mc of existing.entities.mentionedCountries) {
      removeFromSetMap(this.byMentionedCountry, mc, articleId);
    }
    for (const topic of existing.entities.topics) {
      removeFromSetMap(this.byTopic, topic, articleId);
    }
    for (const ent of existing.entities.organizationsAndPeople) {
      removeFromSetMap(this.byEntity, ent, articleId);
    }

    this.removeFromTimeSorted(articleId);

    const prevLen = this.docLengths.get(articleId) ?? 0;
    this.totalDocLengthSum = Math.max(0, this.totalDocLengthSum - prevLen);
    this.docLengths.delete(articleId);

    const { termFreqs } = extractWeightedTermFrequencies(
      this.getArticleWeightedFields(existing)
    );

    for (const term of termFreqs.keys()) {
      const postings = this.invertedIndex.get(term);
      if (postings) {
        postings.delete(articleId);
        if (postings.size === 0) {
          this.invertedIndex.delete(term);
        }
      }
    }

    return true;
  }

  private removeFeedArticlesFromMemory(feedId: string): number {
    const ids = Array.from(this.byFeed.get(feedId.toLowerCase()) ?? []);
    for (const id of ids) {
      this.removeArticleFromMemory(id);
    }
    this.byFeed.delete(feedId.toLowerCase());
    return ids.length;
  }

  private async appendWal(entry: WalEntry): Promise<void> {
    await appendFile(this.walPath, JSON.stringify(entry) + "\n", "utf8");
    this.walOpsSinceSnapshot++;
    if (this.walOpsSinceSnapshot >= 400) {
      await this.compactSnapshot();
    }
  }

  async saveFeeds(): Promise<void> {
    const list = Array.from(this.feeds.values());
    await Bun.write(this.feedsPath, JSON.stringify(list, null, 2));
  }

  async compactSnapshot(): Promise<void> {
    const now = Date.now();
    const snapshot: SnapshotData = {
      version: 1,
      savedAt: now,
      feeds: Array.from(this.feeds.values()),
      articles: Array.from(this.articles.values()),
    };
    await Bun.write(this.snapshotPath, JSON.stringify(snapshot));
    await Bun.write(this.walPath, "");
    await this.saveFeeds();
    this.lastSnapshotAt = now;
    this.walOpsSinceSnapshot = 0;
  }

  listFeeds(): FeedConfig[] {
    return Array.from(this.feeds.values()).sort(
      (a, b) => b.createdAt - a.createdAt
    );
  }

  getFeed(id: string): FeedConfig | undefined {
    return this.feeds.get(id);
  }

  getFeedByUrl(url: string): FeedConfig | undefined {
    const normalized = url.trim().toLowerCase();
    for (const f of this.feeds.values()) {
      if (f.url.trim().toLowerCase() === normalized) return f;
    }
    return undefined;
  }

  async upsertFeed(feed: FeedConfig): Promise<FeedConfig> {
    const norm = this.normalizeFeedConfig(feed);
    this.feeds.set(norm.id, norm);
    await this.saveFeeds();
    return norm;
  }

  async deleteFeed(feedId: string): Promise<boolean> {
    const existed = this.feeds.delete(feedId);
    if (!existed) return false;
    this.removeFeedArticlesFromMemory(feedId);
    await this.saveFeeds();
    await this.appendWal({
      op: "delete_feed_articles",
      feedId,
      ts: Date.now(),
    });
    return true;
  }

  getFeedArticleCount(feedId: string): number {
    return this.byFeed.get(feedId.toLowerCase())?.size ?? 0;
  }

  async ingestFeedItems(
    feed: FeedConfig,
    items: ParsedFeedItem[]
  ): Promise<{ added: Article[]; updated: Article[] }> {
    const added: Article[] = [];
    const updated: Article[] = [];
    const now = Date.now();

    for (const item of items) {
      const canonicalHash = computeCanonicalHash(item.url, item.guid);
      const existingId = this.canonicalToArticleId.get(canonicalHash);
      const existing = existingId ? this.articles.get(existingId) : undefined;

      const contentText = existing?.fullContentExtracted
        ? existing.contentText
        : stripHtmlToText(item.contentHtml || item.summary);
      const words = contentText
        ? contentText.split(/\s+/).filter(Boolean).length
        : 0;

      const entities = extractArticleEntities(
        item.title,
        item.summary,
        contentText || item.summary,
        feed.country
      );

      const categories = Array.from(
        new Set(
          [
            feed.category,
            ...feed.tags,
            ...item.categories,
            ...entities.topics,
          ]
            .map((c) => c.trim().toLowerCase())
            .filter(Boolean)
        )
      );

      const article: Article = {
        id: existing ? existing.id : `art_${canonicalHash}`,
        feedId: feed.id,
        feedTitle: feed.title,
        guid: item.guid,
        url: item.url,
        canonicalHash,
        domain: extractDomain(item.url || feed.siteUrl || feed.url),
        region: feed.region ?? "global",
        country: (feed.country ?? "INT").toUpperCase(),
        language: (feed.language ?? "en").toLowerCase().slice(0, 2),
        title: item.title,
        summary: item.summary,
        contentHtml: item.contentHtml ?? existing?.contentHtml,
        contentText: contentText || item.summary,
        markdownContent: existing?.markdownContent,
        author: item.author ?? existing?.author,
        categories,
        entities,
        imageUrl: item.imageUrl ?? existing?.imageUrl,
        publishedAt: item.publishedAt || existing?.publishedAt || now,
        ingestedAt: existing ? existing.ingestedAt : now,
        wordCount: existing?.fullContentExtracted ? existing.wordCount : words,
        readingTimeMinutes: existing?.fullContentExtracted
          ? existing.readingTimeMinutes
          : Math.max(1, Math.ceil(words / 220)),
        fullContentExtracted: existing?.fullContentExtracted ?? false,
      };

      this.indexArticleInMemory(article);
      await this.appendWal({
        op: "upsert_article",
        article,
        ts: now,
      });

      if (existing) {
        updated.push(article);
      } else {
        added.push(article);
      }
    }

    return { added, updated };
  }

  async upsertArticle(article: Article): Promise<Article> {
    const enriched = this.normalizeArticle({
      ...article,
      entities: extractArticleEntities(
        article.title,
        article.summary,
        article.contentText,
        article.country
      ),
    });
    this.indexArticleInMemory(enriched);
    await this.appendWal({
      op: "upsert_article",
      article: enriched,
      ts: Date.now(),
    });
    return enriched;
  }

  getArticle(id: string): Article | undefined {
    return this.articles.get(id);
  }

  async deleteArticle(id: string): Promise<boolean> {
    const removed = this.removeArticleFromMemory(id);
    if (removed) {
      await this.appendWal({
        op: "delete_article",
        articleId: id,
        ts: Date.now(),
      });
    }
    return removed;
  }

  /**
   * Proprietary Okapi BM25 + Geopolitical & Entity Faceted Search Engine
   */
  search(query: SearchQuery): SearchResult {
    const startTime = performance.now();
    const limit = Math.min(Math.max(query.limit ?? 25, 1), 200);
    const offset = Math.max(query.offset ?? 0, 0);

    let candidateIds: Set<string> | null = null;

    const intersectWith = (set: Set<string>) => {
      if (candidateIds === null) {
        candidateIds = new Set(set);
      } else {
        for (const id of candidateIds) {
          if (!set.has(id)) {
            candidateIds.delete(id);
          }
        }
      }
    };

    if (query.feedId) {
      intersectWith(
        this.byFeed.get(query.feedId.trim().toLowerCase()) ?? new Set()
      );
    }
    if (query.category) {
      intersectWith(
        this.byCategory.get(query.category.trim().toLowerCase()) ?? new Set()
      );
    }
    if (query.region) {
      intersectWith(
        this.byRegion.get(query.region.trim().toLowerCase()) ?? new Set()
      );
    }
    if (query.country) {
      intersectWith(
        this.byCountry.get(query.country.trim().toLowerCase()) ?? new Set()
      );
    }
    if (query.mentionedCountry) {
      intersectWith(
        this.byMentionedCountry.get(
          query.mentionedCountry.trim().toLowerCase()
        ) ?? new Set()
      );
    }
    if (query.language) {
      intersectWith(
        this.byLanguage.get(query.language.trim().toLowerCase()) ?? new Set()
      );
    }
    if (query.topic) {
      intersectWith(
        this.byTopic.get(query.topic.trim().toLowerCase()) ?? new Set()
      );
    }
    if (query.entity) {
      intersectWith(
        this.byEntity.get(query.entity.trim().toLowerCase()) ?? new Set()
      );
    }
    if (query.domain) {
      const normDom = query.domain.toLowerCase().replace(/^www\./, "").trim();
      intersectWith(this.byDomain.get(normDom) ?? new Set());
    }

    const hasTextQuery = Boolean(query.q && query.q.trim().length > 0);
    const sortMode = query.sort ?? (hasTextQuery ? "relevance" : "date");

    if (!hasTextQuery) {
      const matched: ScoredArticle[] = [];
      for (const id of this.timeSortedIds) {
        if (candidateIds !== null && !candidateIds.has(id)) continue;
        const art = this.articles.get(id);
        if (!art) continue;
        if (query.from !== undefined && art.publishedAt < query.from) continue;
        if (query.to !== undefined && art.publishedAt > query.to) continue;
        matched.push({ article: art, score: 1.0, matchedTerms: [] });
      }

      const paged = matched.slice(offset, offset + limit);
      return {
        total: matched.length,
        limit,
        offset,
        queryTimeMs: Number((performance.now() - startTime).toFixed(3)),
        results: paged,
      };
    }

    const rawQuery = query.q!.trim();
    const queryTokens = tokenize(rawQuery);
    const rawPhraseLower = rawQuery.toLowerCase();

    const N = this.articles.size;
    if (N === 0 || queryTokens.length === 0) {
      return {
        total: 0,
        limit,
        offset,
        queryTimeMs: Number((performance.now() - startTime).toFixed(3)),
        results: [],
      };
    }

    const avgdl = Math.max(1, this.totalDocLengthSum / N);
    const k1 = 1.5;
    const b = 0.75;

    const scores = new Map<string, number>();
    const matchedTermsMap = new Map<string, Set<string>>();

    for (const qTerm of queryTokens) {
      const exactPostings = this.invertedIndex.get(qTerm);
      if (exactPostings) {
        const df = exactPostings.size;
        const idf = Math.log(1 + (N - df + 0.5) / (df + 0.5));

        for (const [docId, tf] of exactPostings.entries()) {
          if (candidateIds !== null && !candidateIds.has(docId)) continue;
          const dl = this.docLengths.get(docId) ?? avgdl;
          const bm25 =
            idf * ((tf * (k1 + 1)) / (tf + k1 * (1 - b + b * (dl / avgdl))));
          scores.set(docId, (scores.get(docId) ?? 0) + bm25);
          let termSet = matchedTermsMap.get(docId);
          if (!termSet) {
            termSet = new Set();
            matchedTermsMap.set(docId, termSet);
          }
          termSet.add(qTerm);
        }
      }

      if (qTerm.length >= 3) {
        for (const [vocabTerm, postings] of this.invertedIndex.entries()) {
          if (vocabTerm === qTerm || !vocabTerm.startsWith(qTerm)) continue;
          const df = postings.size;
          const idf = Math.log(1 + (N - df + 0.5) / (df + 0.5)) * 0.45;

          for (const [docId, tf] of postings.entries()) {
            if (candidateIds !== null && !candidateIds.has(docId)) continue;
            const dl = this.docLengths.get(docId) ?? avgdl;
            const bm25 =
              idf * ((tf * (k1 + 1)) / (tf + k1 * (1 - b + b * (dl / avgdl))));
            scores.set(docId, (scores.get(docId) ?? 0) + bm25);
            let termSet = matchedTermsMap.get(docId);
            if (!termSet) {
              termSet = new Set();
              matchedTermsMap.set(docId, termSet);
            }
            termSet.add(vocabTerm);
          }
        }
      }
    }

    const scoredResults: ScoredArticle[] = [];
    for (const [docId, baseScore] of scores.entries()) {
      const article = this.articles.get(docId);
      if (!article) continue;
      if (query.from !== undefined && article.publishedAt < query.from) continue;
      if (query.to !== undefined && article.publishedAt > query.to) continue;

      let finalScore = baseScore;
      if (rawPhraseLower.length >= 3) {
        if (article.title.toLowerCase().includes(rawPhraseLower)) {
          finalScore *= 1.45;
        } else if (article.summary.toLowerCase().includes(rawPhraseLower)) {
          finalScore *= 1.2;
        }
      }

      const matchedCount = matchedTermsMap.get(docId)?.size ?? 1;
      if (queryTokens.length > 1 && matchedCount > 1) {
        finalScore *= 1 + 0.15 * (matchedCount - 1);
      }

      scoredResults.push({
        article,
        score: Number(finalScore.toFixed(4)),
        matchedTerms: Array.from(matchedTermsMap.get(docId) ?? []),
      });
    }

    if (sortMode === "date") {
      scoredResults.sort(
        (a, b) =>
          b.article.publishedAt - a.article.publishedAt || b.score - a.score
      );
    } else {
      scoredResults.sort(
        (a, b) =>
          b.score - a.score || b.article.publishedAt - a.article.publishedAt
      );
    }

    const paged = scoredResults.slice(offset, offset + limit);

    return {
      total: scoredResults.length,
      limit,
      offset,
      queryTimeMs: Number((performance.now() - startTime).toFixed(3)),
      results: paged,
    };
  }

  /**
   * Find related articles across the world covering the same story or entities
   */
  findRelatedArticles(articleId: string, limit = 8): ScoredArticle[] {
    const source = this.articles.get(articleId);
    if (!source) return [];

    const queryText = [
      source.title,
      ...source.entities.organizationsAndPeople,
      ...source.entities.topics,
    ].join(" ");

    const rawRes = this.search({
      q: queryText,
      limit: limit + 10,
      sort: "relevance",
    });

    const filtered: ScoredArticle[] = [];
    for (const item of rawRes.results) {
      if (item.article.id === articleId) continue;
      // Boost cross-country / cross-feed perspective
      let score = item.score;
      if (item.article.feedId !== source.feedId) {
        score *= 1.15;
      }
      if (item.article.country !== source.country) {
        score *= 1.1;
      }
      filtered.push({
        article: item.article,
        score: Number(score.toFixed(4)),
        matchedTerms: item.matchedTerms,
      });
    }

    filtered.sort((a, b) => b.score - a.score);
    return filtered.slice(0, limit);
  }

  /**
   * Discover multi-source global story clusters across recent articles
   */
  discoverStoryClusters(maxClusters = 15, recentPoolSize = 400): StoryCluster[] {
    const recentIds = this.timeSortedIds.slice(0, recentPoolSize);
    const assigned = new Set<string>();
    const clusters: StoryCluster[] = [];

    // Pre-tokenize titles + entities for fast Jaccard/IDF overlap
    const docFeatures = new Map<string, Set<string>>();
    for (const id of recentIds) {
      const art = this.articles.get(id);
      if (!art) continue;
      const tokens = tokenize(
        `${art.title} ${art.entities.organizationsAndPeople.join(" ")}`
      );
      docFeatures.set(id, new Set(tokens));
    }

    for (let i = 0; i < recentIds.length; i++) {
      const seedId = recentIds[i];
      if (assigned.has(seedId)) continue;
      const seedArt = this.articles.get(seedId);
      const seedFeat = docFeatures.get(seedId);
      if (!seedArt || !seedFeat || seedFeat.size < 3) continue;

      const members: Article[] = [seedArt];
      const sharedCounts = new Map<string, number>();
      for (const t of seedFeat) sharedCounts.set(t, 1);

      for (let j = i + 1; j < recentIds.length; j++) {
        const candId = recentIds[j];
        if (assigned.has(candId)) continue;
        const candArt = this.articles.get(candId);
        const candFeat = docFeatures.get(candId);
        if (!candArt || !candFeat || candFeat.size < 3) continue;

        let intersection = 0;
        for (const t of candFeat) {
          if (seedFeat.has(t)) intersection++;
        }

        const minSize = Math.min(seedFeat.size, candFeat.size);
        const overlapRatio = intersection / minSize;
        if (intersection >= 3 && overlapRatio >= 0.42) {
          members.push(candArt);
          for (const t of candFeat) {
            if (seedFeat.has(t)) {
              sharedCounts.set(t, (sharedCounts.get(t) ?? 0) + 1);
            }
          }
        }
      }

      if (members.length >= 2) {
        for (const m of members) assigned.add(m.id);
        const sources = Array.from(new Set(members.map((m) => m.feedTitle)));
        const regions = Array.from(new Set(members.map((m) => m.region)));
        const countries = Array.from(new Set(members.map((m) => m.country)));
        const topics = Array.from(
          new Set(members.flatMap((m) => m.entities.topics))
        );
        const sharedTerms = Array.from(sharedCounts.entries())
          .filter(([, c]) => c >= 2)
          .sort((a, b) => b[1] - a[1])
          .slice(0, 6)
          .map(([t]) => t);

        clusters.push({
          id: `cluster_${seedArt.canonicalHash}`,
          headline: seedArt.title,
          leadArticleId: seedArt.id,
          articleCount: members.length,
          sources,
          regions,
          countries,
          topics,
          sharedTerms,
          latestPublishedAt: Math.max(...members.map((m) => m.publishedAt)),
          articles: members.slice(0, 10).map((m) => ({
            id: m.id,
            title: m.title,
            feedTitle: m.feedTitle,
            country: m.country,
            region: m.region,
            url: m.url,
            publishedAt: m.publishedAt,
          })),
        });
      }
    }

    clusters.sort(
      (a, b) =>
        b.sources.length - a.sources.length ||
        b.articleCount - a.articleCount ||
        b.latestPublishedAt - a.latestPublishedAt
    );

    return clusters.slice(0, maxClusters);
  }

  getFacets(): FacetCounts {
    return {
      regions: setMapToCounts(this.byRegion),
      countries: setMapToCounts(this.byCountry, true),
      mentionedCountries: setMapToCounts(this.byMentionedCountry, true, 40),
      categories: setMapToCounts(this.byCategory, false, 40),
      topics: setMapToCounts(this.byTopic),
      languages: setMapToCounts(this.byLanguage),
      domains: setMapToCounts(this.byDomain, false, 40),
      topEntities: setMapToCounts(this.byEntity, false, 40),
    };
  }

  getStats(): EngineStats {
    let totalPostings = 0;
    for (const postings of this.invertedIndex.values()) {
      totalPostings += postings.size;
    }

    let enabledFeeds = 0;
    for (const f of this.feeds.values()) {
      if (f.enabled) enabledFeeds++;
    }

    const totalArticles = this.articles.size;
    const avgDocLength =
      totalArticles > 0
        ? Number((this.totalDocLengthSum / totalArticles).toFixed(2))
        : 0;

    const facets = this.getFacets();

    return {
      totalFeeds: this.feeds.size,
      enabledFeeds,
      totalArticles,
      vocabularySize: this.invertedIndex.size,
      totalPostings,
      averageDocumentLength: avgDocLength,
      ...facets,
      dataDir: this.dataDir,
      lastSnapshotAt: this.lastSnapshotAt,
      uptimeSeconds: Math.floor((Date.now() - this.startedAt) / 1000),
    };
  }
}
