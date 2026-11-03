import { createOyezApp } from "./api/server.ts";
import { DEFAULT_FEED_SEEDS } from "./defaults.ts";
import { OyezIndexEngine } from "./index/engine.ts";
import { FeedScheduler } from "./poller/scheduler.ts";
import type { FeedConfig } from "./types.ts";

export async function startServer() {
  const port = Number(process.env.PORT ?? 3000);
  const host = process.env.HOST ?? "0.0.0.0";
  const dataDir = process.env.OYEZ_DATA_DIR ?? "./data";
  const shouldSeed =
    process.argv.includes("--seed") || process.env.OYEZ_SEED_DEFAULTS === "1";

  const engine = new OyezIndexEngine(dataDir);
  await engine.init();

  if (shouldSeed && engine.listFeeds().length === 0) {
    const now = Date.now();
    for (const seed of DEFAULT_FEED_SEEDS) {
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
    }
    console.log(
      `[Oyez] Seeded ${DEFAULT_FEED_SEEDS.length} global feeds across all regions.`
    );
  }

  const scheduler = new FeedScheduler(engine, 8);
  const app = createOyezApp(engine, scheduler);

  scheduler.start(15_000);

  const server = Bun.serve({
    port,
    hostname: host,
    fetch: app.fetch,
  });

  console.log(
    `🔔 Oyez! Oyez! Oyez! Server listening on http://${server.hostname}:${server.port}`
  );

  const shutdown = async () => {
    console.log("\n[Oyez] Shutting down and compacting index snapshot...");
    scheduler.stop();
    await engine.compactSnapshot();
    server.stop();
    process.exit(0);
  };

  process.on("SIGINT", shutdown);
  process.on("SIGTERM", shutdown);

  return { server, app, engine, scheduler };
}

if (import.meta.main) {
  await startServer();
}
