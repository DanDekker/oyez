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
  AI_TIER_COUNT = 5,
  AI_LICENSE_COUNT = 5,
  MAX_EXTRACTED_COUNTRIES = 12,
  MAX_EXTRACTED_ORGS = 12,
  MAX_EXTRACTED_CVES = 8,
  MAX_EXTRACTED_ASSETS = 10,
  MAX_EXTRACTED_EVENTS = 8,
  MAX_EXTRACTED_AI_MODELS = 12,
  MAX_EXTRACTED_AI_INFRA = 12,
  MAX_EXTRACTED_HF_REPOS = 8,
  MAX_EXTRACTED_SPORTS = 8,
  MAX_LEXICAL_TOKENS = 256
};

typedef enum {
  AI_TIER_NONE = 0,                 /* Non-AI general baseline */
  AI_TIER_APPLIED_RAG = 1,          /* Embeddings, vector databases, RAG, copilots & enterprise apps */
  AI_TIER_INFERENCE_STACK = 2,      /* vLLM, SGLang, TensorRT-LLM, KV-cache, quantization & speculative decode */
  AI_TIER_NEOCLOUD_CLUSTER = 3,     /* Neoclouds (CoreWeave/Lambda/Together), Blackwell/Hopper/MI300X/TPU/Groq clusters */
  AI_TIER_FRONTIER_PRETRAINING = 4  /* Frontier pretraining, test-time compute RL (GRPO/RLHF), MoE & scaling laws */
} AiComputeTier;

typedef enum {
  AI_LICENSE_UNSPECIFIED = 0,             /* Non-model general baseline */
  AI_LICENSE_CLOSED_PROPRIETARY = 1,      /* Closed-source proprietary frontier API (GPT-4o/o1/o3, Claude 3.5/3.7, Gemini Pro/Ultra) */
  AI_LICENSE_OPEN_WEIGHTS_RESTRICTED = 2, /* Open-weights under custom/community/non-commercial license (Llama Community, Gemma, Flux.1-dev) */
  AI_LICENSE_OPEN_WEIGHTS_PERMISSIVE = 3, /* Permissive MIT / Apache-2.0 open-weights on Hugging Face (DeepSeek-R1/V3, Qwen-2.5, Mistral-7B/Mixtral, Phi-4) */
  AI_LICENSE_OPEN_SCIENCE_OSI = 4         /* Full OSI Open-Source AI: weights + open pretraining dataset + code (OLMo-2/Dolma, StarCoder2/The-Stack, Pythia) */
} AiWeightLicense;

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
  /* Simplicial Nerve N(C) of the Poset Category C & Hodge Cochain Complex */
  uint32_t nerve_f_vector[6];                       /* (f_0..f_5) k-simplices of strict chains u_0 < ... < u_k */
  int32_t euler_characteristic;                     /* chi(N(C)) = sum_{k=0}^5 (-1)^k f_k == 1 */
  uint32_t betti_numbers[2];                        /* (beta_0, beta_1) = (1, 0) of contractible nerve N(C) */
  uint32_t mobius_total_variation;                  /* sum_{u <= v} |mu(u, v)| over Rota's incidence algebra */
  double laplacian_trace;                           /* Tr(L_0) of weighted poset graph Laplacian */
  double spectral_radius;                           /* lambda_max(L_0) via power iteration */
  double fiedler_eigenvalue;                        /* lambda_2(L_0) algebraic connectivity of Poset C */
  double dirichlet_energy;                          /* <f, L_0 f> = sum_{u < v} w(u,v) (f(u) - f(v))^2 */
  double cochain_exactness_norm;                    /* ||delta_1(delta_0(f))||_2 == 0 over N_2(C) 2-simplices */
  double hodge_1_curl_norm;                         /* ||delta_1(F)||_2^2 1-cochain simplicial curl over N_2(C) */
  double hodge_gradient_norm;                       /* ||delta_0(phi)||_2^2 exact Hodge potential flow component */
  double hodge_solenoidal_norm;                     /* ||F - delta_0(phi)||_2^2 divergence-free solenoidal residual */
  double leinster_magnitude;                        /* Mag(C) = sum_{u,v} (Z^{-1})_{u,v} over Lawvere metric */
  double monad_closure_defect;                      /* ||T(f) - f||_2 where T = iota^* Lan_iota is Left Kan monad */
} SheafCohomology;

#define PERSISTENCE_FILTRATION_STEPS 8

typedef struct {
  double threshold_eps;      /* Lawvere metric / normalized density filtration parameter eps_k */
  uint32_t active_mask;      /* Bitmask of active 0-simplices V_k at step k */
  uint32_t f_vector[3];      /* (|V_k|, |E_k|, |T_k|) active 0-, 1-, 2-simplices */
  uint32_t betti_0;          /* Connected components beta_0(N_eps(C)) */
  uint32_t betti_1;          /* 1st topological Betti number beta_1(N_eps(C)) (topological voids) */
  int32_t euler_char;        /* chi(N_eps(C)) = |V_k| - |E_k| + |T_k| */
} PersistenceStep;

typedef struct {
  PersistenceStep steps[PERSISTENCE_FILTRATION_STEPS];
  double total_persistence_l1;  /* Integrated L1 barcode persistence norm sum_k (beta_0 + beta_1) * d_eps */
  double max_void_lifetime;     /* Longest-lived 1-dimensional topological hole (beta_1 > 0) */
  uint32_t persistent_core_mask;/* Nodes surviving across the deepest filtration thresholds */
} PersistenceBarcode;

typedef struct {
  double unit_eta[ONTOLOGY_NODE_COUNT];       /* Adjunction unit eta_c: f(c) -> (iota^* Lan_iota f)(c) */
  double counit_eps[ONTOLOGY_NODE_COUNT];     /* Adjunction counit eps_c: (iota^* Ran_iota F)(c) -> F(c) */
  double kleisli_flow[ONTOLOGY_NODE_COUNT];   /* Kleisli monadic propagation T(f)(c) */
  double comonad_core[ONTOLOGY_NODE_COUNT];   /* Comonadic interior G(F)(c) */
  double zig_zag_triangle_defect;             /* ||(eps_Lan o Lan(eta))(f) - Lan(f)||_2 == 0 */
  double kleisli_entropy;                     /* Categorical Shannon entropy over Kleisli normalized kernel */
  uint32_t eilenberg_moore_mask;              /* Bitmask of Eilenberg-Moore coalgebra/algebra fixed points */
} AdjunctionMonadState;

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
  char ai_models[MAX_EXTRACTED_AI_MODELS][24];
  size_t ai_model_count;
  char ai_infra[MAX_EXTRACTED_AI_INFRA][24];
  size_t ai_infra_count;
  char hf_repos[MAX_EXTRACTED_HF_REPOS][64];
  size_t hf_repo_count;
  char sports[MAX_EXTRACTED_SPORTS][24];
  size_t sport_count;
  uint32_t direct_ontology_mask;
  uint32_t closure_ontology_mask;
  NewsType news_type;
  ClearanceLevel clearance;
  double opsec_score;
  FiscalStance fiscal_stance;
  double macro_score;
  AiComputeTier ai_tier;
  double ai_compute_score;
  AiWeightLicense ai_license;
  double openness_score;
  double sports_score;
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

const char *ai_compute_tier_slug(AiComputeTier tier);
int ai_compute_tier_from_slug(const char *slug, AiComputeTier *out);

const char *ai_weight_license_slug(AiWeightLicense lic);
int ai_weight_license_from_slug(const char *slug, AiWeightLicense *out);

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
int ontology_mobius_value(OntologyNodeId u, OntologyNodeId v);
double ontology_wasserstein_distance(uint32_t mask_a, uint32_t mask_b);
double ontology_lawvere_distance(uint32_t mask_from, uint32_t mask_to);
double ontology_profunctor_coend(const double *presheaf_f, const double *copresheaf_g);
double ontology_leinster_magnitude(void);
double ontology_node_magnitude_weight(OntologyNodeId u);
double ontology_yoneda_lemma_eval(OntologyNodeId c, const double *presheaf_f);
void ontology_yoneda_kan_transform(const double *presheaf_f, double *out_yoneda_envelope);
uint32_t ontology_heyting_implication(uint32_t upper_a, uint32_t upper_b);
uint32_t ontology_heyting_negation(uint32_t upper_a);
uint32_t ontology_lawvere_tierney_sheafify(uint32_t sub_mask_a);
int ontology_is_grothendieck_dense(uint32_t sub_mask_a);
double ontology_information_specificity(uint32_t mask);
void ontology_sheaf_cohomology(const double *raw_fiber_counts,
                               const double *closure_fiber_counts,
                               SheafCohomology *out);
void ontology_persistent_homology(const double *raw_fiber_counts,
                                  PersistenceBarcode *out_barcode);
void ontology_adjunction_monad(const double *raw_fiber_counts,
                               AdjunctionMonadState *out_state);

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
