import type { ExtractedArticleContent } from "../types.ts";
import { resolveUrl } from "../parser/feed.ts";
import {
  findAllDescendants,
  getNodeText,
  parseMarkup,
  type XmlNode,
} from "../parser/xml.ts";

const UNLIKELY_CANDIDATES =
  /banner|breadcrumbs|combx|comment|community|cover-wrap|disqus|extra|footer|gdpr|header|legends|menu|related|remark|replies|rss|shoutbox|sidebar|skyscraper|social|sponsor|supplemental|ad-break|agegate|pagination|pager|popup|newsletter|cookie|nav|share/i;

const POSITIVE_HINTS =
  /article|body|content|entry|hentry|h-entry|main|page|pagination|post|text|blog|story|prose/i;

const NEGATIVE_HINTS =
  /hidden|hid|banner|combx|comment|com-|contact|foot|footer|footnote|gdpr|masthead|media|meta|outbrain|promo|related|scroll|share|shoutbox|sidebar|skyscraper|sponsor|shopping|tags|tool|widget/i;

const NOISE_TAGS = new Set([
  "script",
  "style",
  "noscript",
  "svg",
  "form",
  "nav",
  "footer",
  "aside",
  "iframe",
  "button",
  "input",
  "select",
  "textarea",
]);

function getClassAndId(node: XmlNode): string {
  return `${node.attributes["class"] ?? ""} ${node.attributes["id"] ?? ""} ${node.attributes["role"] ?? ""}`;
}

function isNoiseNode(node: XmlNode): boolean {
  if (NOISE_TAGS.has(node.localName)) return true;
  if (
    node.attributes["aria-hidden"] === "true" ||
    node.attributes["hidden"] !== undefined
  ) {
    return true;
  }
  const classAndId = getClassAndId(node);
  if (
    UNLIKELY_CANDIDATES.test(classAndId) &&
    !POSITIVE_HINTS.test(classAndId) &&
    node.localName !== "article" &&
    node.localName !== "main" &&
    node.localName !== "body"
  ) {
    return true;
  }
  return false;
}

function computeLinkDensity(node: XmlNode): number {
  const totalText = getNodeText(node).trim().length;
  if (totalText === 0) return 0;
  const links = findAllDescendants(node, (n) => n.localName === "a");
  let linkChars = 0;
  for (const a of links) {
    linkChars += getNodeText(a).trim().length;
  }
  return linkChars / totalText;
}

function scoreNode(node: XmlNode): number {
  let score = 0;
  switch (node.localName) {
    case "article":
      score += 40;
      break;
    case "main":
      score += 30;
      break;
    case "section":
      score += 10;
      break;
    case "div":
      score += 5;
      break;
    default:
      break;
  }

  const classAndId = getClassAndId(node);
  if (POSITIVE_HINTS.test(classAndId)) score += 25;
  if (NEGATIVE_HINTS.test(classAndId)) score -= 25;

  const paragraphs = findAllDescendants(
    node,
    (n) => n.localName === "p" || n.localName === "pre" || n.localName === "blockquote"
  );

  for (const p of paragraphs) {
    if (isNoiseNode(p)) continue;
    const text = getNodeText(p).trim();
    if (text.length < 25) continue;
    score += 1;
    score += text.split(",").length;
    score += Math.min(Math.floor(text.length / 100), 4);
  }

  const linkDensity = computeLinkDensity(node);
  score = score * (1 - Math.min(linkDensity, 0.9));

  return score;
}

function renderInlineMarkdown(node: XmlNode, baseUrl?: string): string {
  if (node.type === "text" || node.type === "cdata") {
    return node.text.replace(/\s+/g, " ");
  }
  if (isNoiseNode(node)) return "";

  const tag = node.localName;
  if (tag === "br") return "\n";
  if (tag === "code") {
    const inner = getNodeText(node).trim();
    return inner ? `\`${inner}\`` : "";
  }
  if (tag === "strong" || tag === "b") {
    const inner = node.children.map((c) => renderInlineMarkdown(c, baseUrl)).join("").trim();
    return inner ? `**${inner}**` : "";
  }
  if (tag === "em" || tag === "i") {
    const inner = node.children.map((c) => renderInlineMarkdown(c, baseUrl)).join("").trim();
    return inner ? `*${inner}*` : "";
  }
  if (tag === "a") {
    const inner = node.children.map((c) => renderInlineMarkdown(c, baseUrl)).join("").trim();
    const href = resolveUrl(node.attributes["href"], baseUrl);
    if (!inner) return "";
    if (!href || href.startsWith("javascript:") || href.startsWith("#")) return inner;
    return `[${inner}](${href})`;
  }
  if (tag === "img") {
    const src = resolveUrl(
      node.attributes["src"] || node.attributes["data-src"],
      baseUrl
    );
    if (!src) return "";
    const alt = (node.attributes["alt"] ?? "").trim();
    return `![${alt}](${src})`;
  }

  return node.children.map((c) => renderInlineMarkdown(c, baseUrl)).join("");
}

function renderBlocksToMarkdown(node: XmlNode, baseUrl?: string): string[] {
  if (isNoiseNode(node)) return [];

  const blocks: string[] = [];
  const tag = node.localName;

  if (/^h[1-6]$/.test(tag)) {
    const level = Number(tag[1]);
    const text = getNodeText(node).replace(/\s+/g, " ").trim();
    if (text) {
      blocks.push(`${"#".repeat(level)} ${text}`);
    }
    return blocks;
  }

  if (tag === "p") {
    const inline = node.children
      .map((c) => renderInlineMarkdown(c, baseUrl))
      .join("")
      .replace(/[ \t]+/g, " ")
      .trim();
    if (inline) blocks.push(inline);
    return blocks;
  }

  if (tag === "pre") {
    const rawCode = getNodeText(node).replace(/^\n+|\n+$/g, "");
    if (rawCode.trim()) {
      blocks.push(`\`\`\`\n${rawCode}\n\`\`\``);
    }
    return blocks;
  }

  if (tag === "blockquote") {
    const text = getNodeText(node).replace(/\s+/g, " ").trim();
    if (text) {
      blocks.push(`> ${text}`);
    }
    return blocks;
  }

  if (tag === "ul" || tag === "ol") {
    const items = node.children.filter(
      (c) => c.type === "element" && c.localName === "li"
    );
    const lines: string[] = [];
    items.forEach((li, idx) => {
      const itemText = li.children
        .map((c) => renderInlineMarkdown(c, baseUrl))
        .join("")
        .replace(/\s+/g, " ")
        .trim();
      if (itemText) {
        const prefix = tag === "ol" ? `${idx + 1}. ` : "- ";
        lines.push(`${prefix}${itemText}`);
      }
    });
    if (lines.length > 0) {
      blocks.push(lines.join("\n"));
    }
    return blocks;
  }

  for (const child of node.children) {
    if (child.type === "element") {
      blocks.push(...renderBlocksToMarkdown(child, baseUrl));
    } else if (
      (child.type === "text" || child.type === "cdata") &&
      (tag === "article" || tag === "main" || tag === "div" || tag === "section")
    ) {
      const trimmed = child.text.replace(/\s+/g, " ").trim();
      if (trimmed.length > 40) {
        blocks.push(trimmed);
      }
    }
  }

  return blocks;
}

export function extractReadableContent(
  html: string,
  pageUrl?: string
): ExtractedArticleContent {
  const doc = parseMarkup(html, { htmlMode: true });

  // Extract metadata from <meta> and <title>
  const metaNodes = findAllDescendants(doc, (n) => n.localName === "meta");
  const metaMap: Record<string, string> = {};
  for (const m of metaNodes) {
    const key = (
      m.attributes["property"] ||
      m.attributes["name"] ||
      m.attributes["itemprop"] ||
      ""
    )
      .toLowerCase()
      .trim();
    const content = (m.attributes["content"] ?? "").trim();
    if (key && content && !metaMap[key]) {
      metaMap[key] = content;
    }
  }

  const titleNode = findAllDescendants(doc, (n) => n.localName === "title")[0];
  const h1Node = findAllDescendants(doc, (n) => n.localName === "h1")[0];

  const title =
    metaMap["og:title"] ||
    metaMap["twitter:title"] ||
    (h1Node ? getNodeText(h1Node).replace(/\s+/g, " ").trim() : "") ||
    (titleNode ? getNodeText(titleNode).replace(/\s+/g, " ").trim() : "") ||
    undefined;

  const byline =
    metaMap["author"] ||
    metaMap["article:author"] ||
    metaMap["dc.creator"] ||
    metaMap["byl"] ||
    undefined;

  const siteName = metaMap["og:site_name"] || undefined;
  const excerpt =
    metaMap["og:description"] ||
    metaMap["description"] ||
    metaMap["twitter:description"] ||
    undefined;

  let leadImageUrl = resolveUrl(
    metaMap["og:image"] || metaMap["twitter:image"],
    pageUrl
  );

  // Score candidate containers
  const candidateTags = new Set(["article", "main", "section", "div"]);
  const candidates = findAllDescendants(
    doc,
    (n) => candidateTags.has(n.localName) && !isNoiseNode(n)
  );

  let bestNode: XmlNode | undefined;
  let bestScore = -Infinity;

  for (const candidate of candidates) {
    const s = scoreNode(candidate);
    if (s > bestScore) {
      bestScore = s;
      bestNode = candidate;
    }
  }

  if (!bestNode) {
    bestNode =
      findAllDescendants(doc, (n) => n.localName === "body")[0] ?? doc;
  }

  if (!leadImageUrl) {
    const firstImg = findAllDescendants(
      bestNode,
      (n) => n.localName === "img" && Boolean(n.attributes["src"])
    )[0];
    if (firstImg) {
      leadImageUrl = resolveUrl(firstImg.attributes["src"], pageUrl);
    }
  }

  const markdownBlocks = renderBlocksToMarkdown(bestNode, pageUrl);
  let markdownContent = markdownBlocks.join("\n\n").trim();

  // Plain text extraction from blocks (or fallback to stripped text)
  let textContent = markdownBlocks
    .map((b) =>
      b
        .replace(/^#+\s+/gm, "")
        .replace(/^>\s+/gm, "")
        .replace(/!\[[^\]]*\]\([^)]+\)/g, "")
        .replace(/\[([^\]]+)\]\([^)]+\)/g, "$1")
        .replace(/(\*\*|\*|`)/g, "")
        .trim()
    )
    .filter(Boolean)
    .join("\n\n")
    .trim();

  if (!textContent) {
    textContent = getNodeText(bestNode)
      .replace(/[ \t]+/g, " ")
      .replace(/\n\s*\n+/g, "\n\n")
      .trim();
    markdownContent = textContent;
  }

  const words = textContent ? textContent.split(/\s+/).filter(Boolean) : [];
  const wordCount = words.length;
  const readingTimeMinutes = Math.max(1, Math.ceil(wordCount / 220));

  return {
    title,
    byline,
    siteName,
    excerpt: excerpt || textContent.slice(0, 280) || undefined,
    textContent,
    markdownContent,
    leadImageUrl: leadImageUrl || undefined,
    wordCount,
    readingTimeMinutes,
    extractedAt: Date.now(),
  };
}

export async function fetchAndExtractArticle(
  url: string,
  timeoutMs = 10000
): Promise<ExtractedArticleContent> {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);
  try {
    const response = await fetch(url, {
      signal: controller.signal,
      headers: {
        "User-Agent": "OyezNewsBot/0.1 (+https://github.com/oyez; News Aggregator)",
        Accept: "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
      },
    });
    if (!response.ok) {
      throw new Error(`HTTP ${response.status} fetching article ${url}`);
    }
    const html = await response.text();
    return extractReadableContent(html, url);
  } finally {
    clearTimeout(timer);
  }
}
