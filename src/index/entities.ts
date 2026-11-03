import type { ArticleEntities, NewsType } from "../types.ts";
import { OYEZ_NEWS_ONTOLOGY } from "./category_theory.ts";

const COUNTRY_GAZETTEER: Record<string, string[]> = {
  US: ["united states", "u.s.", "usa", "washington d.c.", "pentagon", "white house", "wall street", "american", "california", "new york", "texas"],
  CA: ["canada", "canadian", "ottawa", "toronto", "vancouver", "montreal", "quebec"],
  MX: ["mexico", "mexican", "mexico city", "guadalajara", "monterrey"],
  BR: ["brazil", "brazilian", "brasilia", "sao paulo", "rio de janeiro", "lula"],
  AR: ["argentina", "argentine", "buenos aires", "milei"],
  CL: ["chile", "chilean", "santiago"],
  CO: ["colombia", "colombian", "bogota", "medellin"],
  PE: ["peru", "peruvian", "lima"],
  VE: ["venezuela", "venezuelan", "caracas"],
  CU: ["cuba", "cuban", "havana"],
  GB: ["united kingdom", "u.k.", "britain", "british", "london", "westminster", "downing street", "scotland", "england", "wales"],
  IE: ["ireland", "irish", "dublin"],
  FR: ["france", "french", "paris", "macron", "elysee"],
  DE: ["germany", "german", "berlin", "frankfurt", "munich", "bundestag"],
  IT: ["italy", "italian", "rome", "milan"],
  ES: ["spain", "spanish", "madrid", "barcelona"],
  PT: ["portugal", "portuguese", "lisbon"],
  NL: ["netherlands", "dutch", "amsterdam", "the hague"],
  BE: ["belgium", "belgian", "brussels"],
  CH: ["switzerland", "swiss", "zurich", "geneva", "bern"],
  AT: ["austria", "austrian", "vienna"],
  SE: ["sweden", "swedish", "stockholm"],
  NO: ["norway", "norwegian", "oslo"],
  DK: ["denmark", "danish", "copenhagen"],
  FI: ["finland", "finnish", "helsinki"],
  PL: ["poland", "polish", "warsaw"],
  UA: ["ukraine", "ukrainian", "kyiv", "kharkiv", "odesa", "zelensky"],
  CZ: ["czechia", "czech", "prague"],
  RO: ["romania", "romanian", "bucharest"],
  HU: ["hungary", "hungarian", "budapest"],
  GR: ["greece", "greek", "athens"],
  RS: ["serbia", "serbian", "belgrade"],
  RU: ["russia", "russian", "moscow", "kremlin"],
  TR: ["turkey", "turkish", "turkiye", "ankara", "istanbul"],
  IL: ["israel", "israeli", "tel aviv", "jerusalem"],
  PS: ["palestine", "palestinian", "gaza", "west bank", "ramallah"],
  LB: ["lebanon", "lebanese", "beirut"],
  SY: ["syria", "syrian", "damascus"],
  JO: ["jordan", "jordanian", "amman"],
  IQ: ["iraq", "iraqi", "baghdad"],
  IR: ["iran", "iranian", "tehran"],
  SA: ["saudi arabia", "saudi", "riyadh", "jeddah"],
  AE: ["united arab emirates", "uae", "dubai", "abu dhabi", "emirati"],
  QA: ["qatar", "qatari", "doha"],
  KW: ["kuwait", "kuwaiti"],
  YE: ["yemen", "yemeni", "sanaa"],
  EG: ["egypt", "egyptian", "cairo", "suez"],
  MA: ["morocco", "moroccan", "rabat", "casablanca"],
  DZ: ["algeria", "algerian", "algiers"],
  TN: ["tunisia", "tunisian", "tunis"],
  NG: ["nigeria", "nigerian", "lagos", "abuja"],
  ZA: ["south africa", "south african", "johannesburg", "cape town", "pretoria"],
  KE: ["kenya", "kenyan", "nairobi"],
  ET: ["ethiopia", "ethiopian", "addis ababa"],
  GH: ["ghana", "ghanaian", "accra"],
  SN: ["senegal", "senegalese", "dakar"],
  TZ: ["tanzania", "tanzanian", "dar es salaam", "dodoma"],
  UG: ["uganda", "ugandan", "kampala"],
  CD: ["congo", "congolese", "kinshasa", "drc"],
  SD: ["sudan", "sudanese", "khartoum"],
  RW: ["rwanda", "rwandan", "kigali"],
  IN: ["india", "indian", "new delhi", "mumbai", "bengaluru", "bangalore", "hyderabad", "chennai"],
  PK: ["pakistan", "pakistani", "islamabad", "karachi", "lahore"],
  BD: ["bangladesh", "bangladeshi", "dhaka"],
  LK: ["sri lanka", "sri lankan", "colombo"],
  NP: ["nepal", "nepali", "kathmandu"],
  AF: ["afghanistan", "afghan", "kabul"],
  CN: ["china", "chinese", "beijing", "shanghai", "shenzhen", "guangzhou"],
  JP: ["japan", "japanese", "tokyo", "osaka", "kyoto"],
  KR: ["south korea", "korean", "seoul", "busan"],
  KP: ["north korea", "pyongyang"],
  TW: ["taiwan", "taiwanese", "taipei"],
  HK: ["hong kong"],
  MN: ["mongolia", "mongolian", "ulaanbaatar"],
  SG: ["singapore", "singaporean"],
  ID: ["indonesia", "indonesian", "jakarta", "bali"],
  MY: ["malaysia", "malaysian", "kuala lumpur"],
  TH: ["thailand", "thai", "bangkok"],
  VN: ["vietnam", "vietnamese", "hanoi", "ho chi minh"],
  PH: ["philippines", "philippine", "filipino", "manila"],
  MM: ["myanmar", "burma", "burmese", "yangon"],
  KH: ["cambodia", "cambodian", "phnom penh"],
  KZ: ["kazakhstan", "kazakh", "astana", "almaty"],
  UZ: ["uzbekistan", "uzbek", "tashkent"],
  GE: ["georgia", "georgian", "tbilisi"],
  AM: ["armenia", "armenian", "yerevan"],
  AZ: ["azerbaijan", "azerbaijani", "baku"],
  AU: ["australia", "australian", "sydney", "melbourne", "canberra", "brisbane"],
  NZ: ["new zealand", "auckland", "wellington", "kiwi"],
  FJ: ["fiji", "fijian", "suva"],
  PG: ["papua new guinea", "port moresby"],
};

const TOPIC_KEYWORDS: Record<string, string[]> = {
  "ai-and-machine-learning": [
    "artificial intelligence",
    "machine learning",
    "large language model",
    "llm",
    "neural network",
    "generative ai",
    "transformer model",
    "openai",
    "anthropic",
    "deepmind",
    "inference",
  ],
  "semiconductors-and-hardware": [
    "semiconductor",
    "semiconductors",
    "microchip",
    "microchips",
    "wafer",
    "tsmc",
    "nvidia",
    "gpu",
    "asic",
    "riscv",
    "risc-v",
    "foundry",
    "lithography",
    "asml",
    "silicon",
  ],
  cybersecurity: [
    "cybersecurity",
    "ransomware",
    "zero-day",
    "vulnerability",
    "malware",
    "data breach",
    "phishing",
    "encryption",
    "exploit",
    "botnet",
  ],
  "climate-and-environment": [
    "climate change",
    "global warming",
    "carbon emissions",
    "greenhouse gas",
    "biodiversity",
    "deforestation",
    "wildfire",
    "drought",
    "glacier",
    "net zero",
    "renewable",
  ],
  "energy-and-commodities": [
    "solar power",
    "wind turbine",
    "nuclear reactor",
    "crude oil",
    "natural gas",
    "opec",
    "battery storage",
    "lithium",
    "power grid",
    "clean energy",
    "hydrogen",
  ],
  "macroeconomics-and-finance": [
    "inflation",
    "interest rate",
    "interest rates",
    "central bank",
    "federal reserve",
    "gdp",
    "recession",
    "bond yield",
    "stock market",
    "imf",
    "fiscal policy",
    "monetary policy",
    "treasury",
  ],
  "geopolitics-and-diplomacy": [
    "summit",
    "treaty",
    "sanctions",
    "ambassador",
    "diplomatic",
    "bilateral",
    "united nations",
    "security council",
    "foreign minister",
    "sovereignty",
    "ceasefire",
  ],
  "defense-and-conflict": [
    "military",
    "missile",
    "artillery",
    "airstrike",
    "defense ministry",
    "armed forces",
    "naval",
    "nato",
    "drone strike",
    "peacekeeping",
    "insurgency",
  ],
  "elections-and-democracy": [
    "election",
    "elections",
    "ballot",
    "parliament",
    "coalition government",
    "referendum",
    "polling station",
    "voter turnout",
    "prime minister",
    "presidential race",
    "legislature",
  ],
  "public-health-and-biotech": [
    "vaccine",
    "clinical trial",
    "epidemic",
    "pandemic",
    "genomics",
    "crispr",
    "world health organization",
    "pharmaceutical",
    "oncology",
    "pathogen",
    "antibiotic",
  ],
  "space-and-aerospace": [
    "spacecraft",
    "satellite",
    "orbital",
    "nasa",
    "esa",
    "jaxa",
    "isro",
    "spacex",
    "lunar",
    "exoplanet",
    "telescope",
    "launch vehicle",
  ],
  "trade-and-supply-chain": [
    "tariff",
    "tariffs",
    "supply chain",
    "supply chains",
    "export control",
    "export controls",
    "free trade",
    "wto",
    "shipping lane",
    "freight",
    "customs",
    "import duty",
  ],
  "open-source-and-software": [
    "open source",
    "linux kernel",
    "compiler",
    "typescript",
    "rust",
    "database",
    "distributed system",
    "protocol",
    "webassembly",
    "git",
  ],
};

const KNOWN_ORGANIZATIONS = [
  "United Nations",
  "NATO",
  "European Union",
  "ASEAN",
  "African Union",
  "BRICS",
  "OPEC",
  "IMF",
  "World Bank",
  "World Health Organization",
  "WTO",
  "G7",
  "G20",
  "Federal Reserve",
  "European Central Bank",
  "Bank of Japan",
  "Bank of England",
  "International Criminal Court",
  "Amnesty International",
  "Red Cross",
  "NASA",
  "ESA",
  "ISRO",
  "JAXA",
  "CERN",
  "OpenAI",
  "DeepMind",
  "Anthropic",
  "NVIDIA",
  "TSMC",
  "Linux Foundation",
];

const PROPER_NOUN_SKIP = new Set([
  "The",
  "This",
  "That",
  "These",
  "Those",
  "What",
  "When",
  "Where",
  "Which",
  "While",
  "Who",
  "Why",
  "How",
  "New",
  "More",
  "Most",
  "Some",
  "Many",
  "After",
  "Before",
  "During",
  "Under",
  "Over",
  "Between",
  "Into",
  "Through",
  "Monday",
  "Tuesday",
  "Wednesday",
  "Thursday",
  "Friday",
  "Saturday",
  "Sunday",
  "January",
  "February",
  "March",
  "April",
  "May",
  "June",
  "July",
  "August",
  "September",
  "October",
  "November",
  "December",
  "Read",
  "Watch",
  "Listen",
  "Breaking",
  "Update",
  "Report",
  "Analysis",
  "Opinion",
]);

interface PhraseMatchHit {
  countries?: string[];
  topics?: string[];
  orgs?: string[];
}

/**
 * Precompiled Single-Pass N-Gram Phrase Automaton (1..4 word window)
 * Eliminates 130+ regex scans per article in favor of O(L) hash table lookups.
 */
const PHRASE_TABLE = new Map<string, PhraseMatchHit>();
const FIRST_WORD_MAX_LEN = new Map<string, number>();

function registerPhrase(
  rawPhrase: string,
  kind: "country" | "topic" | "org",
  value: string
): void {
  const words = rawPhrase
    .toLowerCase()
    .split(/[^a-z0-9.]+/)
    .filter(Boolean);
  if (words.length === 0) return;
  const key = words.join(" ");
  const firstWord = words[0];
  const prevMax = FIRST_WORD_MAX_LEN.get(firstWord) ?? 0;
  if (words.length > prevMax) {
    FIRST_WORD_MAX_LEN.set(firstWord, words.length);
  }

  let hit = PHRASE_TABLE.get(key);
  if (!hit) {
    hit = {};
    PHRASE_TABLE.set(key, hit);
  }
  if (kind === "country") {
    hit.countries = hit.countries ?? [];
    if (!hit.countries.includes(value)) hit.countries.push(value);
  } else if (kind === "topic") {
    hit.topics = hit.topics ?? [];
    if (!hit.topics.includes(value)) hit.topics.push(value);
  } else {
    hit.orgs = hit.orgs ?? [];
    if (!hit.orgs.includes(value)) hit.orgs.push(value);
  }
}

for (const [code, phrases] of Object.entries(COUNTRY_GAZETTEER)) {
  for (const p of phrases) registerPhrase(p, "country", code);
}
for (const [topic, phrases] of Object.entries(TOPIC_KEYWORDS)) {
  for (const p of phrases) registerPhrase(p, "topic", topic);
}
for (const org of KNOWN_ORGANIZATIONS) {
  registerPhrase(org, "org", org);
}

/**
 * Classify the epistemic / journalistic genre (NewsType) of an article
 */
export function classifyNewsType(
  title: string,
  summary: string,
  contentText: string,
  feedCategory?: string,
  url?: string
): NewsType {
  const text = `${title} ${summary} ${contentText.slice(0, 1000)}`.toLowerCase();
  const urlLower = (url ?? "").toLowerCase();

  if (
    /\b(?:breaking:|breaking news|just in:|live updates|urgent:|flash:|developing story)/.test(
      text
    )
  ) {
    return "breaking-dispatch";
  }

  if (
    feedCategory === "investigative" ||
    /\b(?:investigation|investigative|revealed|leaked|whistleblower|exclusive report|osint|bellingcat|propublica|icij)\b/.test(
      text
    )
  ) {
    return "investigative-report";
  }

  if (
    urlLower.includes("/opinion/") ||
    urlLower.includes("/editorial/") ||
    urlLower.includes("/commentisfree/") ||
    /\b(?:opinion:|editorial:|op-ed|column:|commentary:|why we must|in my view)/.test(
      text
    )
  ) {
    return "opinion-and-editorial";
  }

  if (
    feedCategory === "science" ||
    /\b(?:study finds|researchers|peer-reviewed|published in nature|clinical trial|scientists discover|journal of|laboratory|genome|arxiv)\b/.test(
      text
    )
  ) {
    return "scientific-research";
  }

  if (
    /\b(?:explained:|explainer:|what to know about|how does|everything you need to know|q&a:|deep dive|guide to)/.test(
      text
    )
  ) {
    return "explainer-and-analysis";
  }

  if (
    /\b(?:supreme court|parliament passes|legislation|bill signed|central bank|regulation|sanctions imposed|treaty ratified|ruling|indictment|verdict|ministry announced)\b/.test(
      text
    )
  ) {
    return "policy-and-legislative";
  }

  if (
    /\b(?:quarterly earnings|stock market|gdp growth|bond yields|inflation rate|interest rates|ipo|market rally|barrel of oil|trade deficit|fiscal quarter)\b/.test(
      text
    )
  ) {
    return "market-and-economic";
  }

  if (
    /\b(?:press release|official statement|joint communique|ministry of foreign affairs states)\b/.test(
      text
    )
  ) {
    return "press-and-official";
  }

  if (
    feedCategory === "culture" ||
    /\b(?:book review|film festival|exhibition|album|museum|architecture|cuisine|portrait of)\b/.test(
      text
    )
  ) {
    return "cultural-and-feature";
  }

  return "general-report";
}

export function extractArticleEntities(
  title: string,
  summary: string,
  contentText: string,
  sourceCountry?: string,
  feedCategory?: string,
  extraCategories: string[] = []
): ArticleEntities {
  const combined = `${title}\n${summary}\n${contentText.slice(0, 4000)}`;
  const words = combined
    .toLowerCase()
    .split(/[^a-z0-9.]+/)
    .filter(Boolean);

  const mentionedSet = new Set<string>();
  if (sourceCountry && sourceCountry !== "INT" && sourceCountry.length === 2) {
    mentionedSet.add(sourceCountry.toUpperCase());
  }
  const topicsSet = new Set<string>();
  const entitiesSet = new Set<string>();

  // Single linear O(L) scan over word tokens
  const len = words.length;
  for (let i = 0; i < len; i++) {
    const w0 = words[i].replace(/\.+$/, "");
    const maxWindow = Math.max(
      FIRST_WORD_MAX_LEN.get(words[i]) ?? 0,
      FIRST_WORD_MAX_LEN.get(w0) ?? 0
    );
    if (maxWindow === 0) continue;

    let phrase = "";
    const limit = Math.min(maxWindow, len - i);
    for (let k = 0; k < limit; k++) {
      const wk = k === limit - 1 ? words[i + k].replace(/\.+$/, "") : words[i + k];
      phrase = k === 0 ? wk : `${phrase} ${wk}`;
      const hit = PHRASE_TABLE.get(phrase) ?? (k === 0 ? PHRASE_TABLE.get(words[i]) : undefined);
      if (hit) {
        if (hit.countries) {
          for (const c of hit.countries) mentionedSet.add(c);
        }
        if (hit.topics) {
          for (const t of hit.topics) topicsSet.add(t);
        }
        if (hit.orgs) {
          for (const o of hit.orgs) entitiesSet.add(o);
        }
      }
    }
  }

  // Functorial upward closure in the News Ontology Poset Category C
  const seedNodes = [
    ...(feedCategory ? [feedCategory] : []),
    ...extraCategories,
    ...topicsSet,
  ];
  const ontologyClosure = OYEZ_NEWS_ONTOLOGY.upwardClosureOf(seedNodes);

  // Extract capitalized 2-to-3 word proper noun sequences from title & summary
  const titleAndSummary = `${title} . ${summary}`;
  const properPhraseRegex =
    /\b([A-Z][a-z]{2,15}(?:\s+[A-Z][a-z]{2,15}){1,2})\b/g;
  let match: RegExpExecArray | null;
  while ((match = properPhraseRegex.exec(titleAndSummary)) !== null) {
    const phrase = match[1].trim();
    const pWords = phrase.split(/\s+/);
    if (pWords.some((w) => PROPER_NOUN_SKIP.has(w))) continue;
    entitiesSet.add(phrase);
    if (entitiesSet.size >= 14) break;
  }

  return {
    mentionedCountries: Array.from(mentionedSet),
    topics: Array.from(topicsSet),
    ontologyClosure,
    organizationsAndPeople: Array.from(entitiesSet).slice(0, 12),
  };
}
