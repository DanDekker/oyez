import { describe, expect, test } from "bun:test";
import { mkdtemp, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { createOyezApp } from "../src/api/server.ts";
import { OyezIndexEngine } from "../src/index/engine.ts";

describe("Oyez REST API", () => {
  test("handles feed CRUD, article search, readability extraction, and tokenization", async () => {
    const tempDir = await mkdtemp(join(tmpdir(), "oyez-api-test-"));
    try {
      const engine = new OyezIndexEngine(tempDir);
      await engine.init();
      const app = createOyezApp(engine);

      // 1. Health check
      const healthRes = await app.fetch(new Request("http://localhost/health"));
      expect(healthRes.status).toBe(200);
      const healthJson = (await healthRes.json()) as { status: string };
      expect(healthJson.status).toBe("ok");

      // 2. Register feed (pollNow: false)
      const createFeedRes = await app.fetch(
        new Request("http://localhost/api/v1/feeds", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({
            url: "https://example.com/rss.xml",
            title: "Example News",
            category: "world",
            tags: ["international"],
            pollNow: false,
          }),
        })
      );
      expect(createFeedRes.status).toBe(201);
      const createdBody = (await createFeedRes.json()) as {
        feed: { id: string; title: string };
      };
      const feedId = createdBody.feed.id;
      expect(createdBody.feed.title).toBe("Example News");

      // 3. Ingest articles & query via /api/v1/articles
      const feedObj = engine.getFeed(feedId)!;
      await engine.ingestFeedItems(feedObj, [
        {
          guid: "art-100",
          url: "https://example.com/news/solar-grid",
          title: "Solar Grid Expansion Reaches Record Capacity",
          summary: "Renewable energy installations surged by 40% this quarter.",
          categories: ["energy", "climate"],
          publishedAt: Date.now(),
        },
      ]);

      const searchRes = await app.fetch(
        new Request("http://localhost/api/v1/articles?q=solar+renewable")
      );
      expect(searchRes.status).toBe(200);
      const searchBody = (await searchRes.json()) as {
        total: number;
        results: Array<{ article: { title: string }; score: number }>;
      };
      expect(searchBody.total).toBe(1);
      expect(searchBody.results[0].article.title).toContain("Solar Grid");

      // 4. Standalone readability extraction via POST /api/v1/extract
      const extractRes = await app.fetch(
        new Request("http://localhost/api/v1/extract", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({
            url: "https://example.com/news/solar-grid",
            html: "<article><h1>Solar Grid Expansion</h1><p>This is the full article body detailing how grid-scale battery storage and solar arrays transformed regional power reliability.</p></article>",
          }),
        })
      );
      expect(extractRes.status).toBe(200);
      const extractBody = (await extractRes.json()) as {
        extracted: { title: string; textContent: string };
      };
      expect(extractBody.extracted.title).toBe("Solar Grid Expansion");
      expect(extractBody.extracted.textContent).toContain("grid-scale battery storage");

      // 5. Tokenize via POST /api/v1/tokenize
      const tokRes = await app.fetch(
        new Request("http://localhost/api/v1/tokenize", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({
            text: "Oyez news aggregator indexing",
          }),
        })
      );
      expect(tokRes.status).toBe(200);
      const tokBody = (await tokRes.json()) as {
        losslessRoundTrip: boolean;
        tokenCount: number;
        lexicalTerms: string[];
      };
      expect(tokBody.losslessRoundTrip).toBe(true);
      expect(tokBody.tokenCount).toBeGreaterThan(0);
      expect(tokBody.lexicalTerms).toContain("oyez");
    } finally {
      await rm(tempDir, { recursive: true, force: true });
    }
  });
});
