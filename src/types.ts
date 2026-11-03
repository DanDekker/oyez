export type FeedFormat = "rss2" | "atom" | "rdf" | "jsonfeed";

export interface FeedConfig {
  id: string;
  url: string;
  title: string;
  description?: string;
  siteUrl?: string;
  language?: string;
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
  domain: string;
  title: string;
  summary: string;
  contentHtml?: string;
  contentText: string;
  markdownContent?: string;
  author?: string;
  categories: string[];
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
  domain?: string;
  from?: number;
  to?: number;
  sort?: "relevance" | "date";
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
  results: ScoredArticle[];
}

export interface EngineStats {
  totalFeeds: number;
  enabledFeeds: number;
  totalArticles: number;
  vocabularySize: number;
  totalPostings: number;
  averageDocumentLength: number;
  categories: Record<string, number>;
  domains: Record<string, number>;
  dataDir: string;
  lastSnapshotAt?: number;
  uptimeSeconds: number;
}
