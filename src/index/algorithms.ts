import type { Article, ScoredArticle } from "../types.ts";

const MASK64 = 0xffffffffffffffffn;

function splitMix64(seed: bigint): bigint {
  let z = (seed + 0x9e3779b97f4a7c15n) & MASK64;
  z = ((z ^ (z >> 30n)) * 0xbf58476d1ce4e5b9n) & MASK64;
  z = ((z ^ (z >> 27n)) * 0x94d049bb133111ebn) & MASK64;
  return (z ^ (z >> 31n)) & MASK64;
}

function hashToken64(token: string): bigint {
  let h = BigInt(Bun.hash(token)) & MASK64;
  return splitMix64(h);
}

/**
 * 64-bit Charikar Locality-Sensitive SimHash Fingerprint
 * Maps a weighted document feature vector into a 64-bit signature where
 * cosine similarity is preserved under bitwise Hamming distance.
 */
export function computeSimHash64(termWeights: Map<string, number>): string {
  const accum = new Float64Array(64);

  for (const [term, weight] of termWeights.entries()) {
    const h = hashToken64(term);
    for (let bit = 0; bit < 64; bit++) {
      const isSet = Number((h >> BigInt(bit)) & 1n);
      accum[bit] += isSet === 1 ? weight : -weight;
    }
  }

  let fingerprint = 0n;
  for (let bit = 0; bit < 64; bit++) {
    if (accum[bit] > 0) {
      fingerprint |= 1n << BigInt(bit);
    }
  }

  return fingerprint.toString(16).padStart(16, "0");
}

export function simHashHammingDistance(hexA: string, hexB: string): number {
  if (!hexA || !hexB) return 64;
  let xor =
    (BigInt("0x" + hexA.padStart(16, "0")) ^
      BigInt("0x" + hexB.padStart(16, "0"))) &
    MASK64;
  let count = 0;
  while (xor > 0n) {
    xor &= xor - 1n;
    count++;
  }
  return count;
}

export function simHashSimilarity(hexA: string, hexB: string): number {
  const dist = simHashHammingDistance(hexA, hexB);
  return Number(((64 - dist) / 64).toFixed(4));
}

/**
 * Submodular Maximum Marginal Relevance (MMR) Diversification
 * Maximizes f(S) = sum_i Rel(d_i) - penalty(redundancy) via greedy selection:
 *   argmax_{d in R \ S} [ lambda * Rel_norm(d) - (1 - lambda) * max_{s in S} Sim(d, s) ]
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

  const selected: ScoredArticle[] = [candidates[0]];
  const remaining = candidates.slice(1);

  const pairSimilarity = (a: Article, b: Article): number => {
    const lshSim = simHashSimilarity(a.simhash64, b.simhash64);
    const sameFeed = a.feedId === b.feedId ? 0.25 : 0;
    const sameCountry = a.country === b.country ? 0.1 : 0;
    const sameRegion = a.region === b.region ? 0.08 : 0;
    return Math.min(1, lshSim * 0.65 + sameFeed + sameCountry + sameRegion);
  };

  while (selected.length < limit && remaining.length > 0) {
    let bestIdx = 0;
    let bestMmr = -Infinity;

    for (let i = 0; i < remaining.length; i++) {
      const cand = remaining[i];
      const normRel = cand.score / maxScore;

      let maxRedundancy = 0;
      for (const sel of selected) {
        const sim = pairSimilarity(cand.article, sel.article);
        if (sim > maxRedundancy) maxRedundancy = sim;
      }

      const mmrScore =
        clampedLambda * normRel - (1 - clampedLambda) * maxRedundancy;
      if (mmrScore > bestMmr) {
        bestMmr = mmrScore;
        bestIdx = i;
      }
    }

    const chosen = remaining.splice(bestIdx, 1)[0];
    selected.push(chosen);
  }

  return selected;
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
 * Power-Iteration Eigenvector Centrality / PageRank on the Entity-Article Graph
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

  // Nodes: article nodes ("doc:<id>"), entity nodes ("ent:<name>"), topic nodes ("top:<name>"), country nodes ("geo:<code>")
  const adj = new Map<string, string[]>();
  const addEdge = (u: string, v: string) => {
    let listU = adj.get(u);
    if (!listU) {
      listU = [];
      adj.set(u, listU);
    }
    listU.push(v);

    let listV = adj.get(v);
    if (!listV) {
      listV = [];
      adj.set(v, listV);
    }
    listV.push(u);
  };

  const docMap = new Map<string, Article>();
  for (const art of articles) {
    const docNode = `doc:${art.id}`;
    docMap.set(docNode, art);

    for (const ent of art.entities.organizationsAndPeople) {
      addEdge(docNode, `ent:${ent}`);
    }
    for (const top of art.entities.topics) {
      addEdge(docNode, `top:${top}`);
    }
    for (const geo of art.entities.mentionedCountries) {
      addEdge(docNode, `geo:${geo}`);
    }
  }

  const nodes = Array.from(adj.keys());
  const N = nodes.length;
  if (N === 0) {
    return {
      topEntities: [],
      topTopics: [],
      topCountries: [],
      topArticles: [],
    };
  }

  let ranks = new Map<string, number>();
  const initVal = 1 / N;
  for (const u of nodes) ranks.set(u, initVal);

  const teleport = (1 - damping) / N;

  for (let iter = 0; iter < iterations; iter++) {
    const nextRanks = new Map<string, number>();
    for (const u of nodes) nextRanks.set(u, teleport);

    for (const u of nodes) {
      const neighbors = adj.get(u)!;
      const deg = neighbors.length;
      if (deg === 0) continue;
      const share = (damping * (ranks.get(u) ?? 0)) / deg;
      for (const v of neighbors) {
        nextRanks.set(v, (nextRanks.get(v) ?? teleport) + share);
      }
    }
    ranks = nextRanks;
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

  for (const [node, score] of ranks.entries()) {
    const degree = adj.get(node)?.length ?? 0;
    const scaled = Number((score * 1000).toFixed(4));
    if (node.startsWith("ent:")) {
      entitiesList.push({ key: node.slice(4), score: scaled, degree });
    } else if (node.startsWith("top:")) {
      topicsList.push({ key: node.slice(4), score: scaled, degree });
    } else if (node.startsWith("geo:")) {
      countriesList.push({ key: node.slice(4), score: scaled, degree });
    } else if (node.startsWith("doc:")) {
      const art = docMap.get(node);
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
