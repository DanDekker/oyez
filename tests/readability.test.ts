import { describe, expect, test } from "bun:test";
import { extractReadableContent } from "../src/extractor/readability.ts";

describe("Readability Content Extractor", () => {
  test("extracts main article body, metadata, markdown, and strips navigation/ads", () => {
    const html = `<!DOCTYPE html>
      <html>
        <head>
          <title>Why Local-First Search Matters | The Herald</title>
          <meta property="og:title" content="Why Local-First Search Matters" />
          <meta name="author" content="Claude Shannon" />
          <meta property="og:site_name" content="The Herald" />
          <meta property="og:image" content="/assets/hero.png" />
          <meta name="description" content="An exploration of inverted indexes and BM25." />
        </head>
        <body>
          <nav class="main-nav">
            <a href="/">Home</a> | <a href="/login">Sign In</a> | <a href="/subscribe">Subscribe</a>
          </nav>
          <div class="sidebar-promo">
            <p>Buy our sponsor's product right now!</p>
          </div>
          <article class="post-content">
            <h1>Why Local-First Search Matters</h1>
            <p>Information retrieval has relied on inverted indexes, term frequency, and inverse document frequency for decades.</p>
            <p>By pairing <strong>Okapi BM25</strong> with an append-only log, applications can index thousands of articles per second without external database dependencies.</p>
            <blockquote>Simplicity is prerequisite for reliability.</blockquote>
            <ul>
              <li>Fast in-memory postings traversal</li>
              <li>Deterministic tokenization</li>
            </ul>
          </article>
          <footer class="site-footer">
            <p>Copyright 2026 The Herald. All rights reserved.</p>
          </footer>
        </body>
      </html>`;

    const extracted = extractReadableContent(html, "https://herald.example.com/posts/local-search");
    expect(extracted.title).toBe("Why Local-First Search Matters");
    expect(extracted.byline).toBe("Claude Shannon");
    expect(extracted.siteName).toBe("The Herald");
    expect(extracted.leadImageUrl).toBe("https://herald.example.com/assets/hero.png");
    expect(extracted.textContent).toContain("Information retrieval has relied on inverted indexes");
    expect(extracted.textContent).not.toContain("Copyright 2026 The Herald");
    expect(extracted.markdownContent).toContain("**Okapi BM25**");
    expect(extracted.markdownContent).toContain("> Simplicity is prerequisite for reliability.");
    expect(extracted.markdownContent).toContain("- Fast in-memory postings traversal");
    expect(extracted.wordCount).toBeGreaterThan(25);
    expect(extracted.readingTimeMinutes).toBe(1);
  });
});
