import { DEFAULT_FEED_SEEDS } from "../defaults.ts";
import {
  extractReadableContent,
  fetchAndExtractArticle,
} from "../extractor/readability.ts";
import { OyezIndexEngine } from "../index/engine.ts";
import { M1Tokenizer, tokenize } from "../index/tokenizer.ts";
import { FeedScheduler } from "../poller/scheduler.ts";
import type { Article, FeedConfig } from "../types.ts";
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
  const activeScheduler = scheduler ?? new FeedScheduler(engine);
  const m1Tokenizer = new M1Tokenizer();
  const router = new OyezRouter();

  router.get("/", () => {
    return jsonResponse({
      name: "Oyez",
      tagline: "Oyez! Oyez! Oyez! A news API and RSS aggregator for the people.",
      version: "0.1.0",
      endpoints: [
        "GET    /health",
        "GET    /api/v1/stats",
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
    });
  });

  router.get("/api/v1/stats", () => {
    return jsonResponse(engine.getStats());
  });

  router.get("/api/v1/feeds", () => {
    const feeds = engine.listFeeds().map((f) => ({
      ...f,
      articleCount: engine.getFeedArticleCount(f.id),
    }));
    return jsonResponse({ total: feeds.length, feeds });
  });

  router.post("/api/v1/feeds", async ({ req }) => {
    const body = await readJsonBody<{
      url?: string;
      title?: string;
      category?: string;
      tags?: string[];
      pollIntervalSeconds?: number;
      scrapeFullContent?: boolean;
      enabled?: boolean;
      pollNow?: boolean;
    }>(req);

    const rawUrl = (body.url ?? "").trim();
    if (!rawUrl || (!rawUrl.startsWith("http://") && !rawUrl.startsWith("https://"))) {
      return errorResponse("A valid http(s) feed 'url' is required", 400);
    }

    const existing = engine.getFeedByUrl(rawUrl);
    if (existing) {
      return errorResponse(`Feed with URL already exists (${existing.id})`, 409, {
        feed: existing,
      });
    }

    const now = Date.now();
    const id = `feed_${Bun.hash(rawUrl).toString(36)}`;
    const pollIntervalSeconds = Math.max(60, Number(body.pollIntervalSeconds ?? 900));

    const feed: FeedConfig = {
      id,
      url: rawUrl,
      title: (body.title ?? "").trim() || rawUrl,
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
    try {
      const body = await req.json();
      if (body && typeof body === "object" && "pollNow" in body) {
        pollNow = Boolean((body as Record<string, unknown>).pollNow);
      }
    } catch {
      // Optional JSON body
    }

    const now = Date.now();
    const added: FeedConfig[] = [];

    for (const seed of DEFAULT_FEED_SEEDS) {
      if (engine.getFeedByUrl(seed.url)) continue;
      const id = `feed_${Bun.hash(seed.url).toString(36)}`;
      const feed: FeedConfig = {
        id,
        url: seed.url,
        title: seed.title,
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

    const pollResults = pollNow ? await activeScheduler.pollAll(true) : undefined;
    return jsonResponse(
      {
        addedCount: added.length,
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
      category?: string;
      tags?: string[];
      enabled?: boolean;
      pollIntervalSeconds?: number;
      scrapeFullContent?: boolean;
    }>(req);

    const updated: FeedConfig = {
      ...feed,
      title: body.title !== undefined ? body.title.trim() : feed.title,
      category:
        body.category !== undefined
          ? body.category.trim().toLowerCase()
          : feed.category,
      tags: Array.isArray(body.tags)
        ? body.tags.map((t) => String(t).trim()).filter(Boolean)
        : feed.tags,
      enabled: body.enabled !== undefined ? Boolean(body.enabled) : feed.enabled,
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
    const domain = query.get("domain") ?? undefined;
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

    const searchResult = engine.search({
      q,
      feedId,
      category,
      domain,
      from: from && !Number.isNaN(from) ? from : undefined,
      to: to && !Number.isNaN(to) ? to : undefined,
      sort,
      limit,
      offset,
    });

    return jsonResponse(searchResult);
  });

  router.get("/api/v1/articles/:id", ({ params }) => {
    const article = engine.getArticle(params.id);
    if (!article) {
      return errorResponse(`Article not found: ${params.id}`, 404);
    }
    return jsonResponse({ article });
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
      return errorResponse("Article does not have a valid HTTP(S) URL to extract", 400);
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

    await engine.upsertArticle(updated);
    return jsonResponse({
      article: updated,
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
