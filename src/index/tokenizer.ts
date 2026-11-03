export const TOKEN_BASE = 256;
export const TOKEN_MAX_VOCAB = 131072;
export const TOKEN_MAX_SPECIAL = 64;
export const TOKEN_MAX_CORPUS = 0xfffffffe;

export const M1_BPE_END = 0x1f000;
export const M1_INDENT_BASE = 0x1f000;
export const M1_PRIMITIVE_BASE = 0x1f400;
export const M1_LANDMARK_BASE = 0x1f800;
export const M1_CONTROL_BASE = 0x1fc00;
export const M1_MAX_SEQUENCE = 1048576;
export const M1_SERIALIZER_REVISION = "oyez.m1.serializer.v2";

const NIL = 0xffffffff;

export enum M1Control {
  M1_PAD = M1_CONTROL_BASE,
  M1_BOS,
  M1_EOS,
  M1_SYSTEM,
  M1_DEVELOPER,
  M1_USER,
  M1_ASSISTANT,
  M1_TOOL,
  M1_END,
  M1_THOUGHT,
  M1_THOUGHT_END,
  M1_INTENT,
  M1_ANCHOR,
  M1_VERBATIM,
  M1_REDUCE,
  M1_FIM_PREFIX,
  M1_FIM_MIDDLE,
  M1_FIM_SUFFIX,
  M1_FIM_SELECT,
  M1_REPO,
  M1_FILE,
  M1_COMMIT,
  M1_SINK0,
  M1_SINK1,
  M1_SINK2,
  M1_SINK3,
}

const PRIMITIVE_TYPES = [
  "void",
  "bool",
  "char",
  "int",
  "float",
  "double",
  "size_t",
  "ptrdiff_t",
  "uint8_t",
  "uint16_t",
  "uint32_t",
  "uint64_t",
  "int8_t",
  "int16_t",
  "int32_t",
  "int64_t",
] as const;

const PRIMITIVE_TYPE_BYTES: Uint8Array[] = PRIMITIVE_TYPES.map((t) =>
  new TextEncoder().encode(t)
);

const CONTROL_NAMES = [
  "<|pad|>",
  "<|bos|>",
  "<|eos|>",
  "<|system|>",
  "<|developer|>",
  "<|user|>",
  "<|assistant|>",
  "<|tool|>",
  "<|end|>",
  "<|thought|>",
  "</|thought|>",
  "@INTENT_DECODE",
  "@ANCHOR_SCAN",
  "@VERBATIM_PULL",
  "@STATE_REDUCE",
  "<|fim_prefix|>",
  "<|fim_middle|>",
  "<|fim_suffix|>",
  "<|fim_Select|>",
  "<|repo_start|>",
  "<|file_sep|>",
  "<|commit_diff|>",
  "<|sink:0|>",
  "<|sink:1|>",
  "<|sink:2|>",
  "<|sink:3|>",
] as const;

/* -------------------- Reflected CRC-32 & SHA-256 Digest ------------------- */

const CRC32_NIBBLE_TABLE = (() => {
  const table = new Uint32Array(16);
  for (let i = 0; i < 16; i++) {
    let v = i;
    for (let bit = 0; bit < 4; bit++) {
      v = (v >>> 1) ^ (v & 1 ? 0xedb88320 : 0);
    }
    table[i] = v >>> 0;
  }
  return table;
})();

export function checksumCrc32Update(
  previous: number,
  bytes: Uint8Array
): number {
  let value = ~previous >>> 0;
  for (let i = 0; i < bytes.length; i++) {
    value ^= bytes[i];
    value = (value >>> 4) ^ CRC32_NIBBLE_TABLE[value & 15];
    value = (value >>> 4) ^ CRC32_NIBBLE_TABLE[value & 15];
  }
  return ~value >>> 0;
}

export function checksumCrc32(bytes: Uint8Array): number {
  return checksumCrc32Update(0, bytes);
}

export function artifactDigest(bytes: Uint8Array | string): string {
  const hasher = new Bun.CryptoHasher("sha256");
  hasher.update(bytes);
  return hasher.digest("hex");
}

export function m1SerializerDigest(): string {
  const contract =
    M1_SERIALIZER_REVISION +
    ":sinks22-25,bos1,roles3-7,end8;" +
    "control-offsets0-25;source-only-landmarks-before0+1024n;data-never-controls;" +
    "window1048576-includes-framing-landmarks-and-output;reject-overflow;" +
    "indent:spaces1-256,tabs1-256,LFspaces0-255,LFtabs1-256;" +
    "primitives:lowerhex00-ff,0x00-ff,r0-255,types:void,bool,char,int,float,double,size_t,ptrdiff_t," +
    "uint8_t,uint16_t,uint32_t,uint64_t,int8_t,int16_t,int32_t,int64_t;pointerdepth0-15;" +
    "controls:pad,bos,eos,system,developer,user,assistant,tool,end,thought,/thought," +
    "INTENT_DECODE,ANCHOR_SCAN,VERBATIM_PULL,STATE_REDUCE,fim_prefix,fim_middle,fim_suffix," +
    "fim_Select,repo_start,file_sep,commit_diff,sink0,sink1,sink2,sink3;" +
    "reserved-before-BPE;no-Unicode-normalization";
  return artifactDigest(contract);
}

/* --------------------- 64-bit Mixing & Pair Key Helpers ------------------- */

const MASK64 = 0xffffffffffffffffn;

function mix64(x: bigint): bigint {
  let v = x & MASK64;
  v = ((v ^ (v >> 33n)) * 0xff51afd7ed558ccdn) & MASK64;
  v = ((v ^ (v >> 33n)) * 0xc4ceb9fe1a85ec53n) & MASK64;
  v = (v ^ (v >> 33n)) & MASK64;
  return v;
}

function mkkey(l: number, r: number): bigint {
  return ((BigInt(l >>> 0) << 32n) | BigInt(r >>> 0)) & MASK64;
}

function keyL(k: bigint): number {
  return Number((k >> 32n) & 0xffffffffn) >>> 0;
}

function keyR(k: bigint): number {
  return Number(k & 0xffffffffn) >>> 0;
}

/* ----------------------- Wire Serialization Helpers ----------------------- */

function writeU32LE(buf: number[], value: number): void {
  const v = value >>> 0;
  buf.push(v & 0xff, (v >>> 8) & 0xff, (v >>> 16) & 0xff, (v >>> 24) & 0xff);
}

function readU32LE(
  data: Uint8Array,
  offset: { pos: number }
): number | null {
  if (offset.pos + 4 > data.length) return null;
  const p = offset.pos;
  const v =
    (data[p] |
      (data[p + 1] << 8) |
      (data[p + 2] << 16) |
      (data[p + 3] << 24)) >>>
    0;
  offset.pos += 4;
  return v;
}

/* ------------------------ Pair Hash Table & Heaps ------------------------- */

interface PairSlot {
  key: bigint;
  count: number;
  pos: Uint32Array;
  npos: number;
  used: boolean;
}

class PairTable {
  slots: PairSlot[];
  cap: number;
  mask: bigint;
  usedCount = 0;

  constructor(hint: number) {
    let cap = 16;
    while (cap < hint * 2 && cap < 1 << 24) cap <<= 1;
    this.cap = cap;
    this.mask = BigInt(cap - 1);
    this.slots = Array.from({ length: cap }, () => ({
      key: 0n,
      count: 0,
      pos: new Uint32Array(0),
      npos: 0,
      used: false,
    }));
  }

  find(key: bigint): PairSlot | null {
    let i = Number(mix64(key) & this.mask);
    for (;;) {
      const s = this.slots[i];
      if (!s.used) return null;
      if (s.key === key) return s;
      i = (i + 1) & (this.cap - 1);
    }
  }

  private grow(): void {
    const ncap = this.cap << 1;
    const nmask = BigInt(ncap - 1);
    const ns: PairSlot[] = Array.from({ length: ncap }, () => ({
      key: 0n,
      count: 0,
      pos: new Uint32Array(0),
      npos: 0,
      used: false,
    }));
    for (let i = 0; i < this.cap; i++) {
      const o = this.slots[i];
      if (!o.used) continue;
      let j = Number(mix64(o.key) & nmask);
      while (ns[j].used) j = (j + 1) & (ncap - 1);
      ns[j] = o;
    }
    this.slots = ns;
    this.cap = ncap;
    this.mask = nmask;
  }

  add(key: bigint): PairSlot {
    if ((this.usedCount + 1) * 10 >= this.cap * 7) {
      this.grow();
    }
    let i = Number(mix64(key) & this.mask);
    for (;;) {
      const s = this.slots[i];
      if (!s.used) {
        s.used = true;
        s.key = key;
        s.count = 0;
        s.pos = new Uint32Array(4);
        s.npos = 0;
        this.usedCount++;
        return s;
      }
      if (s.key === key) return s;
      i = (i + 1) & (this.cap - 1);
    }
  }
}

function pushPos(slot: PairSlot, p: number): void {
  if (slot.npos === slot.pos.length) {
    const nextCap = slot.pos.length ? slot.pos.length * 2 : 4;
    const next = new Uint32Array(nextCap);
    next.set(slot.pos);
    slot.pos = next;
  }
  slot.pos[slot.npos++] = p >>> 0;
}

interface HEnt {
  count: number;
  key: bigint;
}

function hGt(x: HEnt, y: HEnt): boolean {
  if (x.count !== y.count) return x.count > y.count;
  return x.key < y.key;
}

class MaxHeap {
  a: HEnt[] = [];

  push(count: number, key: bigint): void {
    let i = this.a.length;
    this.a.push({ count, key });
    while (i > 0) {
      const p = (i - 1) >>> 1;
      if (!hGt(this.a[i], this.a[p])) break;
      const tmp = this.a[i];
      this.a[i] = this.a[p];
      this.a[p] = tmp;
      i = p;
    }
  }

  pop(): HEnt | null {
    if (this.a.length === 0) return null;
    const out = this.a[0];
    const last = this.a.pop()!;
    if (this.a.length > 0) {
      this.a[0] = last;
      let i = 0;
      const n = this.a.length;
      for (;;) {
        const l = 2 * i + 1;
        const r = l + 1;
        let b = i;
        if (l < n && hGt(this.a[l], this.a[b])) b = l;
        if (r < n && hGt(this.a[r], this.a[b])) b = r;
        if (b === i) break;
        const tmp = this.a[i];
        this.a[i] = this.a[b];
        this.a[b] = tmp;
        i = b;
      }
    }
    return out;
  }
}

interface REnt {
  rank: number;
  pos: number;
}

function reLt(x: REnt, y: REnt): boolean {
  if (x.rank !== y.rank) return x.rank < y.rank;
  return x.pos < y.pos;
}

class MinHeap {
  a: REnt[] = [];

  push(rank: number, pos: number): void {
    let i = this.a.length;
    this.a.push({ rank, pos });
    while (i > 0) {
      const p = (i - 1) >>> 1;
      if (!reLt(this.a[i], this.a[p])) break;
      const tmp = this.a[i];
      this.a[i] = this.a[p];
      this.a[p] = tmp;
      i = p;
    }
  }

  pop(): REnt | null {
    if (this.a.length === 0) return null;
    const out = this.a[0];
    const last = this.a.pop()!;
    if (this.a.length > 0) {
      this.a[0] = last;
      let i = 0;
      const n = this.a.length;
      for (;;) {
        const l = 2 * i + 1;
        const r = l + 1;
        let b = i;
        if (l < n && reLt(this.a[l], this.a[b])) b = l;
        if (r < n && reLt(this.a[r], this.a[b])) b = r;
        if (b === i) break;
        const tmp = this.a[i];
        this.a[i] = this.a[b];
        this.a[b] = tmp;
        i = b;
      }
    }
    return out;
  }
}

/* --------------------- Byte-Level BPE Tokenizer Core ---------------------- */

export interface TokenTrainConfig {
  targetVocab: number;
  minFrequency: number;
}

export function defaultTrainConfig(): TokenTrainConfig {
  return { targetVocab: 1024, minFrequency: 2 };
}

export class BpeTokenizer {
  vocab = TOKEN_BASE;
  nMerges = 0;
  nSpecial = 0;
  mergeLeft = new Uint32Array(0);
  mergeRight = new Uint32Array(0);
  special: string[] = [];

  piecePool = new Uint8Array(TOKEN_BASE);
  pieceOff = new Uint32Array(TOKEN_BASE + 1);
  private rankMap = new Map<bigint, number>();

  constructor() {
    this.finalize();
  }

  isValid(): boolean {
    if (this.vocab !== TOKEN_BASE + this.nMerges + this.nSpecial) return false;
    if (this.vocab > TOKEN_MAX_VOCAB) return false;
    if (
      this.mergeLeft.length < this.nMerges ||
      this.mergeRight.length < this.nMerges
    )
      return false;
    if (this.special.length !== this.nSpecial) return false;
    for (let i = 0; i < this.nMerges; i++) {
      if (
        this.mergeLeft[i] >= TOKEN_BASE + i ||
        this.mergeRight[i] >= TOKEN_BASE + i
      ) {
        return false;
      }
    }
    return true;
  }

  finalize(): boolean {
    const nTok = TOKEN_BASE + this.nMerges;
    const off = new Uint32Array(nTok + 1);
    const len = new Uint32Array(nTok);
    for (let i = 0; i < TOKEN_BASE; i++) len[i] = 1;
    for (let i = 0; i < this.nMerges; i++) {
      const L = this.mergeLeft[i];
      const R = this.mergeRight[i];
      if (L >= TOKEN_BASE + i || R >= TOKEN_BASE + i) return false;
      len[TOKEN_BASE + i] = len[L] + len[R];
    }
    off[0] = 0;
    for (let i = 0; i < nTok; i++) off[i + 1] = off[i] + len[i];
    const total = off[nTok];
    const pool = new Uint8Array(total || 1);
    for (let b = 0; b < TOKEN_BASE; b++) pool[off[b]] = b;
    for (let i = 0; i < this.nMerges; i++) {
      const L = this.mergeLeft[i];
      const R = this.mergeRight[i];
      const dst = off[TOKEN_BASE + i];
      pool.set(pool.subarray(off[L], off[L] + len[L]), dst);
      pool.set(pool.subarray(off[R], off[R] + len[R]), dst + len[L]);
    }

    const rm = new Map<bigint, number>();
    for (let i = 0; i < this.nMerges; i++) {
      const k = mkkey(this.mergeLeft[i], this.mergeRight[i]);
      if (rm.has(k)) return false;
      rm.set(k, i);
    }

    this.piecePool = pool;
    this.pieceOff = off;
    this.rankMap = rm;
    this.vocab = TOKEN_BASE + this.nMerges + this.nSpecial;
    return true;
  }

  train(
    data: Uint8Array | string,
    cfg: Partial<TokenTrainConfig> = {},
    segOffsets?: Uint32Array | number[],
    specialNames: string[] = []
  ): void {
    const bytes =
      typeof data === "string" ? new TextEncoder().encode(data) : data;
    const length = bytes.length;
    if (length > TOKEN_MAX_CORPUS) {
      throw new Error("token: corpus too large");
    }
    if (specialNames.length > TOKEN_MAX_SPECIAL) {
      throw new Error("token: too many special tokens");
    }

    const minFrequency = Math.max(2, cfg.minFrequency ?? 2);
    const target = Math.min(TOKEN_MAX_VOCAB, cfg.targetVocab ?? 1024);
    const basePlusSpecial = TOKEN_BASE + specialNames.length;
    const maxMerges = target > basePlusSpecial ? target - basePlusSpecial : 0;

    this.special = [...specialNames];
    this.nSpecial = specialNames.length;
    this.nMerges = 0;
    this.mergeLeft = new Uint32Array(maxMerges);
    this.mergeRight = new Uint32Array(maxMerges);

    const symId = new Uint32Array(length);
    const symPrev = new Uint32Array(length);
    const symNext = new Uint32Array(length);
    const symAlive = new Uint8Array(length);

    for (let i = 0; i < length; i++) {
      symId[i] = bytes[i];
      symPrev[i] = i === 0 ? NIL : i - 1;
      symNext[i] = i + 1 < length ? i + 1 : NIL;
      symAlive[i] = 1;
    }

    if (segOffsets && segOffsets.length > 0) {
      const segCount = segOffsets.length - 1;
      if (segOffsets[0] !== 0 || segOffsets[segCount] !== length) {
        throw new Error("token: segment offsets must span [0,length]");
      }
      for (let s = 1; s < segCount; s++) {
        const b = segOffsets[s];
        if (b === 0 || b >= length || segOffsets[s] < segOffsets[s - 1]) {
          throw new Error("token: bad segment offset");
        }
        symNext[b - 1] = NIL;
        symPrev[b] = NIL;
      }
    }

    if (maxMerges === 0 || length < 2) {
      if (!this.finalize()) throw new Error("token: finalize failed");
      return;
    }

    const pt = new PairTable(length);
    const heap = new MaxHeap();

    for (let i = 0; i < length; i++) {
      if (!symAlive[i] || symNext[i] === NIL) continue;
      const k = mkkey(symId[i], symId[symNext[i]]);
      const s = pt.add(k);
      s.count++;
      pushPos(s, i);
    }

    for (let i = 0; i < pt.cap; i++) {
      const s = pt.slots[i];
      if (s.used && s.count > 0) {
        heap.push(s.count, s.key);
      }
    }

    while (this.nMerges < maxMerges) {
      let best: PairSlot | null = null;
      for (;;) {
        const top = heap.pop();
        if (!top) break;
        const s = pt.find(top.key);
        if (!s || s.count <= 0) continue;
        if (s.count !== top.count) {
          heap.push(s.count, top.key);
          continue;
        }
        best = s;
        break;
      }
      if (!best || best.count < minFrequency) break;

      const pkey = best.key;
      const A = keyL(pkey);
      const B = keyR(pkey);
      const Z = TOKEN_BASE + this.nMerges;
      this.mergeLeft[this.nMerges] = A;
      this.mergeRight[this.nMerges] = B;
      this.nMerges++;

      const np = best.npos;
      const posbuf = best.pos.slice(0, np);

      for (let idx = 0; idx < np; idx++) {
        const p = posbuf[idx];
        if (!symAlive[p] || symId[p] !== A) continue;
        const q = symNext[p];
        if (q === NIL || !symAlive[q] || symId[q] !== B) continue;

        const pprev = symPrev[p];
        const qnext = symNext[q];

        if (pprev !== NIL && symAlive[pprev]) {
          const d = pt.find(mkkey(symId[pprev], A));
          if (d && d.count > 0) d.count--;
        }
        if (qnext !== NIL && symAlive[qnext]) {
          const d = pt.find(mkkey(B, symId[qnext]));
          if (d && d.count > 0) d.count--;
        }

        symId[p] = Z;
        symNext[p] = qnext;
        if (qnext !== NIL) symPrev[qnext] = p;
        symAlive[q] = 0;

        if (pprev !== NIL && symAlive[pprev]) {
          const k = mkkey(symId[pprev], Z);
          const s = pt.add(k);
          pushPos(s, pprev);
          s.count++;
          heap.push(s.count, k);
        }
        if (qnext !== NIL && symAlive[qnext]) {
          const k = mkkey(Z, symId[qnext]);
          const s = pt.add(k);
          pushPos(s, p);
          s.count++;
          heap.push(s.count, k);
        }
      }

      const cleared = pt.find(pkey);
      if (cleared) {
        cleared.count = 0;
        cleared.pos = new Uint32Array(0);
        cleared.npos = 0;
      }
    }

    this.mergeLeft = this.mergeLeft.slice(0, this.nMerges);
    this.mergeRight = this.mergeRight.slice(0, this.nMerges);

    if (!this.finalize()) {
      throw new Error("token: finalize failed");
    }
  }

  encode(input: Uint8Array | string): Uint32Array {
    const bytes =
      typeof input === "string" ? new TextEncoder().encode(input) : input;
    return this.encodeSymbols(bytes, null);
  }

  mergeIds(symbols: Uint32Array | number[]): Uint32Array {
    const arr =
      symbols instanceof Uint32Array ? symbols : new Uint32Array(symbols);
    return this.encodeSymbols(null, arr);
  }

  private encodeSymbols(
    bytes: Uint8Array | null,
    symbols: Uint32Array | null
  ): Uint32Array {
    if (!this.isValid()) {
      throw new Error("token: invalid tokenizer");
    }
    const length = bytes ? bytes.length : symbols ? symbols.length : 0;
    if (length > TOKEN_MAX_CORPUS) {
      throw new Error("token: input too large");
    }
    if (symbols) {
      for (let i = 0; i < length; i++) {
        if (symbols[i] >= TOKEN_MAX_VOCAB) {
          throw new Error("token: symbol outside vocabulary address space");
        }
      }
    }
    if (length === 0) return new Uint32Array(0);

    const symId = new Uint32Array(length);
    const symPrev = new Uint32Array(length);
    const symNext = new Uint32Array(length);
    const symAlive = new Uint8Array(length);

    for (let i = 0; i < length; i++) {
      symId[i] = symbols ? symbols[i] : bytes![i];
      symPrev[i] = i === 0 ? NIL : i - 1;
      symNext[i] = i + 1 < length ? i + 1 : NIL;
      symAlive[i] = 1;
    }

    const heap = new MinHeap();
    for (let i = 0; i + 1 < length; i++) {
      const rank = this.rankMap.get(mkkey(symId[i], symId[i + 1]));
      if (rank !== undefined) {
        heap.push(rank, i);
      }
    }

    for (;;) {
      const e = heap.pop();
      if (!e) break;
      const p = e.pos;
      if (!symAlive[p]) continue;
      const q = symNext[p];
      if (q === NIL || !symAlive[q]) continue;
      const rank = this.rankMap.get(mkkey(symId[p], symId[q]));
      if (rank === undefined || rank !== e.rank) continue;

      const Z = TOKEN_BASE + e.rank;
      const pprev = symPrev[p];
      const qnext = symNext[q];
      symId[p] = Z;
      symNext[p] = qnext;
      if (qnext !== NIL) symPrev[qnext] = p;
      symAlive[q] = 0;

      if (pprev !== NIL && symAlive[pprev]) {
        const nr = this.rankMap.get(mkkey(symId[pprev], Z));
        if (nr !== undefined) heap.push(nr, pprev);
      }
      if (qnext !== NIL && symAlive[qnext]) {
        const nr = this.rankMap.get(mkkey(Z, symId[qnext]));
        if (nr !== undefined) heap.push(nr, p);
      }
    }

    const out: number[] = [];
    for (let cur = 0; cur !== NIL; cur = symNext[cur]) {
      out.push(symId[cur]);
    }
    return new Uint32Array(out);
  }

  decode(ids: Uint32Array | number[]): Uint8Array {
    if (!this.isValid()) throw new Error("token: invalid tokenizer");
    const byteBacked = TOKEN_BASE + this.nMerges;
    let totalLen = 0;
    for (let i = 0; i < ids.length; i++) {
      const id = ids[i];
      if (id >= this.vocab) {
        throw new Error(`token: id ${id} out of range`);
      }
      if (id < byteBacked) {
        totalLen += this.pieceOff[id + 1] - this.pieceOff[id];
      }
    }

    const out = new Uint8Array(totalLen);
    let offset = 0;
    for (let i = 0; i < ids.length; i++) {
      const id = ids[i];
      if (id < byteBacked) {
        const a = this.pieceOff[id];
        const b = this.pieceOff[id + 1];
        out.set(this.piecePool.subarray(a, b), offset);
        offset += b - a;
      }
    }
    return out;
  }

  decodeToString(ids: Uint32Array | number[]): string {
    return new TextDecoder().decode(this.decode(ids));
  }

  specialId(name: string): number | null {
    if (!this.isValid()) return null;
    for (let i = 0; i < this.nSpecial; i++) {
      if (this.special[i] === name) return TOKEN_BASE + this.nMerges + i;
    }
    return null;
  }

  idName(id: number): string | null {
    if (!this.isValid()) return null;
    const first = TOKEN_BASE + this.nMerges;
    if (id < first || id >= this.vocab) return null;
    return this.special[id - first] ?? null;
  }

  save(): Uint8Array {
    if (!this.isValid()) throw new Error("token: invalid tokenizer");
    const body: number[] = [];
    writeU32LE(body, 1); // version
    writeU32LE(body, this.nMerges);
    writeU32LE(body, this.nSpecial);
    for (let i = 0; i < this.nMerges; i++) {
      writeU32LE(body, this.mergeLeft[i]);
      writeU32LE(body, this.mergeRight[i]);
    }
    const encoder = new TextEncoder();
    for (let i = 0; i < this.nSpecial; i++) {
      const sBytes = encoder.encode(this.special[i]);
      writeU32LE(body, sBytes.length);
      for (let j = 0; j < sBytes.length; j++) body.push(sBytes[j]);
    }

    const bodyBytes = new Uint8Array(body);
    const crc = checksumCrc32(bodyBytes);
    const magic = encoder.encode("OYEZBP01");
    const out = new Uint8Array(magic.length + bodyBytes.length + 4);
    out.set(magic, 0);
    out.set(bodyBytes, 8);
    const crcArr: number[] = [];
    writeU32LE(crcArr, crc);
    out.set(crcArr, 8 + bodyBytes.length);
    return out;
  }

  load(bytes: Uint8Array): void {
    if (bytes.length < 12) throw new Error("token: short blob");
    const magic = new TextDecoder().decode(bytes.subarray(0, 8));
    if (magic !== "OYEZBP01") throw new Error("token: bad magic");
    const bodyLen = bytes.length - 12;
    const body = bytes.subarray(8, 8 + bodyLen);
    const crcCursor = { pos: bytes.length - 4 };
    const storedCrc = readU32LE(bytes, crcCursor);
    if (storedCrc === null || checksumCrc32(body) !== storedCrc) {
      throw new Error("token: checksum mismatch");
    }

    const cursor = { pos: 0 };
    const version = readU32LE(body, cursor);
    if (version !== 1) throw new Error("token: bad version");
    const nMerges = readU32LE(body, cursor);
    const nSpecial = readU32LE(body, cursor);
    if (nMerges === null || nSpecial === null) {
      throw new Error("token: truncated header");
    }
    if (
      nSpecial > TOKEN_MAX_SPECIAL ||
      TOKEN_BASE + nMerges + nSpecial > TOKEN_MAX_VOCAB
    ) {
      throw new Error("token: vocab exceeds cap");
    }

    const mergeLeft = new Uint32Array(nMerges);
    const mergeRight = new Uint32Array(nMerges);
    for (let i = 0; i < nMerges; i++) {
      const l = readU32LE(body, cursor);
      const r = readU32LE(body, cursor);
      if (l === null || r === null) throw new Error("token: truncated merges");
      if (l >= TOKEN_BASE + i || r >= TOKEN_BASE + i) {
        throw new Error("token: merge references a later or unknown token");
      }
      mergeLeft[i] = l;
      mergeRight[i] = r;
    }

    const special: string[] = [];
    const decoder = new TextDecoder();
    for (let i = 0; i < nSpecial; i++) {
      const n = readU32LE(body, cursor);
      if (n === null || cursor.pos + n > bodyLen) {
        throw new Error("token: truncated special");
      }
      special.push(decoder.decode(body.subarray(cursor.pos, cursor.pos + n)));
      cursor.pos += n;
    }

    if (cursor.pos !== bodyLen) {
      throw new Error("token: trailing bytes");
    }

    this.nMerges = nMerges;
    this.nSpecial = nSpecial;
    this.mergeLeft = mergeLeft;
    this.mergeRight = mergeRight;
    this.special = special;
    if (!this.finalize()) {
      throw new Error("token: finalize failed");
    }
  }
}

/* --------------- M1 Fixed-Piece & Structured Sequence Engine -------------- */

function hexVal(c: number): number {
  if (c >= 0x30 && c <= 0x39) return c - 0x30;
  if (c >= 0x61 && c <= 0x66) return c - 0x61 + 10;
  return -1;
}

export function fixedPiece(
  p: Uint8Array,
  pos: number,
  end: number
): { len: number; id: number } {
  const n = end - pos;
  if (n <= 0) return { len: 0, id: 0 };

  const b0 = p[pos];
  // Space (0x20) or Tab (0x09)
  if (b0 === 0x20 || b0 === 0x09) {
    let run = 1;
    while (run < n && run < 256 && p[pos + run] === b0) run++;
    const id = M1_INDENT_BASE + (b0 === 0x09 ? 256 : 0) + run - 1;
    return { len: run, id };
  }

  // LF (0x0a) + indent
  if (b0 === 0x0a) {
    let run = 0;
    if (n > 1 && p[pos + 1] === 0x09) {
      while (run + 1 < n && run < 256 && p[pos + run + 1] === 0x09) run++;
      return { len: run + 1, id: M1_INDENT_BASE + 768 + run - 1 };
    } else {
      while (run + 1 < n && run < 255 && p[pos + run + 1] === 0x20) run++;
      return { len: run + 1, id: M1_INDENT_BASE + 512 + run };
    }
  }

  // 0x00 .. 0xff
  if (
    n >= 4 &&
    b0 === 0x30 &&
    p[pos + 1] === 0x78 &&
    hexVal(p[pos + 2]) >= 0 &&
    hexVal(p[pos + 3]) >= 0
  ) {
    const val = hexVal(p[pos + 2]) * 16 + hexVal(p[pos + 3]);
    return { len: 4, id: M1_PRIMITIVE_BASE + 256 + val };
  }

  // 00 .. ff
  if (n >= 2 && hexVal(b0) >= 0 && hexVal(p[pos + 1]) >= 0) {
    const val = hexVal(b0) * 16 + hexVal(p[pos + 1]);
    return { len: 2, id: M1_PRIMITIVE_BASE + val };
  }

  // r0 .. r255
  if (b0 === 0x72 && n >= 2 && p[pos + 1] >= 0x30 && p[pos + 1] <= 0x39) {
    let j = 1;
    let value = 0;
    while (j < n && j < 5 && p[pos + j] >= 0x30 && p[pos + j] <= 0x39) {
      value = value * 10 + (p[pos + j] - 0x30);
      j++;
    }
    if (
      value < 256 &&
      (j === n || p[pos + j] < 0x30 || p[pos + j] > 0x39) &&
      (j === 2 || p[pos + 1] !== 0x30)
    ) {
      return { len: j, id: M1_PRIMITIVE_BASE + 512 + value };
    }
  }

  // Primitive types + pointer stars
  let best = 0;
  let bestId = 0;
  for (let i = 0; i < 16; i++) {
    const tBytes = PRIMITIVE_TYPE_BYTES[i];
    const tLen = tBytes.length;
    if (n < tLen) continue;
    let match = true;
    for (let k = 0; k < tLen; k++) {
      if (p[pos + k] !== tBytes[k]) {
        match = false;
        break;
      }
    }
    if (!match) continue;
    let stars = 0;
    while (tLen + stars < n && stars < 15 && p[pos + tLen + stars] === 0x2a) {
      stars++;
    }
    if (tLen + stars > best) {
      best = tLen + stars;
      bestId = M1_PRIMITIVE_BASE + 768 + (16 * i + stars);
    }
  }

  return { len: best, id: bestId };
}

function expandFixedPiece(id: number): Uint8Array {
  if (id >= M1_INDENT_BASE && id < M1_PRIMITIVE_BASE) {
    const x = id - M1_INDENT_BASE;
    const kind = Math.floor(x / 256);
    let n = x % 256;
    if (kind < 2) {
      n++;
      const out = new Uint8Array(n);
      out.fill(kind ? 0x09 : 0x20);
      return out;
    }
    if (kind === 3) n++;
    const out = new Uint8Array(n + 1);
    out[0] = 0x0a;
    out.fill(kind === 3 ? 0x09 : 0x20, 1);
    return out;
  }

  if (id >= M1_PRIMITIVE_BASE && id < M1_LANDMARK_BASE) {
    const x = id - M1_PRIMITIVE_BASE;
    const kind = Math.floor(x / 256);
    const n = x % 256;
    const encoder = new TextEncoder();
    if (kind === 0) {
      return encoder.encode(n.toString(16).padStart(2, "0"));
    }
    if (kind === 1) {
      return encoder.encode("0x" + n.toString(16).padStart(2, "0"));
    }
    if (kind === 2) {
      return encoder.encode(`r${n}`);
    }
    const typeName = PRIMITIVE_TYPES[Math.floor(n / 16)];
    const stars = "*".repeat(n % 16);
    return encoder.encode(typeName + stars);
  }

  return new Uint8Array(0);
}

export function m1TokenizerName(id: number): string | null {
  if (id >= M1_LANDMARK_BASE && id < M1_CONTROL_BASE) {
    const blk = (id - M1_LANDMARK_BASE).toString().padStart(4, "0");
    return `<|blk:${blk}|>`;
  }
  if (
    id >= M1_CONTROL_BASE &&
    id < M1_CONTROL_BASE + CONTROL_NAMES.length
  ) {
    return CONTROL_NAMES[id - M1_CONTROL_BASE];
  }
  return null;
}

export class M1Tokenizer {
  bpe = new BpeTokenizer();
  sha256 = "";

  constructor() {
    this.sha256 = artifactDigest(this.save());
  }

  isValid(): boolean {
    return (
      this.bpe.isValid() &&
      this.bpe.vocab <= M1_BPE_END &&
      this.bpe.nSpecial === 0
    );
  }

  dataId(id: number): boolean {
    return (
      this.isValid() &&
      (id < this.bpe.vocab ||
        (id >= M1_INDENT_BASE && id < M1_LANDMARK_BASE))
    );
  }

  train(
    codeInput: Uint8Array | string,
    proseInput: Uint8Array | string,
    targetBpe = 1024
  ): void {
    const code =
      typeof codeInput === "string"
        ? new TextEncoder().encode(codeInput)
        : codeInput;
    const prose =
      typeof proseInput === "string"
        ? new TextEncoder().encode(proseInput)
        : proseInput;

    const nc = code.length;
    const np = prose.length;
    if (nc < 7 || np < 3 || targetBpe < 256 || targetBpe > M1_BPE_END) {
      throw new Error("invalid 70/30 tokenizer corpus or BPE target");
    }

    const unit = Math.min(Math.floor(nc / 7), Math.floor(np / 3));
    if (unit > Math.floor(TOKEN_MAX_CORPUS / 10)) {
      throw new Error("tokenizer corpus exceeds 32-bit positions");
    }

    const sample = new Uint8Array(unit * 10);
    for (let i = 0; i < 10; i++) {
      const src = i < 7 ? code : prose;
      const sourceLen = i < 7 ? nc : np;
      const parts = i < 7 ? 7 : 3;
      const index = i < 7 ? i : i - 7;
      const startOffset = index * Math.floor(sourceLen / parts);
      sample.set(src.subarray(startOffset, startOffset + unit), i * unit);
    }

    const offsets: number[] = [0];
    let used = 0;

    for (let segment = 0; segment < 10; segment++) {
      let pos = segment * unit;
      const end = pos + unit;
      while (pos < end) {
        const fp = fixedPiece(sample, pos, end);
        if (fp.len > 0) {
          pos += fp.len;
          continue;
        }
        const start = pos++;
        while (pos < end && fixedPiece(sample, pos, end).len === 0) {
          pos++;
        }
        sample.copyWithin(used, start, pos);
        used += pos - start;
        offsets.push(used);
      }
    }

    const nextBpe = new BpeTokenizer();
    nextBpe.train(
      sample.subarray(0, used),
      { targetVocab: targetBpe, minFrequency: 2 },
      offsets,
      []
    );
    this.bpe = nextBpe;
    const blob = this.save();
    this.sha256 = artifactDigest(blob);
  }

  encode(input: Uint8Array | string): Uint32Array {
    if (!this.isValid()) throw new Error("invalid encode");
    const bytes =
      typeof input === "string" ? new TextEncoder().encode(input) : input;
    const n = bytes.length;
    if (n > TOKEN_MAX_CORPUS) throw new Error("invalid encode");

    const ids: number[] = [];
    let pos = 0;
    while (pos < n) {
      const fp = fixedPiece(bytes, pos, n);
      if (fp.len > 0) {
        ids.push(fp.id);
        pos += fp.len;
      } else {
        ids.push(bytes[pos++]);
      }
    }
    return this.bpe.mergeIds(ids);
  }

  decode(ids: Uint32Array | number[]): Uint8Array {
    if (!this.isValid()) throw new Error("invalid decode");
    for (let i = 0; i < ids.length; i++) {
      if (!this.dataId(ids[i])) {
        throw new Error("unassigned or structural token in source decode");
      }
    }

    const chunks: Uint8Array[] = [];
    let total = 0;
    for (let i = 0; i < ids.length; i++) {
      const id = ids[i];
      if (id < this.bpe.vocab) {
        const start = this.bpe.pieceOff[id];
        const end = this.bpe.pieceOff[id + 1];
        const slice = this.bpe.piecePool.subarray(start, end);
        chunks.push(slice);
        total += slice.length;
      } else {
        const exp = expandFixedPiece(id);
        chunks.push(exp);
        total += exp.length;
      }
    }

    const out = new Uint8Array(total);
    let offset = 0;
    for (const c of chunks) {
      out.set(c, offset);
      offset += c.length;
    }
    return out;
  }

  decodeToString(ids: Uint32Array | number[]): string {
    return new TextDecoder().decode(this.decode(ids));
  }

  save(): Uint8Array {
    if (!this.isValid()) throw new Error("invalid tokenizer");
    const encoder = new TextEncoder();
    const header: number[] = [];
    for (const b of encoder.encode("OYEZMT02")) header.push(b);
    writeU32LE(header, 2);
    const serHex = encoder.encode(m1SerializerDigest());
    for (const b of serHex) header.push(b);

    const bpeBlob = this.bpe.save();
    const preSha = new Uint8Array(header.length + bpeBlob.length);
    preSha.set(header, 0);
    preSha.set(bpeBlob, header.length);

    const shaHex = encoder.encode(artifactDigest(preSha));
    const full = new Uint8Array(preSha.length + 64);
    full.set(preSha, 0);
    full.set(shaHex, preSha.length);
    return full;
  }

  load(bytes: Uint8Array): void {
    if (bytes.length < 160) throw new Error("short tokenizer");
    const decoder = new TextDecoder();
    const magic = decoder.decode(bytes.subarray(0, 8));
    const cursor = { pos: 8 };
    const version = readU32LE(bytes, cursor);
    const serStored = decoder.decode(bytes.subarray(12, 76));
    const shaStored = decoder.decode(bytes.subarray(bytes.length - 64));
    const shaComputed = artifactDigest(bytes.subarray(0, bytes.length - 64));

    if (
      magic !== "OYEZMT02" ||
      version !== 2 ||
      serStored !== m1SerializerDigest() ||
      shaStored !== shaComputed
    ) {
      throw new Error("tokenizer version, serializer or SHA-256 mismatch");
    }

    const nextBpe = new BpeTokenizer();
    nextBpe.load(bytes.subarray(76, bytes.length - 64));
    if (nextBpe.vocab > M1_BPE_END || nextBpe.nSpecial !== 0) {
      throw new Error("BPE inventory overlaps reserved vocabulary");
    }
    this.bpe = nextBpe;
    this.sha256 = artifactDigest(bytes);
  }
}

export class M1Sequence {
  ids: number[] = [];
  sourceTokens = 0;
  outputReserve: number;
  generationOpen = false;
  tokenizerSha256: string;
  serializerSha256: string;

  constructor(tokenizer: M1Tokenizer, outputReserve = 1024) {
    if (
      !tokenizer.isValid() ||
      tokenizer.sha256.length !== 64 ||
      outputReserve <= 0 ||
      outputReserve > M1_MAX_SEQUENCE - 5
    ) {
      throw new Error("invalid serializer initialization");
    }
    this.outputReserve = outputReserve;
    this.tokenizerSha256 = tokenizer.sha256;
    this.serializerSha256 = m1SerializerDigest();
    for (let i = 0; i < 4; i++) {
      this.ids.push(M1Control.M1_SINK0 + i);
    }
    this.ids.push(M1Control.M1_BOS);
  }

  appendMessage(
    tokenizer: M1Tokenizer,
    role: M1Control,
    body: Uint8Array | string
  ): void {
    if (
      this.generationOpen ||
      role < M1Control.M1_SYSTEM ||
      role > M1Control.M1_TOOL ||
      !tokenizer.isValid() ||
      tokenizer.sha256 !== this.tokenizerSha256
    ) {
      throw new Error(
        "invalid framing, closed input, role or tokenizer identity"
      );
    }

    const encoded = tokenizer.encode(body);
    const n = encoded.length;
    const landmarks =
      Math.floor((this.sourceTokens + n + 1023) / 1024) -
      Math.floor((this.sourceTokens + 1023) / 1024);
    const extra = n + landmarks + 2;

    if (
      this.ids.length + extra + this.outputReserve >
      M1_MAX_SEQUENCE
    ) {
      throw new Error(
        "framed input plus output reserve exceeds 1,048,576 tokens; source retained by caller"
      );
    }

    this.ids.push(role);
    for (let i = 0; i < n; i++) {
      if (this.sourceTokens % 1024 === 0) {
        this.ids.push(
          M1_LANDMARK_BASE + Math.floor(this.sourceTokens / 1024)
        );
      }
      this.ids.push(encoded[i]);
      this.sourceTokens++;
    }
    this.ids.push(M1Control.M1_END);
  }

  openAssistant(): void {
    if (this.generationOpen) {
      throw new Error("invalid framing or assistant already open");
    }
    if (this.ids.length + 1 + this.outputReserve > M1_MAX_SEQUENCE) {
      throw new Error(
        "framed input plus output reserve exceeds 1,048,576 tokens; source retained by caller"
      );
    }
    this.ids.push(M1Control.M1_ASSISTANT);
    this.generationOpen = true;
  }
}

/* ------------------ Search Engine Lexical + Subword Bridge ---------------- */

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
  const lower = text.toLowerCase();
  const rawTokens = lower.split(/[^a-z0-9]+/);
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
