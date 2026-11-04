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
  WEATHER_SEVERITY_COUNT = 5,
  MAX_EXTRACTED_COUNTRIES = 12,
  MAX_EXTRACTED_ORGS = 12,
  MAX_EXTRACTED_CVES = 8,
  MAX_EXTRACTED_ASSETS = 10,
  MAX_EXTRACTED_EVENTS = 8,
  MAX_EXTRACTED_AI_MODELS = 12,
  MAX_EXTRACTED_AI_INFRA = 12,
  MAX_EXTRACTED_HF_REPOS = 8,
  MAX_EXTRACTED_SPORTS = 8,
  MAX_EXTRACTED_WEATHER = 8,
  MAX_LEXICAL_TOKENS = 512
};

typedef char oyez_c99_ontology_node_count_assert[(ONTOLOGY_NODE_COUNT == 32) ? 1 : -1];

typedef enum {
  WEATHER_CALM = 0,          /* Routine / non-weather baseline */
  WEATHER_ADVISORY = 1,      /* Meteorological outlook, ENSO phase, seasonal anomaly, air quality */
  WEATHER_WATCH = 2,         /* Severe thunderstorm, gale, flood watch, winter storm watch, G1-G2 storm */
  WEATHER_WARNING = 3,       /* Hurricane/typhoon, tornado warning, flash flood, atmospheric river, wildfire red flag */
  WEATHER_CATASTROPHIC = 4   /* Category 4/5 cyclone, EF4/EF5 tornado, M7+ earthquake/tsunami, X-class solar storm */
} WeatherSeverity;

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
  ONTO_NONE = -1,
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

/* Exhaustive structural audit of the finite ontology category.  Pair counts
 * are over unordered pairs, enumerated by numeric IDs a <= b and including
 * the diagonal. */
typedef struct {
  uint32_t object_count;
  uint32_t morphism_count;
  uint32_t pair_count;
  uint32_t unique_join_pairs;
  uint32_t ambiguous_join_pairs;
  uint32_t missing_join_pairs;
  uint32_t unique_meet_pairs;
  uint32_t ambiguous_meet_pairs;
  uint32_t missing_meet_pairs;
  int is_reflexive;
  int is_antisymmetric;
  int is_transitive;
  int is_lattice;
} OntologyOrderAudit;

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
  double hodge_coexact_norm;                        /* ||delta_1^T(psi)||_2^2 coexact 2-simplex curl component */
  double hodge_harmonic_norm;                       /* ||F - delta_0(phi) - delta_1^T(psi)||_2^2 ker(L_1) harmonic */
  double hodge_solenoidal_norm;                     /* ||F - delta_0(phi)||_2^2 divergence-free solenoidal residual */
  double laplacian_1_trace;                         /* Tr(L_1) = Tr(delta_0 delta_0^T + delta_1^T delta_1) */
  double leinster_magnitude;                        /* Mag(C) = sum_{u,v} (Z^{-1})_{u,v} over Lawvere metric */
  double monad_closure_defect;                      /* ||T(f) - f||_2 where T = iota^* Lan_iota is Left Kan monad */
  double isbell_codensity_defect;                   /* ||((F^*)^#)^* - F^*||_2 == 0 Isbell envelope identity */
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
  double codensity_monad[ONTOLOGY_NODE_COUNT];/* Enriched Isbell-Kan codensity hull (F^*)^#(c) */
  double bousfield_localized[ONTOLOGY_NODE_COUNT]; /* Bousfield j-sheaf localized presheaf L_j(F)(c) */
  double zig_zag_triangle_defect;             /* ||(eps_Lan o Lan(eta))(f) - Lan(f)||_2 == 0 */
  double beck_coequalizer_defect;             /* Beck's Monadicity split fork defect ||mu o T(eta) - id||_2 == 0 */
  double isbell_adjunction_defect;            /* ||((F^*)^#)^* - F^*||_2 == 0 Isbell triple conjugation */
  double kleisli_entropy;                     /* Categorical Shannon entropy over Kleisli normalized kernel */
  uint32_t eilenberg_moore_mask;              /* Bitmask of Eilenberg-Moore coalgebra/algebra fixed points */
} AdjunctionMonadState;

#define SPECTRAL_FILTRATION_RANKS 6

typedef struct {
  uint32_t rank_strata_masks[SPECTRAL_FILTRATION_RANKS]; /* Objects u in C of categorical height p in {0..5} */
  uint32_t e0_simplex_counts[SPECTRAL_FILTRATION_RANKS][4]; /* E_0^{p, k-p} filtered k-simplices at stratum p */
  double e1_stratum_energy[SPECTRAL_FILTRATION_RANKS];   /* E_1 vertical fiber cohomology norm at stratum p */
  double e2_derived_energy[SPECTRAL_FILTRATION_RANKS];   /* Heuristic neighboring-stratum variation */
  double e_inf_abutment_energy[SPECTRAL_FILTRATION_RANKS]; /* Heuristic smoothed rank-page summary */
  int32_t euler_e0;                                      /* Legacy page-shaped copy of chi(N(C)) */
  int32_t euler_e1;                                      /* Legacy page-shaped copy of chi(N(C)) */
  int32_t euler_e2;                                      /* Legacy page-shaped copy of chi(N(C)) */
  int32_t euler_e_inf;                                   /* Legacy page-shaped copy of chi(N(C)) */
  double differential_nilpotency_defect;                 /* Reserved legacy field; differentials are not computed */
  int derived_pages_computed;                            /* Always false for this diagnostic */
} SpectralSequenceState;

/* Honest name for the diagnostic above; the original typedef remains for
 * source compatibility. */
typedef SpectralSequenceState RankPageDiagnostic;

typedef struct {
  double entropic_wasserstein;   /* Kantorovich entropic optimal transport cost W_eps(mu, nu) */
  double sinkhorn_divergence;    /* Debiased Sinkhorn divergence S_eps(mu, nu) >= 0 (== 0 iff mu == nu) */
  double marginal_defect_l1;     /* ||Pi*1 - mu||_1 + ||Pi^T*1 - nu||_1 Sinkhorn scaling convergence error */
  double coupling_entropy;       /* Shannon entropy H(Pi) of optimal transport plan Pi */
  double fisher_rao_geodesic;    /* Riemannian Fisher-Rao geodesic distance d_FR(mu, nu) on Delta^31(C) */
  double cheeger_constant;       /* Cheeger isoperimetric bottleneck constant h(C) of Hasse graph */
  int cheeger_buser_holds;       /* Verifies Cheeger-Buser spectral bounds lambda_2/(2*d_max) <= h(C) <= sqrt(2*d_max*lambda_2) */
} SinkhornTransportState;

typedef struct {
  double gauss_bonnet_vertex[ONTOLOGY_NODE_COUNT]; /* McKean-Singer / Knill-Gauss-Bonnet K_GB(u) = sum_{k=0}^5 (-1)^k deg_k(u)/(k+1) */
  double gauss_bonnet_total;                       /* sum_{u in C} K_GB(u) == chi(N(C)) == 1.0 */
  double gauss_bonnet_defect;                      /* |sum_u K_GB(u) - chi(N(C))| == 0 */
  double forman_scalar[ONTOLOGY_NODE_COUNT];       /* Forman-Ricci vertex scalar curvature R_F(u) = sum_{e ni u} kappa_F(e) */
  double forman_mean_curvature;                    /* Mean Forman-Ricci edge curvature across N_1(C) covering morphisms */
  double ollivier_mean_curvature;                  /* Mean Ollivier-Ricci coarse curvature kappa_O(u,v) = 1 - W_1(m_u, m_v)/d(u,v) */
  double ollivier_min_bridge;                      /* Minimum (most negatively curved hyperbolic bridge) Ollivier curvature */
  double ollivier_max_cluster;                     /* Maximum (most positively curved triangle cluster) Ollivier curvature */
  double ricci_flow_dirichlet_after;               /* Dirichlet energy after 1 normalized Hamilton-Ollivier Ricci flow step */
  double ricci_flow_volume_defect;                 /* |sum_e w^{(1)}(e) - sum_e w^{(0)}(e)| == 0 metric volume conservation */
} RicciCurvatureState;

typedef struct {
  double ihara_u;                                  /* Complex/real spectral parameter u in (0, 1/rho(B)) */
  double ihara_zeta_val;                           /* Ihara-Bass Zeta function zeta_C(u) = ((1-u^2)^{|E|-|V|} det(I - uA + u^2 Q))^{-1} */
  double ihara_bass_det;                           /* Exact 32x32 deformed Laplacian determinant det(I - uA + u^2(D - I)) */
  double ihara_det_at_zero_defect;                 /* |det(I)|_{u=0} - 1| == 0 */
  double ihara_det_at_one_defect;                  /* |det(I - A + (D - I))|_{u=1}| = |det(L_0)| == 0 */
  double hashimoto_spectral_radius;                /* Non-backtracking Hashimoto matrix spectral radius rho(B) */
  double tropical_eigenvalue;                      /* Karp's max-plus tropical eigenvalue lambda_trop(M) = max_u min_k (M^n - M^k)/(n-k) */
  double tropical_eigenvector[ONTOLOGY_NODE_COUNT];/* Tropical max-plus eigenvector v_trop */
  double tropical_polytope_proj[ONTOLOGY_NODE_COUNT]; /* Tropical convex hull projection pi_trop(F) onto macro-pillar generators */
  double tropical_idempotence_defect;              /* ||pi_trop(pi_trop(F)) - pi_trop(F)||_2 == 0 */
} ZetaTropicalState;

typedef struct {
  double rb_f[ONTOLOGY_NODE_COUNT];                /* Rota-Baxter summation R(f)(u_i) = sum_{j <= i} f(u_j) */
  double rb_g[ONTOLOGY_NODE_COUNT];                /* Rota-Baxter summation R(g)(u_i) = sum_{j <= i} g(u_j) */
  double rb_double_product[ONTOLOGY_NODE_COUNT];   /* Associative Rota-Baxter product (f *_R g) = R(f)g + f R(g) - fg */
  double rota_baxter_identity_defect;              /* ||R(f)R(g) - R(R(f)g + f R(g) - fg)||_2 == 0 */
  double spitzer_birkhoff_defect;                  /* ||(1 + R(f)) - exp(R(ln_RB(f)))||_2 Algebraic Birkhoff / Atkinson defect == 0 */
} RotaBaxterState;

typedef struct {
  double lorenz_state[3];            /* Terminal RK4 state (x, y, z) on Lorenz-63 convective attractor */
  double lyapunov_max;               /* Maximal Lyapunov exponent lambda_1 > 0 via Benettin tangent QR renormalization */
  double lyapunov_sum_div;           /* Phase-space volume contraction div(F) = -(sigma + 1 + beta) = -13.6667 */
  double kaplan_yorke_dim;           /* Kaplan-Yorke fractal attractor dimension D_KY = 2 + (lambda_1 + lambda_2)/|lambda_3| */
  double predictability_horizon;     /* Lyapunov e-folding predictability horizon T_pred = ln(1/eps_0) / lambda_1 */
  double saturation_vapor_hpa;       /* Clausius-Clapeyron saturation vapor pressure e_s(T) (hPa) */
  double wet_bulb_celsius;           /* Stull psychrometric wet-bulb temperature T_w (C) */
  double rossby_phase_speed;         /* Barotropic Rossby-Haurwitz planetary wave phase speed c_x (m/s) */
  double rossby_stationary_k;        /* Stationary Rossby wavenumber K_s = sqrt(beta / U) for jet-stream blocking */
  int omega_blocking_regime;         /* 1 if planetary wavenumber resonates near K_s causing persistent blocking */
} WeatherPhysicsState;

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
  char weather[MAX_EXTRACTED_WEATHER][24];
  size_t weather_count;
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
  WeatherSeverity weather_severity;
  double weather_score;
} ExtractedEntities;

typedef struct {
  char term[32];
  uint64_t hash;
  float weight;
  uint32_t tf;
  uint32_t pos_mask;
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

const char *weather_severity_slug(WeatherSeverity sev);
int weather_severity_from_slug(const char *slug, WeatherSeverity *out);
void ontology_atmospheric_dynamics(double temp_celsius,
                                   double rel_humidity_pct,
                                   double zonal_wind_ms,
                                   double latitude_deg,
                                   double anomaly_forcing,
                                   WeatherPhysicsState *out_wx);

void ontology_init(void);
/* ONTO_NONE maps to a sentinel descriptor with slug "none"; other invalid
 * IDs retain the historical ONTO_ROOT fallback. */
const OntologyNode *ontology_node(OntologyNodeId id);
int ontology_from_slug(const char *slug, OntologyNodeId *out);
int ontology_has_morphism(OntologyNodeId sub, OntologyNodeId sup);
uint32_t ontology_upward_closure(uint32_t mask);
uint32_t ontology_downward_closure(uint32_t mask);
uint32_t ontology_minimal_upper_bounds(OntologyNodeId a, OntologyNodeId b);
uint32_t ontology_maximal_lower_bounds(OntologyNodeId a, OntologyNodeId b);
/* Return 0 and set the output iff the corresponding bound is unique;
 * otherwise return -1 without selecting an arbitrary candidate. */
int ontology_unique_join(OntologyNodeId a, OntologyNodeId b, OntologyNodeId *out_join);
int ontology_unique_meet(OntologyNodeId a, OntologyNodeId b, OntologyNodeId *out_meet);
/* Deprecated scalar helper: returns the unique join or ONTO_NONE.  This is a
 * deliberate behavior change from the former arbitrary tie-breaker; prefer
 * ontology_unique_join for new code. */
OntologyNodeId ontology_join(OntologyNodeId a, OntologyNodeId b);
/* Returns every maximal lower bound, not an arbitrarily selected meet. */
uint32_t ontology_meet_mask(OntologyNodeId a, OntologyNodeId b);
int ontology_is_ideal(uint32_t mask);
uint32_t ontology_ideal_join(uint32_t ideal_a, uint32_t ideal_b);
uint32_t ontology_ideal_meet(uint32_t ideal_a, uint32_t ideal_b);
void ontology_order_audit(OntologyOrderAudit *out_audit);
void ontology_left_kan_extend(const double *raw_scores, double *out_kan_scores);
void ontology_right_kan_extend(const double *raw_scores, double *out_ran_scores);
void ontology_mobius_invert(const double *upper_section, double *out_fundamental);
int ontology_mobius_value(OntologyNodeId u, OntologyNodeId v);
double ontology_wasserstein_distance(uint32_t mask_a, uint32_t mask_b);
double ontology_lawvere_distance(uint32_t mask_from, uint32_t mask_to);
double ontology_profunctor_coend(const double *presheaf_f, const double *copresheaf_g);
double ontology_profunctor_end(const double *presheaf_f, const double *presheaf_g);
double ontology_weighted_colimit(const double *weight_w, const double *diagram_f);
double ontology_weighted_limit(const double *weight_w, const double *diagram_g);
void ontology_isbell_conjugate_covariant(const double *presheaf_f, double *out_copresheaf_f_star);
void ontology_isbell_conjugate_contravariant(const double *copresheaf_g, double *out_presheaf_g_sharp);
double ontology_isbell_envelope_defect(const double *presheaf_f);
double ontology_leinster_magnitude(void);
double ontology_node_magnitude_weight(OntologyNodeId u);
double ontology_yoneda_lemma_eval(OntologyNodeId c, const double *presheaf_f);
void ontology_yoneda_kan_transform(const double *presheaf_f, double *out_yoneda_envelope);
uint32_t ontology_heyting_implication(uint32_t upper_a, uint32_t upper_b);
uint32_t ontology_heyting_negation(uint32_t upper_a);
uint32_t ontology_coheyting_subtraction(uint32_t sub_mask_a, uint32_t sub_mask_b);
uint32_t ontology_coheyting_negation(uint32_t sub_mask_a);
uint32_t ontology_coheyting_boundary(uint32_t sub_mask_a);
uint32_t ontology_universal_quantifier(uint32_t sub_mask_s);
/* Exact characteristic sieve chi_A(c) = down(c) intersect down(A). */
uint32_t ontology_subobject_classifier_sieve(uint32_t sub_mask_s, OntologyNodeId stage);
/* A scalar visualization of the exact sieve, weighted by the similarity kernel. */
void ontology_subobject_classifier_score(uint32_t sub_mask_s, double *out_score);
/* Compatibility alias for ontology_subobject_classifier_score. */
void ontology_subobject_classifier_omega(uint32_t sub_mask_s, double *out_omega_truth);
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
/* Associative, commutative cocone semipromonoidal convolution.  It has no
 * global unit on unrestricted signal arrays and no representable unit object;
 * on isotone max-plus sections, the constant-zero section is a unit. */
void ontology_cocone_convolution(const double *presheaf_f,
                                 const double *presheaf_g,
                                 double *out_fg);
void ontology_cocone_internal_hom(const double *presheaf_g,
                                  const double *presheaf_h,
                                  double *out_hom_gh);
double ontology_cocone_adjunction_defect(const double *presheaf_f,
                                         const double *presheaf_g,
                                         const double *presheaf_h);
/* Deprecated source aliases.  Their semantics now follow cocone convolution,
 * not the former arbitrary-join calculation; migrate to the cocone names. */
void ontology_day_convolution(const double *presheaf_f,
                              const double *presheaf_g,
                              double *out_day_fg);
void ontology_day_internal_hom(const double *presheaf_g,
                               const double *presheaf_h,
                               double *out_hom_gh);
double ontology_day_adjunction_defect(const double *presheaf_f,
                                      const double *presheaf_g,
                                      const double *presheaf_h);
void ontology_sinkhorn_optimal_transport(const double *density_p,
                                         const double *density_q,
                                         double epsilon,
                                         SinkhornTransportState *out_ot);
double ontology_fisher_rao_distance(const double *density_p,
                                    const double *density_q);
void ontology_heat_kernel_propagate(const double *initial_f,
                                    double time_t,
                                    double *out_diffused);
double ontology_cheeger_constant(int *out_buser_inequality_holds);
void ontology_rank_page_diagnostic(const double *raw_fiber_counts,
                                   RankPageDiagnostic *out_diagnostic);
void ontology_modal_closure_diagnostic(uint32_t sub_a,
                                       uint32_t sub_b,
                                       uint32_t *out_intermediate,
                                       uint32_t *out_target,
                                       int *out_closure_matches);
/* Deprecated source aliases; these diagnostics do not construct a spectral
 * sequence or a Quillen model-category factorization. */
void ontology_spectral_sequence(const double *raw_fiber_counts,
                                SpectralSequenceState *out_spectral);
void ontology_quillen_factorize(uint32_t sub_a,
                                uint32_t sub_b,
                                uint32_t *out_acyclic_cofib_m,
                                uint32_t *out_fibration_target,
                                int *out_weak_equiv_holds);
void ontology_polynomial_functor_eval(const double *fiber_x,
                                      double *out_poly_px,
                                      double *out_codesign_lens);
void ontology_ricci_curvature(const double *presheaf_f,
                              RicciCurvatureState *out_ricci);
void ontology_ihara_zeta_and_tropical(const double *presheaf_f,
                                      double u_param,
                                      ZetaTropicalState *out_zt);
void ontology_rota_baxter_algebra(const double *presheaf_f,
                                  const double *presheaf_g,
                                  RotaBaxterState *out_rb);

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
