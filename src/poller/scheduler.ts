import { fetchAndExtractArticle } from "../extractor/readability.ts";
import type { OyezIndexEngine } from "../index/engine.ts";
import { parseFeed } from "../parser/feed.ts";
import type { Article, FeedConfig } from "../types.ts";

export interface PollResult {
  feedId: string;
  url: string;
  status: "updated" | "not_modified" | "error";
  addedCount: number;
  updatedCount: number;
  extractedCount: number;
  durationMs: number;
  error?: string;
}

export class FeedScheduler {
  private readonly engine: OyezIndexEngine;
  private timer: ReturnType<typeof setInterval> | null = null;
  private activePolls = new Set<string>();
  private readonly maxConcurrentPolls: number;

  constructor(engine: OyezIndexEngine, maxConcurrentPolls = 4) {
    this.engine = engine;
    this.maxConcurrentPolls = maxConcurrentPolls;
  }

  start(tickMs = 15_000): void {
    if (this.timer) return;
    this.timer = setInterval(() => {
      void this.tick();
    }, tickMs);
  }

  stop(): void {
    if (this.timer) {
      clearInterval(this.timer);
      this.timer = null;
    }
  }

  async tick(): Promise<void> {
    const now = Date.now();
    const dueFeeds = this.engine
      .listFeeds()
      .filter(
        (f) =>
          f.enabled &&
          f.nextPollAt <= now &&
          !this.activePolls.has(f.id)
      );

    const availableSlots = Math.max(
      0,
      this.maxConcurrentPolls - this.activePolls.size
    );
    const batch = dueFeeds.slice(0, availableSlots);

    await Promise.allSettled(batch.map((f) => this.pollFeed(f.id)));
  }

  async pollAll(onlyEnabled = true): Promise<PollResult[]> {
    const feeds = this.engine
      .listFeeds()
      .filter((f) => (!onlyEnabled || f.enabled) && !this.activePolls.has(f.id));

    const results: PollResult[] = [];
    for (let i = 0; i < feeds.length; i += this.maxConcurrentPolls) {
      const slice = feeds.slice(i, i + this.maxConcurrentPolls);
      const batchResults = await Promise.all(
        slice.map((f) => this.pollFeed(f.id))
      );
      results.push(...batchResults);
    }
    return results;
  }

  async pollFeed(feedId: string, timeoutMs = 12_000): Promise<PollResult> {
    const startTime = performance.now();
    const feed = this.engine.getFeed(feedId);
    if (!feed) {
      return {
        feedId,
        url: "",
        status: "error",
        addedCount: 0,
        updatedCount: 0,
        extractedCount: 0,
        durationMs: 0,
        error: `Feed not found: ${feedId}`,
      };
    }

    if (this.activePolls.has(feedId)) {
      return {
        feedId,
        url: feed.url,
        status: "error",
        addedCount: 0,
        updatedCount: 0,
        extractedCount: 0,
        durationMs: 0,
        error: "Poll already in progress for this feed",
      };
    }

    this.activePolls.add(feedId);
    const now = Date.now();

    try {
      const headers: Record<string, string> = {
        "User-Agent": "OyezNewsBot/0.1 (+https://github.com/dandekker/oyez)",
        Accept:
          "application/rss+xml, application/atom+xml, application/feed+json, application/xml, text/xml, */*",
      };
      if (feed.etag) {
        headers["If-None-Match"] = feed.etag;
      }
      if (feed.lastModified) {
        headers["If-Modified-Since"] = feed.lastModified;
      }

      const controller = new AbortController();
      const timer = setTimeout(() => controller.abort(), timeoutMs);

      let response: Response;
      try {
        response = await fetch(feed.url, {
          headers,
          signal: controller.signal,
        });
      } finally {
        clearTimeout(timer);
      }

      if (response.status === 304) {
        const updatedFeed: FeedConfig = {
          ...feed,
          lastPolledAt: now,
          lastSuccessAt: now,
          nextPollAt: now + feed.pollIntervalSeconds * 1000,
          consecutiveFailures: 0,
          lastError: undefined,
          updatedAt: now,
        };
        await this.engine.upsertFeed(updatedFeed);

        return {
          feedId: feed.id,
          url: feed.url,
          status: "not_modified",
          addedCount: 0,
          updatedCount: 0,
          extractedCount: 0,
          durationMs: Number((performance.now() - startTime).toFixed(2)),
        };
      }

      if (!response.ok) {
        throw new Error(`HTTP ${response.status} ${response.statusText}`);
      }

      const rawBody = await response.text();
      const parsed = parseFeed(rawBody, feed.url);

      const newEtag = response.headers.get("etag") ?? feed.etag;
      const newLastModified =
        response.headers.get("last-modified") ?? feed.lastModified;

      const updatedFeed: FeedConfig = {
        ...feed,
        title:
          feed.title === feed.url || !feed.title ? parsed.title : feed.title,
        description: parsed.description ?? feed.description,
        siteUrl: parsed.siteUrl ?? feed.siteUrl,
        language: parsed.language ?? feed.language,
        etag: newEtag,
        lastModified: newLastModified,
        lastPolledAt: now,
        lastSuccessAt: now,
        nextPollAt: now + feed.pollIntervalSeconds * 1000,
        consecutiveFailures: 0,
        lastError: undefined,
        updatedAt: now,
      };

      await this.engine.upsertFeed(updatedFeed);

      const { added, updated } = await this.engine.ingestFeedItems(
        updatedFeed,
        parsed.items
      );

      let extractedCount = 0;
      if (updatedFeed.scrapeFullContent && added.length > 0) {
        // Scrape up to 5 newly added articles per poll to stay polite
        const toScrape = added.slice(0, 5);
        for (const art of toScrape) {
          if (!art.url.startsWith("http")) continue;
          try {
            const extracted = await fetchAndExtractArticle(art.url, 8_000);
            if (extracted.textContent.length > art.contentText.length) {
              const enriched: Article = {
                ...art,
                contentText: extracted.textContent,
                markdownContent: extracted.markdownContent,
                author: art.author || extracted.byline,
                imageUrl: art.imageUrl || extracted.leadImageUrl,
                wordCount: extracted.wordCount,
                readingTimeMinutes: extracted.readingTimeMinutes,
                fullContentExtracted: true,
              };
              await this.engine.upsertArticle(enriched);
              extractedCount++;
            }
          } catch {
            // Keep feed summary content if full-page scrape fails
          }
        }
      }

      return {
        feedId: feed.id,
        url: feed.url,
        status: "updated",
        addedCount: added.length,
        updatedCount: updated.length,
        extractedCount,
        durationMs: Number((performance.now() - startTime).toFixed(2)),
      };
    } catch (err) {
      const errorMessage = err instanceof Error ? err.message : String(err);
      const failures = feed.consecutiveFailures + 1;
      const backoffMultiplier = Math.pow(2, Math.min(failures, 5));
      const backoffMs = feed.pollIntervalSeconds * 1000 * backoffMultiplier;

      const failedFeed: FeedConfig = {
        ...feed,
        lastPolledAt: now,
        nextPollAt: now + backoffMs,
        consecutiveFailures: failures,
        lastError: errorMessage,
        updatedAt: now,
      };
      await this.engine.upsertFeed(failedFeed);

      return {
        feedId: feed.id,
        url: feed.url,
        status: "error",
        addedCount: 0,
        updatedCount: 0,
        extractedCount: 0,
        durationMs: Number((performance.now() - startTime).toFixed(2)),
        error: errorMessage,
      };
    } finally {
      this.activePolls.delete(feedId);
    }
  }
}
