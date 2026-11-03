import { mkdir, appendFile } from "node:fs/promises";
import { join } from "node:path";
import type {
  Article,
  EngineStats,
  FeedConfig,
  ParsedFeedItem,
  ScoredArticle,
  SearchQuery,
  SearchResult,
} from "../types.ts";
import { stripHtmlToText } from "../parser/xml.ts";
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
      // Strip common tracking query parameters
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
  // Per-document total weighted token length (for BM25 length normalization)
  private docLengths = new Map<string, number>();
  private totalDocLengthSum = 0;

  // Secondary indexes
  private byFeed = new Map<string, Set<string>>();
  private byCategory = new Map<string, Set<string>>();
  private byDomain = new Map<string, Set<string>>();
  // Maintained sorted in descending order of article.publishedAt
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

    // 1. Load snapshot if present
    const snapshotFile = Bun.file(this.snapshotPath);
    if (await snapshotFile.exists()) {
      try {
        const snapshot = (await snapshotFile.json()) as SnapshotData;
        this.lastSnapshotAt = snapshot.savedAt;
        if (Array.isArray(snapshot.feeds)) {
          for (const f of snapshot.feeds) {
            this.feeds.set(f.id, f);
          }
        }
        if (Array.isArray(snapshot.articles)) {
          for (const a of snapshot.articles) {
            this.indexArticleInMemory(a);
          }
        }
      } catch (err) {
        console.warn("[OyezEngine] Warning loading snapshot:", err);
      }
    }

    // 2. Load feeds.json override if newer
    const feedsFile = Bun.file(this.feedsPath);
    if (await feedsFile.exists()) {
      try {
        const feedsList = (await feedsFile.json()) as FeedConfig[];
        if (Array.isArray(feedsList)) {
          this.feeds.clear();
          for (const f of feedsList) {
            this.feeds.set(f.id, f);
          }
        }
      } catch (err) {
        console.warn("[OyezEngine] Warning loading feeds.json:", err);
      }
    }

    // 3. Replay WAL entries
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
            this.indexArticleInMemory(entry.article);
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

  private indexArticleInMemory(article: Article): void {
    // If replacing an existing article, unindex old version first
    if (this.articles.has(article.id)) {
      this.removeArticleFromMemory(article.id);
    }

    this.articles.set(article.id, article);
    this.canonicalToArticleId.set(article.canonicalHash, article.id);

    // Secondary index: feed
    let feedSet = this.byFeed.get(article.feedId);
    if (!feedSet) {
      feedSet = new Set();
      this.byFeed.set(article.feedId, feedSet);
    }
    feedSet.add(article.id);

    // Secondary index: categories
    for (const cat of article.categories) {
      const normCat = cat.toLowerCase().trim();
      if (!normCat) continue;
      let catSet = this.byCategory.get(normCat);
      if (!catSet) {
        catSet = new Set();
        this.byCategory.set(normCat, catSet);
      }
      catSet.add(article.id);
    }

    // Secondary index: domain
    if (article.domain) {
      let domSet = this.byDomain.get(article.domain);
      if (!domSet) {
        domSet = new Set();
        this.byDomain.set(article.domain, domSet);
      }
      domSet.add(article.id);
    }

    // Secondary index: time-sorted
    this.insertIntoTimeSorted(article.id, article.publishedAt);

    // Inverted index: field-weighted tokenization
    const { termFreqs, totalWeightedLength } = extractWeightedTermFrequencies([
      { text: article.title, weight: 3.5 },
      { text: article.categories.join(" "), weight: 2.5 },
      { text: article.author ?? "", weight: 2.0 },
      { text: article.summary, weight: 1.5 },
      { text: article.contentText, weight: 1.0 },
    ]);

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

    this.byFeed.get(existing.feedId)?.delete(articleId);
    for (const cat of existing.categories) {
      const normCat = cat.toLowerCase().trim();
      this.byCategory.get(normCat)?.delete(articleId);
    }
    if (existing.domain) {
      this.byDomain.get(existing.domain)?.delete(articleId);
    }
    this.removeFromTimeSorted(articleId);

    const prevLen = this.docLengths.get(articleId) ?? 0;
    this.totalDocLengthSum = Math.max(0, this.totalDocLengthSum - prevLen);
    this.docLengths.delete(articleId);

    const { termFreqs } = extractWeightedTermFrequencies([
      { text: existing.title, weight: 3.5 },
      { text: existing.categories.join(" "), weight: 2.5 },
      { text: existing.author ?? "", weight: 2.0 },
      { text: existing.summary, weight: 1.5 },
      { text: existing.contentText, weight: 1.0 },
    ]);

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
    const ids = Array.from(this.byFeed.get(feedId) ?? []);
    for (const id of ids) {
      this.removeArticleFromMemory(id);
    }
    this.byFeed.delete(feedId);
    return ids.length;
  }

  private async appendWal(entry: WalEntry): Promise<void> {
    await appendFile(this.walPath, JSON.stringify(entry) + "\n", "utf8");
    this.walOpsSinceSnapshot++;
    if (this.walOpsSinceSnapshot >= 250) {
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

  // Feed CRUD
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
    this.feeds.set(feed.id, feed);
    await this.saveFeeds();
    return feed;
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
    return this.byFeed.get(feedId)?.size ?? 0;
  }

  // Article Ingest & Upsert
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
      const words = contentText ? contentText.split(/\s+/).filter(Boolean).length : 0;

      const categories = Array.from(
        new Set(
          [feed.category, ...feed.tags, ...item.categories]
            .map((c) => c.trim())
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
        title: item.title,
        summary: item.summary,
        contentHtml: item.contentHtml ?? existing?.contentHtml,
        contentText: contentText || item.summary,
        markdownContent: existing?.markdownContent,
        author: item.author ?? existing?.author,
        categories,
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
    this.indexArticleInMemory(article);
    await this.appendWal({
      op: "upsert_article",
      article,
      ts: Date.now(),
    });
    return article;
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
   * Proprietary Okapi BM25 + Secondary Index Search Engine
   */
  search(query: SearchQuery): SearchResult {
    const startTime = performance.now();
    const limit = Math.min(Math.max(query.limit ?? 25, 1), 200);
    const offset = Math.max(query.offset ?? 0, 0);

    // 1. Build candidate set from secondary indexes if filters are provided
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
      intersectWith(this.byFeed.get(query.feedId) ?? new Set());
    }
    if (query.category) {
      const normCat = query.category.toLowerCase().trim();
      intersectWith(this.byCategory.get(normCat) ?? new Set());
    }
    if (query.domain) {
      const normDom = query.domain.toLowerCase().replace(/^www\./, "").trim();
      intersectWith(this.byDomain.get(normDom) ?? new Set());
    }

    const hasTextQuery = Boolean(query.q && query.q.trim().length > 0);
    const sortMode = query.sort ?? (hasTextQuery ? "relevance" : "date");

    // 2. If no text query, use time-sorted index directly
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

    // 3. Full-text Okapi BM25 scoring
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
      // Exact stem postings
      const exactPostings = this.invertedIndex.get(qTerm);
      if (exactPostings) {
        const df = exactPostings.size;
        const idf = Math.log(1 + (N - df + 0.5) / (df + 0.5));

        for (const [docId, tf] of exactPostings.entries()) {
          if (candidateIds !== null && !candidateIds.has(docId)) continue;
          const dl = this.docLengths.get(docId) ?? avgdl;
          const numerator = tf * (k1 + 1);
          const denominator = tf + k1 * (1 - b + b * (dl / avgdl));
          const bm25 = idf * (numerator / denominator);

          scores.set(docId, (scores.get(docId) ?? 0) + bm25);
          let termSet = matchedTermsMap.get(docId);
          if (!termSet) {
            termSet = new Set();
            matchedTermsMap.set(docId, termSet);
          }
          termSet.add(qTerm);
        }
      }

      // Prefix expansion for terms >= 3 chars
      if (qTerm.length >= 3) {
        for (const [vocabTerm, postings] of this.invertedIndex.entries()) {
          if (vocabTerm === qTerm || !vocabTerm.startsWith(qTerm)) continue;
          const df = postings.size;
          const idf = Math.log(1 + (N - df + 0.5) / (df + 0.5)) * 0.45; // Discounted prefix weight

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

      // Phrase match boost if multi-token or exact substring in title/content
      if (rawPhraseLower.length >= 3) {
        if (article.title.toLowerCase().includes(rawPhraseLower)) {
          finalScore *= 1.45;
        } else if (article.summary.toLowerCase().includes(rawPhraseLower)) {
          finalScore *= 1.2;
        }
      }

      // Multi-term coverage boost (reward docs matching more unique query terms)
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

  getStats(): EngineStats {
    let totalPostings = 0;
    for (const postings of this.invertedIndex.values()) {
      totalPostings += postings.size;
    }

    const categories: Record<string, number> = {};
    for (const [cat, set] of this.byCategory.entries()) {
      if (set.size > 0) categories[cat] = set.size;
    }

    const domains: Record<string, number> = {};
    for (const [dom, set] of this.byDomain.entries()) {
      if (set.size > 0) domains[dom] = set.size;
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

    return {
      totalFeeds: this.feeds.size,
      enabledFeeds,
      totalArticles,
      vocabularySize: this.invertedIndex.size,
      totalPostings,
      averageDocumentLength: avgDocLength,
      categories,
      domains,
      dataDir: this.dataDir,
      lastSnapshotAt: this.lastSnapshotAt,
      uptimeSeconds: Math.floor((Date.now() - this.startedAt) / 1000),
    };
  }
}
