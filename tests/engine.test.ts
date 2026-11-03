import { describe, expect, test } from "bun:test";
import { rm, mkdtemp } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { OyezIndexEngine } from "../src/index/engine.ts";
import type { FeedConfig } from "../src/types.ts";

describe("OyezIndexEngine (BM25 + WAL + Snapshots)", () => {
  test("indexes articles, ranks with BM25, filters by category/domain, and persists across restarts", async () => {
    const tempDir = await mkdtemp(join(tmpdir(), "oyez-engine-test-"));
    try {
      const engine = new OyezIndexEngine(tempDir);
      await engine.init();

      const now = Date.now();
      const feed: FeedConfig = {
        id: "feed_tech",
        url: "https://tech.example.com/rss.xml",
        title: "Tech Wire",
        category: "technology",
        tags: ["systems"],
        enabled: true,
        pollIntervalSeconds: 900,
        scrapeFullContent: false,
        nextPollAt: now,
        consecutiveFailures: 0,
        createdAt: now,
        updatedAt: now,
      };

      await engine.upsertFeed(feed);

      const { added } = await engine.ingestFeedItems(feed, [
        {
          guid: "item-1",
          url: "https://tech.example.com/posts/quantum-computing?utm_source=rss",
          title: "Quantum Computing Reaches New Milestone",
          summary: "Researchers demonstrate fault-tolerant logical qubits in laboratory.",
          categories: ["quantum", "physics"],
          publishedAt: now - 10_000,
        },
        {
          guid: "item-2",
          url: "https://tech.example.com/posts/rust-zig-bun-performance",
          title: "High-Speed Systems Programming with Bun and TypeScript",
          summary: "Exploring low-latency HTTP servers and custom inverted indexes.",
          categories: ["programming", "bun"],
          publishedAt: now - 5_000,
        },
        {
          guid: "item-3",
          url: "https://cooking.example.org/posts/sourdough-bread",
          title: "Artisanal Sourdough Bread Baking Guide",
          summary: "How to maintain a starter and bake crusty loaves.",
          categories: ["cooking"],
          publishedAt: now - 1_000,
        },
      ]);

      expect(added.length).toBe(3);

      // Deduplication check: ingesting same URL without utm_source updates in-place
      const dupRes = await engine.ingestFeedItems(feed, [
        {
          guid: "item-1-dup",
          url: "https://tech.example.com/posts/quantum-computing",
          title: "Quantum Computing Reaches New Milestone (Updated)",
          summary: "Researchers demonstrate fault-tolerant logical qubits in laboratory.",
          categories: ["quantum", "physics"],
          publishedAt: now - 10_000,
        },
      ]);
      expect(dupRes.added.length).toBe(0);
      expect(dupRes.updated.length).toBe(1);
      expect(engine.getStats().totalArticles).toBe(3);

      // BM25 Search
      const quantumSearch = engine.search({ q: "quantum qubits" });
      expect(quantumSearch.total).toBe(1);
      expect(quantumSearch.results[0].article.title).toContain("Quantum Computing");

      // Secondary index filter by category + domain
      const domainFilter = engine.search({ domain: "cooking.example.org" });
      expect(domainFilter.total).toBe(1);
      expect(domainFilter.results[0].article.title).toContain("Sourdough");

      // Restart engine from WAL (before explicit snapshot) and verify state recovery
      const recoveredEngine = new OyezIndexEngine(tempDir);
      await recoveredEngine.init();
      expect(recoveredEngine.getStats().totalFeeds).toBe(1);
      expect(recoveredEngine.getStats().totalArticles).toBe(3);

      const postRestartSearch = recoveredEngine.search({ q: "inverted indexes" });
      expect(postRestartSearch.total).toBe(1);
      expect(postRestartSearch.results[0].article.title).toContain("Bun and TypeScript");
    } finally {
      await rm(tempDir, { recursive: true, force: true });
    }
  });
});
