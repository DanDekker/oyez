import type { ArticleEntities } from "../types.ts";

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
    "microchip",
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
    "cve-",
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
    "supply chain",
    "export control",
    "free trade",
    "wto",
    "shipping lane",
    "freight",
    "customs",
    "import duty",
    "semiconductor export",
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

function escapeRegex(s: string): string {
  return s.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

// Precompile boundary regexes for gazetteer & organizations
const COUNTRY_PATTERNS: Array<{ code: string; regex: RegExp }> = Object.entries(
  COUNTRY_GAZETTEER
).map(([code, terms]) => {
  const joined = terms.map(escapeRegex).join("|");
  return {
    code,
    regex: new RegExp(`\\b(?:${joined})\\b`, "i"),
  };
});

const ORG_PATTERNS: Array<{ name: string; regex: RegExp }> =
  KNOWN_ORGANIZATIONS.map((name) => ({
    name,
    regex: new RegExp(`\\b${escapeRegex(name)}\\b`, "i"),
  }));

export function extractArticleEntities(
  title: string,
  summary: string,
  contentText: string,
  sourceCountry?: string
): ArticleEntities {
  const combined = `${title}\n${summary}\n${contentText.slice(0, 4000)}`;
  const lower = combined.toLowerCase();

  // 1. Mentioned countries
  const mentionedSet = new Set<string>();
  if (sourceCountry && sourceCountry !== "INT" && sourceCountry.length === 2) {
    mentionedSet.add(sourceCountry.toUpperCase());
  }
  for (const { code, regex } of COUNTRY_PATTERNS) {
    if (regex.test(lower)) {
      mentionedSet.add(code);
    }
  }

  // 2. Semantic topics
  const topicsSet = new Set<string>();
  for (const [topic, keywords] of Object.entries(TOPIC_KEYWORDS)) {
    for (const kw of keywords) {
      if (lower.includes(kw)) {
        topicsSet.add(topic);
        break;
      }
    }
  }

  // 3. Organizations and multi-word Proper Noun phrases
  const entitiesSet = new Set<string>();
  for (const { name, regex } of ORG_PATTERNS) {
    if (regex.test(combined)) {
      entitiesSet.add(name);
    }
  }

  // Extract capitalized 2-to-3 word proper noun sequences from title & summary
  const titleAndSummary = `${title} . ${summary}`;
  const properPhraseRegex =
    /\b([A-Z][a-z]{2,15}(?:\s+[A-Z][a-z]{2,15}){1,2})\b/g;
  let match: RegExpExecArray | null;
  while ((match = properPhraseRegex.exec(titleAndSummary)) !== null) {
    const phrase = match[1].trim();
    const words = phrase.split(/\s+/);
    if (words.some((w) => PROPER_NOUN_SKIP.has(w))) continue;
    entitiesSet.add(phrase);
    if (entitiesSet.size >= 14) break;
  }

  return {
    mentionedCountries: Array.from(mentionedSet),
    topics: Array.from(topicsSet),
    organizationsAndPeople: Array.from(entitiesSet).slice(0, 12),
  };
}
