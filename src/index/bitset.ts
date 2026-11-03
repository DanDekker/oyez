/**
 * SWAR 32-bit population count (number of set bits in a 32-bit unsigned integer)
 * Executes in ~4 CPU instructions with zero branches or allocations.
 */
export function popcnt32(x: number): number {
  let v = x >>> 0;
  v = v - ((v >>> 1) & 0x55555555);
  v = (v & 0x33333333) + ((v >>> 2) & 0x33333333);
  return (((v + (v >>> 4)) & 0x0f0f0f0f) * 0x01010101) >>> 24;
}

/**
 * Count trailing zeros of a non-zero 32-bit integer using Math.clz32(w & -w)
 */
export function ctz32(x: number): number {
  return 31 - Math.clz32(x & -x);
}

/**
 * High-Performance Packed 32-bit Word BitSet for O(N/32) Secondary Index Intersections
 */
export class FastBitSet {
  words: Uint32Array;
  private cachedSize = 0;
  private sizeDirty = false;

  constructor(initialBitCapacity = 256) {
    const numWords = Math.max(4, Math.ceil(initialBitCapacity / 32));
    this.words = new Uint32Array(numWords);
  }

  private ensureWordCapacity(wordIndex: number): void {
    if (wordIndex < this.words.length) return;
    let nextLen = this.words.length;
    while (nextLen <= wordIndex) nextLen <<= 1;
    const next = new Uint32Array(nextLen);
    next.set(this.words);
    this.words = next;
  }

  add(bit: number): void {
    const wordIdx = bit >>> 5;
    const mask = 1 << (bit & 31);
    this.ensureWordCapacity(wordIdx);
    const prev = this.words[wordIdx];
    if ((prev & mask) === 0) {
      this.words[wordIdx] = prev | mask;
      if (!this.sizeDirty) this.cachedSize++;
    }
  }

  delete(bit: number): void {
    const wordIdx = bit >>> 5;
    if (wordIdx >= this.words.length) return;
    const mask = 1 << (bit & 31);
    const prev = this.words[wordIdx];
    if ((prev & mask) !== 0) {
      this.words[wordIdx] = prev & ~mask;
      if (!this.sizeDirty) this.cachedSize--;
    }
  }

  has(bit: number): boolean {
    const wordIdx = bit >>> 5;
    if (wordIdx >= this.words.length) return false;
    return (this.words[wordIdx] & (1 << (bit & 31))) !== 0;
  }

  get size(): number {
    if (this.sizeDirty) {
      let total = 0;
      const w = this.words;
      for (let i = 0; i < w.length; i++) {
        if (w[i] !== 0) total += popcnt32(w[i]);
      }
      this.cachedSize = total;
      this.sizeDirty = false;
    }
    return this.cachedSize;
  }

  clone(): FastBitSet {
    const copy = new FastBitSet(this.words.length * 32);
    copy.words.set(this.words);
    copy.cachedSize = this.cachedSize;
    copy.sizeDirty = this.sizeDirty;
    return copy;
  }

  andInPlace(other: FastBitSet): this {
    const minLen = Math.min(this.words.length, other.words.length);
    for (let i = 0; i < minLen; i++) {
      this.words[i] &= other.words[i];
    }
    for (let i = minLen; i < this.words.length; i++) {
      this.words[i] = 0;
    }
    this.sizeDirty = true;
    return this;
  }

  orInPlace(other: FastBitSet): this {
    this.ensureWordCapacity(other.words.length - 1);
    for (let i = 0; i < other.words.length; i++) {
      this.words[i] |= other.words[i];
    }
    this.sizeDirty = true;
    return this;
  }

  forEach(fn: (bit: number) => void): void {
    const w = this.words;
    const len = w.length;
    for (let i = 0; i < len; i++) {
      let word = w[i];
      if (word === 0) continue;
      const base = i << 5;
      while (word !== 0) {
        const tz = 31 - Math.clz32(word & -word);
        fn(base + tz);
        word &= word - 1;
      }
    }
  }
}

/**
 * Contiguous TypedArray Postings List (Slot IDs + Float32 Weighted TF)
 */
export class PackedPostings {
  slots: Uint32Array;
  weights: Float32Array;
  length = 0;
  private slotToPos = new Map<number, number>();

  constructor(initialCapacity = 8) {
    this.slots = new Uint32Array(initialCapacity);
    this.weights = new Float32Array(initialCapacity);
  }

  private grow(): void {
    const nextCap = this.slots.length << 1;
    const nextSlots = new Uint32Array(nextCap);
    const nextWeights = new Float32Array(nextCap);
    nextSlots.set(this.slots);
    nextWeights.set(this.weights);
    this.slots = nextSlots;
    this.weights = nextWeights;
  }

  set(slot: number, weight: number): void {
    const existingIdx = this.slotToPos.get(slot);
    if (existingIdx !== undefined) {
      this.weights[existingIdx] = weight;
      return;
    }
    if (this.length === this.slots.length) {
      this.grow();
    }
    const idx = this.length++;
    this.slots[idx] = slot;
    this.weights[idx] = weight;
    this.slotToPos.set(slot, idx);
  }

  delete(slot: number): boolean {
    const idx = this.slotToPos.get(slot);
    if (idx === undefined) return false;
    const lastIdx = --this.length;
    if (idx !== lastIdx) {
      const movedSlot = this.slots[lastIdx];
      this.slots[idx] = movedSlot;
      this.weights[idx] = this.weights[lastIdx];
      this.slotToPos.set(movedSlot, idx);
    }
    this.slotToPos.delete(slot);
    return true;
  }
}

interface TrieNode {
  children: Map<number, TrieNode>;
  term: string | null;
}

/**
 * Compact Radix/Character Trie for O(|prefix| + k) Prefix Term Lookup
 */
export class TermPrefixTrie {
  private root: TrieNode = { children: new Map(), term: null };

  insert(term: string): void {
    let curr = this.root;
    for (let i = 0; i < term.length; i++) {
      const code = term.charCodeAt(i);
      let next = curr.children.get(code);
      if (!next) {
        next = { children: new Map(), term: null };
        curr.children.set(code, next);
      }
      curr = next;
    }
    curr.term = term;
  }

  delete(term: string): void {
    const walk = (node: TrieNode, depth: number): boolean => {
      if (depth === term.length) {
        node.term = null;
        return node.children.size === 0;
      }
      const code = term.charCodeAt(depth);
      const child = node.children.get(code);
      if (!child) return false;
      const shouldPrune = walk(child, depth + 1);
      if (shouldPrune) {
        node.children.delete(code);
        return node.term === null && node.children.size === 0;
      }
      return false;
    };
    walk(this.root, 0);
  }

  findWithPrefix(prefix: string, maxMatches = 24): string[] {
    let curr = this.root;
    for (let i = 0; i < prefix.length; i++) {
      const next = curr.children.get(prefix.charCodeAt(i));
      if (!next) return [];
      curr = next;
    }

    const out: string[] = [];
    const collect = (node: TrieNode) => {
      if (out.length >= maxMatches) return;
      if (node.term !== null && node.term !== prefix) {
        out.push(node.term);
      }
      for (const child of node.children.values()) {
        if (out.length >= maxMatches) break;
        collect(child);
      }
    };
    collect(curr);
    return out;
  }
}
