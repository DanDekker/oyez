import { WORLD_FEED_CATALOG } from "../catalog/world_feeds.ts";
import { OyezIndexEngine } from "../index/engine.ts";
import { FeedScheduler } from "../poller/scheduler.ts";
import type { FeedConfig } from "../types.ts";

async function main() {
  const dataDir = process.env.OYEZ_DATA_DIR ?? "./data";
  const engine = new OyezIndexEngine(dataDir);
  await engine.init();

  const now = Date.now();
  let newlyRegistered = 0;

  for (const seed of WORLD_FEED_CATALOG) {
    if (engine.getFeedByUrl(seed.url)) continue;
    const feed: FeedConfig = {
      id: `feed_${Bun.hash(seed.url).toString(36)}`,
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
    newlyRegistered++;
  }

  const totalFeeds = engine.listFeeds().length;
  console.log(
    `[Oyez Global Indexer] Registered ${newlyRegistered} new feeds (${totalFeeds} total across 11 world regions).`
  );
  console.log(
    `[Oyez Global Indexer] Polling global feeds with 16 concurrent workers...`
  );

  const scheduler = new FeedScheduler(engine, 16);
  const startTime = performance.now();
  const results = await scheduler.pollAll(true);
  const elapsedSec = ((performance.now() - startTime) / 1000).toFixed(2);

  let updatedFeeds = 0;
  let totalAdded = 0;
  let totalUpdated = 0;
  let failedFeeds = 0;

  for (const r of results) {
    if (r.status === "updated" || r.status === "not_modified") {
      updatedFeeds++;
      totalAdded += r.addedCount;
      totalUpdated += r.updatedCount;
    } else {
      failedFeeds++;
    }
  }

  await engine.compactSnapshot();
  const stats = engine.getStats();
  const clusters = engine.discoverStoryClusters(8);
  const concepts = engine.getConceptLattice(2, 5);
  const centrality = engine.getPageRankCentrality(8);

  console.log(`\n=============================================================`);
  console.log(`🌍 OYEZ GLOBAL DEEP INDEX REPORT (${elapsedSec}s)`);
  console.log(`=============================================================`);
  console.log(
    `Feeds Polled       : ${results.length} (${updatedFeeds} succeeded, ${failedFeeds} unreachable)`
  );
  console.log(
    `Articles Indexed   : ${stats.totalArticles} (+${totalAdded} new, ${totalUpdated} refreshed)`
  );
  console.log(`BM25 Vocabulary    : ${stats.vocabularySize} unique stems`);
  console.log(`Total Postings     : ${stats.totalPostings} weighted postings`);
  console.log(`Avg Doc Weight Len : ${stats.averageDocumentLength}`);

  console.log(`\n--- Articles by World Region ---`);
  for (const [region, count] of Object.entries(stats.regions)) {
    console.log(`  ${region.padEnd(18)} : ${count}`);
  }

  console.log(`\n--- Epistemic News Types ---`);
  for (const [newsType, count] of Object.entries(stats.newsTypes)) {
    console.log(`  ${newsType.padEnd(26)} : ${count}`);
  }

  console.log(`\n--- Presheaf Ontology Category Fibers F(c) ---`);
  for (const [ontCat, count] of Object.entries(stats.ontologyCategories)) {
    console.log(`  ${ontCat.padEnd(32)} : ${count}`);
  }

  console.log(`\n--- Top Mentioned Countries (ISO-2) ---`);
  const topCountries = Object.entries(stats.mentionedCountries).slice(0, 15);
  console.log("  " + topCountries.map(([c, n]) => `${c}(${n})`).join(", "));

  if (centrality.topEntities.length > 0) {
    console.log(`\n--- Top PageRank Central Entities ---`);
    console.log(
      "  " +
        centrality.topEntities
          .map((e) => `${e.key} (${e.score})`)
          .join(", ")
    );
  }

  if (concepts.length > 0) {
    console.log(`\n--- Top Galois Formal Concepts (Closed Intent / Extent) ---`);
    for (const c of concepts) {
      console.log(`  • [Extent=${c.extentSize}] { ${c.intent.join(", ")} }`);
    }
  }

  if (clusters.length > 0) {
    console.log(`\n--- Top Multi-Source Story Clusters ---`);
    for (const c of clusters.slice(0, 5)) {
      console.log(
        `  • [${c.articleCount} articles | ${c.regions.join(", ")}] ${c.headline}`
      );
    }
  }
  console.log(`=============================================================\n`);
}

if (import.meta.main) {
  await main();
}
