import type { Article, ScoredArticle } from "../types.ts";
import { popcnt32 } from "./bitset.ts";

/**
 * Fast 64-bit SplitMix hash represented as two 32-bit unsigned integers [hi, lo]
 * Uses Math.imul for zero-allocation 32-bit mixing.
 */
export function hashToken64Words(token: string): [number, number] {
  const bHash = Bun.hash(token);
  let lo: number;
  let hi: number;
  if (typeof bHash === "bigint") {
    lo = Number(bHash & 0xffffffffn) >>> 0;
    hi = Number((bHash >> 32n) & 0xffffffffn) >>> 0;
  } else {
    lo = bHash >>> 0;
    hi = Math.imul(lo ^ (lo >>> 16), 0x85ebca6b) >>> 0;
  }

  // Extra avalanche pass across hi/lo
  lo = Math.imul(lo ^ (hi >>> 13), 0xc2b2ae35) >>> 0;
  hi = Math.imul(hi ^ (lo >>> 15), 0x27d4eb2f) >>> 0;
  lo = (lo ^ (hi >>> 16)) >>> 0;
  return [hi, lo];
}

export function computeSimHash64Words(
  termWeights: Map<string, number>
): [number, number] {
  const accum = new Float64Array(64);

  for (const [term, weight] of termWeights.entries()) {
    const [hi, lo] = hashToken64Words(term);
    for (let bit = 0; bit < 32; bit++) {
      accum[bit] += (lo >>> bit) & 1 ? weight : -weight;
      accum[bit + 32] += (hi >>> bit) & 1 ? weight : -weight;
    }
  }

  let loOut = 0;
  let hiOut = 0;
  for (let bit = 0; bit < 32; bit++) {
    if (accum[bit] > 0) loOut |= 1 << bit;
    if (accum[bit + 32] > 0) hiOut |= 1 << bit;
  }

  return [hiOut >>> 0, loOut >>> 0];
}

export function formatSimHash64Hex(hi: number, lo: number): string {
  return (
    (hi >>> 0).toString(16).padStart(8, "0") +
    (lo >>> 0).toString(16).padStart(8, "0")
  );
}

export function parseSimHash64Words(hex: string): [number, number] {
  if (!hex) return [0, 0];
  const padded = hex.padStart(16, "0");
  const hi = parseInt(padded.slice(0, 8), 16) >>> 0;
  const lo = parseInt(padded.slice(8, 16), 16) >>> 0;
  return [hi, lo];
}

export function simHashHammingDistanceWords(
  hiA: number,
  loA: number,
  hiB: number,
  loB: number
): number {
  return popcnt32(hiA ^ hiB) + popcnt32(loA ^ loB);
}

/**
 * 64-bit Charikar Locality-Sensitive SimHash Fingerprint
 */
export function computeSimHash64(termWeights: Map<string, number>): string {
  const [hi, lo] = computeSimHash64Words(termWeights);
  return formatSimHash64Hex(hi, lo);
}

export function simHashHammingDistance(hexA: string, hexB: string): number {
  if (!hexA || !hexB) return 64;
  const [hiA, loA] = parseSimHash64Words(hexA);
  const [hiB, loB] = parseSimHash64Words(hexB);
  return popcnt32(hiA ^ hiB) + popcnt32(loA ^ loB);
}

export function simHashSimilarity(hexA: string, hexB: string): number {
  const dist = simHashHammingDistance(hexA, hexB);
  return Number(((64 - dist) / 64).toFixed(4));
}

/**
 * Submodular Maximum Marginal Relevance (MMR) Diversification
 */
export function diversifyWithMMR(
  candidates: ScoredArticle[],
  limit: number,
  lambda = 0.7
): ScoredArticle[] {
  if (candidates.length <= 1 || limit <= 1) {
    return candidates.slice(0, limit);
  }

  const clampedLambda = Math.min(Math.max(lambda, 0), 1);
  const maxScore = Math.max(...candidates.map((c) => c.score), 1e-6);

  // Pre-parse SimHash words once for all candidates
  const n = candidates.length;
  const hiArr = new Uint32Array(n);
  const loArr = new Uint32Array(n);
  for (let i = 0; i < n; i++) {
    const [hi, lo] = parseSimHash64Words(candidates[i].article.simhash64);
    hiArr[i] = hi;
    loArr[i] = lo;
  }

  const selectedIndices: number[] = [0];
  const used = new Uint8Array(n);
  used[0] = 1;

  while (selectedIndices.length < limit && selectedIndices.length < n) {
    let bestIdx = -1;
    let bestMmr = -Infinity;

    for (let i = 1; i < n; i++) {
      if (used[i]) continue;
      const cand = candidates[i];
      const normRel = cand.score / maxScore;
      const candArt = cand.article;
      const cHi = hiArr[i];
      const cLo = loArr[i];

      let maxRedundancy = 0;
      for (let s = 0; s < selectedIndices.length; s++) {
        const sIdx = selectedIndices[s];
        const selArt = candidates[sIdx].article;
        const dist =
          popcnt32(cHi ^ hiArr[sIdx]) + popcnt32(cLo ^ loArr[sIdx]);
        const lshSim = (64 - dist) / 64;
        const sameFeed = candArt.feedId === selArt.feedId ? 0.25 : 0;
        const sameCountry = candArt.country === selArt.country ? 0.1 : 0;
        const sameRegion = candArt.region === selArt.region ? 0.08 : 0;
        const sim = Math.min(
          1,
          lshSim * 0.65 + sameFeed + sameCountry + sameRegion
        );
        if (sim > maxRedundancy) maxRedundancy = sim;
      }

      const mmrScore =
        clampedLambda * normRel - (1 - clampedLambda) * maxRedundancy;
      if (mmrScore > bestMmr) {
        bestMmr = mmrScore;
        bestIdx = i;
      }
    }

    if (bestIdx === -1) break;
    used[bestIdx] = 1;
    selectedIndices.push(bestIdx);
  }

  return selectedIndices.map((idx) => candidates[idx]);
}

export interface PageRankEntry {
  key: string;
  score: number;
  degree: number;
}

export interface BipartitePageRankResult {
  topEntities: PageRankEntry[];
  topTopics: PageRankEntry[];
  topCountries: PageRankEntry[];
  topArticles: Array<{
    id: string;
    title: string;
    feedTitle: string;
    region: string;
    country: string;
    newsType: string;
    centralityScore: number;
  }>;
}

/**
 * CSR / TypedArray Power-Iteration Eigenvector Centrality (PageRank)
 * Uses flat integer node IDs and Float64Array vectors for cache-local convergence.
 */
export function computeEntityPageRank(
  articles: Article[],
  iterations = 20,
  damping = 0.85,
  topK = 15
): BipartitePageRankResult {
  if (articles.length === 0) {
    return {
      topEntities: [],
      topTopics: [],
      topCountries: [],
      topArticles: [],
    };
  }

  const nodeToId = new Map<string, number>();
  const idToNode: string[] = [];
  const adj: number[][] = [];
  const docMap = new Map<number, Article>();

  const getNodeId = (key: string): number => {
    let id = nodeToId.get(key);
    if (id === undefined) {
      id = idToNode.length;
      nodeToId.set(key, id);
      idToNode.push(key);
      adj.push([]);
    }
    return id;
  };

  const addEdge = (u: number, v: number) => {
    adj[u].push(v);
    adj[v].push(u);
  };

  for (const art of articles) {
    const docId = getNodeId(`doc:${art.id}`);
    docMap.set(docId, art);

    for (const ent of art.entities.organizationsAndPeople) {
      addEdge(docId, getNodeId(`ent:${ent}`));
    }
    for (const top of art.entities.topics) {
      addEdge(docId, getNodeId(`top:${top}`));
    }
    for (const geo of art.entities.mentionedCountries) {
      addEdge(docId, getNodeId(`geo:${geo}`));
    }
  }

  const N = idToNode.length;
  if (N === 0) {
    return {
      topEntities: [],
      topTopics: [],
      topCountries: [],
      topArticles: [],
    };
  }

  let ranks = new Float64Array(N);
  let nextRanks = new Float64Array(N);
  const initVal = 1 / N;
  ranks.fill(initVal);
  const teleport = (1 - damping) / N;

  for (let iter = 0; iter < iterations; iter++) {
    nextRanks.fill(teleport);
    for (let u = 0; u < N; u++) {
      const neighbors = adj[u];
      const deg = neighbors.length;
      if (deg === 0) continue;
      const share = (damping * ranks[u]) / deg;
      for (let k = 0; k < deg; k++) {
        nextRanks[neighbors[k]] += share;
      }
    }
    const tmp = ranks;
    ranks = nextRanks;
    nextRanks = tmp;
  }

  const entitiesList: PageRankEntry[] = [];
  const topicsList: PageRankEntry[] = [];
  const countriesList: PageRankEntry[] = [];
  const articlesList: Array<{
    id: string;
    title: string;
    feedTitle: string;
    region: string;
    country: string;
    newsType: string;
    centralityScore: number;
  }> = [];

  for (let u = 0; u < N; u++) {
    const node = idToNode[u];
    const degree = adj[u].length;
    const scaled = Number((ranks[u] * 1000).toFixed(4));
    if (node.startsWith("ent:")) {
      entitiesList.push({ key: node.slice(4), score: scaled, degree });
    } else if (node.startsWith("top:")) {
      topicsList.push({ key: node.slice(4), score: scaled, degree });
    } else if (node.startsWith("geo:")) {
      countriesList.push({ key: node.slice(4), score: scaled, degree });
    } else if (node.startsWith("doc:")) {
      const art = docMap.get(u);
      if (art) {
        articlesList.push({
          id: art.id,
          title: art.title,
          feedTitle: art.feedTitle,
          region: art.region,
          country: art.country,
          newsType: art.newsType,
          centralityScore: scaled,
        });
      }
    }
  }

  entitiesList.sort((a, b) => b.score - a.score);
  topicsList.sort((a, b) => b.score - a.score);
  countriesList.sort((a, b) => b.score - a.score);
  articlesList.sort((a, b) => b.centralityScore - a.centralityScore);

  return {
    topEntities: entitiesList.slice(0, topK),
    topTopics: topicsList.slice(0, topK),
    topCountries: countriesList.slice(0, topK),
    topArticles: articlesList.slice(0, topK),
  };
}
