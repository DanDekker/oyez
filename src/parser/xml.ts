export interface XmlNode {
  type: "element" | "text" | "cdata";
  tag: string;
  localName: string;
  attributes: Record<string, string>;
  children: XmlNode[];
  text: string;
}

const NAMED_ENTITIES: Record<string, string> = {
  amp: "&",
  lt: "<",
  gt: ">",
  quot: '"',
  apos: "'",
  nbsp: " ",
  ndash: "–",
  mdash: "—",
  lsquo: "‘",
  rsquo: "’",
  ldquo: "“",
  rdquo: "”",
  hellip: "…",
  copy: "©",
  reg: "®",
  trade: "™",
  bull: "•",
  euro: "€",
  pound: "£",
  yen: "¥",
  cent: "¢",
  deg: "°",
  plusmn: "±",
  times: "×",
  divide: "÷",
  frac12: "½",
  frac14: "¼",
  frac34: "¾",
};

const HTML_VOID_TAGS = new Set([
  "area",
  "base",
  "br",
  "col",
  "embed",
  "hr",
  "img",
  "input",
  "link",
  "meta",
  "param",
  "source",
  "track",
  "wbr",
]);

export function decodeEntities(input: string): string {
  if (!input || !input.includes("&")) return input;
  return input.replace(/&(#(?:x[0-9a-fA-F]+|\d+)|[a-zA-Z0-9]+);/g, (full, entity: string) => {
    if (entity.startsWith("#x") || entity.startsWith("#X")) {
      const code = parseInt(entity.slice(2), 16);
      return Number.isNaN(code) || code <= 0 ? full : String.fromCodePoint(code);
    }
    if (entity.startsWith("#")) {
      const code = parseInt(entity.slice(1), 10);
      return Number.isNaN(code) || code <= 0 ? full : String.fromCodePoint(code);
    }
    const lower = entity.toLowerCase();
    return NAMED_ENTITIES[lower] ?? full;
  });
}

function parseAttributes(attrString: string): Record<string, string> {
  const attrs: Record<string, string> = {};
  const regex = /([a-zA-Z_:][a-zA-Z0-9_.:-]*)\s*(?:=\s*(?:"([^"]*)"|'([^']*)'|([^\s"'=<>`]+)))?/g;
  let match: RegExpExecArray | null;
  while ((match = regex.exec(attrString)) !== null) {
    const key = match[1].toLowerCase();
    const rawVal = match[2] ?? match[3] ?? match[4] ?? "";
    attrs[key] = decodeEntities(rawVal.trim());
  }
  return attrs;
}

export interface ParseMarkupOptions {
  htmlMode?: boolean;
}

export function parseMarkup(source: string, options: ParseMarkupOptions = {}): XmlNode {
  const htmlMode = options.htmlMode ?? false;
  const root: XmlNode = {
    type: "element",
    tag: "#document",
    localName: "#document",
    attributes: {},
    children: [],
    text: "",
  };

  const stack: XmlNode[] = [root];
  let pos = 0;
  const len = source.length;

  while (pos < len) {
    const lt = source.indexOf("<", pos);
    if (lt === -1) {
      const rawText = source.slice(pos);
      if (rawText) {
        stack[stack.length - 1].children.push({
          type: "text",
          tag: "#text",
          localName: "#text",
          attributes: {},
          children: [],
          text: decodeEntities(rawText),
        });
      }
      break;
    }

    if (lt > pos) {
      const rawText = source.slice(pos, lt);
      stack[stack.length - 1].children.push({
        type: "text",
        tag: "#text",
        localName: "#text",
        attributes: {},
        children: [],
        text: decodeEntities(rawText),
      });
    }

    // CDATA section
    if (source.startsWith("<![CDATA[", lt)) {
      const endCdata = source.indexOf("]]>", lt + 9);
      if (endCdata === -1) {
        const cdataText = source.slice(lt + 9);
        stack[stack.length - 1].children.push({
          type: "cdata",
          tag: "#cdata",
          localName: "#cdata",
          attributes: {},
          children: [],
          text: cdataText,
        });
        break;
      }
      const cdataText = source.slice(lt + 9, endCdata);
      stack[stack.length - 1].children.push({
        type: "cdata",
        tag: "#cdata",
        localName: "#cdata",
        attributes: {},
        children: [],
        text: cdataText,
      });
      pos = endCdata + 3;
      continue;
    }

    // Comment
    if (source.startsWith("<!--", lt)) {
      const endComment = source.indexOf("-->", lt + 4);
      pos = endComment === -1 ? len : endComment + 3;
      continue;
    }

    // Processing instruction <?...?>
    if (source.startsWith("<?", lt)) {
      const endPi = source.indexOf("?>", lt + 2);
      pos = endPi === -1 ? len : endPi + 2;
      continue;
    }

    // Doctype or other <! declaration
    if (source.startsWith("<!", lt)) {
      const endDecl = source.indexOf(">", lt + 2);
      pos = endDecl === -1 ? len : endDecl + 1;
      continue;
    }

    // Find end of tag, respecting quoted attributes
    let gt = lt + 1;
    let quote: string | null = null;
    while (gt < len) {
      const ch = source[gt];
      if (quote) {
        if (ch === quote) quote = null;
      } else {
        if (ch === '"' || ch === "'") quote = ch;
        else if (ch === ">") break;
      }
      gt++;
    }

    if (gt >= len) {
      break;
    }

    const rawTagContent = source.slice(lt + 1, gt).trim();
    pos = gt + 1;

    if (!rawTagContent) continue;

    // Closing tag </...>
    if (rawTagContent.startsWith("/")) {
      const closeTagName = rawTagContent.slice(1).trim().toLowerCase();
      const closeLocal = closeTagName.includes(":")
        ? closeTagName.split(":")[1]
        : closeTagName;

      for (let i = stack.length - 1; i >= 1; i--) {
        if (stack[i].tag === closeTagName || stack[i].localName === closeLocal) {
          stack.length = i;
          break;
        }
      }
      continue;
    }

    const selfClosing = rawTagContent.endsWith("/");
    const cleanTagContent = selfClosing
      ? rawTagContent.slice(0, -1).trim()
      : rawTagContent;

    const spaceIdx = cleanTagContent.search(/\s/);
    const rawTagName =
      spaceIdx === -1 ? cleanTagContent : cleanTagContent.slice(0, spaceIdx);
    const attrStr =
      spaceIdx === -1 ? "" : cleanTagContent.slice(spaceIdx + 1);

    const tag = rawTagName.toLowerCase();
    const localName = tag.includes(":") ? tag.split(":")[1] : tag;
    const attributes = attrStr ? parseAttributes(attrStr) : {};

    const node: XmlNode = {
      type: "element",
      tag,
      localName,
      attributes,
      children: [],
      text: "",
    };

    stack[stack.length - 1].children.push(node);

    // Raw text tags in HTML mode (script / style)
    if (htmlMode && (localName === "script" || localName === "style") && !selfClosing) {
      const closePattern = new RegExp(`</${localName}\\s*>`, "i");
      const rest = source.slice(pos);
      const closeMatch = closePattern.exec(rest);
      if (closeMatch && closeMatch.index !== undefined) {
        pos += closeMatch.index + closeMatch[0].length;
      } else {
        pos = len;
      }
      continue;
    }

    const isVoid = htmlMode && HTML_VOID_TAGS.has(localName);
    if (!selfClosing && !isVoid) {
      stack.push(node);
    }
  }

  return root;
}

export function getNodeText(node: XmlNode | undefined): string {
  if (!node) return "";
  if (node.type === "text" || node.type === "cdata") {
    return node.text;
  }
  let out = "";
  for (const child of node.children) {
    out += getNodeText(child);
  }
  return out;
}

export function findChild(
  parent: XmlNode,
  ...names: string[]
): XmlNode | undefined {
  const lowerNames = names.map((n) => n.toLowerCase());
  for (const child of parent.children) {
    if (child.type !== "element") continue;
    if (lowerNames.includes(child.tag) || lowerNames.includes(child.localName)) {
      return child;
    }
  }
  return undefined;
}

export function findChildren(
  parent: XmlNode,
  ...names: string[]
): XmlNode[] {
  const lowerNames = names.map((n) => n.toLowerCase());
  return parent.children.filter(
    (child) =>
      child.type === "element" &&
      (lowerNames.includes(child.tag) || lowerNames.includes(child.localName))
  );
}

export function findAllDescendants(
  root: XmlNode,
  predicate: (node: XmlNode) => boolean
): XmlNode[] {
  const matches: XmlNode[] = [];
  function walk(node: XmlNode) {
    if (node.type === "element") {
      if (predicate(node)) {
        matches.push(node);
      }
      for (const child of node.children) {
        walk(child);
      }
    }
  }
  walk(root);
  return matches;
}

export function stripHtmlToText(html: string): string {
  if (!html) return "";
  const cleaned = html
    .replace(/<script[\s\S]*?<\/script>/gi, " ")
    .replace(/<style[\s\S]*?<\/style>/gi, " ")
    .replace(/<(?:br|p|div|li|tr|h[1-6]|blockquote)[^>]*>/gi, "\n")
    .replace(/<[^>]+>/g, " ");
  return decodeEntities(cleaned)
    .replace(/[ \t]+/g, " ")
    .replace(/\n\s*\n+/g, "\n\n")
    .trim();
}
