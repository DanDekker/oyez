import { describe, expect, test } from "bun:test";
import { mkdtemp, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { createOyezApp } from "../src/api/server.ts";
import { WORLD_FEED_CATALOG } from "../src/catalog/world_feeds.ts";
import { OyezIndexEngine } from "../src/index/engine.ts";
import { extractArticleEntities } from "../src/index/entities.ts";

describe("Global Catalog, Geopolitical Gazetteer & Deep Faceted Index", () => {
  test("WORLD_FEED_CATALOG covers all 11 world regions and 40+ countries", () => {
    const regions = new Set(WORLD_FEED_CATALOG.map((f) => f.region));
    const countries = new Set(WORLD_FEED_CATALOG.map((f) => f.country));

    expect(WORLD_FEED_CATALOG.length).toBeGreaterThanOrEqual(85);
    expect(regions.has("north-america")).toBe(true);
    expect(regions.has("latin-america")).toBe(true);
    expect(regions.has("europe")).toBe(true);
    expect(regions.has("middle-east")).toBe(true);
    expect(regions.has("africa")).toBe(true);
    expect(regions.has("south-asia")).toBe(true);
    expect(regions.has("east-asia")).toBe(true);
    expect(regions.has("southeast-asia")).toBe(true);
    expect(regions.has("central-asia")).toBe(true);
    expect(regions.has("oceania")).toBe(true);
    expect(regions.has("global")).toBe(true);
    expect(countries.size).toBeGreaterThanOrEqual(40);
  });

  test("extractArticleEntities identifies countries, topics, and international organizations", () => {
    const entities = extractArticleEntities(
      "European Central Bank and Bank of Japan Discuss Semiconductor Supply Chains in Tokyo",
      "Officials from Germany, France, and Taiwan met with TSMC and United Nations trade envoys.",
      "The bilateral summit focused on artificial intelligence, export control tariffs, and inflation.",
      "JP"
    );

    expect(entities.mentionedCountries).toContain("JP");
    expect(entities.mentionedCountries).toContain("DE");
    expect(entities.mentionedCountries).toContain("FR");
    expect(entities.mentionedCountries).toContain("TW");
    expect(entities.topics).toContain("semiconductors-and-hardware");
    expect(entities.topics).toContain("ai-and-machine-learning");
    expect(entities.topics).toContain("macroeconomics-and-finance");
    expect(entities.topics).toContain("trade-and-supply-chain");
    expect(entities.organizationsAndPeople).toContain("European Central Bank");
    expect(entities.organizationsAndPeople).toContain("Bank of Japan");
    expect(entities.organizationsAndPeople).toContain("TSMC");
    expect(entities.organizationsAndPeople).toContain("United Nations");
  });

  test("indexes multi-region stories, filters by geo/entity facets, and clusters global coverage", async () => {
    const tempDir = await mkdtemp(join(tmpdir(), "oyez-world-test-"));
    try {
      const engine = new OyezIndexEngine(tempDir);
      await engine.init();
      const app = createOyezApp(engine);

      const now = Date.now();
      const feedJp = await engine.upsertFeed({
        id: "feed_jp",
        url: "https://japantimes.example.jp/rss",
        title: "Japan Dispatch",
        region: "east-asia",
        country: "JP",
        language: "en",
        category: "regional",
        tags: ["japan"],
        enabled: true,
        pollIntervalSeconds: 900,
        scrapeFullContent: false,
        nextPollAt: now,
        consecutiveFailures: 0,
        createdAt: now,
        updatedAt: now,
      });

      const feedDe = await engine.upsertFeed({
        id: "feed_de",
        url: "https://dw.example.de/rss",
        title: "Berlin Wire",
        region: "europe",
        country: "DE",
        language: "en",
        category: "world",
        tags: ["germany"],
        enabled: true,
        pollIntervalSeconds: 900,
        scrapeFullContent: false,
        nextPollAt: now,
        consecutiveFailures: 0,
        createdAt: now,
        updatedAt: now,
      });

      await engine.ingestFeedItems(feedJp, [
        {
          guid: "jp-1",
          url: "https://japantimes.example.jp/articles/semiconductor-alliance-tokyo",
          title: "Tokyo and Berlin Sign New Semiconductor Foundry Alliance with TSMC",
          summary: "Japan and Germany expand microchip wafer manufacturing and clean energy cooperation.",
          categories: ["technology"],
          publishedAt: now - 2000,
        },
      ]);

      await engine.ingestFeedItems(feedDe, [
        {
          guid: "de-1",
          url: "https://dw.example.de/articles/berlin-tokyo-semiconductor-alliance",
          title: "Berlin and Tokyo Sign Semiconductor Foundry Alliance with TSMC",
          summary: "Germany and Japan partner on microchip wafer production and renewable power.",
          categories: ["technology"],
          publishedAt: now - 1000,
        },
      ]);

      // Filter by region=east-asia
      const eastAsiaRes = engine.search({ region: "east-asia" });
      expect(eastAsiaRes.total).toBe(1);
      expect(eastAsiaRes.results[0].article.country).toBe("JP");

      // Filter by mentionedCountry=DE (both articles mention Germany/Berlin!)
      const mentionedDeRes = engine.search({ mentionedCountry: "DE" });
      expect(mentionedDeRes.total).toBe(2);

      // Filter by topic=semiconductors-and-hardware
      const topicRes = engine.search({ topic: "semiconductors-and-hardware" });
      expect(topicRes.total).toBe(2);

      // Filter by entity=TSMC
      const entityRes = engine.search({ entity: "TSMC" });
      expect(entityRes.total).toBe(2);

      // Story clustering detects that both sources cover the same event
      const clusters = engine.discoverStoryClusters(5);
      expect(clusters.length).toBe(1);
      expect(clusters[0].articleCount).toBe(2);
      expect(clusters[0].regions.sort()).toEqual(["east-asia", "europe"]);

      // Related articles endpoint
      const firstId = eastAsiaRes.results[0].article.id;
      const relatedRes = await app.fetch(
        new Request(`http://localhost/api/v1/articles/${firstId}/related`)
      );
      expect(relatedRes.status).toBe(200);
      const relatedJson = (await relatedRes.json()) as {
        relatedCount: number;
      };
      expect(relatedJson.relatedCount).toBe(1);
    } finally {
      await rm(tempDir, { recursive: true, force: true });
    }
  });
});
