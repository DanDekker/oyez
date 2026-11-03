import { describe, expect, test } from "bun:test";
import { mkdtemp, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { createOyezApp } from "../src/api/server.ts";
import {
  computeSimHash64,
  simHashHammingDistance,
  simHashSimilarity,
} from "../src/index/algorithms.ts";
import { OYEZ_NEWS_ONTOLOGY } from "../src/index/category_theory.ts";
import { OyezIndexEngine } from "../src/index/engine.ts";
import { classifyNewsType } from "../src/index/entities.ts";

describe("Category Theory, Ontology Poset, Galois FCA, Pullbacks & Algorithms", () => {
  test("Poset category laws, diamond lattice Join/Meet, and Left Kan Extension", () => {
    // Reflexivity & Transitivity: semiconductors-and-hardware -> trade-and-supply-chain -> economy-and-markets -> all
    expect(
      OYEZ_NEWS_ONTOLOGY.hasMorphism(
        "semiconductors-and-hardware",
        "semiconductors-and-hardware"
      )
    ).toBe(true);
    expect(
      OYEZ_NEWS_ONTOLOGY.hasMorphism(
        "semiconductors-and-hardware",
        "trade-and-supply-chain"
      )
    ).toBe(true);
    expect(
      OYEZ_NEWS_ONTOLOGY.hasMorphism(
        "semiconductors-and-hardware",
        "economy-and-markets"
      )
    ).toBe(true);
    expect(
      OYEZ_NEWS_ONTOLOGY.hasMorphism("semiconductors-and-hardware", "all")
    ).toBe(true);

    // Join (Least Upper Bounds): cybersecurity and defense-and-conflict both refine conflict-and-security
    const joinBounds = OYEZ_NEWS_ONTOLOGY.join(
      "cybersecurity",
      "defense-and-conflict"
    );
    expect(joinBounds).toContain("conflict-and-security");

    // Left Kan Extension propagates leaf topic scores to parent domains
    const kan = OYEZ_NEWS_ONTOLOGY.leftKanExtendScores(
      { "ai-and-machine-learning": 10, cybersecurity: 6 },
      "weighted-sum"
    );
    expect(kan["technology-and-computing"]).toBeGreaterThan(10);
    expect(kan["science-and-nature"]).toBeGreaterThan(0);
    expect(kan["conflict-and-security"]).toBeGreaterThan(0);
  });

  test("classifyNewsType distinguishes epistemic genres", () => {
    expect(
      classifyNewsType(
        "BREAKING: Central Bank Announces Emergency Rate Cut",
        "Urgent dispatch from Frankfurt.",
        ""
      )
    ).toBe("breaking-dispatch");

    expect(
      classifyNewsType(
        "Scientists Discover Room-Temperature Quantum Coherence in Lab",
        "A peer-reviewed study published in Nature.",
        ""
      )
    ).toBe("scientific-research");

    expect(
      classifyNewsType(
        "Secret Offshore Ledger Leaked in Cross-Border Investigation",
        "Whistleblower documents revealed by investigative reporters.",
        ""
      )
    ).toBe("investigative-report");

    expect(
      classifyNewsType(
        "Explained: How Submodular Optimization Shapes Information Retrieval",
        "Everything you need to know about greedy approximation guarantees.",
        ""
      )
    ).toBe("explainer-and-analysis");
  });

  test("64-bit Charikar SimHash preserves cosine similarity under bitwise Hamming distance", () => {
    const docA = new Map<string, number>([
      ["semiconductor", 5],
      ["tsmc", 4],
      ["wafer", 3],
      ["taipei", 3],
      ["foundry", 2],
    ]);
    const docANear = new Map<string, number>([
      ["semiconductor", 5],
      ["tsmc", 4],
      ["wafer", 3],
      ["taipei", 3],
      ["lithography", 1],
    ]);
    const docUnrelated = new Map<string, number>([
      ["sourdough", 5],
      ["fermentation", 4],
      ["bakery", 3],
      ["flour", 3],
      ["crust", 2],
    ]);

    const hashA = computeSimHash64(docA);
    const hashANear = computeSimHash64(docANear);
    const hashUnrelated = computeSimHash64(docUnrelated);

    expect(hashA.length).toBe(16);
    const nearDist = simHashHammingDistance(hashA, hashANear);
    const farDist = simHashHammingDistance(hashA, hashUnrelated);

    expect(nearDist).toBeLessThan(10);
    expect(farDist).toBeGreaterThan(nearDist);
    expect(simHashSimilarity(hashA, hashANear)).toBeGreaterThan(0.8);
  });

  test("Presheaf fiber indexing, Galois FCA Concept Lattice, Categorical Pullbacks, PageRank & MMR", async () => {
    const tempDir = await mkdtemp(join(tmpdir(), "oyez-cat-test-"));
    try {
      const engine = new OyezIndexEngine(tempDir);
      await engine.init();
      const app = createOyezApp(engine);
      const now = Date.now();

      const feedEu = await engine.upsertFeed({
        id: "feed_eu",
        url: "https://eu.example.org/rss",
        title: "European Dispatch",
        region: "europe",
        country: "DE",
        language: "en",
        category: "world",
        tags: ["europe"],
        enabled: true,
        pollIntervalSeconds: 900,
        scrapeFullContent: false,
        nextPollAt: now,
        consecutiveFailures: 0,
        createdAt: now,
        updatedAt: now,
      });

      const feedAsia = await engine.upsertFeed({
        id: "feed_asia",
        url: "https://asia.example.jp/rss",
        title: "Tokyo Tech Wire",
        region: "east-asia",
        country: "JP",
        language: "en",
        category: "technology",
        tags: ["asia"],
        enabled: true,
        pollIntervalSeconds: 900,
        scrapeFullContent: false,
        nextPollAt: now,
        consecutiveFailures: 0,
        createdAt: now,
        updatedAt: now,
      });

      await engine.ingestFeedItems(feedEu, [
        {
          guid: "eu-1",
          url: "https://eu.example.org/1",
          title: "Parliament Passes Semiconductor Export Control Regulation with TSMC and European Union",
          summary: "Berlin and Taipei coordinate on microchip wafer supply chains and artificial intelligence.",
          categories: ["policy"],
          publishedAt: now - 3000,
        },
        {
          guid: "eu-2",
          url: "https://eu.example.org/2",
          title: "Parliament Passes Semiconductor Export Control Regulation with TSMC in Berlin",
          summary: "Berlin and Taipei coordinate on microchip wafer supply chains and artificial intelligence rules.",
          categories: ["policy"],
          publishedAt: now - 2500,
        },
      ]);

      await engine.ingestFeedItems(feedAsia, [
        {
          guid: "asia-1",
          url: "https://asia.example.jp/1",
          title: "Study Finds New TSMC Semiconductor Wafer Lithography Boosts Artificial Intelligence in Taipei",
          summary: "Researchers in Japan and Taiwan partner with European Union labs on microchip foundry efficiency.",
          categories: ["research"],
          publishedAt: now - 1000,
        },
      ]);

      // 1. Contravariant Presheaf Functoriality:
      // "semiconductors-and-hardware" <= "trade-and-supply-chain" <= "economy-and-markets"
      // So querying category="economy-and-markets" automatically returns all 3 articles!
      const econFiber = engine.search({ category: "economy-and-markets" });
      expect(econFiber.total).toBe(3);

      // 2. Categorical Pullback between Europe and East Asia over pivot="topic"
      const pullbackRes = await app.fetch(
        new Request(
          "http://localhost/api/v1/pullback?leftRegion=europe&rightRegion=east-asia&pivot=topic"
        )
      );
      expect(pullbackRes.status).toBe(200);
      const pullbackJson = (await pullbackRes.json()) as {
        totalWitnesses: number;
      };
      expect(pullbackJson.totalWitnesses).toBeGreaterThanOrEqual(2);

      // 3. Formal Concept Lattice (Galois Connection)
      const conceptsRes = await app.fetch(
        new Request("http://localhost/api/v1/concepts?minSupport=2")
      );
      expect(conceptsRes.status).toBe(200);
      const conceptsJson = (await conceptsRes.json()) as {
        totalConcepts: number;
      };
      expect(conceptsJson.totalConcepts).toBeGreaterThan(0);

      // 4. Bipartite Entity-Article PageRank Centrality
      const prRes = await app.fetch(
        new Request("http://localhost/api/v1/centrality")
      );
      expect(prRes.status).toBe(200);
      const prJson = (await prRes.json()) as {
        topEntities: Array<{ key: string; score: number }>;
      };
      expect(prJson.topEntities.some((e) => e.key === "TSMC")).toBe(true);

      // 5. SimHash Near-Duplicate Detection between eu-1 and eu-2
      const eu1Id = econFiber.results.find((r) =>
        r.article.guid === "eu-1"
      )!.article.id;
      const dupRes = await app.fetch(
        new Request(
          `http://localhost/api/v1/articles/${eu1Id}/duplicates?maxDistance=20`
        )
      );
      expect(dupRes.status).toBe(200);
      const dupJson = (await dupRes.json()) as { count: number };
      expect(dupJson.count).toBeGreaterThanOrEqual(1);

      // 6. Submodular MMR Diversification prioritizes cross-region diversity over near-duplicate eu-2
      const mmrRes = engine.search({
        q: "TSMC semiconductor wafer",
        diversify: true,
        mmrLambda: 0.5,
        limit: 2,
      });
      const regionsInTop2 = new Set(mmrRes.results.map((r) => r.article.region));
      expect(regionsInTop2.size).toBe(2);
    } finally {
      await rm(tempDir, { recursive: true, force: true });
    }
  });
});
