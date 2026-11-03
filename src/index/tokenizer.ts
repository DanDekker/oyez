const STOP_WORDS = new Set([
  "a",
  "about",
  "above",
  "after",
  "again",
  "against",
  "all",
  "am",
  "an",
  "and",
  "any",
  "are",
  "as",
  "at",
  "be",
  "because",
  "been",
  "before",
  "being",
  "below",
  "between",
  "both",
  "but",
  "by",
  "can",
  "could",
  "did",
  "do",
  "does",
  "doing",
  "down",
  "during",
  "each",
  "few",
  "for",
  "from",
  "further",
  "had",
  "has",
  "have",
  "having",
  "he",
  "her",
  "here",
  "hers",
  "herself",
  "him",
  "himself",
  "his",
  "how",
  "i",
  "if",
  "in",
  "into",
  "is",
  "it",
  "its",
  "itself",
  "just",
  "me",
  "more",
  "most",
  "my",
  "myself",
  "no",
  "nor",
  "not",
  "now",
  "of",
  "off",
  "on",
  "once",
  "only",
  "or",
  "other",
  "our",
  "ours",
  "ourselves",
  "out",
  "over",
  "own",
  "same",
  "she",
  "should",
  "so",
  "some",
  "such",
  "than",
  "that",
  "the",
  "their",
  "theirs",
  "them",
  "themselves",
  "then",
  "there",
  "these",
  "they",
  "this",
  "those",
  "through",
  "to",
  "too",
  "under",
  "until",
  "up",
  "very",
  "was",
  "we",
  "were",
  "what",
  "when",
  "where",
  "which",
  "while",
  "who",
  "whom",
  "why",
  "will",
  "with",
  "would",
  "you",
  "your",
  "yours",
  "yourself",
  "yourselves",
]);

export function stemToken(word: string): string {
  if (word.length <= 3) return word;
  let w = word;

  if (w.endsWith("ies") && w.length > 4) {
    w = w.slice(0, -3) + "y";
  } else if (w.endsWith("es") && w.length > 4) {
    if (/(?:sh|ch|ss|x|z)es$/.test(w)) {
      w = w.slice(0, -2);
    } else {
      w = w.slice(0, -1);
    }
  } else if (w.endsWith("s") && !w.endsWith("ss") && !w.endsWith("us")) {
    w = w.slice(0, -1);
  }

  if (w.endsWith("ingly") && w.length > 7) {
    w = w.slice(0, -5);
  } else if (w.endsWith("edly") && w.length > 6) {
    w = w.slice(0, -4);
  } else if (w.endsWith("ing") && w.length > 5) {
    w = w.slice(0, -3);
  } else if (w.endsWith("ed") && w.length > 4 && !w.endsWith("eed")) {
    w = w.slice(0, -2);
  }

  // Collapse double trailing consonants like "runn" -> "run"
  if (w.length > 3 && /([bcdfghjklmnpqrstvwxz])\1$/.test(w)) {
    const last = w[w.length - 1];
    if (last !== "l" && last !== "s" && last !== "z") {
      w = w.slice(0, -1);
    }
  }

  if (w.endsWith("ational") && w.length > 9) {
    w = w.slice(0, -5) + "e";
  } else if (w.endsWith("ization") && w.length > 9) {
    w = w.slice(0, -5) + "e";
  } else if (w.endsWith("ness") && w.length > 6) {
    w = w.slice(0, -4);
  } else if (w.endsWith("ment") && w.length > 6) {
    w = w.slice(0, -4);
  } else if (w.endsWith("able") && w.length > 6) {
    w = w.slice(0, -4);
  } else if (w.endsWith("ible") && w.length > 6) {
    w = w.slice(0, -4);
  }

  return w;
}

export function tokenize(text: string): string[] {
  if (!text) return [];
  const normalized = text
    .normalize("NFKD")
    .replace(/[\u0300-\u036f]/g, "")
    .toLowerCase();

  const rawTokens = normalized.split(/[^a-z0-9]+/);
  const tokens: string[] = [];

  for (const raw of rawTokens) {
    if (raw.length < 2) continue;
    if (STOP_WORDS.has(raw)) continue;
    const stemmed = stemToken(raw);
    if (stemmed.length >= 2 && !STOP_WORDS.has(stemmed)) {
      tokens.push(stemmed);
    }
  }

  return tokens;
}

export interface WeightedField {
  text: string;
  weight: number;
}

export function extractWeightedTermFrequencies(
  fields: WeightedField[]
): { termFreqs: Map<string, number>; totalWeightedLength: number } {
  const termFreqs = new Map<string, number>();
  let totalWeightedLength = 0;

  for (const field of fields) {
    if (!field.text || field.weight <= 0) continue;
    const tokens = tokenize(field.text);
    for (const token of tokens) {
      termFreqs.set(token, (termFreqs.get(token) ?? 0) + field.weight);
      totalWeightedLength += field.weight;
    }
  }

  return { termFreqs, totalWeightedLength };
}
