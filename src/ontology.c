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

static const char *const k_clearance_slugs[CLEARANCE_LEVEL_COUNT] = {
    "public-open",
    "osint-verified",
    "tradecraft-and-opsec",
    "zero-day-and-crypto",
    "strategic-high-clearance",
};

static const char *const k_fiscal_stance_slugs[FISCAL_STANCE_COUNT] = {
    "neutral-baseline",
    "hawkish-tightening",
    "dovish-easing",
    "bullish-expansion",
    "bearish-contraction",
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
    {ONTO_AI, "ai", "Artificial Intelligence & Foundation Models", BIT(ONTO_TECHNOLOGY) | BIT(ONTO_SCIENCE), 0u, 0u},
    {ONTO_SCIENCE, "science", "Fundamental & Applied Science", BIT(ONTO_KNOWLEDGE_ECONOMY) | BIT(ONTO_PLANETARY_SYSTEMS), 0u, 0u},
    {ONTO_SPACE, "space", "Aerospace & Astrophysics", BIT(ONTO_SCIENCE) | BIT(ONTO_TECHNOLOGY), 0u, 0u},
    {ONTO_HEALTH, "health", "Medicine, Biotech & Public Health", BIT(ONTO_PLANETARY_SYSTEMS) | BIT(ONTO_SCIENCE) | BIT(ONTO_SOCIETY), 0u, 0u},
    {ONTO_CLIMATE, "climate", "Climate, Environment & Biodiversity", BIT(ONTO_PLANETARY_SYSTEMS) | BIT(ONTO_SCIENCE), 0u, 0u},
    {ONTO_ENERGY, "energy", "Energy Systems & Commodities", BIT(ONTO_CLIMATE) | BIT(ONTO_ECONOMY) | BIT(ONTO_GEOPOLITICS), 0u, 0u},
    {ONTO_HUMAN_RIGHTS, "human-rights", "Human Rights, Migration & Humanitarian", BIT(ONTO_SOCIETY) | BIT(ONTO_LAW) | BIT(ONTO_GEOPOLITICS), 0u, 0u},
    {ONTO_LABOR, "labor", "Labor, Unions & Employment", BIT(ONTO_SOCIETY) | BIT(ONTO_ECONOMY), 0u, 0u},
    {ONTO_CULTURE, "culture", "Arts, Media & Culture", BIT(ONTO_SOCIETY), 0u, 0u},
    {ONTO_SPORTS, "sports", "Global Sports & Athletics", BIT(ONTO_SOCIETY), 0u, 0u},
    {ONTO_QUANTUM, "quantum", "Quantum Physics & Quantum Computing", BIT(ONTO_SCIENCE) | BIT(ONTO_TECHNOLOGY), 0u, 0u},
    {ONTO_SEMICONDUCTORS, "semiconductors", "Semiconductors, Foundry & Silicon Architecture", BIT(ONTO_TECHNOLOGY) | BIT(ONTO_ECONOMY) | BIT(ONTO_GEOPOLITICS), 0u, 0u},
    {ONTO_BIOTECH, "biotech", "Genomics, Synthetic Biology & CRISPR", BIT(ONTO_HEALTH) | BIT(ONTO_SCIENCE), 0u, 0u},
    {ONTO_ROBOTICS, "robotics", "Robotics, Autonomous Systems & Embodied AI", BIT(ONTO_AI) | BIT(ONTO_TECHNOLOGY), 0u, 0u},
    {ONTO_MATHEMATICS, "mathematics", "Pure & Applied Mathematics, Category Theory & Logic", BIT(ONTO_SCIENCE) | BIT(ONTO_AI), 0u, 0u},
    {ONTO_MATERIALS, "materials", "Condensed Matter, Superconductors & Metamaterials", BIT(ONTO_SCIENCE) | BIT(ONTO_SEMICONDUCTORS) | BIT(ONTO_ENERGY), 0u, 0u},
    {ONTO_CRYPTOGRAPHY, "cryptography", "Zero-Knowledge Proofs, Lattice Crypto & Formal Security", BIT(ONTO_CYBER) | BIT(ONTO_MATHEMATICS), 0u, 0u},
    {ONTO_NUCLEAR_FUSION, "nuclear-fusion", "Plasma Physics, Tokamaks & Advanced Fission", BIT(ONTO_ENERGY) | BIT(ONTO_SCIENCE), 0u, 0u},
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

const char *clearance_level_slug(ClearanceLevel level) {
  if((unsigned)level >= CLEARANCE_LEVEL_COUNT) return "public-open";
  return k_clearance_slugs[level];
}

int clearance_level_from_slug(const char *slug, ClearanceLevel *out) {
  if(!slug || !out) return -1;
  for(uint32_t i = 0; i < CLEARANCE_LEVEL_COUNT; ++i) {
    if(strcmp(slug, k_clearance_slugs[i]) == 0) {
      *out = (ClearanceLevel)i;
      return 0;
    }
  }
  return -1;
}

const char *fiscal_stance_slug(FiscalStance stance) {
  if((unsigned)stance >= FISCAL_STANCE_COUNT) return "neutral-baseline";
  return k_fiscal_stance_slugs[stance];
}

int fiscal_stance_from_slug(const char *slug, FiscalStance *out) {
  if(!slug || !out) return -1;
  for(uint32_t i = 0; i < FISCAL_STANCE_COUNT; ++i) {
    if(strcmp(slug, k_fiscal_stance_slugs[i]) == 0) {
      *out = (FiscalStance)i;
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

void ontology_right_kan_extend(const double *raw_scores, double *out_ran_scores) {
  ontology_init();
  for(uint32_t c = 0; c < ONTOLOGY_NODE_COUNT; ++c) {
    uint32_t filter = g_ontology[c].upward_mask;
    double limit_val = 1e18;
    int any = 0;
    while(filter) {
      uint32_t sup = ctz64((uint64_t)filter);
      if(sup != ONTO_ROOT) {
        double v = raw_scores[sup];
        if(!any || v < limit_val) {
          limit_val = v;
          any = 1;
        }
      }
      filter &= (filter - 1u);
    }
    out_ran_scores[c] = any ? limit_val : raw_scores[c];
  }
}

uint32_t ontology_heyting_implication(uint32_t sub_mask_a, uint32_t sub_mask_b) {
  ontology_init();
  /* In the distributive lattice of downward-closed ideals (specialized sub-domains):
   * da = downward closure of A, db = downward closure of B.
   * Since AI <= TECHNOLOGY, da(AI) is a subset of db(TECHNOLOGY), so (da & ~db) == 0.
   * For any node c, c is in (A => B) iff downward_mask[c] & da <= db. */
  uint32_t da = ontology_downward_closure(sub_mask_a);
  uint32_t db = ontology_downward_closure(sub_mask_b);
  uint32_t imp = 0u;
  for(uint32_t c = 0; c < ONTOLOGY_NODE_COUNT; ++c) {
    uint32_t down_c = g_ontology[c].downward_mask;
    if(((down_c & da) & ~db) == 0u) {
      imp |= BIT(c);
    }
  }
  return imp;
}

uint32_t ontology_heyting_negation(uint32_t sub_mask_a) {
  return ontology_heyting_implication(sub_mask_a, 0u);
}

void ontology_sheaf_cohomology(const double *raw_fiber_counts,
                               const double *closure_fiber_counts,
                               SheafCohomology *out) {
  if(!out) return;
  ontology_init();
  memset(out, 0, sizeof(*out));
  double total_norm = 0.0;
  for(uint32_t c = 0; c < ONTOLOGY_NODE_COUNT; ++c) {
    uint32_t strict_sub = g_ontology[c].downward_mask & ~BIT(c);
    out->grothendieck_sieve[c] = strict_sub;
    double sub_sum = 0.0;
    uint32_t scan = strict_sub;
    while(scan) {
      uint32_t u = ctz64((uint64_t)scan);
      sub_sum += raw_fiber_counts ? raw_fiber_counts[u] : 0.0;
      scan &= (scan - 1u);
    }
    double direct = raw_fiber_counts ? raw_fiber_counts[c] : 0.0;
    double actual_closure = closure_fiber_counts ? closure_fiber_counts[c] : (direct + sub_sum);
    double glued = direct + sub_sum;
    /* Overlap defect: how much double-counting occurs across intersecting sub-fibers in the sieve */
    double defect = glued > actual_closure ? (glued - actual_closure) : 0.0;
    out->sheaf_section[c] = actual_closure;
    out->cohomology_defect[c] = defect;
    total_norm += defect * defect;
  }
  out->total_cohomology_norm = total_norm;
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

  if(feed_category && (strcmp(feed_category, "science") == 0 || strcmp(feed_category, "ai") == 0 ||
                       strcmp(feed_category, "space") == 0)) {
    scores[NEWS_TYPE_SCIENTIFIC_RESEARCH] += 2.6;
  }
  if(text_contains_word(lower, len, "peer-reviewed") || text_contains_word(lower, len, "arxiv") ||
     text_contains_word(lower, len, "biorxiv") || text_contains_word(lower, len, "medrxiv") ||
     text_contains_word(lower, len, "chemrxiv") || text_contains_word(lower, len, "preprint") ||
     text_contains_word(lower, len, "theorem") || text_contains_word(lower, len, "synchrotron") ||
     text_contains_word(lower, len, "tokamak") || text_contains_word(lower, len, "qubit") ||
     text_contains_word(lower, len, "researchers") || text_contains_word(lower, len, "scientists") ||
     text_contains_word(lower, len, "clinical trial") || text_contains_word(lower, len, "genome") ||
     text_contains_word(lower, len, "quantum") || text_contains_word(lower, len, "telescope")) {
    scores[NEWS_TYPE_SCIENTIFIC_RESEARCH] += 3.6;
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
    {ONTO_AI, {"artificial intelligence", "llm", "neural network", "openai", "deepmind", "anthropic", "generative ai", "machine learning", "transformer"}},
    {ONTO_ROBOTICS, {"robotics", "humanoid", "autonomous vehicle", "drone", "actuator", "manipulator", "embodied ai", "quadruped", NULL}},
    {ONTO_QUANTUM, {"quantum computing", "qubit", "superconducting qubit", "quantum error correction", "entanglement", "topological qubit", "quantum supremacy", "quantum photonics", NULL}},
    {ONTO_SEMICONDUCTORS, {"semiconductor", "tsmc", "asml", "lithography", "wafer", "foundry", "euv", "risc-v", "hbm"}},
    {ONTO_BIOTECH, {"crispr", "gene editing", "genomics", "mrna", "protein folding", "alphafold", "synthetic biology", "sequencing", "cell therapy"}},
    {ONTO_MATHEMATICS, {"category theory", "theorem", "conjecture", "topos", "cohomology", "homotopy", "kan extension", "formal verification", "type theory"}},
    {ONTO_MATERIALS, {"superconductor", "graphene", "perovskite", "metamaterial", "topological insulator", "condensed matter", "solid-state battery", "cathode", NULL}},
    {ONTO_CRYPTOGRAPHY, {"zero-knowledge", "zk-snark", "post-quantum cryptography", "lattice cryptography", "homomorphic encryption", "cryptographic", "elliptic curve", "cipher", NULL}},
    {ONTO_NUCLEAR_FUSION, {"nuclear fusion", "tokamak", "iter", "stellarator", "plasma confinement", "inertial confinement", "deuterium", "tritium", NULL}},
    {ONTO_CYBER, {"cybersecurity", "ransomware", "zero-day", "malware", "hacker", "data breach", "encryption", "phishing", NULL}},
    {ONTO_TECHNOLOGY, {"microchip", "software", "cloud computing", "open source", "linux", "silicon", "chipmaker", "compiler", "kernel"}},
    {ONTO_SPACE, {"nasa", "esa", "spacex", "satellite", "spacecraft", "astronaut", "lunar", "orbit", "telescope"}},
    {ONTO_SCIENCE, {"physics", "quantum", "researchers", "scientists", "laboratory", "peer-reviewed", "chemistry", "genome", "synchrotron"}},
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
    {"CERN", {"cern", "large hadron collider", "lhc", NULL}},
    {"NASA", {"nasa", "jet propulsion laboratory", NULL, NULL}},
    {"ESA", {"european space agency", "esa", NULL, NULL}},
    {"ISRO", {"isro", "indian space research", NULL, NULL}},
    {"JAXA", {"jaxa", "japan aerospace", NULL, NULL}},
    {"CAS", {"chinese academy of sciences", NULL, NULL, NULL}},
    {"CNRS", {"cnrs", NULL, NULL, NULL}},
    {"Max Planck", {"max planck", NULL, NULL, NULL}},
    {"RIKEN", {"riken", NULL, NULL, NULL}},
    {"CSIRO", {"csiro", NULL, NULL, NULL}},
    {"ITER", {"iter", "iter organization", NULL, NULL}},
    {"NSA", {"national security agency", "nsa", "cnsa 2.0", NULL}},
    {"DARPA", {"darpa", "defense advanced research", NULL, NULL}},
    {"IARPA", {"iarpa", NULL, NULL, NULL}},
    {"GCHQ/NCSC", {"gchq", "ncsc", NULL, NULL}},
    {"CISA", {"cisa", "known exploited vulnerabilities", NULL, NULL}},
    {"ANSSI", {"anssi", "cert-fr", NULL, NULL}},
    {"BSI", {"bsi", NULL, NULL, NULL}},
    {"ENISA", {"enisa", NULL, NULL, NULL}},
    {"W3C", {"w3c", "world wide web consortium", "webgpu", "webauthn"}},
    {"WHATWG", {"whatwg", "html living standard", NULL, NULL}},
    {"IETF", {"ietf", "internet engineering task force", "quic", "privacy pass"}},
    {"Tor Project", {"tor project", "onion service", "pluggable transport", NULL}},
    {"BoE", {"bank of england", "mpc", NULL, NULL}},
    {"BoJ", {"bank of japan", "ueda", NULL, NULL}},
    {"PBOC", {"people's bank of china", "pboc", NULL, NULL}},
    {"RBI", {"reserve bank of india", "rbi", NULL, NULL}},
    {"SNB", {"swiss national bank", "snb", NULL, NULL}},
    {"SEC", {"securities and exchange commission", "sec filing", "10-k", NULL}},
    {"CFTC", {"commodity futures trading commission", "cftc", NULL, NULL}},
    {"BIS", {"bank for international settlements", NULL, NULL, NULL}},
    {"CBO", {"congressional budget office", "cbo", NULL, NULL}},
};

typedef struct {
  const char *symbol;
  const char *const terms[5];
} AssetRule;

static const AssetRule k_asset_rules[] = {
    /* Currencies (G10 & Major EM FX) */
    {"USD", {"u.s. dollar", "greenback", "dxy", "dollar index", NULL}},
    {"EUR", {"eurozone", "eur/usd", "single currency", "euro", NULL}},
    {"JPY", {"japanese yen", "usd/jpy", "yen carry", "yen", NULL}},
    {"GBP", {"pound sterling", "gbp/usd", "sterling", "british pound", NULL}},
    {"CHF", {"swiss franc", "usd/chf", "eur/chf", NULL, NULL}},
    {"CNY", {"renminbi", "yuan", "usd/cny", "usd/cnh", NULL}},
    {"INR", {"indian rupee", "usd/inr", "rupee", NULL, NULL}},
    {"BRL", {"brazilian real", "usd/brl", "selic", NULL, NULL}},
    {"CAD", {"canadian dollar", "loonie", "usd/cad", NULL, NULL}},
    {"AUD", {"australian dollar", "aussie dollar", "aud/usd", NULL, NULL}},
    {"MXN", {"mexican peso", "usd/mxn", "super peso", NULL, NULL}},
    {"ZAR", {"south african rand", "usd/zar", NULL, NULL, NULL}},
    {"TRY", {"turkish lira", "usd/try", NULL, NULL, NULL}},
    /* Equity Indices, Sovereign Bonds & Commodities */
    {"SPX", {"s&p 500", "wall street", "sp500", NULL, NULL}},
    {"NDX", {"nasdaq", "nasdaq-100", NULL, NULL, NULL}},
    {"DJI", {"dow jones", "djia", NULL, NULL, NULL}},
    {"NIKKEI", {"nikkei 225", "topix", "tokyo stock exchange", NULL, NULL}},
    {"FTSE", {"ftse 100", "london stock exchange", NULL, NULL, NULL}},
    {"DAX", {"dax 40", "frankfurt stock", NULL, NULL, NULL}},
    {"NIFTY", {"nifty 50", "sensex", "bse", NULL, NULL}},
    {"UST10Y", {"treasury yield", "10-year yield", "10-year treasury", "sovereign bond", "gilt yield"}},
    {"BRENT", {"brent crude", "wti crude", "crude oil", "opec+", NULL}},
    {"XAU", {"gold bullion", "gold price", "spot gold", "precious metal", NULL}},
    {"COPPER", {"copper price", "lme copper", "comex copper", NULL, NULL}},
    {"LITHIUM", {"lithium carbonate", "lithium spodumene", "battery metal", NULL, NULL}},
    {"NVDA", {"nvidia", "blackwell", "h100", NULL, NULL}},
    {"TSMC", {"tsmc", "taiwan semiconductor", NULL, NULL, NULL}},
    {"ASML", {"asml", "extreme ultraviolet", NULL, NULL, NULL}},
    {"BTC", {"bitcoin", "btc", "spot etf", NULL, NULL}},
};

typedef struct {
  const char *event_name;
  const char *const terms[5];
} FiscalEventRule;

static const FiscalEventRule k_fiscal_event_rules[] = {
    {"rate-decision", {"interest rate", "basis points", "rate hike", "rate cut", "policy rate"}},
    {"inflation-print", {"cpi", "core inflation", "pce deflator", "hicp", "producer price"}},
    {"labor-payrolls", {"nonfarm payrolls", "unemployment rate", "jobless claims", "labor market", NULL}},
    {"gdp-release", {"real gdp", "gdp growth", "economic contraction", "recession", NULL}},
    {"sovereign-debt-auction", {"treasury auction", "bond auction", "quarterly refunding", "debt ceiling", "eurobond"}},
    {"fiscal-budget", {"federal budget", "budget deficit", "fiscal stimulus", "austerity", "autumn budget"}},
    {"earnings-guidance", {"quarterly earnings", "eps beat", "revenue guidance", "10-k", "share buyback"}},
    {"fx-intervention", {"fx intervention", "currency swap", "capital controls", "devaluation", "foreign exchange reserves"}},
    {"trade-tariff", {"import tariff", "export control", "customs duty", "trade war", "countervailing"}},
    {"sovereign-restructuring", {"sovereign default", "imf bailout", "debt restructuring", "credit rating downgrade", NULL}},
};

static void extract_cve_identifiers(const char *lower_text, size_t len, ExtractedEntities *out) {
  if(!lower_text || len < 9u) return;
  size_t i = 0;
  while(i + 9u <= len && out->cve_count < MAX_EXTRACTED_CVES) {
    if(memcmp(lower_text + i, "cve-", 4u) == 0 &&
       isdigit((unsigned char)lower_text[i + 4u]) &&
       isdigit((unsigned char)lower_text[i + 5u]) &&
       isdigit((unsigned char)lower_text[i + 6u]) &&
       isdigit((unsigned char)lower_text[i + 7u]) &&
       lower_text[i + 8u] == '-') {
      size_t j = i + 9u;
      while(j < len && isdigit((unsigned char)lower_text[j]) && (j - i) < 18u) j++;
      if(j > i + 9u) {
        size_t clen = j - i;
        for(size_t k = 0; k < clen; ++k) {
          out->cves[out->cve_count][k] = (char)toupper((unsigned char)lower_text[i + k]);
        }
        out->cves[out->cve_count][clen] = '\0';
        out->cve_count++;
        i = j;
        continue;
      }
    }
    i++;
  }
}

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

  extract_cve_identifiers(lower, len, out);

  /* Compute OPSEC / Tradecraft / High-Clearance score & classification */
  double opsec = 0.0;
  ClearanceLevel level = CLEARANCE_PUBLIC;

  if(text_contains_word(lower, len, "osint") || text_contains_word(lower, len, "satellite imagery") ||
     text_contains_word(lower, len, "geolocation") || text_contains_word(lower, len, "telemetry") ||
     text_contains_word(lower, len, "sinkhole") || text_contains_word(lower, len, "bgp")) {
    opsec += 2.2;
    if(level < CLEARANCE_OSINT) level = CLEARANCE_OSINT;
  }

  if(text_contains_word(lower, len, "opsec") || text_contains_word(lower, len, "anti-bot") ||
     text_contains_word(lower, len, "turnstile") || text_contains_word(lower, len, "fingerprint") ||
     text_contains_word(lower, len, "ja3") || text_contains_word(lower, len, "ja4") ||
     text_contains_word(lower, len, "webauthn") || text_contains_word(lower, len, "passkey") ||
     text_contains_word(lower, len, "webassembly") || text_contains_word(lower, len, "sandbox") ||
     text_contains_word(lower, len, "onion") || text_contains_word(lower, len, "qubes") ||
     text_contains_word(lower, len, "grapheneos") || text_contains_word(lower, len, "wireguard")) {
    opsec += 3.5;
    if(level < CLEARANCE_TRADECRAFT) level = CLEARANCE_TRADECRAFT;
  }

  if(out->cve_count > 0 || text_contains_word(lower, len, "zero-day") ||
     text_contains_word(lower, len, "0-day") || text_contains_word(lower, len, "exploit") ||
     text_contains_word(lower, len, "shellcode") || text_contains_word(lower, len, "rootkit") ||
     text_contains_word(lower, len, "bootkit") || text_contains_word(lower, len, "spyware") ||
     text_contains_word(lower, len, "pegasus") || text_contains_word(lower, len, "heap") ||
     text_contains_word(lower, len, "use-after-free") || text_contains_word(lower, len, "post-quantum")) {
    opsec += 4.8 + (double)out->cve_count * 1.5;
    if(level < CLEARANCE_ZERO_DAY) level = CLEARANCE_ZERO_DAY;
  }

  if(text_contains_word(lower, len, "sigint") || text_contains_word(lower, len, "geoint") ||
     text_contains_word(lower, len, "c4isr") || text_contains_word(lower, len, "reconnaissance") ||
     text_contains_word(lower, len, "hypersonic") || text_contains_word(lower, len, "deterrence") ||
     text_contains_word(lower, len, "classified") || text_contains_word(lower, len, "darpa") ||
     text_contains_word(lower, len, "iarpa") || text_contains_word(lower, len, "nsa")) {
    opsec += 5.2;
    if(level < CLEARANCE_STRATEGIC) level = CLEARANCE_STRATEGIC;
  }

  out->clearance = level;
  out->opsec_score = opsec;

  /* Extract Market Assets / Currencies & Fiscal Events */
  for(size_t i = 0; i < sizeof(k_asset_rules) / sizeof(k_asset_rules[0]); ++i) {
    if(out->asset_count >= MAX_EXTRACTED_ASSETS) break;
    for(size_t t = 0; t < 5u && k_asset_rules[i].terms[t]; ++t) {
      if(text_contains_word(lower, len, k_asset_rules[i].terms[t])) {
        strncpy(out->assets[out->asset_count], k_asset_rules[i].symbol, 15u);
        out->assets[out->asset_count][15] = '\0';
        out->asset_count++;
        break;
      }
    }
  }

  for(size_t i = 0; i < sizeof(k_fiscal_event_rules) / sizeof(k_fiscal_event_rules[0]); ++i) {
    if(out->fiscal_event_count >= MAX_EXTRACTED_EVENTS) break;
    for(size_t t = 0; t < 5u && k_fiscal_event_rules[i].terms[t]; ++t) {
      if(text_contains_word(lower, len, k_fiscal_event_rules[i].terms[t])) {
        strncpy(out->fiscal_events[out->fiscal_event_count], k_fiscal_event_rules[i].event_name, 23u);
        out->fiscal_events[out->fiscal_event_count][23] = '\0';
        out->fiscal_event_count++;
        break;
      }
    }
  }

  double hawk = 0.0, dove = 0.0, bull = 0.0, bear = 0.0;
  if(text_contains_word(lower, len, "rate hike") || text_contains_word(lower, len, "hawkish") ||
     text_contains_word(lower, len, "quantitative tightening") || text_contains_word(lower, len, "raises interest rate") ||
     text_contains_word(lower, len, "inflation surge") || text_contains_word(lower, len, "austerity")) {
    hawk += 3.5;
  }
  if(text_contains_word(lower, len, "rate cut") || text_contains_word(lower, len, "dovish") ||
     text_contains_word(lower, len, "quantitative easing") || text_contains_word(lower, len, "fiscal stimulus") ||
     text_contains_word(lower, len, "liquidity injection") || text_contains_word(lower, len, "cuts interest rate")) {
    dove += 3.5;
  }
  if(text_contains_word(lower, len, "rally") || text_contains_word(lower, len, "bullish") ||
     text_contains_word(lower, len, "record high") || text_contains_word(lower, len, "earnings beat") ||
     text_contains_word(lower, len, "gdp expansion") || text_contains_word(lower, len, "surges")) {
    bull += 3.0;
  }
  if(text_contains_word(lower, len, "recession") || text_contains_word(lower, len, "bearish") ||
     text_contains_word(lower, len, "selloff") || text_contains_word(lower, len, "sovereign default") ||
     text_contains_word(lower, len, "debt crisis") || text_contains_word(lower, len, "plunges")) {
    bear += 3.2;
  }

  FiscalStance stance = FISCAL_NEUTRAL;
  double best_macro = 0.5;
  if(hawk > best_macro) { best_macro = hawk; stance = FISCAL_HAWKISH; }
  if(dove > best_macro) { best_macro = dove; stance = FISCAL_DOVISH; }
  if(bull > best_macro) { best_macro = bull; stance = FISCAL_BULLISH; }
  if(bear > best_macro) { best_macro = bear; stance = FISCAL_BEARISH; }

  out->fiscal_stance = stance;
  out->macro_score = (double)out->asset_count * 1.4 + (double)out->fiscal_event_count * 2.2 + best_macro;
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
