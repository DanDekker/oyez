import type { Article } from "../types.ts";

export interface OntologyMorphism {
  from: string;
  to: string;
  label: string;
}

export interface FormalConcept {
  id: string;
  /** Intent: closed set of shared attributes (categories, topics, newsType, region) */
  intent: string[];
  /** Extent size: number of articles satisfying all attributes in intent */
  extentSize: number;
  /** Sample article IDs in extent */
  extentArticleIds: string[];
  /** Direct superconcept IDs in the Hasse diagram */
  superConcepts: string[];
  /** Direct subconcept IDs in the Hasse diagram */
  subConcepts: string[];
}

export interface PullbackWitness {
  pivotValue: string;
  pivotDimension: "topic" | "mentionedCountry" | "entity" | "ontologyCategory";
  leftArticle: {
    id: string;
    title: string;
    feedTitle: string;
    region: string;
    country: string;
    newsType: string;
  };
  rightArticle: {
    id: string;
    title: string;
    feedTitle: string;
    region: string;
    country: string;
    newsType: string;
  };
  sharedAttributes: string[];
}

/**
 * Finite Poset / Directed Acyclic Concept Category (C, <=)
 * Objects are news category & topic nodes; there is a unique morphism u -> v iff u <= v
 * (i.e. u is a refinement / subcategory of v).
 */
export class NewsOntologyCategory {
  private readonly objects = new Set<string>();
  /** Direct covering relations u -> v (Hasse diagram edges) */
  private readonly directEdges = new Map<string, Set<string>>();
  /** Transitive reflexive closure: u -> Set of all v such that u <= v (Upward closure / Principal Filter) */
  private readonly upwardClosure = new Map<string, Set<string>>();
  /** Inverse transitive reflexive closure: v -> Set of all u such that u <= v (Downward closure / Principal Ideal) */
  private readonly downwardClosure = new Map<string, Set<string>>();

  constructor(edges: Array<[string, string]>) {
    for (const [sub, sup] of edges) {
      const u = sub.toLowerCase().trim();
      const v = sup.toLowerCase().trim();
      this.objects.add(u);
      this.objects.add(v);
      let set = this.directEdges.get(u);
      if (!set) {
        set = new Set();
        this.directEdges.set(u, set);
      }
      set.add(v);
    }
    this.recomputeTransitiveClosure();
  }

  private recomputeTransitiveClosure(): void {
    this.upwardClosure.clear();
    this.downwardClosure.clear();

    for (const obj of this.objects) {
      this.upwardClosure.set(obj, new Set([obj]));
      this.downwardClosure.set(obj, new Set([obj]));
    }

    // BFS/DFS from each node to build reflexive-transitive closure
    for (const start of this.objects) {
      const visited = new Set<string>([start]);
      const queue: string[] = [start];
      while (queue.length > 0) {
        const curr = queue.shift()!;
        const nexts = this.directEdges.get(curr);
        if (!nexts) continue;
        for (const nxt of nexts) {
          if (!visited.has(nxt)) {
            visited.add(nxt);
            queue.push(nxt);
            this.upwardClosure.get(start)!.add(nxt);
            this.downwardClosure.get(nxt)!.add(start);
          }
        }
      }
    }
  }

  getObjects(): string[] {
    return Array.from(this.objects).sort();
  }

  hasMorphism(u: string, v: string): boolean {
    const normU = u.toLowerCase().trim();
    const normV = v.toLowerCase().trim();
    return this.upwardClosure.get(normU)?.has(normV) ?? false;
  }

  /**
   * Principal Filter (Upward Closure): ↑u = { v in Ob(C) | u <= v }
   * Used by the contravariant presheaf index functor F: C^op -> FinSet
   */
  upwardClosureOf(terms: Iterable<string>): string[] {
    const result = new Set<string>();
    for (const raw of terms) {
      const t = raw.toLowerCase().trim();
      if (!t) continue;
      const ancestors = this.upwardClosure.get(t);
      if (ancestors) {
        for (const a of ancestors) result.add(a);
      } else {
        result.add(t);
        result.add("all");
      }
    }
    return Array.from(result);
  }

  /**
   * Principal Ideal (Downward Closure): ↓v = { u in Ob(C) | u <= v }
   */
  downwardClosureOf(v: string): string[] {
    const normV = v.toLowerCase().trim();
    return Array.from(this.downwardClosure.get(normV) ?? [normV]);
  }

  /**
   * Categorical Join (Least Upper Bound / Minimal Common Ancestors of u and v)
   */
  join(u: string, v: string): string[] {
    const upU = this.upwardClosure.get(u.toLowerCase().trim()) ?? new Set();
    const upV = this.upwardClosure.get(v.toLowerCase().trim()) ?? new Set();
    const commonUpper: string[] = [];
    for (const x of upU) {
      if (upV.has(x)) commonUpper.push(x);
    }
    // Minimal elements among common upper bounds
    return commonUpper.filter(
      (x) =>
        !commonUpper.some(
          (y) => y !== x && this.hasMorphism(y, x)
        )
    );
  }

  /**
   * Categorical Meet (Greatest Lower Bound / Maximal Common Descendants of u and v)
   */
  meet(u: string, v: string): string[] {
    const downU = this.downwardClosure.get(u.toLowerCase().trim()) ?? new Set();
    const downV = this.downwardClosure.get(v.toLowerCase().trim()) ?? new Set();
    const commonLower: string[] = [];
    for (const x of downU) {
      if (downV.has(x)) commonLower.push(x);
    }
    // Maximal elements among common lower bounds
    return commonLower.filter(
      (x) =>
        !commonLower.some(
          (y) => y !== x && this.hasMorphism(x, y)
        )
    );
  }

  /**
   * Left Kan Extension (Lan_i S) of a relevance valuation S: Ob(C) -> R+
   * Aggregates subcategory scores upward along morphisms u -> v.
   */
  leftKanExtendScores(
    valuation: Record<string, number>,
    semiring: "tropical-max" | "weighted-sum" = "weighted-sum"
  ): Record<string, number> {
    const extended: Record<string, number> = {};
    for (const obj of this.objects) {
      const descendants = this.downwardClosure.get(obj) ?? new Set([obj]);
      let acc = 0;
      for (const d of descendants) {
        const val = valuation[d] ?? 0;
        if (val <= 0) continue;
        const discount = d === obj ? 1.0 : 0.85;
        if (semiring === "tropical-max") {
          acc = Math.max(acc, val * discount);
        } else {
          acc += val * discount;
        }
      }
      if (acc > 0) {
        extended[obj] = Number(acc.toFixed(4));
      }
    }
    return extended;
  }

  toHasseDiagram(): Array<{ node: string; parents: string[]; ancestorsCount: number; descendantsCount: number }> {
    return this.getObjects().map((node) => ({
      node,
      parents: Array.from(this.directEdges.get(node) ?? []),
      ancestorsCount: this.upwardClosure.get(node)?.size ?? 1,
      descendantsCount: this.downwardClosure.get(node)?.size ?? 1,
    }));
  }
}

/**
 * Canonical Oyez News Ontology Poset Category
 * Includes multiple inheritance (e.g. cyber-warfare <= cybersecurity AND conflict-and-security;
 * export-controls <= trade-and-supply-chain AND geopolitics-and-diplomacy).
 */
export const OYEZ_NEWS_ONTOLOGY = new NewsOntologyCategory([
  // Top-level domain morphisms -> "all"
  ["geopolitics-and-governance", "all"],
  ["conflict-and-security", "all"],
  ["economy-and-markets", "all"],
  ["technology-and-computing", "all"],
  ["science-and-nature", "all"],
  ["climate-and-energy", "all"],
  ["society-and-culture", "all"],
  ["investigative-and-accountability", "all"],

  // Feed category aliases -> domain nodes
  ["world", "geopolitics-and-governance"],
  ["regional", "geopolitics-and-governance"],
  ["politics", "geopolitics-and-governance"],
  ["technology", "technology-and-computing"],
  ["science", "science-and-nature"],
  ["investigative", "investigative-and-accountability"],
  ["culture", "society-and-culture"],
  ["general", "all"],

  // Fine-grained topics -> Intermediate & Top-Level Domains (with diamond / lattice structure)
  ["geopolitics-and-diplomacy", "geopolitics-and-governance"],
  ["elections-and-democracy", "geopolitics-and-governance"],
  ["defense-and-conflict", "conflict-and-security"],
  ["defense-and-conflict", "geopolitics-and-governance"],

  ["ai-and-machine-learning", "technology-and-computing"],
  ["ai-and-machine-learning", "science-and-nature"],
  ["semiconductors-and-hardware", "technology-and-computing"],
  ["semiconductors-and-hardware", "trade-and-supply-chain"],
  ["open-source-and-software", "technology-and-computing"],

  ["cybersecurity", "technology-and-computing"],
  ["cybersecurity", "conflict-and-security"],

  ["macroeconomics-and-finance", "economy-and-markets"],
  ["trade-and-supply-chain", "economy-and-markets"],
  ["trade-and-supply-chain", "geopolitics-and-governance"],

  ["climate-and-environment", "climate-and-energy"],
  ["climate-and-environment", "science-and-nature"],
  ["energy-and-commodities", "climate-and-energy"],
  ["energy-and-commodities", "economy-and-markets"],

  ["public-health-and-biotech", "science-and-nature"],
  ["public-health-and-biotech", "society-and-culture"],
  ["space-and-aerospace", "science-and-nature"],
  ["space-and-aerospace", "technology-and-computing"],
]);

/**
 * Antitone Galois Connection & Formal Concept Analysis (FCA)
 * Given a sample of articles, builds the Formal Context (G, M, I) and extracts
 * closed Formal Concepts (Extent, Intent) ordered in a Concept Lattice.
 */
export function computeFormalConceptLattice(
  articles: Article[],
  minSupport = 2,
  maxConcepts = 25
): FormalConcept[] {
  if (articles.length === 0) return [];

  // Build incidence map: articleId -> Set of attributes, and attribute -> Set of articleIds
  const attrToDocs = new Map<string, Set<string>>();
  const docToAttrs = new Map<string, Set<string>>();

  for (const art of articles) {
    const attrs = new Set<string>([
      `region:${art.region}`,
      `type:${art.newsType}`,
      ...art.entities.topics.map((t) => `topic:${t}`),
      ...art.entities.mentionedCountries.slice(0, 4).map((c) => `geo:${c}`),
    ]);
    docToAttrs.set(art.id, attrs);
    for (const a of attrs) {
      let s = attrToDocs.get(a);
      if (!s) {
        s = new Set();
        attrToDocs.set(a, s);
      }
      s.add(art.id);
    }
  }

  // Filter attributes with support >= minSupport
  const frequentAttrs = Array.from(attrToDocs.entries())
    .filter(([, docs]) => docs.size >= minSupport)
    .sort((a, b) => b[1].size - a[1].size)
    .slice(0, 30);

  // Galois closure operator: given a candidate attribute set B,
  // Extent(B) = B' = intersection of attrToDocs[m] for m in B
  // Closure(B) = B'' = Intent(Extent(B)) = all attributes common to every doc in B'
  const extentOf = (attrs: Iterable<string>): Set<string> => {
    let result: Set<string> | null = null;
    for (const a of attrs) {
      const docs = attrToDocs.get(a);
      if (!docs) return new Set();
      if (result === null) {
        result = new Set(docs);
      } else {
        for (const d of result) {
          if (!docs.has(d)) result.delete(d);
        }
      }
    }
    return result ?? new Set();
  };

  const intentOf = (docs: Set<string>): string[] => {
    if (docs.size === 0) return [];
    let common: Set<string> | null = null;
    for (const d of docs) {
      const attrs = docToAttrs.get(d);
      if (!attrs) continue;
      if (common === null) {
        common = new Set(attrs);
      } else {
        for (const a of common) {
          if (!attrs.has(a)) common.delete(a);
        }
      }
    }
    return Array.from(common ?? []).sort();
  };

  const conceptsMap = new Map<
    string,
    { intent: string[]; extent: Set<string> }
  >();

  // Generate closed concepts from singletons and pairs of frequent attributes
  for (let i = 0; i < frequentAttrs.length; i++) {
    const [attrA] = frequentAttrs[i];
    const ext1 = extentOf([attrA]);
    if (ext1.size >= minSupport) {
      const int1 = intentOf(ext1);
      const key1 = int1.join("|");
      if (!conceptsMap.has(key1)) {
        conceptsMap.set(key1, { intent: int1, extent: ext1 });
      }
    }

    for (let j = i + 1; j < Math.min(frequentAttrs.length, i + 12); j++) {
      const [attrB] = frequentAttrs[j];
      const ext2 = extentOf([attrA, attrB]);
      if (ext2.size >= minSupport) {
        const int2 = intentOf(ext2);
        const key2 = int2.join("|");
        if (!conceptsMap.has(key2)) {
          conceptsMap.set(key2, { intent: int2, extent: ext2 });
        }
      }
    }
  }

  // Prioritize multi-attribute concepts (richer intersections) then support size
  const rawConcepts = Array.from(conceptsMap.values())
    .sort(
      (a, b) =>
        (b.intent.length > 1 ? 1 : 0) - (a.intent.length > 1 ? 1 : 0) ||
        b.extent.size - a.extent.size ||
        b.intent.length - a.intent.length
    )
    .slice(0, maxConcepts);

  const nodes: FormalConcept[] = rawConcepts.map((c, idx) => ({
    id: `concept_${idx + 1}`,
    intent: c.intent,
    extentSize: c.extent.size,
    extentArticleIds: Array.from(c.extent).slice(0, 6),
    superConcepts: [],
    subConcepts: [],
  }));

  // Compute partial order C1 < C2 iff Intent(C2) is a proper subset of Intent(C1)
  const isProperSubset = (sub: string[], sup: string[]): boolean => {
    if (sub.length >= sup.length) return false;
    const supSet = new Set(sup);
    return sub.every((x) => supSet.has(x));
  };

  for (let i = 0; i < nodes.length; i++) {
    for (let j = 0; j < nodes.length; j++) {
      if (i === j) continue;
      // C_i is a subconcept of C_j if Intent(C_j) is a proper subset of Intent(C_i)
      if (isProperSubset(nodes[j].intent, nodes[i].intent)) {
        // Check if direct covering (no intermediate concept k)
        const hasIntermediate = nodes.some(
          (kNode, k) =>
            k !== i &&
            k !== j &&
            isProperSubset(nodes[j].intent, kNode.intent) &&
            isProperSubset(kNode.intent, nodes[i].intent)
        );
        if (!hasIntermediate) {
          nodes[i].superConcepts.push(nodes[j].id);
          nodes[j].subConcepts.push(nodes[i].id);
        }
      }
    }
  }

  return nodes;
}

/**
 * Categorical Pullback (Fibered Product X x_Z Y)
 * Given two article slices X and Y (e.g. from two different world regions or epistemic newsTypes)
 * and a projection dimension Z ("topic" | "mentionedCountry" | "entity" | "ontologyCategory"),
 * constructs the universal pullback pairs (x, y, z) where f(x) = z = g(y).
 */
export function computeCategoricalPullback(
  leftSlice: Article[],
  rightSlice: Article[],
  pivotDimension: "topic" | "mentionedCountry" | "entity" | "ontologyCategory" = "topic",
  maxPairs = 20
): PullbackWitness[] {
  const project = (art: Article): string[] => {
    switch (pivotDimension) {
      case "topic":
        return art.entities.topics;
      case "mentionedCountry":
        return art.entities.mentionedCountries.filter((c) => c !== art.country);
      case "entity":
        return art.entities.organizationsAndPeople;
      case "ontologyCategory":
        return art.entities.ontologyClosure.filter((c) => c !== "all");
    }
  };

  // Index right fiber Y by pivot value z in Z
  const rightFiberByZ = new Map<string, Article[]>();
  for (const y of rightSlice) {
    for (const z of project(y)) {
      let bucket = rightFiberByZ.get(z);
      if (!bucket) {
        bucket = [];
        rightFiberByZ.set(z, bucket);
      }
      bucket.push(y);
    }
  }

  const witnesses: PullbackWitness[] = [];
  const seenPairs = new Set<string>();

  for (const x of leftSlice) {
    const xPivots = project(x);
    for (const z of xPivots) {
      const yCandidates = rightFiberByZ.get(z);
      if (!yCandidates) continue;

      for (const y of yCandidates) {
        if (x.id === y.id) continue;
        const pairKey = x.id < y.id ? `${x.id}:${y.id}` : `${y.id}:${x.id}`;
        if (seenPairs.has(pairKey)) continue;

        const sharedTopics = x.entities.topics.filter((t) =>
          y.entities.topics.includes(t)
        );
        const sharedCountries = x.entities.mentionedCountries.filter((c) =>
          y.entities.mentionedCountries.includes(c)
        );
        const sharedOrgs = x.entities.organizationsAndPeople.filter((o) =>
          y.entities.organizationsAndPeople.includes(o)
        );

        const sharedAttributes = Array.from(
          new Set([
            ...sharedTopics.map((t) => `topic:${t}`),
            ...sharedCountries.map((c) => `country:${c}`),
            ...sharedOrgs.map((o) => `entity:${o}`),
          ])
        );

        if (sharedAttributes.length >= 2) {
          seenPairs.add(pairKey);
          witnesses.push({
            pivotValue: z,
            pivotDimension,
            leftArticle: {
              id: x.id,
              title: x.title,
              feedTitle: x.feedTitle,
              region: x.region,
              country: x.country,
              newsType: x.newsType,
            },
            rightArticle: {
              id: y.id,
              title: y.title,
              feedTitle: y.feedTitle,
              region: y.region,
              country: y.country,
              newsType: y.newsType,
            },
            sharedAttributes,
          });
        }
      }
    }
  }

  witnesses.sort(
    (a, b) => b.sharedAttributes.length - a.sharedAttributes.length
  );
  return witnesses.slice(0, maxPairs);
}
