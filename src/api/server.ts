import { WORLD_FEED_CATALOG } from "../catalog/world_feeds.ts";
import {
  extractReadableContent,
  fetchAndExtractArticle,
} from "../extractor/readability.ts";
import { OyezIndexEngine } from "../index/engine.ts";
import { M1Tokenizer, tokenize } from "../index/tokenizer.ts";
import { FeedScheduler } from "../poller/scheduler.ts";
import type { Article, FeedConfig, WorldRegion } from "../types.ts";
import {
  errorResponse,
  jsonResponse,
  OyezRouter,
  readJsonBody,
} from "./router.ts";

export interface OyezApp {
  engine: OyezIndexEngine;
  scheduler: FeedScheduler;
  m1Tokenizer: M1Tokenizer;
  router: OyezRouter;
  fetch: (req: Request) => Promise<Response>;
}

export function createOyezApp(
  engine: OyezIndexEngine,
  scheduler?: FeedScheduler
): OyezApp {
  const activeScheduler = scheduler ?? new FeedScheduler(engine, 8);
  const m1Tokenizer = new M1Tokenizer();
  const router = new OyezRouter();

  router.get("/", () => {
    return jsonResponse({
      name: "Oyez",
      tagline:
        "Oyez! Oyez! Oyez! A global news API, RSS aggregator, and category-theoretic deep search engine for the people.",
      version: "0.3.0",
      endpoints: [
        "GET    /health",
        "GET    /api/v1/stats",
        "GET    /api/v1/facets",
        "GET    /api/v1/trending",
        "GET    /api/v1/ontology",
        "GET    /api/v1/concepts",
        "GET    /api/v1/pullback",
        "GET    /api/v1/centrality",
        "GET    /api/v1/catalog",
        "GET    /api/v1/feeds",
        "POST   /api/v1/feeds",
        "POST   /api/v1/feeds/seed",
        "POST   /api/v1/feeds/refresh",
        "GET    /api/v1/feeds/:id",
        "PATCH  /api/v1/feeds/:id",
        "DELETE /api/v1/feeds/:id",
        "POST   /api/v1/feeds/:id/refresh",
        "GET    /api/v1/articles",
        "GET    /api/v1/articles/:id",
        "GET    /api/v1/articles/:id/related",
        "GET    /api/v1/articles/:id/duplicates",
        "DELETE /api/v1/articles/:id",
        "POST   /api/v1/articles/:id/extract",
        "POST   /api/v1/extract",
        "POST   /api/v1/tokenize",
        "POST   /api/v1/snapshot",
      ],
    });
  });

  router.get("/health", () => {
    const stats = engine.getStats();
    return jsonResponse({
      status: "ok",
      uptimeSeconds: stats.uptimeSeconds,
      totalFeeds: stats.totalFeeds,
      totalArticles: stats.totalArticles,
      vocabularySize: stats.vocabularySize,
    });
  });

  router.get("/api/v1/stats", () => {
    return jsonResponse(engine.getStats());
  });

  router.get("/api/v1/facets", () => {
    return jsonResponse(engine.getFacets());
  });

  router.get("/api/v1/trending", ({ query }) => {
    const limit = Math.min(Math.max(Number(query.get("limit") ?? 15), 1), 50);
    const facets = engine.getFacets();
    const storyClusters = engine.discoverStoryClusters(limit);
    return jsonResponse({
      storyClusters,
      topNewsTypes: facets.newsTypes,
      topTopics: facets.topics,
      topOntologyCategories: facets.ontologyCategories,
      topEntities: facets.topEntities,
      topMentionedCountries: facets.mentionedCountries,
      coverageByRegion: facets.regions,
    });
  });

  router.get("/api/v1/ontology", ({ query }) => {
    const u = query.get("u") ?? undefined;
    const v = query.get("v") ?? undefined;
    return jsonResponse(engine.getOntologyAnalysis(u, v));
  });

  router.get("/api/v1/concepts", ({ query }) => {
    const minSupport = Math.max(1, Number(query.get("minSupport") ?? 2));
    const limit = Math.min(Math.max(Number(query.get("limit") ?? 20), 1), 60);
    const concepts = engine.getConceptLattice(minSupport, limit);
    return jsonResponse({
      totalConcepts: concepts.length,
      minSupport,
      concepts,
    });
  });

  router.get("/api/v1/pullback", ({ query }) => {
    const leftRegion = query.get("leftRegion") ?? undefined;
    const rightRegion = query.get("rightRegion") ?? undefined;
    const leftCountry = query.get("leftCountry") ?? undefined;
    const rightCountry = query.get("rightCountry") ?? undefined;
    const leftType = query.get("leftType") ?? undefined;
    const rightType = query.get("rightType") ?? undefined;
    const pivotRaw = query.get("pivot") ?? "topic";
    const pivot =
      pivotRaw === "topic" ||
      pivotRaw === "mentionedCountry" ||
      pivotRaw === "entity" ||
      pivotRaw === "ontologyCategory"
        ? pivotRaw
        : "topic";
    const limit = Math.min(Math.max(Number(query.get("limit") ?? 20), 1), 50);

    const witnesses = engine.computePullback(
      { region: leftRegion, country: leftCountry, newsType: leftType },
      { region: rightRegion, country: rightCountry, newsType: rightType },
      pivot,
      limit
    );

    return jsonResponse({
      pivotDimension: pivot,
      leftFiber: { region: leftRegion, country: leftCountry, newsType: leftType },
      rightFiber: {
        region: rightRegion,
        country: rightCountry,
        newsType: rightType,
      },
      totalWitnesses: witnesses.length,
      witnesses,
    });
  });

  router.get("/api/v1/centrality", ({ query }) => {
    const limit = Math.min(Math.max(Number(query.get("limit") ?? 15), 1), 50);
    return jsonResponse(engine.getPageRankCentrality(limit));
  });

  router.get("/api/v1/catalog", ({ query }) => {
    const region = query.get("region")?.toLowerCase();
    const country = query.get("country")?.toUpperCase();
    const category = query.get("category")?.toLowerCase();
    const language = query.get("language")?.toLowerCase();

    const filtered = WORLD_FEED_CATALOG.filter((s) => {
      if (region && s.region !== region) return false;
      if (country && s.country !== country) return false;
      if (category && s.category !== category) return false;
      if (language && s.language !== language) return false;
      return true;
    });

    const regionsSummary: Record<string, number> = {};
    const countriesSummary: Record<string, number> = {};
    for (const s of filtered) {
      regionsSummary[s.region] = (regionsSummary[s.region] ?? 0) + 1;
      countriesSummary[s.country] = (countriesSummary[s.country] ?? 0) + 1;
    }

    return jsonResponse({
      total: filtered.length,
      regions: regionsSummary,
      countries: countriesSummary,
      feeds: filtered,
    });
  });

  router.get("/api/v1/feeds", ({ query }) => {
    const regionFilter = query.get("region")?.toLowerCase();
    const countryFilter = query.get("country")?.toUpperCase();
    const categoryFilter = query.get("category")?.toLowerCase();

    const feeds = engine
      .listFeeds()
      .filter((f) => {
        if (regionFilter && f.region !== regionFilter) return false;
        if (countryFilter && f.country !== countryFilter) return false;
        if (categoryFilter && f.category !== categoryFilter) return false;
        return true;
      })
      .map((f) => ({
        ...f,
        articleCount: engine.getFeedArticleCount(f.id),
      }));
    return jsonResponse({ total: feeds.length, feeds });
  });

  router.post("/api/v1/feeds", async ({ req }) => {
    const body = await readJsonBody<{
      url?: string;
      title?: string;
      region?: WorldRegion;
      country?: string;
      language?: string;
      category?: string;
      tags?: string[];
      pollIntervalSeconds?: number;
      scrapeFullContent?: boolean;
      enabled?: boolean;
      pollNow?: boolean;
    }>(req);

    const rawUrl = (body.url ?? "").trim();
    if (
      !rawUrl ||
      (!rawUrl.startsWith("http://") && !rawUrl.startsWith("https://"))
    ) {
      return errorResponse("A valid http(s) feed 'url' is required", 400);
    }

    const existing = engine.getFeedByUrl(rawUrl);
    if (existing) {
      return errorResponse(
        `Feed with URL already exists (${existing.id})`,
        409,
        { feed: existing }
      );
    }

    const now = Date.now();
    const id = `feed_${Bun.hash(rawUrl).toString(36)}`;
    const pollIntervalSeconds = Math.max(
      60,
      Number(body.pollIntervalSeconds ?? 900)
    );

    const feed: FeedConfig = {
      id,
      url: rawUrl,
      title: (body.title ?? "").trim() || rawUrl,
      region: body.region ?? "global",
      country: (body.country ?? "INT").trim().toUpperCase(),
      language: (body.language ?? "en").trim().toLowerCase().slice(0, 2),
      category: (body.category ?? "general").trim().toLowerCase(),
      tags: Array.isArray(body.tags)
        ? body.tags.map((t) => String(t).trim()).filter(Boolean)
        : [],
      enabled: body.enabled ?? true,
      pollIntervalSeconds,
      scrapeFullContent: Boolean(body.scrapeFullContent ?? false),
      nextPollAt: now,
      consecutiveFailures: 0,
      createdAt: now,
      updatedAt: now,
    };

    await engine.upsertFeed(feed);

    let pollResult = undefined;
    if (body.pollNow !== false) {
      pollResult = await activeScheduler.pollFeed(feed.id);
    }

    const updatedFeed = engine.getFeed(feed.id) ?? feed;
    return jsonResponse(
      {
        feed: {
          ...updatedFeed,
          articleCount: engine.getFeedArticleCount(feed.id),
        },
        pollResult,
      },
      201
    );
  });

  router.post("/api/v1/feeds/seed", async ({ req }) => {
    let pollNow = false;
    let regionFilter: string | undefined;
    let countryFilter: string | undefined;
    let categoryFilter: string | undefined;

    try {
      const body = (await req.json()) as Record<string, unknown>;
      if (body && typeof body === "object") {
        if ("pollNow" in body) pollNow = Boolean(body.pollNow);
        if (typeof body.region === "string")
          regionFilter = body.region.toLowerCase();
        if (typeof body.country === "string")
          countryFilter = body.country.toUpperCase();
        if (typeof body.category === "string")
          categoryFilter = body.category.toLowerCase();
      }
    } catch {
      // Optional JSON body
    }

    const now = Date.now();
    const added: FeedConfig[] = [];

    for (const seed of WORLD_FEED_CATALOG) {
      if (regionFilter && seed.region !== regionFilter) continue;
      if (countryFilter && seed.country !== countryFilter) continue;
      if (categoryFilter && seed.category !== categoryFilter) continue;
      if (engine.getFeedByUrl(seed.url)) continue;

      const id = `feed_${Bun.hash(seed.url).toString(36)}`;
      const feed: FeedConfig = {
        id,
        url: seed.url,
        title: seed.title,
        region: seed.region,
        country: seed.country,
        language: seed.language,
        category: seed.category,
        tags: seed.tags,
        enabled: true,
        pollIntervalSeconds: 900,
        scrapeFullContent: false,
        nextPollAt: now,
        consecutiveFailures: 0,
        createdAt: now,
        updatedAt: now,
      };
      await engine.upsertFeed(feed);
      added.push(feed);
    }

    const pollResults = pollNow
      ? await activeScheduler.pollAll(true)
      : undefined;
    return jsonResponse(
      {
        addedCount: added.length,
        totalRegisteredFeeds: engine.listFeeds().length,
        feeds: added,
        pollResults,
      },
      201
    );
  });

  router.post("/api/v1/feeds/refresh", async () => {
    const results = await activeScheduler.pollAll(true);
    return jsonResponse({
      polledFeeds: results.length,
      results,
    });
  });

  router.get("/api/v1/feeds/:id", ({ params, query }) => {
    const feed = engine.getFeed(params.id);
    if (!feed) {
      return errorResponse(`Feed not found: ${params.id}`, 404);
    }
    const limit = Number(query.get("limit") ?? 20);
    const searchRes = engine.search({
      feedId: feed.id,
      limit,
      sort: "date",
    });
    return jsonResponse({
      feed: {
        ...feed,
        articleCount: engine.getFeedArticleCount(feed.id),
      },
      recentArticles: searchRes.results.map((r) => r.article),
    });
  });

  router.patch("/api/v1/feeds/:id", async ({ params, req }) => {
    const feed = engine.getFeed(params.id);
    if (!feed) {
      return errorResponse(`Feed not found: ${params.id}`, 404);
    }

    const body = await readJsonBody<{
      title?: string;
      region?: WorldRegion;
      country?: string;
      language?: string;
      category?: string;
      tags?: string[];
      enabled?: boolean;
      pollIntervalSeconds?: number;
      scrapeFullContent?: boolean;
    }>(req);

    const updated: FeedConfig = {
      ...feed,
      title: body.title !== undefined ? body.title.trim() : feed.title,
      region: body.region !== undefined ? body.region : feed.region,
      country:
        body.country !== undefined
          ? body.country.trim().toUpperCase()
          : feed.country,
      language:
        body.language !== undefined
          ? body.language.trim().toLowerCase().slice(0, 2)
          : feed.language,
      category:
        body.category !== undefined
          ? body.category.trim().toLowerCase()
          : feed.category,
      tags: Array.isArray(body.tags)
        ? body.tags.map((t) => String(t).trim()).filter(Boolean)
        : feed.tags,
      enabled:
        body.enabled !== undefined ? Boolean(body.enabled) : feed.enabled,
      pollIntervalSeconds:
        body.pollIntervalSeconds !== undefined
          ? Math.max(60, Number(body.pollIntervalSeconds))
          : feed.pollIntervalSeconds,
      scrapeFullContent:
        body.scrapeFullContent !== undefined
          ? Boolean(body.scrapeFullContent)
          : feed.scrapeFullContent,
      updatedAt: Date.now(),
    };

    await engine.upsertFeed(updated);
    return jsonResponse({
      feed: {
        ...updated,
        articleCount: engine.getFeedArticleCount(updated.id),
      },
    });
  });

  router.delete("/api/v1/feeds/:id", async ({ params }) => {
    const deleted = await engine.deleteFeed(params.id);
    if (!deleted) {
      return errorResponse(`Feed not found: ${params.id}`, 404);
    }
    return jsonResponse({ deleted: true, id: params.id });
  });

  router.post("/api/v1/feeds/:id/refresh", async ({ params }) => {
    const feed = engine.getFeed(params.id);
    if (!feed) {
      return errorResponse(`Feed not found: ${params.id}`, 404);
    }
    const result = await activeScheduler.pollFeed(feed.id);
    return jsonResponse({
      result,
      feed: engine.getFeed(feed.id),
    });
  });

  router.get("/api/v1/articles", ({ query }) => {
    const q = query.get("q") ?? undefined;
    const feedId = query.get("feedId") ?? undefined;
    const category = query.get("category") ?? undefined;
    const newsType = query.get("newsType") ?? undefined;
    const region = query.get("region") ?? undefined;
    const country = query.get("country") ?? undefined;
    const mentionedCountry = query.get("mentionedCountry") ?? undefined;
    const language = query.get("language") ?? undefined;
    const topic = query.get("topic") ?? undefined;
    const entity = query.get("entity") ?? undefined;
    const domain = query.get("domain") ?? undefined;
    const diversify = query.get("diversify") === "true";
    const mmrLambdaRaw = query.get("mmrLambda");
    const includeFacets = query.get("facets") === "true";
    const fromRaw = query.get("from");
    const toRaw = query.get("to");
    const sortRaw = query.get("sort");
    const limitRaw = query.get("limit");
    const offsetRaw = query.get("offset");

    const from = fromRaw
      ? Number.isNaN(Number(fromRaw))
        ? Date.parse(fromRaw)
        : Number(fromRaw)
      : undefined;
    const to = toRaw
      ? Number.isNaN(Number(toRaw))
        ? Date.parse(toRaw)
        : Number(toRaw)
      : undefined;

    const sort =
      sortRaw === "relevance" || sortRaw === "date" ? sortRaw : undefined;
    const limit = limitRaw ? Number(limitRaw) : 25;
    const offset = offsetRaw ? Number(offsetRaw) : 0;
    const mmrLambda = mmrLambdaRaw ? Number(mmrLambdaRaw) : undefined;

    const searchResult = engine.search({
      q,
      feedId,
      category,
      newsType,
      region,
      country,
      mentionedCountry,
      language,
      topic,
      entity,
      domain,
      diversify,
      mmrLambda,
      from: from && !Number.isNaN(from) ? from : undefined,
      to: to && !Number.isNaN(to) ? to : undefined,
      sort,
      limit,
      offset,
    });

    if (includeFacets) {
      searchResult.facets = engine.getFacets();
    }

    return jsonResponse(searchResult);
  });

  router.get("/api/v1/articles/:id", ({ params }) => {
    const article = engine.getArticle(params.id);
    if (!article) {
      return errorResponse(`Article not found: ${params.id}`, 404);
    }
    return jsonResponse({ article });
  });

  router.get("/api/v1/articles/:id/related", ({ params, query }) => {
    const article = engine.getArticle(params.id);
    if (!article) {
      return errorResponse(`Article not found: ${params.id}`, 404);
    }
    const limit = Number(query.get("limit") ?? 8);
    const related = engine.findRelatedArticles(article.id, limit);
    return jsonResponse({
      articleId: article.id,
      title: article.title,
      relatedCount: related.length,
      related,
    });
  });

  router.get("/api/v1/articles/:id/duplicates", ({ params, query }) => {
    const article = engine.getArticle(params.id);
    if (!article) {
      return errorResponse(`Article not found: ${params.id}`, 404);
    }
    const maxDistance = Number(query.get("maxDistance") ?? 14);
    const duplicates = engine.findNearDuplicates(article.id, maxDistance);
    return jsonResponse({
      articleId: article.id,
      simhash64: article.simhash64,
      maxHammingDistance: maxDistance,
      count: duplicates.length,
      duplicates,
    });
  });

  router.delete("/api/v1/articles/:id", async ({ params }) => {
    const deleted = await engine.deleteArticle(params.id);
    if (!deleted) {
      return errorResponse(`Article not found: ${params.id}`, 404);
    }
    return jsonResponse({ deleted: true, id: params.id });
  });

  router.post("/api/v1/articles/:id/extract", async ({ params }) => {
    const article = engine.getArticle(params.id);
    if (!article) {
      return errorResponse(`Article not found: ${params.id}`, 404);
    }
    if (!article.url || !article.url.startsWith("http")) {
      return errorResponse(
        "Article does not have a valid HTTP(S) URL to extract",
        400
      );
    }

    const extracted = await fetchAndExtractArticle(article.url);
    const updated: Article = {
      ...article,
      title: article.title || extracted.title || "Untitled",
      contentText: extracted.textContent || article.contentText,
      markdownContent: extracted.markdownContent,
      author: article.author || extracted.byline,
      imageUrl: article.imageUrl || extracted.leadImageUrl,
      wordCount: extracted.wordCount,
      readingTimeMinutes: extracted.readingTimeMinutes,
      fullContentExtracted: true,
    };

    const reindexed = await engine.upsertArticle(updated);
    return jsonResponse({
      article: reindexed,
      extracted,
    });
  });

  router.post("/api/v1/extract", async ({ req }) => {
    const body = await readJsonBody<{ url?: string; html?: string }>(req);
    if (body.html && typeof body.html === "string") {
      const extracted = extractReadableContent(body.html, body.url);
      return jsonResponse({ extracted });
    }
    if (body.url && typeof body.url === "string") {
      const extracted = await fetchAndExtractArticle(body.url.trim());
      return jsonResponse({ extracted });
    }
    return errorResponse("Provide either 'url' or 'html' in request body", 400);
  });

  router.post("/api/v1/tokenize", async ({ req }) => {
    const body = await readJsonBody<{ text?: string }>(req);
    if (typeof body.text !== "string") {
      return errorResponse("Provide 'text' string in request body", 400);
    }
    const ids = Array.from(m1Tokenizer.encode(body.text));
    const decoded = m1Tokenizer.decodeToString(ids);
    const lexicalTerms = tokenize(body.text);
    return jsonResponse({
      tokenCount: ids.length,
      ids,
      losslessRoundTrip: decoded === body.text,
      lexicalTerms,
      serializerSha256: m1Tokenizer.sha256,
    });
  });

  router.post("/api/v1/snapshot", async () => {
    await engine.compactSnapshot();
    return jsonResponse({
      saved: true,
      stats: engine.getStats(),
    });
  });

  return {
    engine,
    scheduler: activeScheduler,
    m1Tokenizer,
    router,
    fetch: (req: Request) => router.handle(req),
  };
}
