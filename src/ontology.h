#ifndef OYEZ_ONTOLOGY_H
#define OYEZ_ONTOLOGY_H

#include <stddef.h>
#include <stdint.h>

enum {
  WORLD_REGION_COUNT = 11,
  NEWS_TYPE_COUNT = 10,
  ONTOLOGY_NODE_COUNT = 32,
  CLEARANCE_LEVEL_COUNT = 5,
  FISCAL_STANCE_COUNT = 5,
  MAX_EXTRACTED_COUNTRIES = 12,
  MAX_EXTRACTED_ORGS = 12,
  MAX_EXTRACTED_CVES = 8,
  MAX_EXTRACTED_ASSETS = 10,
  MAX_EXTRACTED_EVENTS = 8,
  MAX_LEXICAL_TOKENS = 256
};

typedef enum {
  CLEARANCE_PUBLIC = 0,       /* Open general reporting */
  CLEARANCE_OSINT = 1,        /* Verified open-source intelligence / telemetry */
  CLEARANCE_TRADECRAFT = 2,   /* Browser internals, anti-bot, OPSEC & defensive advisories */
  CLEARANCE_ZERO_DAY = 3,     /* Exploit chains, 0-day vulnerability & cryptographic breaks */
  CLEARANCE_STRATEGIC = 4     /* High-clearance national security, C4ISR, SIGINT & deterrence */
} ClearanceLevel;

typedef enum {
  FISCAL_NEUTRAL = 0,         /* Non-market or balanced macro baseline */
  FISCAL_HAWKISH = 1,         /* Rate hikes, QT, fiscal austerity, inflation tightening */
  FISCAL_DOVISH = 2,          /* Rate cuts, QE, fiscal stimulus, liquidity injection */
  FISCAL_BULLISH = 3,         /* Risk-on rally, earnings beat, GDP expansion, IPO surge */
  FISCAL_BEARISH = 4          /* Risk-off selloff, recession, sovereign default, debt distress */
} FiscalStance;

typedef enum {
  REGION_NORTH_AMERICA = 0,
  REGION_LATIN_AMERICA = 1,
  REGION_EUROPE = 2,
  REGION_MIDDLE_EAST = 3,
  REGION_AFRICA = 4,
  REGION_SOUTH_ASIA = 5,
  REGION_EAST_ASIA = 6,
  REGION_SOUTHEAST_ASIA = 7,
  REGION_CENTRAL_ASIA = 8,
  REGION_OCEANIA = 9,
  REGION_GLOBAL = 10
} WorldRegion;

typedef enum {
  NEWS_TYPE_BREAKING_DISPATCH = 0,
  NEWS_TYPE_INVESTIGATIVE_REPORT = 1,
  NEWS_TYPE_POLICY_AND_LEGISLATIVE = 2,
  NEWS_TYPE_SCIENTIFIC_RESEARCH = 3,
  NEWS_TYPE_MARKET_AND_ECONOMIC = 4,
  NEWS_TYPE_OPINION_AND_EDITORIAL = 5,
  NEWS_TYPE_EXPLAINER_AND_ANALYSIS = 6,
  NEWS_TYPE_PRESS_AND_OFFICIAL = 7,
  NEWS_TYPE_CULTURAL_AND_FEATURE = 8,
  NEWS_TYPE_GENERAL_REPORT = 9
} NewsType;

typedef enum {
  ONTO_ROOT = 0,
  ONTO_PUBLIC_AFFAIRS = 1,
  ONTO_KNOWLEDGE_ECONOMY = 2,
  ONTO_PLANETARY_SYSTEMS = 3,
  ONTO_SOCIETY = 4,
  ONTO_POLITICS = 5,
  ONTO_GEOPOLITICS = 6,
  ONTO_LAW = 7,
  ONTO_SECURITY = 8,
  ONTO_DEFENSE = 9,
  ONTO_CYBER = 10,
  ONTO_ECONOMY = 11,
  ONTO_FINANCE = 12,
  ONTO_TECHNOLOGY = 13,
  ONTO_AI = 14,
  ONTO_SCIENCE = 15,
  ONTO_SPACE = 16,
  ONTO_HEALTH = 17,
  ONTO_CLIMATE = 18,
  ONTO_ENERGY = 19,
  ONTO_HUMAN_RIGHTS = 20,
  ONTO_LABOR = 21,
  ONTO_CULTURE = 22,
  ONTO_SPORTS = 23,
  ONTO_QUANTUM = 24,
  ONTO_SEMICONDUCTORS = 25,
  ONTO_BIOTECH = 26,
  ONTO_ROBOTICS = 27,
  ONTO_MATHEMATICS = 28,
  ONTO_MATERIALS = 29,
  ONTO_CRYPTOGRAPHY = 30,
  ONTO_NUCLEAR_FUSION = 31
} OntologyNodeId;

typedef struct {
  OntologyNodeId id;
  const char *slug;
  const char *label;
  uint32_t parents_mask;
  uint32_t upward_mask;   /* Principal filter ^u: all v with u <= v */
  uint32_t downward_mask; /* Principal ideal  vu: all v with v <= u */
} OntologyNode;

typedef struct {
  uint32_t grothendieck_sieve[ONTOLOGY_NODE_COUNT]; /* Covering sieve S(c) for each c */
  double sheaf_section[ONTOLOGY_NODE_COUNT];        /* Glued sheaf section over C */
  double cohomology_defect[ONTOLOGY_NODE_COUNT];    /* Local-to-global H^1 obstruction */
  double total_cohomology_norm;
} SheafCohomology;

typedef struct {
  char countries[MAX_EXTRACTED_COUNTRIES][4];
  size_t country_count;
  char organizations[MAX_EXTRACTED_ORGS][24];
  size_t org_count;
  char cves[MAX_EXTRACTED_CVES][20];
  size_t cve_count;
  char assets[MAX_EXTRACTED_ASSETS][16];
  size_t asset_count;
  char fiscal_events[MAX_EXTRACTED_EVENTS][24];
  size_t fiscal_event_count;
  uint32_t direct_ontology_mask;
  uint32_t closure_ontology_mask;
  NewsType news_type;
  ClearanceLevel clearance;
  double opsec_score;
  FiscalStance fiscal_stance;
  double macro_score;
} ExtractedEntities;

typedef struct {
  char term[32];
  uint64_t hash;
  float weight;
  uint32_t tf;
} LexicalTerm;

const char *world_region_slug(WorldRegion region);
int world_region_from_slug(const char *slug, WorldRegion *out);

const char *news_type_slug(NewsType type);
int news_type_from_slug(const char *slug, NewsType *out);
NewsType news_type_classify(const char *title,
                            const char *summary,
                            const char *content,
                            const char *feed_category);

const char *clearance_level_slug(ClearanceLevel level);
int clearance_level_from_slug(const char *slug, ClearanceLevel *out);

const char *fiscal_stance_slug(FiscalStance stance);
int fiscal_stance_from_slug(const char *slug, FiscalStance *out);

void ontology_init(void);
const OntologyNode *ontology_node(OntologyNodeId id);
int ontology_from_slug(const char *slug, OntologyNodeId *out);
int ontology_has_morphism(OntologyNodeId sub, OntologyNodeId sup);
uint32_t ontology_upward_closure(uint32_t mask);
uint32_t ontology_downward_closure(uint32_t mask);
OntologyNodeId ontology_join(OntologyNodeId a, OntologyNodeId b);
uint32_t ontology_meet_mask(OntologyNodeId a, OntologyNodeId b);
void ontology_left_kan_extend(const double *raw_scores, double *out_kan_scores);
void ontology_right_kan_extend(const double *raw_scores, double *out_ran_scores);
void ontology_mobius_invert(const double *upper_section, double *out_fundamental);
double ontology_wasserstein_distance(uint32_t mask_a, uint32_t mask_b);
uint32_t ontology_heyting_implication(uint32_t upper_a, uint32_t upper_b);
uint32_t ontology_heyting_negation(uint32_t upper_a);
void ontology_sheaf_cohomology(const double *raw_fiber_counts,
                               const double *closure_fiber_counts,
                               SheafCohomology *out);

void entities_extract(const char *title,
                      const char *summary,
                      const char *content,
                      const char *feed_category,
                      const char *const *tags,
                      size_t tag_count,
                      ExtractedEntities *out);

size_t lexical_analyze(const char *text,
                       float field_weight,
                       LexicalTerm *terms,
                       size_t current_count,
                       size_t max_terms,
                       uint32_t *out_word_tokens);
uint64_t simhash64_compute(const LexicalTerm *terms, size_t count);
uint32_t simhash64_distance(uint64_t a, uint64_t b);

#endif
