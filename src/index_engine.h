#ifndef OYEZ_INDEX_ENGINE_H
#define OYEZ_INDEX_ENGINE_H

#include "catalog.h"
#include "core.h"
#include "ontology.h"

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint64_t *words;
  size_t word_cap;
  uint32_t max_slot;
} BitSet;

typedef struct {
  uint32_t id;
  char *title;
  char *feed_url;
  char *site_url;
  char *description;
  char *category;
  WorldRegion region;
  char country[4];
  char *subregion;
  char *language;
  char *source_tier;
  char *editorial_focus;
  uint64_t last_fetched_ms;
  char *last_error;
  uint32_t fetch_count;
  uint32_t error_count;
  uint32_t article_count;
} FeedRecord;

typedef struct {
  uint32_t id;
  uint32_t slot;
  uint32_t feed_id;
  char *guid;
  char *title;
  char *url;
  char *author;
  char *summary;
  char *content_text;
  char *content_html;
  char *published_at;
  uint64_t published_ms;
  char *image_url;
  char *feed_title;
  char *feed_category;
  WorldRegion feed_region;
  char feed_country[4];
  char *feed_language;
  char *feed_source_tier;
  NewsType news_type;
  uint32_t direct_ontology_mask;
  uint32_t closure_ontology_mask;
  char mentioned_countries[MAX_EXTRACTED_COUNTRIES][4];
  size_t mentioned_country_count;
  char mentioned_orgs[MAX_EXTRACTED_ORGS][24];
  size_t mentioned_org_count;
  char mentioned_cves[MAX_EXTRACTED_CVES][20];
  size_t mentioned_cve_count;
  char mentioned_assets[MAX_EXTRACTED_ASSETS][16];
  size_t mentioned_asset_count;
  char mentioned_fiscal_events[MAX_EXTRACTED_EVENTS][24];
  size_t mentioned_fiscal_event_count;
  char mentioned_ai_models[MAX_EXTRACTED_AI_MODELS][24];
  size_t mentioned_ai_model_count;
  char mentioned_ai_infra[MAX_EXTRACTED_AI_INFRA][24];
  size_t mentioned_ai_infra_count;
  char mentioned_hf_repos[MAX_EXTRACTED_HF_REPOS][64];
  size_t mentioned_hf_repo_count;
  char mentioned_sports[MAX_EXTRACTED_SPORTS][24];
  size_t mentioned_sport_count;
  char mentioned_weather[MAX_EXTRACTED_WEATHER][24];
  size_t mentioned_weather_count;
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
  uint32_t word_count;
  uint32_t reading_time_minutes;
  uint64_t simhash64;
  uint64_t m1_sketch64;
  uint32_t m1_landmark_hits;
  uint32_t doc_len;
} ArticleRecord;

typedef struct {
  char term[32];
  uint64_t hash;
  uint32_t *slots;
  float *weights;
  uint32_t *pos_masks;
  uint32_t count;
  uint32_t cap;
  int occupied;
} InvertedBucket;

typedef struct {
  uint16_t band_key;
  uint32_t *slots;
  uint32_t count;
  uint32_t cap;
} SimHashBucket;

typedef struct {
  uint32_t cluster_id;
  const ArticleRecord *lead_article;
  const ArticleRecord *members[16];
  size_t member_count;
  uint32_t region_mask;
  uint32_t ontology_mask;
  double cluster_score;
} StoryCluster;

typedef struct {
  char key[64];
  BitSet bits;
  int occupied;
} CountryFiber;

#define MAX_AUTOMATION_RULES 64
#define MAX_AUTOMATION_ALERTS 256

typedef enum {
  AUTO_ACTION_ALERT_RING = 0,   /* Record structured alert in in-memory ring buffer */
  AUTO_ACTION_JSONL_LOG = 1,    /* Append JSONL event line to local file (e.g. data/alerts.jsonl) */
  AUTO_ACTION_WEBHOOK = 2,      /* Dispatch JSON payload to verified public webhook URL */
  AUTO_ACTION_SNAPSHOT = 3      /* Trigger atomic index snapshot checkpoint */
} AutomationActionType;

typedef struct {
  uint32_t id;
  char name[48];
  char query[64];
  char category[32];
  char region[32];
  char weather[24];
  ClearanceLevel min_clearance;
  FiscalStance fiscal_stance;
  AiComputeTier min_ai_tier;
  WeatherSeverity min_weather_severity;
  double min_score;
  AutomationActionType action;
  char target[128];
  uint32_t hit_count;
  uint64_t last_triggered_ms;
  int enabled;
} AutomationRule;

typedef struct {
  uint64_t alert_id;
  uint32_t rule_id;
  char rule_name[48];
  uint32_t article_id;
  char article_title[160];
  char article_url[160];
  char category_slug[32];
  char weather_severity_slug[32];
  char clearance_slug[32];
  double trigger_score;
  uint64_t triggered_at_ms;
  AutomationActionType action_taken;
} AutomationAlert;

typedef struct {
  uint32_t feed_id;
  const char *title;
  const char *category;
  double ucb1_score;
  double empirical_yield;
  double exploration_bonus;
  double staleness_bonus;
  double error_penalty;
} BanditFeedPriority;

typedef struct {
  /* Single-Machine Resident Memory Accounting (Bytes) */
  size_t struct_base_bytes;
  size_t arena_bytes_used;
  size_t arena_bytes_capacity;
  size_t arena_block_count;
  size_t feed_and_article_table_bytes;
  size_t inverted_index_bytes;
  size_t bitset_fiber_bytes;
  size_t lsh_and_dedup_bytes;
  size_t query_cache_bytes;
  size_t total_resident_bytes;
  size_t query_cache_valid_entries;
  uint64_t index_generation;

  /* Twelve exact or numerically bounded invariant checks. */
  uint32_t checks_passed;
  uint32_t theorems_verified_count; /* Deprecated compatibility name. */
  uint32_t checks_total;
  uint32_t theorems_total_count; /* Deprecated compatibility name. */
  int all_checks_passed;
  int all_theorems_verified; /* Deprecated compatibility name. */
  double leinster_magnitude;
  int32_t euler_characteristic;
  double fiedler_eigenvalue;
  double cheeger_constant;
  double gauss_bonnet_total;
  double ihara_zeta_val;
  double tropical_eigenvalue;
  double sinkhorn_divergence;
  double fisher_rao_geodesic;
  double cocone_adjunction_defect;
  double day_adjunction_defect; /* Deprecated compatibility name. */
  double rota_baxter_defect;
  double max_numerical_residual;
  double max_numerical_defect; /* Deprecated compatibility name. */

  /* Unified Cross-Domain Situation Room & Atmospheric Physics */
  WeatherPhysicsState weather_physics;
  size_t cyber_strategic_articles;
  size_t frontier_ai_articles;
  size_t severe_weather_articles;
  size_t macro_active_articles;
  size_t sports_articles;
  char emerging_category_slug[32];
  double emerging_acceleration_z;

  /* Autonomous Watchpoints & UCB1 Bandit Scheduler Summary */
  size_t active_watchpoint_rules;
  size_t total_watchpoint_alerts;
  uint64_t total_bandit_pulls;
  uint32_t auto_poll_interval_sec;
  uint32_t auto_cycles_completed;
  BanditFeedPriority top_bandit_feeds[4];
  size_t top_bandit_count;
} EngineApplianceState;

typedef struct {
  uint64_t key_hash;
  uint32_t article_id;
  int occupied;
} DedupEntry;

typedef struct {
  uint64_t key_hash;
  uint32_t *slots;
  float *scores;
  size_t count;
  size_t total;
  int valid;
} QueryCacheEntry;

typedef struct {
  FeedRecord *feeds;
  size_t feed_count;
  size_t feed_cap;
  uint32_t next_feed_id;

  ArticleRecord *articles;
  size_t article_count;
  size_t article_cap;
  uint32_t next_article_id;
  uint64_t total_doc_len;

  BitSet all_articles;
  BitSet by_region[WORLD_REGION_COUNT];
  BitSet by_news_type[NEWS_TYPE_COUNT];
  BitSet by_clearance[CLEARANCE_LEVEL_COUNT];
  BitSet by_fiscal_stance[FISCAL_STANCE_COUNT];
  BitSet by_ai_tier[AI_TIER_COUNT];
  BitSet by_ai_license[AI_LICENSE_COUNT];
  BitSet by_weather_severity[WEATHER_SEVERITY_COUNT];
  BitSet by_direct_ontology[ONTOLOGY_NODE_COUNT];
  BitSet by_closure_ontology[ONTOLOGY_NODE_COUNT];

  CountryFiber by_source_country[256];
  CountryFiber by_mentioned_country[256];
  CountryFiber by_asset[128];
  CountryFiber by_fiscal_event[64];
  CountryFiber by_cve[128];
  CountryFiber by_org[128];
  CountryFiber by_ai_model[128];
  CountryFiber by_ai_infra[128];
  CountryFiber by_hf_repo[128];
  CountryFiber by_sport[128];
  CountryFiber by_weather[128];

  InvertedBucket *inv_table;
  size_t inv_cap;
  size_t inv_count;

  /* 4-band x 16-bit Multi-Probe SimHash LSH index (4 * 4096 hash buckets) */
  SimHashBucket *lsh_bands[4];

  /* Open-addressed O(1) Article Deduplication Hash Table */
  DedupEntry *dedup_table;
  size_t dedup_cap;
  size_t dedup_count;

  /* Contiguous 64 KB Block String Arena for O(1) Cache-Local Article String Allocation */
  struct StringArenaBlock *arena_head;

  /* Autonomous Watchpoint Rule Engine, Alert Ring Buffer & UCB1 Bandit Telemetry */
  AutomationRule automation_rules[MAX_AUTOMATION_RULES];
  size_t automation_rule_count;
  uint32_t next_rule_id;
  AutomationAlert automation_alerts[MAX_AUTOMATION_ALERTS];
  size_t automation_alert_head;
  size_t automation_alert_count;
  uint64_t next_alert_id;
  uint64_t total_bandit_pulls;
  uint32_t auto_poll_interval_sec;
  uint64_t last_auto_cycle_ms;
  uint64_t auto_cycles_completed;

  /* 64-entry FNV-1a Open-Addressed Query Response Cache */
  QueryCacheEntry query_cache[64];
  uint64_t index_generation;
  pthread_rwlock_t rwlock;
  pthread_mutex_t cache_lock;
} OyezEngine;

typedef struct {
  const char *query;
  const char *category;
  const char *exclude_category;
  int use_category_morphism;
  const char *news_type;
  const char *clearance;
  const char *fiscal_stance;
  const char *asset;
  const char *fiscal_event;
  const char *cve;
  const char *organization;
  const char *ai_tier;
  const char *ai_model;
  const char *ai_infra;
  const char *ai_license;
  const char *hf_repo;
  const char *sport;
  const char *weather;
  const char *weather_severity;
  const char *region;
  const char *country;
  const char *mentioned_country;
  const char *language;
  const char *source_tier;
  uint32_t feed_id;
  int diversify_mmr;
  double mmr_lambda;
  size_t limit;
  size_t offset;
} ArticleQuery;

typedef struct {
  const ArticleRecord *article;
  double score;
} ScoredArticle;

typedef struct {
  ScoredArticle *hits;
  size_t count;
  size_t total;
  double query_time_ms;
} SearchResponse;

typedef struct {
  uint32_t concept_id;
  uint32_t extent_slots[64];
  size_t extent_count;
  uint32_t intent_ontology_mask;
  uint32_t lawvere_tierney_mask;
  uint32_t intent_region_mask;
  uint32_t intent_news_type_mask;
  uint32_t subconcept_mask;
  uint32_t superconcept_mask;
  double stability_score;
  double intensional_entropy;
} FormalConcept;

typedef struct {
  const ArticleRecord *left;
  const ArticleRecord *right;
  const char *shared_witness;
  uint32_t hamming_distance;
  double coherence_score;
} PullbackPair;

typedef struct {
  uint32_t amalgam_id;
  char shared_witness[32];
  uint32_t member_ids[16];
  size_t member_count;
  uint32_t left_count;
  uint32_t right_count;
  uint32_t coproduct_ontology_mask;
  uint32_t sheafified_ontology_mask;
  uint32_t shared_closure_mask;
  uint32_t region_mask;
  double colimit_cocone_score;
} PushoutAmalgam;

typedef struct {
  WorldRegion source_region;
  WorldRegion target_region;
  uint32_t source_fiber_counts[ONTOLOGY_NODE_COUNT];
  uint32_t target_fiber_counts[ONTOLOGY_NODE_COUNT];
  double component_ratios[ONTOLOGY_NODE_COUNT];
  uint32_t source_support_mask;
  uint32_t target_support_mask;
  uint32_t heyting_truth_mask;
  uint32_t dense_sieve_mask;
  double naturality_defect;
  double coend_pairing;
  double lawvere_transport_cost;
  double reverse_transport_cost;
} PresheafMorphism;

typedef struct {
  OntologyNodeId sub_node;
  OntologyNodeId sup_node;
  const ArticleRecord *sub_article;
  const ArticleRecord *sup_article;
  char transport_witness[32];
  uint32_t hamming_distance;
  double lawvere_geodesic;
  double cartesian_fidelity;
} CartesianLift;

#define TEMPORAL_STALK_COUNT 4

typedef struct {
  double stalk_presheaf[TEMPORAL_STALK_COUNT][ONTOLOGY_NODE_COUNT];
  double velocity[ONTOLOGY_NODE_COUNT];       /* dF/dt across temporal stalks I_0 -> I_3 */
  double acceleration[ONTOLOGY_NODE_COUNT];   /* d^2F/dt^2 discrete temporal Laplacian */
  double connection_laplacian_norm;           /* ||F_{t+1} - Lan_Y(F_t)||_2 temporal sheaf defect */
  double regime_shift_energy;                 /* Cross-fiber transport kinetic energy */
  OntologyNodeId fastest_emerging_node;
  OntologyNodeId highest_acceleration_node;
} TemporalSheafState;

typedef struct {
  const ArticleRecord *stage_a;
  const ArticleRecord *stage_b;
  const ArticleRecord *stage_c;
  char witness_ab[32];
  char witness_bc[32];
  uint32_t composite_ontology_mask;
  double total_lawvere_geodesic;
  double operad_composition_score;
} OperadChain;

typedef struct {
  WorldRegion source_region;
  WorldRegion target_region;
  OntologyNodeId prop_a;
  OntologyNodeId prop_b;
  uint32_t ideal_a;
  uint32_t ideal_b;
  uint32_t heyting_implication_ab;
  uint32_t heyting_negation_a;
  uint32_t lawvere_tierney_sheaf_a;
  uint32_t coheyting_subtraction_ab;
  uint32_t coheyting_supplement_a;
  uint32_t lawvere_boundary_a;
  uint32_t lawvere_boundary_b;
  uint32_t lawvere_boundary_meet_ab;
  int leibniz_rule_holds;
  uint32_t universal_quantifier_ab;
  double classifier_score_a[ONTOLOGY_NODE_COUNT];
  double omega_truth_a[ONTOLOGY_NODE_COUNT]; /* Deprecated compatibility name. */
  uint32_t equalizer_mask;
  double coequalizer_quotient[ONTOLOGY_NODE_COUNT];
  double isbell_covariant_star[ONTOLOGY_NODE_COUNT];
  double isbell_double_hull[ONTOLOGY_NODE_COUNT];
  double isbell_envelope_defect;
  double profunctor_end;
  double profunctor_coend;
  double weighted_colimit;
  double weighted_limit;
  double tensor_hom_adjunction_defect;
  double cocone_convolution_fg[ONTOLOGY_NODE_COUNT];
  double day_convolution_fg[ONTOLOGY_NODE_COUNT]; /* Deprecated compatibility name. */
  double cocone_internal_hom_gh[ONTOLOGY_NODE_COUNT];
  double day_internal_hom_gh[ONTOLOGY_NODE_COUNT]; /* Deprecated compatibility name. */
  double cocone_adjunction_defect;
  double day_adjunction_defect; /* Deprecated compatibility name. */
  uint32_t modal_intermediate;
  uint32_t quillen_acyclic_cofibration_m; /* Deprecated compatibility name. */
  uint32_t modal_factorization_target;
  uint32_t quillen_fibration_target; /* Deprecated compatibility name. */
  int modal_closure_matches;
  int quillen_weak_equivalence_holds; /* Deprecated compatibility name. */
  double polynomial_functor_px[ONTOLOGY_NODE_COUNT];
  double polynomial_codesign_lens[ONTOLOGY_NODE_COUNT];
  double heat_diffused_src[ONTOLOGY_NODE_COUNT];
  SinkhornTransportState sinkhorn_ot;
  RankPageDiagnostic rank_page_diagnostics;
  SpectralSequenceState spectral_seq; /* Deprecated compatibility name. */
  RicciCurvatureState ricci_curv;
  ZetaTropicalState zeta_tropical;
  RotaBaxterState rota_baxter;
} ToposState;

typedef struct {
  char id[80];
  char label[64];
  const char *kind;
  double score;
  uint32_t degree;
} RankedEntity;

void bitset_init(BitSet *bs);
void bitset_free(BitSet *bs);
int bitset_set(BitSet *bs, uint32_t bit);
int bitset_test(const BitSet *bs, uint32_t bit);
uint32_t bitset_popcount(const BitSet *bs);
int bitset_clone(const BitSet *src, BitSet *dst);
void bitset_and_inplace(BitSet *dst, const BitSet *other);
void bitset_andnot_inplace(BitSet *dst, const BitSet *other);
int bitset_or_inplace(BitSet *dst, const BitSet *other);
uint32_t bitset_intersect_count(const BitSet *a, const BitSet *b);
double bitset_jaccard(const BitSet *a, const BitSet *b);

void engine_init(OyezEngine *engine);
void engine_free(OyezEngine *engine);
void engine_read_lock(const OyezEngine *engine);
void engine_write_lock(OyezEngine *engine);
void engine_unlock(const OyezEngine *engine);

int engine_upsert_feed(OyezEngine *engine, const CatalogEntry *entry, uint32_t *out_feed_id);
const FeedRecord *engine_find_feed(const OyezEngine *engine, uint32_t feed_id);
const FeedRecord *engine_find_feed_by_url(const OyezEngine *engine, const char *feed_url);
size_t engine_seed_catalog(OyezEngine *engine, const char *region_slug, const char *country_code);

int engine_ingest_article(OyezEngine *engine,
                          uint32_t feed_id,
                          const char *guid,
                          const char *title,
                          const char *url,
                          const char *author,
                          const char *summary,
                          const char *content_html,
                          const char *published_at,
                          uint64_t published_ms,
                          const char *image_url,
                          const char *const *tags,
                          size_t tag_count,
                          uint32_t *out_article_id,
                          int *out_inserted);

const ArticleRecord *engine_get_article_by_id(const OyezEngine *engine, uint32_t id);
int engine_enrich_article(OyezEngine *engine,
                          uint32_t article_id,
                          const char *author,
                          const char *summary,
                          const char *content_html,
                          const char *content_text,
                          const char *image_url);
void engine_extract_query_snippet(const ArticleRecord *a,
                                  const char *query,
                                  char *out_buf,
                                  size_t out_cap);
int engine_search(const OyezEngine *engine, const ArticleQuery *q, SearchResponse *out);
void search_response_free(SearchResponse *res);
double engine_dpp_rerank(ScoredArticle *pool, size_t count, double lambda);

size_t engine_find_near_duplicates(const OyezEngine *engine,
                                   uint32_t article_id,
                                   uint32_t max_hamming,
                                   const ArticleRecord **out_articles,
                                   uint32_t *out_distances,
                                   size_t max_out);

size_t engine_formal_concepts(const OyezEngine *engine,
                              size_t min_support,
                              FormalConcept *out_concepts,
                              size_t max_concepts);

size_t engine_categorical_pullback(const OyezEngine *engine,
                                   const char *left_region,
                                   const char *right_region,
                                   const char *pivot_dimension,
                                   PullbackPair *out_pairs,
                                   size_t max_pairs);

size_t engine_categorical_pushout(const OyezEngine *engine,
                                  const char *left_region,
                                  const char *right_region,
                                  const char *pivot_dimension,
                                  PushoutAmalgam *out_amalgams,
                                  size_t max_amalgams);

void engine_natural_transformation(const OyezEngine *engine,
                                   WorldRegion source_region,
                                   WorldRegion target_region,
                                   PresheafMorphism *out_morphism);

size_t engine_grothendieck_fibration(const OyezEngine *engine,
                                     const char *base_category,
                                     CartesianLift *out_lifts,
                                     size_t max_lifts);

void engine_temporal_sheaf(const OyezEngine *engine,
                           TemporalSheafState *out_temporal);

void engine_topos_state(const OyezEngine *engine,
                        WorldRegion source_region,
                        WorldRegion target_region,
                        OntologyNodeId prop_a,
                        OntologyNodeId prop_b,
                        ToposState *out_topos);

size_t engine_operad_compose(const OyezEngine *engine,
                             const char *region_a,
                             const char *region_b,
                             const char *region_c,
                             OperadChain *out_chains,
                             size_t max_chains);

size_t engine_bipartite_pagerank(const OyezEngine *engine,
                                 uint32_t iterations,
                                 double damping,
                                 RankedEntity *out_entities,
                                 size_t max_entities,
                                 ScoredArticle *out_articles,
                                 size_t max_articles);

size_t engine_story_clusters(const OyezEngine *engine,
                             uint32_t max_hamming,
                             StoryCluster *out_clusters,
                             size_t max_clusters);

int engine_save_snapshot(const OyezEngine *engine, const char *path);
int engine_load_snapshot(OyezEngine *engine, const char *path);

const char *automation_action_slug(AutomationActionType act);
int automation_action_from_slug(const char *slug, AutomationActionType *out);
int engine_upsert_automation_rule(OyezEngine *engine, const AutomationRule *rule, uint32_t *out_rule_id);
int engine_delete_automation_rule(OyezEngine *engine, uint32_t rule_id);
size_t engine_evaluate_all_automation_rules(OyezEngine *engine);
size_t engine_bandit_select_feeds(const OyezEngine *engine,
                                  BanditFeedPriority *out_priorities,
                                  size_t max_out);
void engine_appliance_state(const OyezEngine *engine,
                            EngineApplianceState *out_app);

#endif
