import {
  WORLD_FEED_CATALOG,
  type WorldFeedSeed,
} from "./catalog/world_feeds.ts";

export type DefaultFeedSeed = WorldFeedSeed;
export const DEFAULT_FEED_SEEDS: WorldFeedSeed[] = WORLD_FEED_CATALOG;
export { WORLD_FEED_CATALOG };
