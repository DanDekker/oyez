#include "ontology.h"

#include "core.h"

#include <ctype.h>
#include <string.h>

static const char *const k_region_slugs[WORLD_REGION_COUNT] = {
    "north-america",
    "latin-america",
    "europe",
    "middle-east",
    "africa",
    "south-asia",
    "east-asia",
    "southeast-asia",
    "central-asia",
    "oceania",
    "global",
};

static const char *const k_news_type_slugs[NEWS_TYPE_COUNT] = {
    "breaking-dispatch",
    "investigative-report",
    "policy-and-legislative",
    "scientific-research",
    "market-and-economic",
    "opinion-and-editorial",
    "explainer-and-analysis",
    "press-and-official",
    "cultural-and-feature",
    "general-report",
};

#define BIT(x) (1u << (uint32_t)(x))

static OntologyNode g_ontology[ONTOLOGY_NODE_COUNT] = {
    {ONTO_ROOT, "all-news", "All Global News (Terminal Object 1)", 0u, 0u, 0u},
    {ONTO_PUBLIC_AFFAIRS, "public-affairs", "Public Affairs, Statecraft & Security", BIT(ONTO_ROOT), 0u, 0u},
    {ONTO_KNOWLEDGE_ECONOMY, "knowledge-economy", "Science, Technology & Markets", BIT(ONTO_ROOT), 0u, 0u},
    {ONTO_PLANETARY_SYSTEMS, "planetary-systems", "Planetary Systems, Climate & Health", BIT(ONTO_ROOT), 0u, 0u},
    {ONTO_SOCIETY, "society", "Civil Society, Culture & Human Affairs", BIT(ONTO_ROOT), 0u, 0u},
    {ONTO_POLITICS, "politics", "Domestic Politics & Elections", BIT(ONTO_PUBLIC_AFFAIRS), 0u, 0u},
    {ONTO_GEOPOLITICS, "geopolitics", "Geopolitics & Diplomacy", BIT(ONTO_PUBLIC_AFFAIRS), 0u, 0u},
    {ONTO_LAW, "law", "Law, Judiciary & Regulation", BIT(ONTO_PUBLIC_AFFAIRS) | BIT(ONTO_POLITICS), 0u, 0u},
    {ONTO_SECURITY, "security", "National & International Security", BIT(ONTO_PUBLIC_AFFAIRS) | BIT(ONTO_GEOPOLITICS), 0u, 0u},
    {ONTO_DEFENSE, "defense", "Armed Conflict & Defense", BIT(ONTO_SECURITY) | BIT(ONTO_GEOPOLITICS), 0u, 0u},
    {ONTO_CYBER, "cyber", "Cybersecurity & Info Warfare", BIT(ONTO_SECURITY) | BIT(ONTO_TECHNOLOGY), 0u, 0u},
    {ONTO_ECONOMY, "economy", "Macroeconomics & Trade", BIT(ONTO_KNOWLEDGE_ECONOMY) | BIT(ONTO_PUBLIC_AFFAIRS), 0u, 0u},
    {ONTO_FINANCE, "finance", "Financial Markets & Central Banking", BIT(ONTO_ECONOMY) | BIT(ONTO_KNOWLEDGE_ECONOMY), 0u, 0u},
    {ONTO_TECHNOLOGY, "technology", "Computing & Engineering", BIT(ONTO_KNOWLEDGE_ECONOMY), 0u, 0u},
    {ONTO_AI, "ai", "Artificial Intelligence & Robotics", BIT(ONTO_TECHNOLOGY) | BIT(ONTO_SCIENCE), 0u, 0u},
    {ONTO_SCIENCE, "science", "Fundamental & Applied Science", BIT(ONTO_KNOWLEDGE_ECONOMY) | BIT(ONTO_PLANETARY_SYSTEMS), 0u, 0u},
    {ONTO_SPACE, "space", "Aerospace & Astrophysics", BIT(ONTO_SCIENCE) | BIT(ONTO_TECHNOLOGY), 0u, 0u},
    {ONTO_HEALTH, "health", "Medicine, Biotech & Public Health", BIT(ONTO_PLANETARY_SYSTEMS) | BIT(ONTO_SCIENCE) | BIT(ONTO_SOCIETY), 0u, 0u},
    {ONTO_CLIMATE, "climate", "Climate, Environment & Biodiversity", BIT(ONTO_PLANETARY_SYSTEMS) | BIT(ONTO_SCIENCE), 0u, 0u},
    {ONTO_ENERGY, "energy", "Energy Systems & Commodities", BIT(ONTO_CLIMATE) | BIT(ONTO_ECONOMY) | BIT(ONTO_GEOPOLITICS), 0u, 0u},
    {ONTO_HUMAN_RIGHTS, "human-rights", "Human Rights, Migration & Humanitarian", BIT(ONTO_SOCIETY) | BIT(ONTO_LAW) | BIT(ONTO_GEOPOLITICS), 0u, 0u},
    {ONTO_LABOR, "labor", "Labor, Unions & Employment", BIT(ONTO_SOCIETY) | BIT(ONTO_ECONOMY), 0u, 0u},
    {ONTO_CULTURE, "culture", "Arts, Media & Culture", BIT(ONTO_SOCIETY), 0u, 0u},
    {ONTO_SPORTS, "sports", "Global Sports & Athletics", BIT(ONTO_SOCIETY), 0u, 0u},
};

static int g_ontology_ready = 0;

void ontology_init(void) {
  if(g_ontology_ready) return;
  for(uint32_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) {
    g_ontology[i].upward_mask = BIT(i) | g_ontology[i].parents_mask;
  }
  /* Transitive closure Warshall pass on 24-bit poset masks */
  for(int iter = 0; iter < 6; ++iter) {
    for(uint32_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) {
      uint32_t m = g_ontology[i].upward_mask;
      uint32_t next = m;
      while(m) {
        uint32_t p = ctz64((uint64_t)m);
        next |= g_ontology[p].upward_mask;
        m &= (m - 1u);
      }
      g_ontology[i].upward_mask = next;
    }
  }
  for(uint32_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) {
    uint32_t down = 0u;
    for(uint32_t j = 0; j < ONTOLOGY_NODE_COUNT; ++j) {
      if((g_ontology[j].upward_mask & BIT(i)) != 0u) {
        down |= BIT(j);
      }
    }
    g_ontology[i].downward_mask = down;
  }
  g_ontology_ready = 1;
}

const char *world_region_slug(WorldRegion region) {
  if((unsigned)region >= WORLD_REGION_COUNT) return "global";
  return k_region_slugs[region];
}

int world_region_from_slug(const char *slug, WorldRegion *out) {
  if(!slug || !out) return -1;
  for(uint32_t i = 0; i < WORLD_REGION_COUNT; ++i) {
    if(strcmp(slug, k_region_slugs[i]) == 0) {
      *out = (WorldRegion)i;
      return 0;
    }
  }
  return -1;
}

const char *news_type_slug(NewsType type) {
  if((unsigned)type >= NEWS_TYPE_COUNT) return "general-report";
  return k_news_type_slugs[type];
}

int news_type_from_slug(const char *slug, NewsType *out) {
  if(!slug || !out) return -1;
  for(uint32_t i = 0; i < NEWS_TYPE_COUNT; ++i) {
    if(strcmp(slug, k_news_type_slugs[i]) == 0) {
      *out = (NewsType)i;
      return 0;
    }
  }
  return -1;
}

const OntologyNode *ontology_node(OntologyNodeId id) {
  ontology_init();
  if((unsigned)id >= ONTOLOGY_NODE_COUNT) return &g_ontology[ONTO_ROOT];
  return &g_ontology[id];
}

int ontology_from_slug(const char *slug, OntologyNodeId *out) {
  if(!slug || !out) return -1;
  ontology_init();
  for(uint32_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) {
    if(strcmp(slug, g_ontology[i].slug) == 0) {
      *out = (OntologyNodeId)i;
      return 0;
    }
  }
  return -1;
}

int ontology_has_morphism(OntologyNodeId sub, OntologyNodeId sup) {
  ontology_init();
  if((unsigned)sub >= ONTOLOGY_NODE_COUNT || (unsigned)sup >= ONTOLOGY_NODE_COUNT) return 0;
  return (g_ontology[sub].upward_mask & BIT(sup)) != 0u;
}

uint32_t ontology_upward_closure(uint32_t mask) {
  ontology_init();
  uint32_t out = 0u;
  while(mask) {
    uint32_t bit = ctz64((uint64_t)mask);
    if(bit < ONTOLOGY_NODE_COUNT) out |= g_ontology[bit].upward_mask;
    mask &= (mask - 1u);
  }
  return out;
}

uint32_t ontology_downward_closure(uint32_t mask) {
  ontology_init();
  uint32_t out = 0u;
  while(mask) {
    uint32_t bit = ctz64((uint64_t)mask);
    if(bit < ONTOLOGY_NODE_COUNT) out |= g_ontology[bit].downward_mask;
    mask &= (mask - 1u);
  }
  return out;
}

OntologyNodeId ontology_join(OntologyNodeId a, OntologyNodeId b) {
  ontology_init();
  if((unsigned)a >= ONTOLOGY_NODE_COUNT || (unsigned)b >= ONTOLOGY_NODE_COUNT) return ONTO_ROOT;
  uint32_t common = g_ontology[a].upward_mask & g_ontology[b].upward_mask;
  /* Minimal element in common upper bounds: node u in common whose downward_mask & common == {u} */
  uint32_t scan = common;
  OntologyNodeId best = ONTO_ROOT;
  uint32_t best_up_size = 999u;
  while(scan) {
    uint32_t u = ctz64((uint64_t)scan);
    uint32_t up_cnt = popcnt64((uint64_t)g_ontology[u].upward_mask);
    if(up_cnt > best_up_size || best == ONTO_ROOT) {
      best = (OntologyNodeId)u;
      best_up_size = up_cnt;
    }
    scan &= (scan - 1u);
  }
  return best;
}

uint32_t ontology_meet_mask(OntologyNodeId a, OntologyNodeId b) {
  ontology_init();
  if((unsigned)a >= ONTOLOGY_NODE_COUNT || (unsigned)b >= ONTOLOGY_NODE_COUNT) return 0u;
  uint32_t common = g_ontology[a].downward_mask & g_ontology[b].downward_mask;
  uint32_t maximal = 0u;
  uint32_t scan = common;
  while(scan) {
    uint32_t l = ctz64((uint64_t)scan);
    uint32_t strict_up = g_ontology[l].upward_mask & ~BIT(l);
    if((strict_up & common) == 0u) {
      maximal |= BIT(l);
    }
    scan &= (scan - 1u);
  }
  return maximal;
}

void ontology_left_kan_extend(const double *raw_scores, double *out_kan_scores) {
  ontology_init();
  for(uint32_t c = 0; c < ONTOLOGY_NODE_COUNT; ++c) {
    uint32_t ideal = g_ontology[c].downward_mask;
    double sum = 0.0;
    while(ideal) {
      uint32_t sub = ctz64((uint64_t)ideal);
      double raw = raw_scores[sub];
      if(raw > 0.0) {
        sum += raw;
      }
      ideal &= (ideal - 1u);
    }
    out_kan_scores[c] = sum;
  }
}

static int text_contains_word(const char *lower_text, size_t len, const char *needle) {
  size_t nlen = strlen(needle);
  if(nlen == 0 || len < nlen) return 0;
  const char *p = lower_text;
  const char *end = lower_text + len;
  while(p + nlen <= end) {
    const char *hit = strstr(p, needle);
    if(!hit) return 0;
    int left_ok = (hit == lower_text) || !isalnum((unsigned char)hit[-1]);
    int right_ok = (hit + nlen >= end) || !isalnum((unsigned char)hit[nlen]);
    if(left_ok && right_ok) return 1;
    p = hit + 1u;
  }
  return 0;
}

static void build_lowercase_window(const char *a,
                                   const char *b,
                                   const char *c,
                                   char *buf,
                                   size_t cap,
                                   size_t *out_len) {
  size_t pos = 0;
  const char *parts[3] = {a, b, c};
  for(int k = 0; k < 3; ++k) {
    const char *s = parts[k];
    if(!s) continue;
    while(*s && pos + 2u < cap) {
      buf[pos++] = (char)tolower((unsigned char)*s++);
    }
    if(pos + 2u < cap) buf[pos++] = ' ';
  }
  buf[pos] = '\0';
  *out_len = pos;
}

NewsType news_type_classify(const char *title,
                            const char *summary,
                            const char *content,
                            const char *feed_category) {
  char lower[4096];
  size_t len = 0;
  build_lowercase_window(title, summary, content, lower, sizeof(lower), &len);

  char tlower[512];
  size_t tlen = 0;
  build_lowercase_window(title, NULL, NULL, tlower, sizeof(tlower), &tlen);

  double scores[NEWS_TYPE_COUNT] = {0};

  if(strstr(tlower, "breaking:") || strstr(tlower, "urgent:") || strstr(tlower, "flash:") ||
     strstr(tlower, "live updates") || strstr(tlower, "just in")) {
    scores[NEWS_TYPE_BREAKING_DISPATCH] += 5.0;
  }
  if(text_contains_word(lower, len, "breaking") || text_contains_word(lower, len, "developing")) {
    scores[NEWS_TYPE_BREAKING_DISPATCH] += 2.0;
  }

  if(feed_category && strcmp(feed_category, "investigative") == 0) {
    scores[NEWS_TYPE_INVESTIGATIVE_REPORT] += 4.5;
  }
  if(text_contains_word(lower, len, "investigation") || text_contains_word(lower, len, "investigative") ||
     text_contains_word(lower, len, "whistleblower") || text_contains_word(lower, len, "leaked") ||
     text_contains_word(lower, len, "uncovered") || text_contains_word(lower, len, "audit")) {
    scores[NEWS_TYPE_INVESTIGATIVE_REPORT] += 3.5;
  }

  if(text_contains_word(lower, len, "parliament") || text_contains_word(lower, len, "senate") ||
     text_contains_word(lower, len, "congress") || text_contains_word(lower, len, "legislation") ||
     text_contains_word(lower, len, "supreme court") || text_contains_word(lower, len, "ruling") ||
     text_contains_word(lower, len, "treaty") || text_contains_word(lower, len, "sanctions") ||
     text_contains_word(lower, len, "regulation") || text_contains_word(lower, len, "decree")) {
    scores[NEWS_TYPE_POLICY_AND_LEGISLATIVE] += 3.2;
  }

  if(feed_category && strcmp(feed_category, "science") == 0) {
    scores[NEWS_TYPE_SCIENTIFIC_RESEARCH] += 2.5;
  }
  if(text_contains_word(lower, len, "peer-reviewed") || text_contains_word(lower, len, "arxiv") ||
     text_contains_word(lower, len, "researchers") || text_contains_word(lower, len, "scientists") ||
     text_contains_word(lower, len, "clinical trial") || text_contains_word(lower, len, "genome") ||
     text_contains_word(lower, len, "quantum") || text_contains_word(lower, len, "telescope")) {
    scores[NEWS_TYPE_SCIENTIFIC_RESEARCH] += 3.4;
  }

  if(feed_category && strcmp(feed_category, "economy") == 0) {
    scores[NEWS_TYPE_MARKET_AND_ECONOMIC] += 2.5;
  }
  if(text_contains_word(lower, len, "inflation") || text_contains_word(lower, len, "central bank") ||
     text_contains_word(lower, len, "interest rate") || text_contains_word(lower, len, "basis points") ||
     text_contains_word(lower, len, "gdp") || text_contains_word(lower, len, "earnings") ||
     text_contains_word(lower, len, "ipo") || text_contains_word(lower, len, "yields")) {
    scores[NEWS_TYPE_MARKET_AND_ECONOMIC] += 3.3;
  }

  if(strstr(tlower, "opinion:") || strstr(tlower, "editorial:") || strstr(tlower, "column:") ||
     strstr(tlower, "op-ed")) {
    scores[NEWS_TYPE_OPINION_AND_EDITORIAL] += 5.0;
  }

  if(strstr(tlower, "explainer:") || strstr(tlower, "analysis:") || strstr(tlower, "what to know") ||
     strstr(tlower, "how ") || strstr(tlower, "why ")) {
    scores[NEWS_TYPE_EXPLAINER_AND_ANALYSIS] += 3.4;
  }

  if(text_contains_word(lower, len, "communique") || text_contains_word(lower, len, "press release") ||
     text_contains_word(lower, len, "ministry of") || text_contains_word(lower, len, "official statement")) {
    scores[NEWS_TYPE_PRESS_AND_OFFICIAL] += 3.1;
  }

  if(feed_category && (strcmp(feed_category, "culture") == 0 || strcmp(feed_category, "sports") == 0)) {
    scores[NEWS_TYPE_CULTURAL_AND_FEATURE] += 2.8;
  }
  if(text_contains_word(lower, len, "festival") || text_contains_word(lower, len, "exhibition") ||
     text_contains_word(lower, len, "cinema") || text_contains_word(lower, len, "tournament") ||
     text_contains_word(lower, len, "championship")) {
    scores[NEWS_TYPE_CULTURAL_AND_FEATURE] += 3.0;
  }

  NewsType best = NEWS_TYPE_GENERAL_REPORT;
  double best_score = 1.5;
  for(uint32_t i = 0; i < NEWS_TYPE_COUNT - 1u; ++i) {
    if(scores[i] > best_score) {
      best_score = scores[i];
      best = (NewsType)i;
    }
  }
  return best;
}

typedef struct {
  const char *iso2;
  const char *const terms[5];
} GeoRule;

static const GeoRule k_geo_rules[] = {
    {"US", {"united states", "u.s.", "washington", "pentagon", "white house"}},
    {"CA", {"canada", "canadian", "ottawa", "toronto", NULL}},
    {"MX", {"mexico", "mexican", "mexico city", NULL, NULL}},
    {"GT", {"guatemala", "guatemalan", "guatemala city", NULL, NULL}},
    {"SV", {"el salvador", "salvadoran", "san salvador", NULL, NULL}},
    {"NI", {"nicaragua", "nicaraguan", "managua", NULL, NULL}},
    {"CR", {"costa rica", "costa rican", "san jose", NULL, NULL}},
    {"CU", {"cuba", "cuban", "havana", NULL, NULL}},
    {"JM", {"jamaica", "jamaican", "kingston", NULL, NULL}},
    {"TT", {"trinidad", "tobago", "port of spain", NULL, NULL}},
    {"BR", {"brazil", "brazilian", "brasilia", "sao paulo", NULL}},
    {"AR", {"argentina", "argentine", "buenos aires", NULL, NULL}},
    {"UY", {"uruguay", "uruguayan", "montevideo", NULL, NULL}},
    {"CO", {"colombia", "colombian", "bogota", NULL, NULL}},
    {"CL", {"chile", "chilean", "santiago", NULL, NULL}},
    {"PE", {"peru", "peruvian", "lima", NULL, NULL}},
    {"EC", {"ecuador", "ecuadorian", "quito", "guayaquil", NULL}},
    {"VE", {"venezuela", "venezuelan", "caracas", NULL, NULL}},
    {"GB", {"united kingdom", "britain", "british", "london", "westminster"}},
    {"IE", {"ireland", "irish", "dublin", NULL, NULL}},
    {"FR", {"france", "french", "paris", "macron", NULL}},
    {"DE", {"germany", "german", "berlin", "bundestag", "frankfurt"}},
    {"IT", {"italy", "italian", "rome", "milan", NULL}},
    {"ES", {"spain", "spanish", "madrid", "barcelona", NULL}},
    {"PT", {"portugal", "portuguese", "lisbon", NULL, NULL}},
    {"NL", {"netherlands", "dutch", "amsterdam", "the hague", NULL}},
    {"BE", {"belgium", "belgian", "brussels", "antwerp", NULL}},
    {"CH", {"switzerland", "swiss", "geneva", "zurich", NULL}},
    {"SE", {"sweden", "swedish", "stockholm", NULL, NULL}},
    {"NO", {"norway", "norwegian", "oslo", "barents", NULL}},
    {"FI", {"finland", "finnish", "helsinki", NULL, NULL}},
    {"IS", {"iceland", "icelandic", "reykjavik", NULL, NULL}},
    {"EE", {"estonia", "estonian", "tallinn", NULL, NULL}},
    {"LV", {"latvia", "latvian", "riga", NULL, NULL}},
    {"LT", {"lithuania", "lithuanian", "vilnius", NULL, NULL}},
    {"PL", {"poland", "polish", "warsaw", NULL, NULL}},
    {"CZ", {"czech", "prague", "czechia", NULL, NULL}},
    {"HU", {"hungary", "hungarian", "budapest", NULL, NULL}},
    {"RO", {"romania", "romanian", "bucharest", NULL, NULL}},
    {"RS", {"serbia", "serbian", "belgrade", "balkans", NULL}},
    {"UA", {"ukraine", "ukrainian", "kyiv", "kharkiv", "odesa"}},
    {"RU", {"russia", "russian", "moscow", "kremlin", NULL}},
    {"TR", {"turkey", "turkish", "ankara", "istanbul", NULL}},
    {"GR", {"greece", "greek", "athens", "aegean", NULL}},
    {"IL", {"israel", "israeli", "jerusalem", "tel aviv", NULL}},
    {"PS", {"palestine", "palestinian", "gaza", "west bank", "ramallah"}},
    {"LB", {"lebanon", "lebanese", "beirut", NULL, NULL}},
    {"SY", {"syria", "syrian", "damascus", "aleppo", NULL}},
    {"JO", {"jordan", "jordanian", "amman", NULL, NULL}},
    {"IR", {"iran", "iranian", "tehran", NULL, NULL}},
    {"IQ", {"iraq", "iraqi", "baghdad", "erbil", NULL}},
    {"YE", {"yemen", "yemeni", "sanaa", "houthi", NULL}},
    {"SA", {"saudi arabia", "saudi", "riyadh", NULL, NULL}},
    {"AE", {"united arab emirates", "uae", "dubai", "abu dhabi", NULL}},
    {"QA", {"qatar", "qatari", "doha", NULL, NULL}},
    {"EG", {"egypt", "egyptian", "cairo", "suez", NULL}},
    {"LY", {"libya", "libyan", "tripoli", "benghazi", NULL}},
    {"TN", {"tunisia", "tunisian", "tunis", NULL, NULL}},
    {"MA", {"morocco", "moroccan", "rabat", "casablanca", NULL}},
    {"NG", {"nigeria", "nigerian", "lagos", "abuja", NULL}},
    {"ZA", {"south africa", "johannesburg", "cape town", "pretoria", NULL}},
    {"KE", {"kenya", "kenyan", "nairobi", NULL, NULL}},
    {"TZ", {"tanzania", "tanzanian", "dar es salaam", "dodoma", NULL}},
    {"UG", {"uganda", "ugandan", "kampala", NULL, NULL}},
    {"CD", {"congo", "congolese", "kinshasa", "goma", NULL}},
    {"ET", {"ethiopia", "ethiopian", "addis ababa", NULL, NULL}},
    {"SO", {"somalia", "somali", "mogadishu", "somaliland", NULL}},
    {"SD", {"sudan", "sudanese", "khartoum", "darfur", NULL}},
    {"GH", {"ghana", "ghanaian", "accra", NULL, NULL}},
    {"SN", {"senegal", "senegalese", "dakar", NULL, NULL}},
    {"ZW", {"zimbabwe", "zimbabwean", "harare", NULL, NULL}},
    {"ZM", {"zambia", "zambian", "lusaka", NULL, NULL}},
    {"NA", {"namibia", "namibian", "windhoek", NULL, NULL}},
    {"IN", {"india", "indian", "new delhi", "mumbai", "bengaluru"}},
    {"PK", {"pakistan", "pakistani", "islamabad", "karachi", NULL}},
    {"BD", {"bangladesh", "bangladeshi", "dhaka", NULL, NULL}},
    {"LK", {"sri lanka", "colombo", "sri lankan", NULL, NULL}},
    {"NP", {"nepal", "nepali", "kathmandu", NULL, NULL}},
    {"AF", {"afghanistan", "afghan", "kabul", "kandahar", NULL}},
    {"CN", {"china", "chinese", "beijing", "shanghai", "shenzhen"}},
    {"JP", {"japan", "japanese", "tokyo", "osaka", NULL}},
    {"KR", {"south korea", "korean", "seoul", NULL, NULL}},
    {"TW", {"taiwan", "taiwanese", "taipei", "tsmc", NULL}},
    {"HK", {"hong kong", NULL, NULL, NULL, NULL}},
    {"MN", {"mongolia", "mongolian", "ulaanbaatar", NULL, NULL}},
    {"SG", {"singapore", "singaporean", NULL, NULL, NULL}},
    {"ID", {"indonesia", "indonesian", "jakarta", "nusantara", NULL}},
    {"PH", {"philippines", "philippine", "manila", "luzon", NULL}},
    {"VN", {"vietnam", "vietnamese", "hanoi", "ho chi minh", NULL}},
    {"TH", {"thailand", "thai", "bangkok", NULL, NULL}},
    {"MM", {"myanmar", "burmese", "yangon", "naypyidaw", NULL}},
    {"MY", {"malaysia", "malaysian", "kuala lumpur", NULL, NULL}},
    {"KH", {"cambodia", "cambodian", "phnom penh", NULL, NULL}},
    {"LA", {"laos", "laotian", "vientiane", NULL, NULL}},
    {"KZ", {"kazakhstan", "kazakh", "astana", "almaty", NULL}},
    {"UZ", {"uzbekistan", "uzbek", "tashkent", NULL, NULL}},
    {"KG", {"kyrgyzstan", "kyrgyz", "bishkek", NULL, NULL}},
    {"TJ", {"tajikistan", "tajik", "dushanbe", NULL, NULL}},
    {"TM", {"turkmenistan", "turkmen", "ashgabat", NULL, NULL}},
    {"GE", {"georgia", "georgian", "tbilisi", NULL, NULL}},
    {"AM", {"armenia", "armenian", "yerevan", NULL, NULL}},
    {"AZ", {"azerbaijan", "azerbaijani", "baku", NULL, NULL}},
    {"AU", {"australia", "australian", "canberra", "sydney", "melbourne"}},
    {"NZ", {"new zealand", "wellington", "auckland", NULL, NULL}},
    {"FJ", {"fiji", "fijian", "suva", NULL, NULL}},
    {"PG", {"papua new guinea", "port moresby", NULL, NULL, NULL}},
    {"WS", {"samoa", "samoan", "apia", NULL, NULL}},
};

typedef struct {
  OntologyNodeId node;
  const char *const terms[9];
} TopicRule;

static const TopicRule k_topic_rules[] = {
    {ONTO_AI, {"artificial intelligence", "llm", "neural network", "openai", "deepmind", "anthropic", "generative ai", "machine learning", NULL}},
    {ONTO_CYBER, {"cybersecurity", "ransomware", "zero-day", "malware", "hacker", "data breach", "encryption", "phishing", NULL}},
    {ONTO_TECHNOLOGY, {"semiconductor", "microchip", "software", "cloud computing", "open source", "linux", "silicon", "chipmaker", NULL}},
    {ONTO_SPACE, {"nasa", "esa", "spacex", "satellite", "spacecraft", "astronaut", "lunar", "orbit", "telescope"}},
    {ONTO_SCIENCE, {"physics", "quantum", "researchers", "scientists", "laboratory", "peer-reviewed", "chemistry", "genome", NULL}},
    {ONTO_FINANCE, {"central bank", "federal reserve", "ecb", "interest rate", "bond yield", "wall street", "stock market", "ipo", "venture capital"}},
    {ONTO_ECONOMY, {"inflation", "gdp", "recession", "tariff", "supply chain", "imf", "world bank", "trade deficit", "macroeconomic"}},
    {ONTO_ENERGY, {"crude oil", "opec", "natural gas", "solar power", "wind farm", "nuclear reactor", "power grid", "battery", "hydrogen"}},
    {ONTO_CLIMATE, {"climate change", "carbon emissions", "greenhouse", "wildfire", "drought", "biodiversity", "deforestation", "glacier", NULL}},
    {ONTO_HEALTH, {"vaccine", "pandemic", "epidemic", "hospital", "clinical trial", "public health", "virus", "cancer", "fda"}},
    {ONTO_DEFENSE, {"military", "missile", "airstrike", "ceasefire", "artillery", "warship", "troops", "battalion", "nato"}},
    {ONTO_SECURITY, {"intelligence agency", "espionage", "counterterrorism", "border security", "surveillance", "national security", NULL, NULL, NULL}},
    {ONTO_GEOPOLITICS, {"diplomacy", "ambassador", "summit", "bilateral", "sanctions", "foreign policy", "treaty", "sovereignty", "geopolitical"}},
    {ONTO_POLITICS, {"parliament", "election", "coalition", "prime minister", "president", "referendum", "ballot", "campaign", "legislature"}},
    {ONTO_LAW, {"supreme court", "judiciary", "indictment", "antitrust", "prosecutor", "constitutional", "lawsuit", "verdict", "tribunal"}},
    {ONTO_HUMAN_RIGHTS, {"human rights", "refugee", "asylum", "humanitarian", "amnesty", "civil liberties", "displaced", "war crimes", NULL}},
    {ONTO_LABOR, {"labor union", "strike", "collective bargaining", "minimum wage", "layoffs", "workforce", "workers", NULL, NULL}},
    {ONTO_CULTURE, {"museum", "cinema", "literature", "architecture", "exhibition", "symphony", "novel", NULL, NULL}},
    {ONTO_SPORTS, {"olympics", "world cup", "championship", "premier league", "tournament", "grand prix", "stadium", NULL, NULL}},
};

typedef struct {
  const char *name;
  const char *const terms[4];
} OrgRule;

static const OrgRule k_org_rules[] = {
    {"UN", {"united nations", "u.n.", "security council", NULL}},
    {"NATO", {"nato", "north atlantic treaty", NULL, NULL}},
    {"EU", {"european union", "european commission", "brussels", NULL}},
    {"WHO", {"world health organization", "w.h.o.", NULL, NULL}},
    {"IMF", {"international monetary fund", "imf", NULL, NULL}},
    {"World Bank", {"world bank", NULL, NULL, NULL}},
    {"WTO", {"world trade organization", "wto", NULL, NULL}},
    {"BRICS", {"brics", NULL, NULL, NULL}},
    {"ASEAN", {"asean", NULL, NULL, NULL}},
    {"African Union", {"african union", NULL, NULL, NULL}},
    {"OPEC", {"opec", "opec+", NULL, NULL}},
    {"G7", {"g7", "group of seven", NULL, NULL}},
    {"G20", {"g20", "group of twenty", NULL, NULL}},
    {"ICC", {"international criminal court", "icj", NULL, NULL}},
    {"Fed", {"federal reserve", "fomc", NULL, NULL}},
    {"ECB", {"european central bank", "ecb", NULL, NULL}},
};

void entities_extract(const char *title,
                      const char *summary,
                      const char *content,
                      const char *feed_category,
                      const char *const *tags,
                      size_t tag_count,
                      ExtractedEntities *out) {
  if(!out) return;
  ontology_init();
  memset(out, 0, sizeof(*out));

  char lower[4096];
  size_t len = 0;
  build_lowercase_window(title, summary, content, lower, sizeof(lower), &len);

  for(size_t i = 0; i < sizeof(k_geo_rules) / sizeof(k_geo_rules[0]); ++i) {
    if(out->country_count >= MAX_EXTRACTED_COUNTRIES) break;
    for(size_t t = 0; t < 5u && k_geo_rules[i].terms[t]; ++t) {
      if(text_contains_word(lower, len, k_geo_rules[i].terms[t])) {
        strncpy(out->countries[out->country_count], k_geo_rules[i].iso2, 3u);
        out->countries[out->country_count][3] = '\0';
        out->country_count++;
        break;
      }
    }
  }

  for(size_t i = 0; i < sizeof(k_org_rules) / sizeof(k_org_rules[0]); ++i) {
    if(out->org_count >= MAX_EXTRACTED_ORGS) break;
    for(size_t t = 0; t < 4u && k_org_rules[i].terms[t]; ++t) {
      if(text_contains_word(lower, len, k_org_rules[i].terms[t])) {
        strncpy(out->organizations[out->org_count], k_org_rules[i].name, 23u);
        out->organizations[out->org_count][23] = '\0';
        out->org_count++;
        break;
      }
    }
  }

  uint32_t direct = 0u;
  if(feed_category) {
    OntologyNodeId fc_node = ONTO_ROOT;
    if(ontology_from_slug(feed_category, &fc_node) == 0 && fc_node != ONTO_ROOT) {
      direct |= BIT(fc_node);
    }
  }
  for(size_t i = 0; i < tag_count; ++i) {
    if(!tags[i]) continue;
    OntologyNodeId t_node = ONTO_ROOT;
    if(ontology_from_slug(tags[i], &t_node) == 0 && t_node != ONTO_ROOT) {
      direct |= BIT(t_node);
    }
  }

  for(size_t i = 0; i < sizeof(k_topic_rules) / sizeof(k_topic_rules[0]); ++i) {
    for(size_t t = 0; t < 9u && k_topic_rules[i].terms[t]; ++t) {
      if(text_contains_word(lower, len, k_topic_rules[i].terms[t])) {
        direct |= BIT(k_topic_rules[i].node);
        break;
      }
    }
  }

  if(direct == 0u) direct = BIT(ONTO_PUBLIC_AFFAIRS);
  out->direct_ontology_mask = direct;
  out->closure_ontology_mask = ontology_upward_closure(direct);
  out->news_type = news_type_classify(title, summary, content, feed_category);
}

static const char *const k_stopwords[] = {
    "a",     "about", "above", "after", "again", "against", "all",   "am",    "an",    "and",
    "any",   "are",   "as",    "at",    "be",    "because", "been",  "before","being", "below",
    "between","both", "but",   "by",    "can",   "could",   "did",   "do",    "does",  "doing",
    "down",  "during","each",  "few",   "for",   "from",    "further","had",  "has",   "have",
    "having","he",    "her",   "here",  "hers",  "herself", "him",   "himself","his",  "how",
    "i",     "if",    "in",    "into",  "is",    "it",      "its",   "itself","just",  "me",
    "more",  "most",  "my",    "myself","no",    "nor",     "not",   "now",   "of",    "off",
    "on",    "once",  "only",  "or",    "other", "our",     "ours",  "out",   "over",  "own",
    "same",  "she",   "should","so",    "some",  "such",    "than",  "that",  "the",   "their",
    "theirs","them",  "then",  "there", "these", "they",    "this",  "those", "through","to",
    "too",   "under", "until", "up",    "very",  "was",     "we",    "were",  "what",  "when",
    "where", "which", "while", "who",   "whom",  "why",     "will",  "with",  "would", "you",
    "your",  "yours",
};

static int is_stopword(const char *w) {
  for(size_t i = 0; i < sizeof(k_stopwords) / sizeof(k_stopwords[0]); ++i) {
    if(strcmp(w, k_stopwords[i]) == 0) return 1;
  }
  return 0;
}

static void stem_word(char *w, size_t len) {
  if(len > 5u && strcmp(w + len - 3u, "ies") == 0) {
    strcpy(w + len - 3u, "y");
  } else if(len > 5u && strcmp(w + len - 3u, "ing") == 0) {
    w[len - 3u] = '\0';
  } else if(len > 4u && strcmp(w + len - 2u, "ed") == 0) {
    w[len - 2u] = '\0';
  } else if(len > 4u && w[len - 1u] == 's' && w[len - 2u] != 's') {
    w[len - 1u] = '\0';
  }
}

size_t lexical_analyze(const char *text,
                       float field_weight,
                       LexicalTerm *terms,
                       size_t current_count,
                       size_t max_terms,
                       uint32_t *out_word_tokens) {
  if(!text || !terms) return current_count;
  size_t p = 0;
  while(text[p] != '\0') {
    while(text[p] != '\0' && !isalnum((unsigned char)text[p])) p++;
    if(text[p] == '\0') break;
    char word[32];
    size_t wlen = 0;
    while(text[p] != '\0' && (isalnum((unsigned char)text[p]) || text[p] == '-')) {
      if(wlen < 30u) {
        word[wlen++] = (char)tolower((unsigned char)text[p]);
      }
      p++;
    }
    while(wlen > 0 && word[wlen - 1u] == '-') wlen--;
    word[wlen] = '\0';
    if(wlen < 2u || is_stopword(word)) continue;
    stem_word(word, wlen);
    wlen = strlen(word);
    if(wlen < 2u) continue;

    if(out_word_tokens) (*out_word_tokens)++;
    uint64_t h = fnv1a64(word, wlen);

    int found = 0;
    for(size_t i = 0; i < current_count; ++i) {
      if(terms[i].hash == h && strcmp(terms[i].term, word) == 0) {
        terms[i].weight += field_weight;
        terms[i].tf += 1u;
        found = 1;
        break;
      }
    }
    if(!found && current_count < max_terms) {
      strcpy(terms[current_count].term, word);
      terms[current_count].hash = h;
      terms[current_count].weight = field_weight;
      terms[current_count].tf = 1u;
      current_count++;
    }
  }
  return current_count;
}

uint64_t simhash64_compute(const LexicalTerm *terms, size_t count) {
  if(!terms || count == 0) return 0u;
  double accum[64] = {0};
  for(size_t i = 0; i < count; ++i) {
    uint64_t h = mix64(terms[i].hash);
    double w = (double)terms[i].weight;
    for(uint32_t bit = 0; bit < 64u; ++bit) {
      if((h >> bit) & 1ULL) accum[bit] += w;
      else accum[bit] -= w;
    }
  }
  uint64_t sig = 0u;
  for(uint32_t bit = 0; bit < 64u; ++bit) {
    if(accum[bit] > 0.0) sig |= (1ULL << bit);
  }
  return sig;
}

uint32_t simhash64_distance(uint64_t a, uint64_t b) {
  return popcnt64(a ^ b);
}
