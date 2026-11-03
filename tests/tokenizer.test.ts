import { describe, expect, test } from "bun:test";
import {
  BpeTokenizer,
  checksumCrc32,
  M1_INDENT_BASE,
  M1_LANDMARK_BASE,
  M1_PRIMITIVE_BASE,
  M1Control,
  M1Sequence,
  M1Tokenizer,
  m1TokenizerName,
  stemToken,
  TOKEN_BASE,
  tokenize,
} from "../src/index/tokenizer.ts";

describe("BpeTokenizer & M1Tokenizer", () => {
  test("base BpeTokenizer lossless round-trip on arbitrary bytes", () => {
    const bpe = new BpeTokenizer();
    expect(bpe.isValid()).toBe(true);
    expect(bpe.vocab).toBe(256);

    const raw = new Uint8Array(256);
    for (let i = 0; i < 256; i++) raw[i] = i;

    const ids = bpe.encode(raw);
    expect(ids.length).toBe(256);
    const decoded = bpe.decode(ids);
    expect(decoded).toEqual(raw);
  });

  test("BpeTokenizer learns merges greedily, respects segment boundaries, and round-trips", () => {
    const bpe = new BpeTokenizer();
    const corpus = "oyez news aggregator ".repeat(20);
    bpe.train(corpus, { targetVocab: 280, minFrequency: 2 }, undefined, [
      "<|sep|>",
      "<|end|>",
    ]);

    expect(bpe.isValid()).toBe(true);
    expect(bpe.nMerges).toBe(22);
    expect(bpe.nSpecial).toBe(2);
    expect(bpe.vocab).toBe(256 + 22 + 2);

    const encoded = bpe.encode(corpus);
    expect(encoded.length).toBeLessThan(corpus.length);
    expect(bpe.decodeToString(encoded)).toBe(corpus);

    const sepId = bpe.specialId("<|sep|>");
    expect(sepId).toBe(TOKEN_BASE + 22);
    expect(bpe.idName(sepId!)).toBe("<|sep|>");

    // Binary serialization round-trip (OYEZBP01 + CRC32)
    const saved = bpe.save();
    const loaded = new BpeTokenizer();
    loaded.load(saved);
    expect(loaded.vocab).toBe(bpe.vocab);
    expect(loaded.encode(corpus)).toEqual(encoded);
    expect(loaded.decodeToString(encoded)).toBe(corpus);
  });

  test("M1Tokenizer fixed pieces, 70/30 training, save/load, and M1Sequence landmarks", () => {
    const m1 = new M1Tokenizer();
    const codeSample =
      "uint32_t* ptr = 0x2a;\n    for (size_t i = 0; i < 64; ++i) {\n\t\tr12 += 0xff;\n    }\n".repeat(
        15
      );
    const proseSample =
      "Breaking news from the global syndicate: open indexing and fast search for everyone. ".repeat(
        15
      );

    m1.train(codeSample, proseSample, 320);
    expect(m1.isValid()).toBe(true);
    expect(m1.bpe.vocab).toBeLessThanOrEqual(320);

    const testSnippet = "uint32_t** x = 0x1f;\n    r7 = 0;\n" + proseSample;
    const ids = m1.encode(testSnippet);
    // Verify fixed pieces were emitted (indent or primitive IDs >= M1_INDENT_BASE)
    expect(ids.some((id) => id >= M1_INDENT_BASE && id < M1_LANDMARK_BASE)).toBe(
      true
    );
    expect(m1.decodeToString(ids)).toBe(testSnippet);

    // Save & load OYEZMT02 artifact
    const blob = m1.save();
    const m1Loaded = new M1Tokenizer();
    m1Loaded.load(blob);
    expect(m1Loaded.sha256).toBe(m1.sha256);
    expect(m1Loaded.decodeToString(m1Loaded.encode(testSnippet))).toBe(
      testSnippet
    );

    // M1Sequence framing + landmarks
    const seq = new M1Sequence(m1Loaded, 512);
    seq.appendMessage(m1Loaded, M1Control.M1_USER, "Hello world!");
    expect(seq.ids.slice(0, 5)).toEqual([
      M1Control.M1_SINK0,
      M1Control.M1_SINK1,
      M1Control.M1_SINK2,
      M1Control.M1_SINK3,
      M1Control.M1_BOS,
    ]);
    expect(seq.ids[5]).toBe(M1Control.M1_USER);
    expect(seq.ids[6]).toBe(M1_LANDMARK_BASE); // Landmark 0 before source token 0
    expect(m1TokenizerName(M1_LANDMARK_BASE)).toBe("<|blk:0000|>");
    expect(m1TokenizerName(M1Control.M1_BOS)).toBe("<|bos|>");

    seq.openAssistant();
    expect(seq.generationOpen).toBe(true);
    expect(seq.ids[seq.ids.length - 1]).toBe(M1Control.M1_ASSISTANT);
  });

  test("CRC32 and lexical stemmer/tokenizer", () => {
    const bytes = new TextEncoder().encode("123456789");
    expect(checksumCrc32(bytes)).toBe(0xcbf43926);

    expect(stemToken("aggregating")).toBe("aggregat");
    expect(stemToken("stories")).toBe("story");
    const tokens = tokenize("The breaking news stories are running quickly!");
    expect(tokens).toContain("break");
    expect(tokens).toContain("new");
    expect(tokens).toContain("story");
    expect(tokens).toContain("run");
  });
});
