export type FeedFormat = "rss2" | "atom" | "rdf" | "jsonfeed";

export type WorldRegion =
  | "north-america"
  | "latin-america"
  | "europe"
  | "middle-east"
  | "africa"
  | "south-asia"
  | "east-asia"
  | "southeast-asia"
  | "central-asia"
  | "oceania"
  | "global";

/**
 * Epistemic / Journalistic Genre Type of a News Article
 */
export type NewsType =
  | "breaking-dispatch"
  | "investigative-report"
  | "policy-and-legislative"
  | "scientific-research"
  | "market-and-economic"
  | "opinion-and-editorial"
  | "explainer-and-analysis"
  | "press-and-official"
  | "cultural-and-feature"
  | "general-report";

export interface ArticleEntities {
  /** ISO-2 country codes mentioned in the article text */
  mentionedCountries: string[];
  /** Leaf and intermediate category/topic nodes in the Ontology Poset */
  topics: string[];
  /** Functorial upward closure of all ancestor categories in the Ontology Poset */
  ontologyClosure: string[];
  /** International organizations, institutions, and proper noun entities */
  organizationsAndPeople: string[];
}

export interface FeedConfig {
  id: string;
  url: string;
  title: string;
  description?: string;
  siteUrl?: string;
  region: WorldRegion;
  /** ISO-2 country code of the publication (e.g. US, GB, JP, BR, NG, IN) or INT */
  country: string;
  /** ISO-639-1 language code (e.g. en, es, fr, de, pt, ja, ar) */
  language: string;
  category: string;
  tags: string[];
  enabled: boolean;
  /** Polling interval in seconds (default: 900 = 15m) */
  pollIntervalSeconds: number;
  /** Automatically fetch & extract full article readability content on ingest */
  scrapeFullContent: boolean;
  /** HTTP conditional request headers */
  etag?: string;
  lastModified?: string;
  /** Polling state */
  lastPolledAt?: number;
  lastSuccessAt?: number;
  nextPollAt: number;
  consecutiveFailures: number;
  lastError?: string;
  createdAt: number;
  updatedAt: number;
}

export interface ExtractedArticleContent {
  title?: string;
  byline?: string;
  siteName?: string;
  excerpt?: string;
  textContent: string;
  markdownContent: string;
  leadImageUrl?: string;
  wordCount: number;
  readingTimeMinutes: number;
  extractedAt: number;
}

export interface Article {
  id: string;
  feedId: string;
  feedTitle: string;
  guid: string;
  url: string;
  canonicalHash: string;
  /** 64-bit Charikar SimHash hex fingerprint for O(1) Hamming LSH near-duplicate & similarity */
  simhash64: string;
  domain: string;
  region: WorldRegion;
  country: string;
  language: string;
  /** Epistemic / journalistic type of the news article */
  newsType: NewsType;
  title: string;
  summary: string;
  contentHtml?: string;
  contentText: string;
  markdownContent?: string;
  author?: string;
  categories: string[];
  entities: ArticleEntities;
  imageUrl?: string;
  publishedAt: number;
  ingestedAt: number;
  wordCount: number;
  readingTimeMinutes: number;
  fullContentExtracted: boolean;
}

export interface ParsedFeedItem {
  guid: string;
  url: string;
  title: string;
  summary: string;
  contentHtml?: string;
  author?: string;
  categories: string[];
  imageUrl?: string;
  publishedAt: number;
}

export interface ParsedFeed {
  format: FeedFormat;
  title: string;
  description?: string;
  siteUrl?: string;
  language?: string;
  items: ParsedFeedItem[];
}

export interface SearchQuery {
  q?: string;
  feedId?: string;
  category?: string;
  newsType?: NewsType | string;
  region?: WorldRegion | string;
  /** Source publication country ISO-2 */
  country?: string;
  /** Country mentioned inside the article */
  mentionedCountry?: string;
  language?: string;
  topic?: string;
  entity?: string;
  domain?: string;
  from?: number;
  to?: number;
  sort?: "relevance" | "date";
  /** Apply Submodular Maximum Marginal Relevance (MMR) diversification */
  diversify?: boolean;
  /** MMR trade-off parameter in [0, 1] (default 0.7) */
  mmrLambda?: number;
  limit?: number;
  offset?: number;
}

export interface ScoredArticle {
  article: Article;
  score: number;
  matchedTerms: string[];
}

export interface SearchResult {
  total: number;
  limit: number;
  offset: number;
  queryTimeMs: number;
  diversified?: boolean;
  facets?: FacetCounts;
  results: ScoredArticle[];
}

export interface FacetCounts {
  newsTypes: Record<string, number>;
  regions: Record<string, number>;
  countries: Record<string, number>;
  mentionedCountries: Record<string, number>;
  categories: Record<string, number>;
  ontologyCategories: Record<string, number>;
  topics: Record<string, number>;
  languages: Record<string, number>;
  domains: Record<string, number>;
  topEntities: Record<string, number>;
}

export interface StoryCluster {
  id: string;
  headline: string;
  leadArticleId: string;
  articleCount: number;
  sources: string[];
  regions: string[];
  countries: string[];
  newsTypes: string[];
  topics: string[];
  sharedTerms: string[];
  latestPublishedAt: number;
  articles: Array<{
    id: string;
    title: string;
    feedTitle: string;
    country: string;
    region: string;
    newsType: NewsType;
    url: string;
    publishedAt: number;
  }>;
}

export interface EngineStats {
  totalFeeds: number;
  enabledFeeds: number;
  totalArticles: number;
  vocabularySize: number;
  totalPostings: number;
  averageDocumentLength: number;
  newsTypes: Record<string, number>;
  regions: Record<string, number>;
  countries: Record<string, number>;
  mentionedCountries: Record<string, number>;
  categories: Record<string, number>;
  ontologyCategories: Record<string, number>;
  topics: Record<string, number>;
  languages: Record<string, number>;
  domains: Record<string, number>;
  topEntities: Record<string, number>;
  dataDir: string;
  lastSnapshotAt?: number;
  uptimeSeconds: number;
}
