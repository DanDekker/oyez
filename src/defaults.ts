export interface DefaultFeedSeed {
  url: string;
  title: string;
  category: string;
  tags: string[];
}

export const DEFAULT_FEED_SEEDS: DefaultFeedSeed[] = [
  {
    url: "https://hnrss.org/frontpage",
    title: "Hacker News Frontpage",
    category: "technology",
    tags: ["tech", "programming", "startups"],
  },
  {
    url: "https://lobste.rs/rss",
    title: "Lobsters",
    category: "technology",
    tags: ["computing", "software", "engineering"],
  },
  {
    url: "https://feeds.bbci.co.uk/news/world/rss.xml",
    title: "BBC World News",
    category: "world",
    tags: ["world", "breaking", "international"],
  },
  {
    url: "https://rss.nytimes.com/services/xml/rss/nyt/World.xml",
    title: "NYT World",
    category: "world",
    tags: ["world", "journalism"],
  },
  {
    url: "https://feeds.arstechnica.com/arstechnica/index",
    title: "Ars Technica",
    category: "science",
    tags: ["technology", "science", "policy"],
  },
];
