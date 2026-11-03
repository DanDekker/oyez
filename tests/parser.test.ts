import { describe, expect, test } from "bun:test";
import { parseFeed } from "../src/parser/feed.ts";
import { decodeEntities, stripHtmlToText } from "../src/parser/xml.ts";

describe("XML, RSS, Atom & JSON Feed Parser", () => {
  test("decodes named, decimal, and hex XML/HTML entities", () => {
    expect(
      decodeEntities("Oyez &amp; Co &mdash; &#8220;News&#8221; &#x2026;")
    ).toBe("Oyez & Co — “News” …");
    expect(
      stripHtmlToText("<p>Hello <b>World</b>!<script>evil()</script></p>")
    ).toBe("Hello World !");
  });

  test("parses RSS 2.0 with CDATA, dc:creator, content:encoded, and media:thumbnail", () => {
    const rssXml = `<?xml version="1.0" encoding="UTF-8"?>
      <rss version="2.0" xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:content="http://purl.org/rss/1.0/modules/content/" xmlns:media="http://search.yahoo.com/mrss/">
        <channel>
          <title>Oyez Daily</title>
          <link>https://oyez.example.com</link>
          <description>Public interest journalism</description>
          <language>en-us</language>
          <item>
            <title><![CDATA[Breakthrough in Distributed Indexing & Search]]></title>
            <link>/articles/distributed-indexing</link>
            <guid isPermaLink="false">oyez-item-001</guid>
            <pubDate>Tue, 03 Nov 2026 01:00:00 GMT</pubDate>
            <dc:creator>Ada Lovelace</dc:creator>
            <category>Technology</category>
            <category>Search</category>
            <media:thumbnail url="/images/thumb1.jpg" />
            <description><![CDATA[<p>Short summary of the breakthrough.</p>]]></description>
            <content:encoded><![CDATA[<p>Full body content of the distributed indexing story.</p>]]></content:encoded>
          </item>
        </channel>
      </rss>`;

    const feed = parseFeed(rssXml, "https://oyez.example.com/rss.xml");
    expect(feed.format).toBe("rss2");
    expect(feed.title).toBe("Oyez Daily");
    expect(feed.siteUrl).toBe("https://oyez.example.com");
    expect(feed.items.length).toBe(1);

    const item = feed.items[0];
    expect(item.title).toBe("Breakthrough in Distributed Indexing & Search");
    expect(item.url).toBe("https://oyez.example.com/articles/distributed-indexing");
    expect(item.guid).toBe("oyez-item-001");
    expect(item.author).toBe("Ada Lovelace");
    expect(item.categories).toEqual(["Technology", "Search"]);
    expect(item.imageUrl).toBe("https://oyez.example.com/images/thumb1.jpg");
    expect(item.summary).toBe("Short summary of the breakthrough.");
    expect(item.contentHtml).toContain("Full body content");
  });

  test("parses Atom 1.0 feed", () => {
    const atomXml = `<?xml version="1.0" encoding="utf-8"?>
      <feed xmlns="http://www.w3.org/2005/Atom">
        <title>Kernel Dispatch</title>
        <subtitle>Systems engineering updates</subtitle>
        <link href="https://kernel.example.org/" rel="alternate" />
        <entry>
          <title>Zero-copy networking arrives</title>
          <link href="https://kernel.example.org/posts/zero-copy" rel="alternate" />
          <id>urn:uuid:1225c695-cfb8-4ebb-aaaa-80da344efa6a</id>
          <published>2026-11-02T18:30:00Z</published>
          <author><name>Linus</name></author>
          <category term="systems" />
          <summary>A look at the new zero-copy packet ring.</summary>
        </entry>
      </feed>`;

    const feed = parseFeed(atomXml);
    expect(feed.format).toBe("atom");
    expect(feed.title).toBe("Kernel Dispatch");
    expect(feed.items.length).toBe(1);
    expect(feed.items[0].title).toBe("Zero-copy networking arrives");
    expect(feed.items[0].author).toBe("Linus");
    expect(feed.items[0].categories).toEqual(["systems"]);
    expect(feed.items[0].url).toBe("https://kernel.example.org/posts/zero-copy");
  });

  test("parses JSON Feed 1.1", () => {
    const jsonFeed = JSON.stringify({
      version: "https://jsonfeed.org/version/1.1",
      title: "JSON Wire",
      home_page_url: "https://wire.example.com",
      items: [
        {
          id: "jw-1",
          url: "https://wire.example.com/posts/1",
          title: "JSON Feed Support",
          content_text: "Native JSON feed ingestion works out of the box.",
          date_published: "2026-11-03T00:00:00Z",
          authors: [{ name: "Grace Hopper" }],
          tags: ["standards", "web"],
        },
      ],
    });

    const feed = parseFeed(jsonFeed);
    expect(feed.format).toBe("jsonfeed");
    expect(feed.title).toBe("JSON Wire");
    expect(feed.items.length).toBe(1);
    expect(feed.items[0].author).toBe("Grace Hopper");
    expect(feed.items[0].categories).toEqual(["standards", "web"]);
  });
});
