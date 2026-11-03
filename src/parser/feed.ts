import type { ParsedFeed, ParsedFeedItem } from "../types.ts";
import {
  findChild,
  findChildren,
  getNodeText,
  parseMarkup,
  stripHtmlToText,
  type XmlNode,
} from "./xml.ts";

export function resolveUrl(candidate: string | undefined, baseUrl?: string): string {
  if (!candidate) return "";
  const trimmed = candidate.trim();
  if (!trimmed) return "";
  if (trimmed.startsWith("http://") || trimmed.startsWith("https://")) {
    return trimmed;
  }
  if (baseUrl) {
    try {
      return new URL(trimmed, baseUrl).toString();
    } catch {
      return trimmed;
    }
  }
  return trimmed;
}

export function parseDateTimestamp(raw: string | undefined, fallback = Date.now()): number {
  if (!raw) return fallback;
  const trimmed = raw.trim();
  if (!trimmed) return fallback;
  const parsed = Date.parse(trimmed);
  if (!Number.isNaN(parsed) && parsed > 0) {
    return parsed;
  }
  return fallback;
}

function extractFirstImgSrc(html: string | undefined, baseUrl?: string): string | undefined {
  if (!html) return undefined;
  const match = /<img[^>]+src=["']([^"']+)["']/i.exec(html);
  if (match?.[1]) {
    return resolveUrl(match[1], baseUrl);
  }
  return undefined;
}

function extractMediaImage(itemNode: XmlNode, baseUrl?: string): string | undefined {
  for (const child of itemNode.children) {
    if (child.type !== "element") continue;
    if (
      child.tag === "media:content" ||
      child.tag === "media:thumbnail" ||
      child.localName === "thumbnail"
    ) {
      const url = child.attributes["url"];
      const medium = child.attributes["medium"] ?? "";
      const type = child.attributes["type"] ?? "";
      if (
        url &&
        (child.tag === "media:thumbnail" ||
          medium === "image" ||
          type.startsWith("image/") ||
          /\.(?:jpe?g|png|webp|gif|avif)(?:\?|$)/i.test(url))
      ) {
        return resolveUrl(url, baseUrl);
      }
    }
    if (child.localName === "enclosure") {
      const url = child.attributes["url"];
      const type = child.attributes["type"] ?? "";
      if (url && type.startsWith("image/")) {
        return resolveUrl(url, baseUrl);
      }
    }
  }
  return undefined;
}

function parseJsonFeed(rawJson: string, feedUrl?: string): ParsedFeed {
  const data = JSON.parse(rawJson) as Record<string, unknown>;
  const title = typeof data.title === "string" ? data.title.trim() : "Untitled Feed";
  const description =
    typeof data.description === "string" ? data.description.trim() : undefined;
  const siteUrl = resolveUrl(
    typeof data.home_page_url === "string" ? data.home_page_url : undefined,
    feedUrl
  );
  const language = typeof data.language === "string" ? data.language : undefined;

  const rawItems = Array.isArray(data.items) ? data.items : [];
  const items: ParsedFeedItem[] = [];

  for (const entry of rawItems) {
    if (!entry || typeof entry !== "object") continue;
    const obj = entry as Record<string, unknown>;
    const url = resolveUrl(
      typeof obj.url === "string"
        ? obj.url
        : typeof obj.external_url === "string"
          ? obj.external_url
          : undefined,
      siteUrl || feedUrl
    );
    const guid =
      typeof obj.id === "string"
        ? obj.id
        : typeof obj.id === "number"
          ? String(obj.id)
          : url;
    const itemTitle =
      typeof obj.title === "string" && obj.title.trim()
        ? obj.title.trim()
        : stripHtmlToText(typeof obj.content_text === "string" ? obj.content_text : "").slice(0, 100) ||
          "Untitled Article";

    const contentHtml =
      typeof obj.content_html === "string" ? obj.content_html : undefined;
    const contentText =
      typeof obj.content_text === "string"
        ? obj.content_text
        : contentHtml
          ? stripHtmlToText(contentHtml)
          : "";
    const summary =
      typeof obj.summary === "string" && obj.summary.trim()
        ? obj.summary.trim()
        : contentText.slice(0, 400);

    let author: string | undefined;
    if (Array.isArray(obj.authors) && obj.authors.length > 0) {
      const first = obj.authors[0] as Record<string, unknown>;
      if (first && typeof first.name === "string") author = first.name;
    } else if (obj.author && typeof obj.author === "object") {
      const a = obj.author as Record<string, unknown>;
      if (typeof a.name === "string") author = a.name;
    }

    const categories = Array.isArray(obj.tags)
      ? obj.tags.filter((t): t is string => typeof t === "string")
      : [];

    const imageUrl = resolveUrl(
      typeof obj.image === "string"
        ? obj.image
        : typeof obj.banner_image === "string"
          ? obj.banner_image
          : extractFirstImgSrc(contentHtml, siteUrl || feedUrl),
      siteUrl || feedUrl
    );

    const publishedAt = parseDateTimestamp(
      typeof obj.date_published === "string"
        ? obj.date_published
        : typeof obj.date_modified === "string"
          ? obj.date_modified
          : undefined
    );

    if (url || itemTitle) {
      items.push({
        guid: guid || url || itemTitle,
        url: url || feedUrl || "",
        title: itemTitle,
        summary,
        contentHtml,
        author,
        categories,
        imageUrl: imageUrl || undefined,
        publishedAt,
      });
    }
  }

  return {
    format: "jsonfeed",
    title,
    description,
    siteUrl: siteUrl || undefined,
    language,
    items,
  };
}

function parseAtomFeed(feedNode: XmlNode, feedUrl?: string): ParsedFeed {
  const title = getNodeText(findChild(feedNode, "title")).trim() || "Untitled Atom Feed";
  const subtitle = getNodeText(findChild(feedNode, "subtitle", "tagline")).trim();

  let siteUrl = "";
  for (const linkNode of findChildren(feedNode, "link")) {
    const rel = (linkNode.attributes["rel"] ?? "alternate").toLowerCase();
    const href = linkNode.attributes["href"];
    if (href && (rel === "alternate" || !siteUrl)) {
      siteUrl = resolveUrl(href, feedUrl);
      if (rel === "alternate") break;
    }
  }

  const language =
    feedNode.attributes["xml:lang"] ?? feedNode.attributes["lang"] ?? undefined;

  const entryNodes = findChildren(feedNode, "entry");
  const items: ParsedFeedItem[] = [];

  for (const entry of entryNodes) {
    const itemTitle =
      stripHtmlToText(getNodeText(findChild(entry, "title")).trim()) || "Untitled";

    let linkUrl = "";
    for (const linkNode of findChildren(entry, "link")) {
      const rel = (linkNode.attributes["rel"] ?? "alternate").toLowerCase();
      const href = linkNode.attributes["href"];
      if (href && (rel === "alternate" || !linkUrl)) {
        linkUrl = resolveUrl(href, siteUrl || feedUrl);
        if (rel === "alternate") break;
      }
    }
    if (!linkUrl) {
      const rawLinkText = getNodeText(findChild(entry, "link")).trim();
      if (rawLinkText) linkUrl = resolveUrl(rawLinkText, siteUrl || feedUrl);
    }

    const idText = getNodeText(findChild(entry, "id")).trim();
    const guid = idText || linkUrl || itemTitle;

    const contentNode = findChild(entry, "content", "content:encoded");
    const summaryNode = findChild(entry, "summary");

    const rawContent = getNodeText(contentNode).trim();
    const rawSummary = getNodeText(summaryNode).trim();

    const contentHtml = rawContent || rawSummary || undefined;
    const summary = stripHtmlToText(rawSummary || rawContent).slice(0, 500);

    let author: string | undefined;
    const authorNode = findChild(entry, "author");
    if (authorNode) {
      const nameNode = findChild(authorNode, "name");
      author = (nameNode ? getNodeText(nameNode) : getNodeText(authorNode)).trim() || undefined;
    }
    if (!author) {
      const dcCreator = getNodeText(findChild(entry, "dc:creator", "creator")).trim();
      if (dcCreator) author = dcCreator;
    }

    const categories: string[] = [];
    for (const catNode of findChildren(entry, "category")) {
      const term = catNode.attributes["label"] || catNode.attributes["term"] || getNodeText(catNode);
      if (term && term.trim()) categories.push(term.trim());
    }

    const dateStr =
      getNodeText(findChild(entry, "published", "issued")).trim() ||
      getNodeText(findChild(entry, "updated", "modified", "dc:date")).trim();

    const publishedAt = parseDateTimestamp(dateStr);
    const imageUrl =
      extractMediaImage(entry, siteUrl || feedUrl) ??
      extractFirstImgSrc(contentHtml, siteUrl || feedUrl);

    items.push({
      guid,
      url: linkUrl || siteUrl || feedUrl || "",
      title: itemTitle,
      summary,
      contentHtml,
      author,
      categories,
      imageUrl,
      publishedAt,
    });
  }

  return {
    format: "atom",
    title,
    description: subtitle || undefined,
    siteUrl: siteUrl || undefined,
    language,
    items,
  };
}

function parseRssOrRdfFeed(
  rootElement: XmlNode,
  format: "rss2" | "rdf",
  feedUrl?: string
): ParsedFeed {
  const channelNode = findChild(rootElement, "channel") ?? rootElement;
  const title =
    getNodeText(findChild(channelNode, "title")).trim() || "Untitled RSS Feed";
  const description =
    getNodeText(findChild(channelNode, "description", "subtitle")).trim() ||
    undefined;

  let siteUrl = "";
  for (const linkNode of findChildren(channelNode, "link")) {
    const text = getNodeText(linkNode).trim();
    const href = linkNode.attributes["href"];
    if (text) {
      siteUrl = resolveUrl(text, feedUrl);
      break;
    }
    if (href) {
      siteUrl = resolveUrl(href, feedUrl);
    }
  }

  const language =
    getNodeText(findChild(channelNode, "language", "dc:language")).trim() ||
    undefined;

  const itemNodes = [
    ...findChildren(channelNode, "item"),
    ...(channelNode !== rootElement ? findChildren(rootElement, "item") : []),
  ];

  const items: ParsedFeedItem[] = [];

  for (const item of itemNodes) {
    const rawTitle = getNodeText(findChild(item, "title")).trim();
    const itemTitle = stripHtmlToText(rawTitle) || "Untitled";

    let linkUrl = getNodeText(findChild(item, "link")).trim();
    if (!linkUrl) {
      const linkAttr = findChild(item, "link")?.attributes["href"];
      if (linkAttr) linkUrl = linkAttr;
    }
    linkUrl = resolveUrl(linkUrl, siteUrl || feedUrl);

    const guidText = getNodeText(findChild(item, "guid", "id")).trim();
    if (!linkUrl && guidText.startsWith("http")) {
      linkUrl = guidText;
    }

    const guid = guidText || linkUrl || itemTitle;

    const encodedContent = getNodeText(
      findChild(item, "content:encoded", "encoded", "content")
    ).trim();
    const descContent = getNodeText(
      findChild(item, "description", "summary")
    ).trim();

    const contentHtml = encodedContent || descContent || undefined;
    const summary = stripHtmlToText(descContent || encodedContent).slice(0, 500);

    const author =
      getNodeText(findChild(item, "dc:creator", "creator", "author")).trim() ||
      undefined;

    const categories: string[] = [];
    for (const catNode of findChildren(item, "category", "dc:subject")) {
      const catText = getNodeText(catNode).trim();
      if (catText) categories.push(catText);
    }

    const dateStr = getNodeText(
      findChild(item, "pubdate", "dc:date", "date", "published", "updated")
    ).trim();
    const publishedAt = parseDateTimestamp(dateStr);

    const imageUrl =
      extractMediaImage(item, siteUrl || feedUrl) ??
      extractFirstImgSrc(contentHtml, siteUrl || feedUrl);

    items.push({
      guid,
      url: linkUrl || siteUrl || feedUrl || "",
      title: itemTitle,
      summary,
      contentHtml,
      author,
      categories,
      imageUrl,
      publishedAt,
    });
  }

  return {
    format,
    title,
    description,
    siteUrl: siteUrl || undefined,
    language,
    items,
  };
}

export function parseFeed(rawInput: string, feedUrl?: string): ParsedFeed {
  const trimmed = rawInput.trim();
  if (!trimmed) {
    throw new Error("Feed payload is empty");
  }

  // Detect JSON Feed
  if (trimmed.startsWith("{")) {
    return parseJsonFeed(trimmed, feedUrl);
  }

  const doc = parseMarkup(trimmed, { htmlMode: false });
  const rootElement = doc.children.find((c) => c.type === "element");
  if (!rootElement) {
    throw new Error("Unable to locate root XML element in feed");
  }

  const rootTag = rootElement.localName.toLowerCase();
  if (rootTag === "feed") {
    return parseAtomFeed(rootElement, feedUrl);
  }
  if (rootTag === "rss") {
    return parseRssOrRdfFeed(rootElement, "rss2", feedUrl);
  }
  if (rootTag === "rdf") {
    return parseRssOrRdfFeed(rootElement, "rdf", feedUrl);
  }
  if (rootTag === "channel") {
    return parseRssOrRdfFeed(rootElement, "rss2", feedUrl);
  }

  throw new Error(`Unsupported feed root element: <${rootElement.tag}>`);
}
