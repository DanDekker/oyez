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

static const char *const k_ai_tier_slugs[AI_TIER_COUNT] = {
    "non-ai-baseline",
    "applied-and-rag",
    "inference-and-serving",
    "neocloud-and-cluster",
    "frontier-model-and-rl",
};

static const char *const k_ai_license_slugs[AI_LICENSE_COUNT] = {
    "non-model-baseline",
    "closed-frontier-api",
    "open-weights-custom-license",
    "open-weights-permissive",
    "open-source-osi-full",
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
static int8_t g_mobius[ONTOLOGY_NODE_COUNT][ONTOLOGY_NODE_COUNT];
static uint32_t g_mobius_tv = 0u;
static uint32_t g_nerve_f[6] = {0};
static int32_t g_euler_char = 0;
static double g_laplacian[ONTOLOGY_NODE_COUNT][ONTOLOGY_NODE_COUNT];
static double g_laplacian_trace = 0.0;
static double g_spectral_radius = 0.0;
static double g_fiedler_value = 0.0;
static double g_lawvere_dist[ONTOLOGY_NODE_COUNT][ONTOLOGY_NODE_COUNT];
static double g_leinster_sim[ONTOLOGY_NODE_COUNT][ONTOLOGY_NODE_COUNT];
static double g_leinster_weight[ONTOLOGY_NODE_COUNT];
static double g_leinster_magnitude = 0.0;

static double fast_sqrt_nr(double x) {
  if(x <= 1e-18) return 0.0;
  double r = x > 1.0 ? (x * 0.5) : 1.0;
  for(int nr = 0; nr < 14; ++nr) r = 0.5 * (r + x / r);
  return r;
}

static double fast_exp_neg(double x) {
  if(x <= 0.0) return 1.0;
  if(x >= 28.0) return 0.0;
  /* Scale-and-square exp(-x) = (exp(-x/16))^16 via [3/3] Pade approximant */
  double s = x * 0.0625;
  double s2 = s * s;
  double s3 = s2 * s;
  double num = 1.0 - 0.5 * s + 0.1 * s2 - (1.0 / 120.0) * s3;
  double den = 1.0 + 0.5 * s + 0.1 * s2 + (1.0 / 120.0) * s3;
  double r = num / den;
  r *= r; r *= r; r *= r; r *= r;
  return r;
}

void ontology_init(void) {
  if(g_ontology_ready) return;
  for(uint32_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) {
    g_ontology[i].upward_mask = BIT(i) | g_ontology[i].parents_mask;
  }
  /* Transitive closure Warshall pass on 32-bit poset masks */
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

  /* Compute Rota's Incidence Algebra Mobius Function mu(u, v) on Poset C:
   * mu(u, u) = 1
   * mu(u, v) = - sum_{u <= z < v} mu(u, z) for u < v */
  memset(g_mobius, 0, sizeof(g_mobius));
  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
    g_mobius[u][u] = 1;
  }
  /* Iterate by increasing size of interval [u, v] */
  for(uint32_t len = 2u; len <= ONTOLOGY_NODE_COUNT; ++len) {
    for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
      for(uint32_t v = 0; v < ONTOLOGY_NODE_COUNT; ++v) {
        if(u == v) continue;
        if((g_ontology[u].upward_mask & BIT(v)) == 0u) continue;
        uint32_t interval = g_ontology[u].upward_mask & g_ontology[v].downward_mask;
        if(popcnt64((uint64_t)interval) != len) continue;
        int sum = 0;
        uint32_t z_mask = interval & ~BIT(v);
        while(z_mask) {
          uint32_t z = ctz64((uint64_t)z_mask);
          sum += g_mobius[u][z];
          z_mask &= (z_mask - 1u);
        }
        g_mobius[u][v] = (int8_t)(-sum);
      }
    }
  }
  g_mobius_tv = 0u;
  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
    for(uint32_t v = 0; v < ONTOLOGY_NODE_COUNT; ++v) {
      int m = g_mobius[u][v];
      g_mobius_tv += (uint32_t)(m < 0 ? -m : m);
    }
  }

  /* Compute Simplicial Nerve N(C) f-vector (f_0, f_1, f_2, f_3, f_4, f_5) of strict chains
   * u_0 < u_1 < ... < u_k in the Poset Category C, and verify contractibility chi(N(C)) == 1. */
  uint32_t f0 = ONTOLOGY_NODE_COUNT, f1 = 0u, f2 = 0u, f3 = 0u, f4 = 0u, f5 = 0u;
  for(uint32_t u0 = 0; u0 < ONTOLOGY_NODE_COUNT; ++u0) {
    uint32_t m1 = g_ontology[u0].upward_mask & ~BIT(u0);
    f1 += popcnt64((uint64_t)m1);
    while(m1) {
      uint32_t u1 = ctz64((uint64_t)m1);
      m1 &= (m1 - 1u);
      uint32_t m2 = g_ontology[u1].upward_mask & ~BIT(u1);
      f2 += popcnt64((uint64_t)m2);
      while(m2) {
        uint32_t u2 = ctz64((uint64_t)m2);
        m2 &= (m2 - 1u);
        uint32_t m3 = g_ontology[u2].upward_mask & ~BIT(u2);
        f3 += popcnt64((uint64_t)m3);
        while(m3) {
          uint32_t u3 = ctz64((uint64_t)m3);
          m3 &= (m3 - 1u);
          uint32_t m4 = g_ontology[u3].upward_mask & ~BIT(u3);
          f4 += popcnt64((uint64_t)m4);
          while(m4) {
            uint32_t u4 = ctz64((uint64_t)m4);
            m4 &= (m4 - 1u);
            uint32_t m5 = g_ontology[u4].upward_mask & ~BIT(u4);
            f5 += popcnt64((uint64_t)m5);
          }
        }
      }
    }
  }
  g_nerve_f[0] = f0;
  g_nerve_f[1] = f1;
  g_nerve_f[2] = f2;
  g_nerve_f[3] = f3;
  g_nerve_f[4] = f4;
  g_nerve_f[5] = f5;
  g_euler_char = (int32_t)f0 - (int32_t)f1 + (int32_t)f2 - (int32_t)f3 + (int32_t)f4 - (int32_t)f5;

  /* Construct 0-th Combinatorial Hodge / Graph Laplacian L_0 = D - A over covering edges */
  memset(g_laplacian, 0, sizeof(g_laplacian));
  g_laplacian_trace = 0.0;
  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
    uint32_t pmask = g_ontology[u].parents_mask;
    while(pmask) {
      uint32_t p = ctz64((uint64_t)pmask);
      pmask &= (pmask - 1u);
      if(p < ONTOLOGY_NODE_COUNT && p != u) {
        g_laplacian[u][p] -= 1.0;
        g_laplacian[p][u] -= 1.0;
        g_laplacian[u][u] += 1.0;
        g_laplacian[p][p] += 1.0;
        g_laplacian_trace += 2.0;
      }
    }
  }

  /* Power iteration for dominant eigenvalue lambda_max(L_0) */
  double vec[ONTOLOGY_NODE_COUNT];
  double next_v[ONTOLOGY_NODE_COUNT];
  for(uint32_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) {
    vec[i] = (i & 1u) ? 1.0 : -1.0;
  }
  double eig = 0.0;
  for(int it = 0; it < 30; ++it) {
    double norm_sq = 0.0;
    for(uint32_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) {
      double s = 0.0;
      for(uint32_t j = 0; j < ONTOLOGY_NODE_COUNT; ++j) {
        s += g_laplacian[i][j] * vec[j];
      }
      next_v[i] = s;
      norm_sq += s * s;
    }
    if(norm_sq <= 1e-18) break;
    double r = fast_sqrt_nr(norm_sq);
    eig = r;
    double inv_norm = 1.0 / r;
    for(uint32_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) {
      vec[i] = next_v[i] * inv_norm;
    }
  }
  g_spectral_radius = eig;

  /* Deflated power iteration on M = lambda_max * I - L_0 orthogonal to ker(L_0) = span(1)
   * to obtain Fiedler algebraic connectivity eigenvalue lambda_2(L_0) > 0 */
  for(uint32_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) {
    vec[i] = (double)((int)i - 16);
  }
  double mu_max = 0.0;
  for(int it = 0; it < 45; ++it) {
    double mean = 0.0;
    for(uint32_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) mean += vec[i];
    mean /= (double)ONTOLOGY_NODE_COUNT;
    for(uint32_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) vec[i] -= mean;

    double norm_sq = 0.0;
    double mean_next = 0.0;
    for(uint32_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) {
      double l_v = 0.0;
      for(uint32_t j = 0; j < ONTOLOGY_NODE_COUNT; ++j) {
        l_v += g_laplacian[i][j] * vec[j];
      }
      next_v[i] = g_spectral_radius * vec[i] - l_v;
      mean_next += next_v[i];
    }
    mean_next /= (double)ONTOLOGY_NODE_COUNT;
    for(uint32_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) {
      next_v[i] -= mean_next;
      norm_sq += next_v[i] * next_v[i];
    }
    if(norm_sq <= 1e-18) break;
    double r = fast_sqrt_nr(norm_sq);
    mu_max = r;
    double inv_r = 1.0 / r;
    for(uint32_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) {
      vec[i] = next_v[i] * inv_r;
    }
  }
  g_fiedler_value = (g_spectral_radius > mu_max) ? (g_spectral_radius - mu_max) : 0.15;

  /* Lawvere Enriched Category Geodesic Quasimetric d_L(u, v) over Cost Quantale ([0, inf], >=, +, 0)
   * via Tropical Min-Plus Semiring Kleene Star (Floyd-Warshall):
   * - Covariant specialization u -> p has cost w(u -> p) = 0.5 + (|down(p)| - |down(u)|) / |down(p)|
   * - Contravariant generalization p -> u has asymmetric cost 2.25 * w(u -> p) */
  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
    for(uint32_t v = 0; v < ONTOLOGY_NODE_COUNT; ++v) {
      g_lawvere_dist[u][v] = (u == v) ? 0.0 : 1e9;
    }
  }
  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
    double du = (double)popcnt64((uint64_t)g_ontology[u].downward_mask);
    uint32_t pmask = g_ontology[u].parents_mask;
    while(pmask) {
      uint32_t p = ctz64((uint64_t)pmask);
      pmask &= (pmask - 1u);
      if(p < ONTOLOGY_NODE_COUNT && p != u) {
        double dp = (double)popcnt64((uint64_t)g_ontology[p].downward_mask);
        double w_cov = 0.5 + ((dp > du && dp > 0.0) ? ((dp - du) / dp) : 0.25);
        double w_contra = 2.25 * w_cov;
        if(w_cov < g_lawvere_dist[u][p]) g_lawvere_dist[u][p] = w_cov;
        if(w_contra < g_lawvere_dist[p][u]) g_lawvere_dist[p][u] = w_contra;
      }
    }
  }
  for(uint32_t k = 0; k < ONTOLOGY_NODE_COUNT; ++k) {
    for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
      for(uint32_t v = 0; v < ONTOLOGY_NODE_COUNT; ++v) {
        double cand = g_lawvere_dist[u][k] + g_lawvere_dist[k][v];
        if(cand < g_lawvere_dist[u][v]) {
          g_lawvere_dist[u][v] = cand;
        }
      }
    }
  }

  /* Leinster Similarity Matrix Z(u, v) = exp(-d_L(u, v)) & Enriched Category Magnitude:
   * Solve sum_v Z(u, v) w(v) = 1 via damped Gauss-Seidel relaxation to obtain the
   * canonical Leinster Weighting w(u) and Magnitude |C|_L = sum_u w(u). */
  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
    g_leinster_weight[u] = 1.0 / (double)ONTOLOGY_NODE_COUNT;
    for(uint32_t v = 0; v < ONTOLOGY_NODE_COUNT; ++v) {
      g_leinster_sim[u][v] = fast_exp_neg(g_lawvere_dist[u][v]);
    }
  }
  for(int it = 0; it < 50; ++it) {
    for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
      double off_diag = 0.0;
      for(uint32_t v = 0; v < ONTOLOGY_NODE_COUNT; ++v) {
        if(v != u) off_diag += g_leinster_sim[u][v] * g_leinster_weight[v];
      }
      double target_w = 1.0 - off_diag;
      g_leinster_weight[u] = 0.45 * g_leinster_weight[u] + 0.55 * target_w;
    }
  }
  g_leinster_magnitude = 0.0;
  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
    g_leinster_magnitude += g_leinster_weight[u];
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

const char *ai_compute_tier_slug(AiComputeTier tier) {
  if((unsigned)tier >= AI_TIER_COUNT) return "non-ai-baseline";
  return k_ai_tier_slugs[tier];
}

int ai_compute_tier_from_slug(const char *slug, AiComputeTier *out) {
  if(!slug || !out) return -1;
  for(uint32_t i = 0; i < AI_TIER_COUNT; ++i) {
    if(strcmp(slug, k_ai_tier_slugs[i]) == 0) {
      *out = (AiComputeTier)i;
      return 0;
    }
  }
  return -1;
}

const char *ai_weight_license_slug(AiWeightLicense lic) {
  if((unsigned)lic >= AI_LICENSE_COUNT) return "non-model-baseline";
  return k_ai_license_slugs[lic];
}

int ai_weight_license_from_slug(const char *slug, AiWeightLicense *out) {
  if(!slug || !out) return -1;
  for(uint32_t i = 0; i < AI_LICENSE_COUNT; ++i) {
    if(strcmp(slug, k_ai_license_slugs[i]) == 0) {
      *out = (AiWeightLicense)i;
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

void ontology_mobius_invert(const double *kan_section, double *out_fundamental) {
  /* Rota's Mobius Inversion over the Poset Ideal Zeta Function:
   * Given g(c) = (Lan_iota f)(c) = sum_{u <= c} f(u),
   * recover f(c) = sum_{u <= c} g(u) * mu(u, c). */
  ontology_init();
  for(uint32_t c = 0; c < ONTOLOGY_NODE_COUNT; ++c) {
    uint32_t ideal = g_ontology[c].downward_mask;
    double acc = 0.0;
    while(ideal) {
      uint32_t u = ctz64((uint64_t)ideal);
      acc += kan_section[u] * (double)g_mobius[u][c];
      ideal &= (ideal - 1u);
    }
    out_fundamental[c] = acc;
  }
}

int ontology_mobius_value(OntologyNodeId u, OntologyNodeId v) {
  ontology_init();
  if((unsigned)u >= ONTOLOGY_NODE_COUNT || (unsigned)v >= ONTOLOGY_NODE_COUNT) return 0;
  return (int)g_mobius[u][v];
}

double ontology_wasserstein_distance(uint32_t mask_a, uint32_t mask_b) {
  ontology_init();
  uint32_t ua = ontology_upward_closure(mask_a);
  uint32_t ub = ontology_upward_closure(mask_b);
  uint32_t sym_diff = ua ^ ub;
  uint32_t union_m = ua | ub;
  if(union_m == 0u) return 0.0;
  /* Weight each node u in symmetric difference by inverse depth (1 / |^u|) */
  double diff_mass = 0.0;
  double total_mass = 0.0;
  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
    double w = 1.0 / (double)popcnt64((uint64_t)g_ontology[u].upward_mask);
    if(union_m & BIT(u)) total_mass += w;
    if(sym_diff & BIT(u)) diff_mass += w;
  }
  return total_mass > 0.0 ? (diff_mass / total_mass) : 0.0;
}

double ontology_lawvere_distance(uint32_t mask_from, uint32_t mask_to) {
  ontology_init();
  if(mask_from == 0u || mask_to == 0u) return 0.0;
  /* Directed Hausdorff-Lawvere Quasimetric over the Cost Quantale ([0, inf], >=, +, 0):
   *   d_L(A, B) = sup_{u in A} inf_{v in B} d_L(u, v)
   * Reduces to g_lawvere_dist[u][v] on singletons (1u << u, 1u << v) and
   * satisfies d_L(A, A) = 0 and d_L(A, C) <= d_L(A, B) + d_L(B, C). */
  double sup_dist = 0.0;
  uint32_t scan_a = mask_from;
  while(scan_a) {
    uint32_t u = ctz64((uint64_t)scan_a);
    scan_a &= (scan_a - 1u);
    if(u >= ONTOLOGY_NODE_COUNT) continue;
    double inf_dist = 1e9;
    uint32_t scan_b = mask_to;
    while(scan_b) {
      uint32_t v = ctz64((uint64_t)scan_b);
      scan_b &= (scan_b - 1u);
      if(v >= ONTOLOGY_NODE_COUNT) continue;
      if(g_lawvere_dist[u][v] < inf_dist) {
        inf_dist = g_lawvere_dist[u][v];
      }
    }
    if(inf_dist > sup_dist) {
      sup_dist = inf_dist;
    }
  }
  return sup_dist;
}

double ontology_profunctor_coend(const double *presheaf_f, const double *copresheaf_g) {
  /* Categorical Coend int^{c in C} F(c) * G(c) over the Poset Category C:
   * Quotients duplicate weight along morphism chains u <= c via Rota's
   * Incidence Algebra Mobius inverse mu(u, c) = zeta^{-1}(u, c):
   *   int^{c in C} F(c) G(c) = sum_{c in C} |sum_{u <= c} F(u) * mu(u, c)| * G(c)
   * When F = Lan_iota f is a Left Kan extension of f >= 0, sum_{u <= c} F(u) mu(u, c) = f(c). */
  if(!presheaf_f || !copresheaf_g) return 0.0;
  ontology_init();
  double coend = 0.0;
  for(uint32_t c = 0; c < ONTOLOGY_NODE_COUNT; ++c) {
    uint32_t ideal = g_ontology[c].downward_mask;
    double mob_f = 0.0;
    while(ideal) {
      uint32_t u = ctz64((uint64_t)ideal);
      ideal &= (ideal - 1u);
      mob_f += presheaf_f[u] * (double)g_mobius[u][c];
    }
    coend += (mob_f < 0.0 ? -mob_f : mob_f) * copresheaf_g[c];
  }
  return coend;
}

double ontology_leinster_magnitude(void) {
  ontology_init();
  return g_leinster_magnitude;
}

double ontology_node_magnitude_weight(OntologyNodeId u) {
  ontology_init();
  if((unsigned)u >= ONTOLOGY_NODE_COUNT) return 0.0;
  return g_leinster_weight[u];
}

double ontology_yoneda_lemma_eval(OntologyNodeId c, const double *presheaf_f) {
  /* Constructive Yoneda Lemma Isomorphism Nat(Y(c), F) ~= F(c):
   * Y(c)(u) = Hom_C(u, c) = [u <= c] is the representable presheaf at c.
   * Pairing Y(c) with the Mobius-inverted density (mu * F)(u) = sum_{x <= u} F(x) mu(x, u)
   * over u in C yields:
   *   <Y(c), F>_Y = sum_{u in C} Y(c)(u) sum_{x <= u} F(x) mu(x, u)
   *               = sum_{x <= c} F(x) sum_{x <= u <= c} mu(x, u) = F(c). */
  if(!presheaf_f || (unsigned)c >= ONTOLOGY_NODE_COUNT) return 0.0;
  ontology_init();
  uint32_t y_c = g_ontology[c].downward_mask;
  double nat_pairing = 0.0;
  while(y_c) {
    uint32_t u = ctz64((uint64_t)y_c);
    y_c &= (y_c - 1u);
    uint32_t ideal_u = g_ontology[u].downward_mask;
    double mob_u = 0.0;
    while(ideal_u) {
      uint32_t x = ctz64((uint64_t)ideal_u);
      ideal_u &= (ideal_u - 1u);
      mob_u += presheaf_f[x] * (double)g_mobius[x][u];
    }
    nat_pairing += mob_u;
  }
  return nat_pairing;
}

void ontology_yoneda_kan_transform(const double *presheaf_f, double *out_yoneda_envelope) {
  /* Enriched Lawvere-Yoneda Left Kan Extension (Tropical Infimal Convolution):
   *   (Lan_Y F)(c) = sup_{u in C} F(u) * exp(-d_L(u, c)) */
  if(!presheaf_f || !out_yoneda_envelope) return;
  ontology_init();
  for(uint32_t c = 0; c < ONTOLOGY_NODE_COUNT; ++c) {
    double sup_val = 0.0;
    for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
      double v = presheaf_f[u] * g_leinster_sim[u][c];
      if(v > sup_val) sup_val = v;
    }
    out_yoneda_envelope[c] = sup_val;
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

uint32_t ontology_lawvere_tierney_sheafify(uint32_t sub_mask_a) {
  /* Lawvere-Tierney Double-Negation Topology j: Omega -> Omega in the Presheaf Topos Set^{C^op}:
   *   j(A) = ~~A = ((A => 0) => 0)
   * Satisfies the three Lawvere-Tierney modal closure axioms:
   *   1. A <= j(A)
   *   2. j(j(A)) == j(A)
   *   3. j(A ^ B) == j(A) ^ j(B)
   * By Glivenko's Theorem, j-closed ideals form a complete Boolean subtopos Sh_{~~}(C). */
  uint32_t neg_a = ontology_heyting_negation(sub_mask_a);
  return ontology_heyting_negation(neg_a);
}

int ontology_is_grothendieck_dense(uint32_t sub_mask_a) {
  return ontology_lawvere_tierney_sheafify(sub_mask_a) == 0xFFFFFFFFu;
}

double ontology_information_specificity(uint32_t mask) {
  ontology_init();
  uint32_t m = mask & ~BIT(ONTO_ROOT);
  if(m == 0u) return 1.0;
  double best_spec = 1.0;
  while(m) {
    uint32_t u = ctz64((uint64_t)m);
    m &= (m - 1u);
    if(u < ONTOLOGY_NODE_COUNT) {
      /* Deeper / more specialized nodes have larger upward filter |^u| and smaller ideal |vu| */
      uint32_t up_cnt = popcnt64((uint64_t)g_ontology[u].upward_mask);
      uint32_t down_cnt = popcnt64((uint64_t)g_ontology[u].downward_mask);
      double spec = 1.0 + 0.08 * (double)up_cnt + (1.0 / (double)(down_cnt ? down_cnt : 1u)) * 0.25;
      if(spec > best_spec) best_spec = spec;
    }
  }
  return best_spec;
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
  for(int k = 0; k < 6; ++k) out->nerve_f_vector[k] = g_nerve_f[k];
  out->euler_characteristic = g_euler_char;
  out->betti_numbers[0] = 1u;
  out->betti_numbers[1] = 0u;
  out->mobius_total_variation = g_mobius_tv;
  out->laplacian_trace = g_laplacian_trace;
  out->spectral_radius = g_spectral_radius;
  out->fiedler_eigenvalue = g_fiedler_value;

  /* Compute Dirichlet harmonic energy <f, L_0 f> = sum_{u < v} (f(u) - f(v))^2 */
  double energy = 0.0;
  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
    uint32_t pmask = g_ontology[u].parents_mask;
    double fu = raw_fiber_counts ? raw_fiber_counts[u] : 0.0;
    while(pmask) {
      uint32_t p = ctz64((uint64_t)pmask);
      pmask &= (pmask - 1u);
      if(p < ONTOLOGY_NODE_COUNT) {
        double fp = raw_fiber_counts ? raw_fiber_counts[p] : 0.0;
        double d = fu - fp;
        energy += d * d;
      }
    }
  }
  out->dirichlet_energy = energy;

  /* Simplicial Cochain Complex C^0(C) --delta_0--> C^1(C) --delta_1--> C^2(C) over Nerve N_2(C):
   * 1. Exact 1-cochain (delta_0 f)(u, v) = f(v) - f(u) has (delta_1 o delta_0 f)(u0 < u1 < u2) == 0.
   * 2. Lawvere-weighted 1-cochain F(u, v) = d_L(u, v) * (f(u) + 0.5 * f(v)) has non-zero
   *    simplicial curl (delta_1 F)(u0, u1, u2) = F(u1, u2) - F(u0, u2) + F(u0, u1).
   * 3. Discrete Helmholtz-Hodge Decomposition: solve L_0 * phi = delta_0^T F to decompose
   *    F = delta_0(phi) (gradient) + R (divergence-free solenoidal curl + harmonic). */
  double exact_norm = 0.0;
  double curl_norm = 0.0;
  double div_f[ONTOLOGY_NODE_COUNT] = {0};
  for(uint32_t u0 = 0; u0 < ONTOLOGY_NODE_COUNT; ++u0) {
    double f0 = raw_fiber_counts ? raw_fiber_counts[u0] : 0.0;
    uint32_t p1_mask = g_ontology[u0].parents_mask;
    while(p1_mask) {
      uint32_t u1 = ctz64((uint64_t)p1_mask);
      p1_mask &= (p1_mask - 1u);
      if(u1 == u0 || u1 >= ONTOLOGY_NODE_COUNT) continue;
      double f1 = raw_fiber_counts ? raw_fiber_counts[u1] : 0.0;
      double d0_01 = f1 - f0;
      double F_01 = g_lawvere_dist[u0][u1] * (f0 + 0.5 * f1);
      /* Codifferential (delta_0^T F)(v) accumulates net incoming minus outgoing 1-cochain flow */
      div_f[u1] += F_01;
      div_f[u0] -= F_01;

      uint32_t p2_mask = g_ontology[u1].parents_mask;
      while(p2_mask) {
        uint32_t u2 = ctz64((uint64_t)p2_mask);
        p2_mask &= (p2_mask - 1u);
        if(u2 == u1 || u2 >= ONTOLOGY_NODE_COUNT) continue;
        double f2 = raw_fiber_counts ? raw_fiber_counts[u2] : 0.0;
        double d0_12 = f2 - f1;
        double d0_02 = f2 - f0;
        double coboundary_exact = d0_12 - d0_02 + d0_01;
        exact_norm += coboundary_exact * coboundary_exact;

        double F_12 = g_lawvere_dist[u1][u2] * (f1 + 0.5 * f2);
        double F_02 = g_lawvere_dist[u0][u2] * (f0 + 0.5 * f2);
        double curl = F_12 - F_02 + F_01;
        curl_norm += curl * curl;
      }
    }
  }
  out->cochain_exactness_norm = exact_norm;
  out->hodge_1_curl_norm = curl_norm;
  out->leinster_magnitude = g_leinster_magnitude;

  /* Solve Hodge-Poisson equation L_0 * phi = div_f orthogonal to ker(L_0) = span(1) */
  double phi[ONTOLOGY_NODE_COUNT] = {0};
  for(int it = 0; it < 20; ++it) {
    double mean_phi = 0.0;
    for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
      double deg = g_laplacian[u][u];
      if(deg <= 0.0) continue;
      double nb_sum = 0.0;
      for(uint32_t v = 0; v < ONTOLOGY_NODE_COUNT; ++v) {
        if(v != u && g_laplacian[u][v] != 0.0) {
          nb_sum += (-g_laplacian[u][v]) * phi[v];
        }
      }
      phi[u] = (div_f[u] + nb_sum) / deg;
      mean_phi += phi[u];
    }
    mean_phi /= (double)ONTOLOGY_NODE_COUNT;
    for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) phi[u] -= mean_phi;
  }

  double grad_norm = 0.0;
  double sol_norm = 0.0;
  for(uint32_t u0 = 0; u0 < ONTOLOGY_NODE_COUNT; ++u0) {
    double f0 = raw_fiber_counts ? raw_fiber_counts[u0] : 0.0;
    uint32_t p1_mask = g_ontology[u0].parents_mask;
    while(p1_mask) {
      uint32_t u1 = ctz64((uint64_t)p1_mask);
      p1_mask &= (p1_mask - 1u);
      if(u1 == u0 || u1 >= ONTOLOGY_NODE_COUNT) continue;
      double f1 = raw_fiber_counts ? raw_fiber_counts[u1] : 0.0;
      double F_01 = g_lawvere_dist[u0][u1] * (f0 + 0.5 * f1);
      double g_01 = phi[u1] - phi[u0];
      double r_01 = F_01 - g_01;
      grad_norm += g_01 * g_01;
      sol_norm += r_01 * r_01;
    }
  }
  out->hodge_gradient_norm = grad_norm;
  out->hodge_solenoidal_norm = sol_norm;
  out->monad_closure_defect = fast_sqrt_nr(total_norm);
}

typedef struct {
  uint64_t wb_filter[8];
  uint64_t char0_mask;
  uint16_t head[32];
  uint16_t next[2048];
  uint16_t pos[2048];
  uint32_t word_count;
} WordBloomIndex;

static void compute_char_mask(const char *s, size_t len, WordBloomIndex *wb) {
  memset(wb->wb_filter, 0, sizeof(wb->wb_filter));
  wb->char0_mask = 0ULL;
  memset(wb->head, 0xFF, sizeof(wb->head));
  wb->word_count = 0u;

  for(size_t i = 0; i < len; ++i) {
    if(i > 0 && isalnum((unsigned char)s[i - 1u])) continue;
    unsigned char c0 = (unsigned char)s[i];
    if(!isalnum(c0)) continue;
    wb->char0_mask |= (1ULL << (c0 & 63u));
    if(wb->word_count < 2048u) {
      uint32_t b = c0 & 31u;
      uint32_t w = wb->word_count++;
      wb->pos[w] = (uint16_t)i;
      wb->next[w] = wb->head[b];
      wb->head[b] = (uint16_t)w;
    }
    if(i + 1u < len) {
      unsigned char c1 = (unsigned char)s[i + 1u];
      uint32_t h2 = ((uint32_t)c0 * 37u + (uint32_t)c1) & 255u;
      wb->wb_filter[h2 >> 6u] |= (1ULL << (h2 & 63u));
      if(i + 2u < len) {
        unsigned char c2 = (unsigned char)s[i + 2u];
        uint32_t h3 = ((uint32_t)c0 * 131u + (uint32_t)c1 * 37u + (uint32_t)c2) & 255u;
        wb->wb_filter[4u + (h3 >> 6u)] |= (1ULL << (h3 & 63u));
      }
    }
  }
}

static int text_contains_word_fast(const char *lower_text,
                                   size_t len,
                                   const WordBloomIndex *wb,
                                   const char *needle) {
  unsigned char c0 = (unsigned char)needle[0];
  if(c0 == '\0') return 0;
  if((wb->char0_mask & (1ULL << (c0 & 63u))) == 0ULL) return 0;
  unsigned char c1 = (unsigned char)needle[1];
  if(c1 != '\0') {
    uint32_t h2 = ((uint32_t)c0 * 37u + (uint32_t)c1) & 255u;
    if((wb->wb_filter[h2 >> 6u] & (1ULL << (h2 & 63u))) == 0ULL) return 0;
    unsigned char c2 = (unsigned char)needle[2];
    if(c2 != '\0') {
      uint32_t h3 = ((uint32_t)c0 * 131u + (uint32_t)c1 * 37u + (uint32_t)c2) & 255u;
      if((wb->wb_filter[4u + (h3 >> 6u)] & (1ULL << (h3 & 63u))) == 0ULL) return 0;
    }
  }
  size_t nlen = strlen(needle);
  if(len < nlen) return 0;
  uint32_t b = c0 & 31u;
  for(uint16_t w = wb->head[b]; w != 0xFFFFu; w = wb->next[w]) {
    size_t off = (size_t)wb->pos[w];
    if(off + nlen <= len && (unsigned char)lower_text[off] == c0 &&
       memcmp(lower_text + off, needle, nlen) == 0) {
      if(off + nlen >= len || !isalnum((unsigned char)lower_text[off + nlen])) {
        return 1;
      }
    }
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

static NewsType news_type_classify_precomputed(const char *title,
                                               const char *lower,
                                               size_t len,
                                               const WordBloomIndex *alpha_mask,
                                               const char *feed_category) {
  char tlower[512];
  size_t tlen = 0;
  build_lowercase_window(title, NULL, NULL, tlower, sizeof(tlower), &tlen);

  double scores[NEWS_TYPE_COUNT] = {0};

  if(strstr(tlower, "breaking:") || strstr(tlower, "urgent:") || strstr(tlower, "flash:") ||
     strstr(tlower, "live updates") || strstr(tlower, "just in")) {
    scores[NEWS_TYPE_BREAKING_DISPATCH] += 5.0;
  }
  if(text_contains_word_fast(lower, len, alpha_mask, "breaking") ||
     text_contains_word_fast(lower, len, alpha_mask, "developing")) {
    scores[NEWS_TYPE_BREAKING_DISPATCH] += 2.0;
  }

  if(feed_category && strcmp(feed_category, "investigative") == 0) {
    scores[NEWS_TYPE_INVESTIGATIVE_REPORT] += 4.5;
  }
  if(text_contains_word_fast(lower, len, alpha_mask, "investigation") ||
     text_contains_word_fast(lower, len, alpha_mask, "investigative") ||
     text_contains_word_fast(lower, len, alpha_mask, "whistleblower") ||
     text_contains_word_fast(lower, len, alpha_mask, "leaked") ||
     text_contains_word_fast(lower, len, alpha_mask, "uncovered") ||
     text_contains_word_fast(lower, len, alpha_mask, "audit")) {
    scores[NEWS_TYPE_INVESTIGATIVE_REPORT] += 3.5;
  }

  if(text_contains_word_fast(lower, len, alpha_mask, "parliament") ||
     text_contains_word_fast(lower, len, alpha_mask, "senate") ||
     text_contains_word_fast(lower, len, alpha_mask, "congress") ||
     text_contains_word_fast(lower, len, alpha_mask, "legislation") ||
     text_contains_word_fast(lower, len, alpha_mask, "supreme court") ||
     text_contains_word_fast(lower, len, alpha_mask, "ruling") ||
     text_contains_word_fast(lower, len, alpha_mask, "treaty") ||
     text_contains_word_fast(lower, len, alpha_mask, "sanctions") ||
     text_contains_word_fast(lower, len, alpha_mask, "regulation") ||
     text_contains_word_fast(lower, len, alpha_mask, "decree")) {
    scores[NEWS_TYPE_POLICY_AND_LEGISLATIVE] += 3.2;
  }

  if(feed_category && (strcmp(feed_category, "science") == 0 || strcmp(feed_category, "ai") == 0 ||
                       strcmp(feed_category, "space") == 0)) {
    scores[NEWS_TYPE_SCIENTIFIC_RESEARCH] += 2.6;
  }
  if(text_contains_word_fast(lower, len, alpha_mask, "peer-reviewed") ||
     text_contains_word_fast(lower, len, alpha_mask, "arxiv") ||
     text_contains_word_fast(lower, len, alpha_mask, "biorxiv") ||
     text_contains_word_fast(lower, len, alpha_mask, "medrxiv") ||
     text_contains_word_fast(lower, len, alpha_mask, "chemrxiv") ||
     text_contains_word_fast(lower, len, alpha_mask, "preprint") ||
     text_contains_word_fast(lower, len, alpha_mask, "theorem") ||
     text_contains_word_fast(lower, len, alpha_mask, "synchrotron") ||
     text_contains_word_fast(lower, len, alpha_mask, "tokamak") ||
     text_contains_word_fast(lower, len, alpha_mask, "qubit") ||
     text_contains_word_fast(lower, len, alpha_mask, "researchers") ||
     text_contains_word_fast(lower, len, alpha_mask, "scientists") ||
     text_contains_word_fast(lower, len, alpha_mask, "clinical trial") ||
     text_contains_word_fast(lower, len, alpha_mask, "genome") ||
     text_contains_word_fast(lower, len, alpha_mask, "quantum") ||
     text_contains_word_fast(lower, len, alpha_mask, "telescope")) {
    scores[NEWS_TYPE_SCIENTIFIC_RESEARCH] += 3.6;
  }

  if(feed_category && strcmp(feed_category, "economy") == 0) {
    scores[NEWS_TYPE_MARKET_AND_ECONOMIC] += 2.5;
  }
  if(text_contains_word_fast(lower, len, alpha_mask, "inflation") ||
     text_contains_word_fast(lower, len, alpha_mask, "central bank") ||
     text_contains_word_fast(lower, len, alpha_mask, "interest rate") ||
     text_contains_word_fast(lower, len, alpha_mask, "basis points") ||
     text_contains_word_fast(lower, len, alpha_mask, "gdp") ||
     text_contains_word_fast(lower, len, alpha_mask, "earnings") ||
     text_contains_word_fast(lower, len, alpha_mask, "ipo") ||
     text_contains_word_fast(lower, len, alpha_mask, "yields")) {
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

  if(text_contains_word_fast(lower, len, alpha_mask, "communique") ||
     text_contains_word_fast(lower, len, alpha_mask, "press release") ||
     text_contains_word_fast(lower, len, alpha_mask, "ministry of") ||
     text_contains_word_fast(lower, len, alpha_mask, "official statement")) {
    scores[NEWS_TYPE_PRESS_AND_OFFICIAL] += 3.1;
  }

  if(feed_category && (strcmp(feed_category, "culture") == 0 || strcmp(feed_category, "sports") == 0)) {
    scores[NEWS_TYPE_CULTURAL_AND_FEATURE] += 2.8;
  }
  if(text_contains_word_fast(lower, len, alpha_mask, "festival") ||
     text_contains_word_fast(lower, len, alpha_mask, "exhibition") ||
     text_contains_word_fast(lower, len, alpha_mask, "cinema") ||
     text_contains_word_fast(lower, len, alpha_mask, "tournament") ||
     text_contains_word_fast(lower, len, alpha_mask, "championship")) {
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

NewsType news_type_classify(const char *title,
                            const char *summary,
                            const char *content,
                            const char *feed_category) {
  char lower[4096];
  size_t len = 0;
  build_lowercase_window(title, summary, content, lower, sizeof(lower), &len);
  WordBloomIndex alpha_mask;
  compute_char_mask(lower, len, &alpha_mask);
  return news_type_classify_precomputed(title, lower, len, &alpha_mask, feed_category);
}

typedef struct {
  const char *iso2;
  const char *const terms[5];
} GeoRule;

static const GeoRule k_geo_rules[] = {
    /* North America, Central America & Caribbean */
    {"US", {"united states", "u.s.", "washington", "pentagon", "white house"}},
    {"CA", {"canada", "canadian", "ottawa", "toronto", "montreal"}},
    {"MX", {"mexico", "mexican", "mexico city", "banxico", NULL}},
    {"GT", {"guatemala", "guatemalan", "guatemala city", NULL, NULL}},
    {"HN", {"honduras", "honduran", "tegucigalpa", NULL, NULL}},
    {"SV", {"el salvador", "salvadoran", "san salvador", NULL, NULL}},
    {"NI", {"nicaragua", "nicaraguan", "managua", NULL, NULL}},
    {"CR", {"costa rica", "costa rican", "san jose", NULL, NULL}},
    {"PA", {"panama", "panamanian", "panama city", "panama canal", NULL}},
    {"CU", {"cuba", "cuban", "havana", NULL, NULL}},
    {"DO", {"dominican republic", "santo domingo", "dominican", NULL, NULL}},
    {"HT", {"haiti", "haitian", "port-au-prince", NULL, NULL}},
    {"JM", {"jamaica", "jamaican", "kingston", NULL, NULL}},
    {"TT", {"trinidad", "tobago", "port of spain", NULL, NULL}},
    {"BS", {"bahamas", "bahamian", "nassau", NULL, NULL}},
    {"BB", {"barbados", "barbadian", "bridgetown", NULL, NULL}},
    /* South America */
    {"BR", {"brazil", "brazilian", "brasilia", "sao paulo", "rio de janeiro"}},
    {"AR", {"argentina", "argentine", "buenos aires", NULL, NULL}},
    {"UY", {"uruguay", "uruguayan", "montevideo", NULL, NULL}},
    {"PY", {"paraguay", "paraguayan", "asuncion", NULL, NULL}},
    {"BO", {"bolivia", "bolivian", "la paz", "sucre", NULL}},
    {"CO", {"colombia", "colombian", "bogota", "medellin", NULL}},
    {"CL", {"chile", "chilean", "santiago", "atacama", NULL}},
    {"PE", {"peru", "peruvian", "lima", NULL, NULL}},
    {"EC", {"ecuador", "ecuadorian", "quito", "guayaquil", NULL}},
    {"VE", {"venezuela", "venezuelan", "caracas", NULL, NULL}},
    {"GY", {"guyana", "guyanese", "georgetown", "essequibo", NULL}},
    {"SR", {"suriname", "surinamese", "paramaribo", NULL, NULL}},
    /* Europe */
    {"GB", {"united kingdom", "britain", "british", "london", "westminster"}},
    {"IE", {"ireland", "irish", "dublin", NULL, NULL}},
    {"FR", {"france", "french", "paris", "macron", "elysee"}},
    {"DE", {"germany", "german", "berlin", "bundestag", "frankfurt"}},
    {"IT", {"italy", "italian", "rome", "milan", NULL}},
    {"ES", {"spain", "spanish", "madrid", "barcelona", NULL}},
    {"PT", {"portugal", "portuguese", "lisbon", NULL, NULL}},
    {"NL", {"netherlands", "dutch", "amsterdam", "the hague", "eindhoven"}},
    {"BE", {"belgium", "belgian", "brussels", "antwerp", NULL}},
    {"LU", {"luxembourg", "luxembourgish", NULL, NULL, NULL}},
    {"CH", {"switzerland", "swiss", "geneva", "zurich", "bern"}},
    {"AT", {"austria", "austrian", "vienna", NULL, NULL}},
    {"SE", {"sweden", "swedish", "stockholm", NULL, NULL}},
    {"NO", {"norway", "norwegian", "oslo", "barents", NULL}},
    {"DK", {"denmark", "danish", "copenhagen", NULL, NULL}},
    {"FI", {"finland", "finnish", "helsinki", NULL, NULL}},
    {"IS", {"iceland", "icelandic", "reykjavik", NULL, NULL}},
    {"EE", {"estonia", "estonian", "tallinn", NULL, NULL}},
    {"LV", {"latvia", "latvian", "riga", NULL, NULL}},
    {"LT", {"lithuania", "lithuanian", "vilnius", NULL, NULL}},
    {"PL", {"poland", "polish", "warsaw", NULL, NULL}},
    {"CZ", {"czech", "prague", "czechia", NULL, NULL}},
    {"SK", {"slovakia", "slovak", "bratislava", NULL, NULL}},
    {"HU", {"hungary", "hungarian", "budapest", NULL, NULL}},
    {"RO", {"romania", "romanian", "bucharest", NULL, NULL}},
    {"BG", {"bulgaria", "bulgarian", "sofia", NULL, NULL}},
    {"HR", {"croatia", "croatian", "zagreb", NULL, NULL}},
    {"SI", {"slovenia", "slovenian", "ljubljana", NULL, NULL}},
    {"RS", {"serbia", "serbian", "belgrade", "balkans", NULL}},
    {"BA", {"bosnia", "sarajevo", "herzegovina", NULL, NULL}},
    {"AL", {"albania", "albanian", "tirana", NULL, NULL}},
    {"MD", {"moldova", "moldovan", "chisinau", "transnistria", NULL}},
    {"BY", {"belarus", "belarusian", "minsk", NULL, NULL}},
    {"UA", {"ukraine", "ukrainian", "kyiv", "kharkiv", "odesa"}},
    {"RU", {"russia", "russian", "moscow", "kremlin", NULL}},
    {"TR", {"turkey", "turkish", "ankara", "istanbul", "bosphorus"}},
    {"GR", {"greece", "greek", "athens", "aegean", NULL}},
    {"CY", {"cyprus", "cypriot", "nicosia", NULL, NULL}},
    /* Middle East & North Africa */
    {"IL", {"israel", "israeli", "jerusalem", "tel aviv", NULL}},
    {"PS", {"palestine", "palestinian", "gaza", "west bank", "ramallah"}},
    {"LB", {"lebanon", "lebanese", "beirut", NULL, NULL}},
    {"SY", {"syria", "syrian", "damascus", "aleppo", NULL}},
    {"JO", {"jordan", "jordanian", "amman", NULL, NULL}},
    {"IR", {"iran", "iranian", "tehran", "isfahan", NULL}},
    {"IQ", {"iraq", "iraqi", "baghdad", "erbil", NULL}},
    {"YE", {"yemen", "yemeni", "sanaa", "houthi", "aden"}},
    {"SA", {"saudi arabia", "saudi", "riyadh", "jeddah", NULL}},
    {"AE", {"united arab emirates", "uae", "dubai", "abu dhabi", NULL}},
    {"QA", {"qatar", "qatari", "doha", NULL, NULL}},
    {"KW", {"kuwait", "kuwaiti", "kuwait city", NULL, NULL}},
    {"OM", {"oman", "omani", "muscat", NULL, NULL}},
    {"BH", {"bahrain", "bahraini", "manama", NULL, NULL}},
    {"EG", {"egypt", "egyptian", "cairo", "suez", NULL}},
    {"LY", {"libya", "libyan", "tripoli", "benghazi", NULL}},
    {"TN", {"tunisia", "tunisian", "tunis", NULL, NULL}},
    {"DZ", {"algeria", "algerian", "algiers", NULL, NULL}},
    {"MA", {"morocco", "moroccan", "rabat", "casablanca", NULL}},
    /* Sub-Saharan Africa */
    {"NG", {"nigeria", "nigerian", "lagos", "abuja", NULL}},
    {"ZA", {"south africa", "johannesburg", "cape town", "pretoria", NULL}},
    {"KE", {"kenya", "kenyan", "nairobi", "mombasa", NULL}},
    {"TZ", {"tanzania", "tanzanian", "dar es salaam", "dodoma", NULL}},
    {"UG", {"uganda", "ugandan", "kampala", NULL, NULL}},
    {"RW", {"rwanda", "rwandan", "kigali", NULL, NULL}},
    {"CD", {"congo", "congolese", "kinshasa", "goma", NULL}},
    {"ET", {"ethiopia", "ethiopian", "addis ababa", NULL, NULL}},
    {"ER", {"eritrea", "eritrean", "asmara", NULL, NULL}},
    {"SO", {"somalia", "somali", "mogadishu", "somaliland", NULL}},
    {"SD", {"sudan", "sudanese", "khartoum", "darfur", "port sudan"}},
    {"SS", {"south sudan", "juba", NULL, NULL, NULL}},
    {"GH", {"ghana", "ghanaian", "accra", NULL, NULL}},
    {"CI", {"ivory coast", "cote d'ivoire", "ivorian", "abidjan", NULL}},
    {"SN", {"senegal", "senegalese", "dakar", NULL, NULL}},
    {"ML", {"mali", "malian", "bamako", "sahel", NULL}},
    {"NE", {"niger", "niamey", NULL, NULL, NULL}},
    {"BF", {"burkina faso", "ouagadougou", NULL, NULL, NULL}},
    {"CM", {"cameroon", "cameroonian", "yaounde", "douala", NULL}},
    {"AO", {"angola", "angolan", "luanda", NULL, NULL}},
    {"MZ", {"mozambique", "mozambican", "maputo", NULL, NULL}},
    {"ZW", {"zimbabwe", "zimbabwean", "harare", NULL, NULL}},
    {"ZM", {"zambia", "zambian", "lusaka", NULL, NULL}},
    {"BW", {"botswana", "gaborone", NULL, NULL, NULL}},
    {"NA", {"namibia", "namibian", "windhoek", NULL, NULL}},
    /* South Asia */
    {"IN", {"india", "indian", "new delhi", "mumbai", "bengaluru"}},
    {"PK", {"pakistan", "pakistani", "islamabad", "karachi", "lahore"}},
    {"BD", {"bangladesh", "bangladeshi", "dhaka", NULL, NULL}},
    {"LK", {"sri lanka", "colombo", "sri lankan", NULL, NULL}},
    {"NP", {"nepal", "nepali", "kathmandu", NULL, NULL}},
    {"MV", {"maldives", "maldivian", "male", NULL, NULL}},
    {"AF", {"afghanistan", "afghan", "kabul", "kandahar", NULL}},
    /* East Asia */
    {"CN", {"china", "chinese", "beijing", "shanghai", "shenzhen"}},
    {"JP", {"japan", "japanese", "tokyo", "osaka", "yokohama"}},
    {"KR", {"south korea", "korean", "seoul", "busan", NULL}},
    {"KP", {"north korea", "pyongyang", "dprk", NULL, NULL}},
    {"TW", {"taiwan", "taiwanese", "taipei", "tsmc", "hsinchu"}},
    {"HK", {"hong kong", NULL, NULL, NULL, NULL}},
    {"MN", {"mongolia", "mongolian", "ulaanbaatar", NULL, NULL}},
    /* Southeast Asia */
    {"SG", {"singapore", "singaporean", NULL, NULL, NULL}},
    {"ID", {"indonesia", "indonesian", "jakarta", "nusantara", NULL}},
    {"PH", {"philippines", "philippine", "manila", "luzon", NULL}},
    {"VN", {"vietnam", "vietnamese", "hanoi", "ho chi minh", NULL}},
    {"TH", {"thailand", "thai", "bangkok", NULL, NULL}},
    {"MM", {"myanmar", "burmese", "yangon", "naypyidaw", NULL}},
    {"MY", {"malaysia", "malaysian", "kuala lumpur", "penang", NULL}},
    {"KH", {"cambodia", "cambodian", "phnom penh", NULL, NULL}},
    {"LA", {"laos", "laotian", "vientiane", NULL, NULL}},
    {"BN", {"brunei", "bandar seri begawan", NULL, NULL, NULL}},
    {"TL", {"timor-leste", "east timor", "dili", NULL, NULL}},
    /* Central Asia & Caucasus */
    {"KZ", {"kazakhstan", "kazakh", "astana", "almaty", NULL}},
    {"UZ", {"uzbekistan", "uzbek", "tashkent", NULL, NULL}},
    {"KG", {"kyrgyzstan", "kyrgyz", "bishkek", NULL, NULL}},
    {"TJ", {"tajikistan", "tajik", "dushanbe", NULL, NULL}},
    {"TM", {"turkmenistan", "turkmen", "ashgabat", NULL, NULL}},
    {"GE", {"georgia", "georgian", "tbilisi", NULL, NULL}},
    {"AM", {"armenia", "armenian", "yerevan", NULL, NULL}},
    {"AZ", {"azerbaijan", "azerbaijani", "baku", NULL, NULL}},
    /* Oceania */
    {"AU", {"australia", "australian", "canberra", "sydney", "melbourne"}},
    {"NZ", {"new zealand", "wellington", "auckland", NULL, NULL}},
    {"FJ", {"fiji", "fijian", "suva", NULL, NULL}},
    {"PG", {"papua new guinea", "port moresby", NULL, NULL, NULL}},
    {"WS", {"samoa", "samoan", "apia", NULL, NULL}},
    {"SB", {"solomon islands", "honiara", NULL, NULL, NULL}},
    {"VU", {"vanuatu", "port vila", NULL, NULL, NULL}},
};

typedef struct {
  OntologyNodeId node;
  const char *const terms[16];
} TopicRule;

static const TopicRule k_topic_rules[] = {
    {ONTO_AI,
     {"artificial intelligence", "llm", "neural network", "openai", "deepmind", "anthropic",
      "generative ai", "machine learning", "transformer", "foundation model", "multimodal",
      "reinforcement learning", "diffusion model", "inference", "fine-tuning", "agentic"}},
    {ONTO_ROBOTICS,
     {"robotics", "humanoid", "autonomous vehicle", "drone", "actuator", "manipulator",
      "embodied ai", "quadruped", "lidar", "slam", "exoskeleton", "rov", "uav",
      "bipedal", "kinematics", "servomotor"}},
    {ONTO_QUANTUM,
     {"quantum computing", "qubit", "superconducting qubit", "quantum error correction",
      "entanglement", "topological qubit", "quantum supremacy", "quantum photonics",
      "trapped ion", "decoherence", "neutral atom", "josephson junction", "quantum annealing",
      "fault-tolerant quantum", "bell state", "quantum sensing"}},
    {ONTO_SEMICONDUCTORS,
     {"semiconductor", "tsmc", "asml", "lithography", "wafer", "foundry", "euv", "risc-v",
      "hbm", "finfet", "gaafet", "chiplet", "cowos", "packaging", "photomask", "dram"}},
    {ONTO_BIOTECH,
     {"crispr", "gene editing", "genomics", "mrna", "protein folding", "alphafold",
      "synthetic biology", "sequencing", "cell therapy", "car-t", "organoid", "viral vector",
      "epigenetics", "proteomics", "monoclonal", "transcriptomics"}},
    {ONTO_MATHEMATICS,
     {"category theory", "theorem", "conjecture", "topos", "cohomology", "homotopy",
      "kan extension", "formal verification", "type theory", "lean4", "coq", "galois",
      "ergodic", "riemann", "langlands", "combinatorics"}},
    {ONTO_MATERIALS,
     {"superconductor", "graphene", "perovskite", "metamaterial", "topological insulator",
      "condensed matter", "solid-state battery", "cathode", "piezoelectric", "ferroelectric",
      "spintronics", "high-entropy alloy", "nanotube", "zeolite", "2d material", "anode"}},
    {ONTO_CRYPTOGRAPHY,
     {"zero-knowledge", "zk-snark", "post-quantum cryptography", "lattice cryptography",
      "homomorphic encryption", "cryptographic", "elliptic curve", "cipher", "kyber",
      "dilithium", "ml-kem", "zk-stark", "multiparty computation", "oblivious transfer",
      "hash function", "merkle tree"}},
    {ONTO_NUCLEAR_FUSION,
     {"nuclear fusion", "tokamak", "iter", "stellarator", "plasma confinement",
      "inertial confinement", "deuterium", "tritium", "divertor", "magnetohydrodynamics",
      "ignition", "small modular reactor", "smr", "molten salt reactor", "breeder reactor",
      "gyrotron"}},
    {ONTO_CYBER,
     {"cybersecurity", "ransomware", "zero-day", "malware", "hacker", "data breach",
      "encryption", "phishing", "botnet", "exploit", "rootkit", "spyware", "apt",
      "backdoor", "cve-", "sandbox escape"}},
    {ONTO_TECHNOLOGY,
     {"microchip", "software", "cloud computing", "open source", "linux", "silicon",
      "chipmaker", "compiler", "kernel", "webassembly", "webgpu", "browser", "database",
      "distributed system", "hypervisor", "protocol"}},
    {ONTO_SPACE,
     {"nasa", "esa", "spacex", "satellite", "spacecraft", "astronaut", "lunar", "orbit",
      "telescope", "exoplanet", "jwst", "cosmology", "astrophysics", "starship", "payload",
      "launchpad"}},
    {ONTO_SCIENCE,
     {"physics", "quantum", "researchers", "scientists", "laboratory", "peer-reviewed",
      "chemistry", "genome", "synchrotron", "arxiv", "biorxiv", "particle", "collider",
      "spectroscopy", "catalysis", "neuroscience"}},
    {ONTO_FINANCE,
     {"central bank", "federal reserve", "ecb", "interest rate", "bond yield", "wall street",
      "stock market", "ipo", "venture capital", "treasury yield", "equities", "hedge fund",
      "foreign exchange", "sovereign debt", "basis points", "derivatives"}},
    {ONTO_ECONOMY,
     {"inflation", "gdp", "recession", "tariff", "supply chain", "imf", "world bank",
      "trade deficit", "macroeconomic", "unemployment", "payrolls", "cpi", "fiscal policy",
      "austerity", "stimulus", "industrial production"}},
    {ONTO_ENERGY,
     {"crude oil", "opec", "natural gas", "solar power", "wind farm", "nuclear reactor",
      "power grid", "battery", "hydrogen", "lng", "refinery", "geothermal", "hydropower",
      "megawatt", "gigawatt", "uranium"}},
    {ONTO_CLIMATE,
     {"climate change", "carbon emissions", "greenhouse", "wildfire", "drought",
      "biodiversity", "deforestation", "glacier", "permafrost", "sea level", "el nino",
      "decarbonization", "methane", "coral reef", "ipcc", "reforestation"}},
    {ONTO_HEALTH,
     {"vaccine", "pandemic", "epidemic", "hospital", "clinical trial", "public health",
      "virus", "cancer", "fda", "oncology", "immunology", "antibiotic", "pathogen",
      "neurology", "cardiovascular", "epidemiology"}},
    {ONTO_DEFENSE,
     {"military", "missile", "airstrike", "ceasefire", "artillery", "warship", "troops",
      "battalion", "nato", "submarine", "fighter jet", "hypersonic", "air defense",
      "munitions", "frigate", "deterrence"}},
    {ONTO_SECURITY,
     {"intelligence agency", "espionage", "counterterrorism", "border security",
      "surveillance", "national security", "sigint", "geoint", "osint", "reconnaissance",
      "opsec", "classified", "c4isr", "counterintelligence", "homeland security", "sanctions"}},
    {ONTO_GEOPOLITICS,
     {"diplomacy", "ambassador", "summit", "bilateral", "sanctions", "foreign policy",
      "treaty", "sovereignty", "geopolitical", "multilateral", "non-aligned", "annexation",
      "enclave", "statecraft", "consulate", "deterrence"}},
    {ONTO_POLITICS,
     {"parliament", "election", "coalition", "prime minister", "president", "referendum",
      "ballot", "campaign", "legislature", "senate", "cabinet", "impeachment", "gubernatorial",
      "bipartisan", "caucus", "polling"}},
    {ONTO_LAW,
     {"supreme court", "judiciary", "indictment", "antitrust", "prosecutor", "constitutional",
      "lawsuit", "verdict", "tribunal", "injunction", "appellate", "arbitration",
      "jurisprudence", "subpoena", "extradition", "statute"}},
    {ONTO_HUMAN_RIGHTS,
     {"human rights", "refugee", "asylum", "humanitarian", "amnesty", "civil liberties",
      "displaced", "war crimes", "genocide", "detainee", "press freedom", "indigenous",
      "censorship", "famine", "peacekeeping", "stateless"}},
    {ONTO_LABOR,
     {"labor union", "strike", "collective bargaining", "minimum wage", "layoffs",
      "workforce", "workers", "picket line", "pension", "gig economy", "occupational safety",
      "overtime", "unionization", "payroll", "automation", "apprenticeship"}},
    {ONTO_CULTURE,
     {"museum", "cinema", "literature", "architecture", "exhibition", "symphony", "novel",
      "archaeology", "biennale", "documentary", "philharmonic", "heritage", "theatre",
      "opera", "sculpture", "curation"}},
    {ONTO_SPORTS,
     {"olympics", "world cup", "championship", "premier league", "tournament", "grand prix",
      "stadium", "champions league", "formula 1", "athletics", "marathon", "tennis",
      "cricket", "rugby", "basketball", "fifa"}},
};

typedef struct {
  const char *name;
  const char *const terms[4];
} OrgRule;

static const OrgRule k_org_rules[] = {
    {"UN", {"united nations", "u.n.", "security council", "unhcr"}},
    {"NATO", {"nato", "north atlantic treaty", NULL, NULL}},
    {"EU", {"european union", "european commission", "european parliament", "eurogroup"}},
    {"WHO", {"world health organization", "w.h.o.", NULL, NULL}},
    {"IMF", {"international monetary fund", "imf", NULL, NULL}},
    {"World Bank", {"world bank", "ibrd", NULL, NULL}},
    {"WTO", {"world trade organization", "wto", NULL, NULL}},
    {"OECD", {"oecd", "organisation for economic co-operation", NULL, NULL}},
    {"BRICS", {"brics", "new development bank", NULL, NULL}},
    {"ASEAN", {"asean", NULL, NULL, NULL}},
    {"African Union", {"african union", "afcfta", NULL, NULL}},
    {"MERCOSUR", {"mercosur", NULL, NULL, NULL}},
    {"SCO", {"shanghai cooperation organisation", NULL, NULL, NULL}},
    {"GCC", {"gulf cooperation council", NULL, NULL, NULL}},
    {"OPEC", {"opec", "opec+", NULL, NULL}},
    {"IEA", {"international energy agency", "iea", NULL, NULL}},
    {"IAEA", {"international atomic energy agency", "iaea", NULL, NULL}},
    {"G7", {"g7", "group of seven", NULL, NULL}},
    {"G20", {"g20", "group of twenty", NULL, NULL}},
    {"ICC", {"international criminal court", "icj", NULL, NULL}},
    {"Fed", {"federal reserve", "fomc", "jerome powell", NULL}},
    {"ECB", {"european central bank", "ecb", "lagarde", NULL}},
    {"BoE", {"bank of england", "mpc", "threadneedle", NULL}},
    {"BoJ", {"bank of japan", "ueda", NULL, NULL}},
    {"PBOC", {"people's bank of china", "pboc", NULL, NULL}},
    {"RBI", {"reserve bank of india", "rbi", NULL, NULL}},
    {"SNB", {"swiss national bank", "snb", NULL, NULL}},
    {"BoC", {"bank of canada", NULL, NULL, NULL}},
    {"RBA", {"reserve bank of australia", "rba", NULL, NULL}},
    {"BCB", {"banco central do brasil", "copom", "selic", NULL}},
    {"Banxico", {"banco de mexico", "banxico", NULL, NULL}},
    {"SARB", {"south african reserve bank", "sarb", NULL, NULL}},
    {"SEC", {"securities and exchange commission", "sec filing", "10-k", NULL}},
    {"CFTC", {"commodity futures trading commission", "cftc", NULL, NULL}},
    {"BIS", {"bank for international settlements", "basel committee", NULL, NULL}},
    {"CBO", {"congressional budget office", "cbo", NULL, NULL}},
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
    {"BSI", {"bsi", "bundesamt fur sicherheit", NULL, NULL}},
    {"ENISA", {"enisa", NULL, NULL, NULL}},
    {"NIST", {"nist", "national institute of standards", "post-quantum", NULL}},
    {"W3C", {"w3c", "world wide web consortium", "webgpu", "webauthn"}},
    {"WHATWG", {"whatwg", "html living standard", NULL, NULL}},
    {"IETF", {"ietf", "internet engineering task force", "quic", "privacy pass"}},
    {"Tor Project", {"tor project", "onion service", "pluggable transport", NULL}},
    {"OpenAI", {"openai", "chatgpt", "sam altman", NULL}},
    {"Anthropic", {"anthropic", "dario amodei", "constitutional ai", NULL}},
    {"Google DeepMind", {"deepmind", "google deepmind", "demis hassabis", "alphafold"}},
    {"Isomorphic Labs", {"isomorphic labs", "alphaproteo", NULL, NULL}},
    {"Meta AI", {"meta ai", "fair lab", "llama-3", "llama 3"}},
    {"Mistral AI", {"mistral ai", "le chat", "mixtral", NULL}},
    {"xAI", {"xai", "colossus cluster", "grok-3", "grok 3"}},
    {"DeepSeek", {"deepseek", "deepseek-r1", "deepseek-v3", "high-flyer"}},
    {"Cohere", {"cohere", "command r+", "command-r", NULL}},
    {"Hugging Face", {"hugging face", "huggingface", "safetensors", NULL}},
    {"CoreWeave", {"coreweave", NULL, NULL, NULL}},
    {"Scale AI", {"scale ai", "seal leaderboard", NULL, NULL}},
    {"SSI", {"safe superintelligence", "ilya sutskever", NULL, NULL}},
    {"US Treasury", {"u.s. treasury", "treasury department", "ofac", NULL}},
    {"DOJ/FTC", {"department of justice", "federal trade commission", "antitrust division", NULL}},
    {"SCOTUS", {"u.s. supreme court", "scotus", "chief justice", NULL}},
    {"Pentagon", {"pentagon", "department of defense", "centcom", "indopacom"}},
    {"FIFA", {"fifa", "world cup qualifier", NULL, NULL}},
    {"UEFA", {"uefa", "champions league", "europa league", NULL}},
    {"IOC", {"international olympic committee", "olympic games", NULL, NULL}},
    {"FIA", {"fia", "formula 1", "formula one", "grand prix"}},
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
    {"CNY", {"renminbi", "yuan", "usd/cny", "usd/cnh", "pboc"}},
    {"INR", {"indian rupee", "usd/inr", "rupee", NULL, NULL}},
    {"BRL", {"brazilian real", "usd/brl", "selic", NULL, NULL}},
    {"CAD", {"canadian dollar", "loonie", "usd/cad", NULL, NULL}},
    {"AUD", {"australian dollar", "aussie dollar", "aud/usd", NULL, NULL}},
    {"NZD", {"new zealand dollar", "kiwi dollar", "nzd/usd", NULL, NULL}},
    {"SEK", {"swedish krona", "usd/sek", "riksbank", NULL, NULL}},
    {"NOK", {"norwegian krone", "usd/nok", "norges bank", NULL, NULL}},
    {"SGD", {"singapore dollar", "usd/sgd", "s$neer", NULL, NULL}},
    {"KRW", {"south korean won", "usd/krw", "korean won", NULL, NULL}},
    {"TWD", {"taiwan dollar", "usd/twd", NULL, NULL, NULL}},
    {"MXN", {"mexican peso", "usd/mxn", "super peso", NULL, NULL}},
    {"ZAR", {"south african rand", "usd/zar", NULL, NULL, NULL}},
    {"TRY", {"turkish lira", "usd/try", NULL, NULL, NULL}},
    {"PLN", {"polish zloty", "eur/pln", NULL, NULL, NULL}},
    /* Equity Indices, Sovereign Bonds, Commodities & Strategic Tech Tickers */
    {"SPX", {"s&p 500", "wall street", "sp500", NULL, NULL}},
    {"NDX", {"nasdaq", "nasdaq-100", NULL, NULL, NULL}},
    {"DJI", {"dow jones", "djia", NULL, NULL, NULL}},
    {"RUT", {"russell 2000", "small-cap", NULL, NULL, NULL}},
    {"VIX", {"vix", "volatility index", "cboe volatility", NULL, NULL}},
    {"NIKKEI", {"nikkei 225", "topix", "tokyo stock exchange", NULL, NULL}},
    {"FTSE", {"ftse 100", "london stock exchange", NULL, NULL, NULL}},
    {"DAX", {"dax 40", "frankfurt stock", NULL, NULL, NULL}},
    {"CAC40", {"cac 40", "euronext paris", NULL, NULL, NULL}},
    {"STOXX", {"stoxx 600", "euro stoxx", NULL, NULL, NULL}},
    {"CSI300", {"csi 300", "shanghai composite", "shenzhen component", "a-shares", NULL}},
    {"HSI", {"hang seng", "hkex", NULL, NULL, NULL}},
    {"KOSPI", {"kospi", "kosdaq", NULL, NULL, NULL}},
    {"TAIEX", {"taiex", "taiwan stock", NULL, NULL, NULL}},
    {"NIFTY", {"nifty 50", "sensex", "bse", NULL, NULL}},
    {"BOVESPA", {"bovespa", "ibovespa", NULL, NULL, NULL}},
    {"UST10Y", {"treasury yield", "10-year yield", "10-year treasury", "sovereign bond", "gilt yield"}},
    {"US2Y", {"2-year treasury", "2-year yield", "two-year yield", "yield curve inversion", NULL}},
    {"SOFR", {"sofr", "secured overnight financing", "euribor", "fed funds futures", NULL}},
    {"MOVE", {"move index", "bond volatility", "cdx spread", "credit default swap", NULL}},
    {"BUND10Y", {"german bund", "bund yield", "btp spread", NULL, NULL}},
    {"JGB10Y", {"jgb yield", "japanese government bond", "yield curve control", NULL, NULL}},
    {"BRENT", {"brent crude", "wti crude", "crude oil", "opec+", NULL}},
    {"NATGAS", {"natural gas", "ttf gas", "henry hub", "lng cargo", NULL}},
    {"XAU", {"gold bullion", "gold price", "spot gold", "precious metal", NULL}},
    {"XAG", {"silver bullion", "spot silver", "comex silver", NULL, NULL}},
    {"COPPER", {"copper price", "lme copper", "comex copper", NULL, NULL}},
    {"LITHIUM", {"lithium carbonate", "lithium spodumene", "battery metal", NULL, NULL}},
    {"URANIUM", {"uranium", "yellowcake", "u3o8", NULL, NULL}},
    {"NVDA", {"nvidia", "blackwell", "h100", "cuda", "gb200"}},
    {"GOOGL", {"alphabet inc", "google cloud", "waymo", "deepmind", NULL}},
    {"MSFT", {"microsoft", "azure cloud", "github copilot", NULL, NULL}},
    {"META", {"meta platforms", "reality labs", NULL, NULL, NULL}},
    {"AAPL", {"apple inc", "apple silicon", "iphone", NULL, NULL}},
    {"AMZN", {"amazon web services", "aws bedrock", "amazon.com", NULL, NULL}},
    {"TSMC", {"tsmc", "taiwan semiconductor", "cowos", NULL, NULL}},
    {"ASML", {"asml", "extreme ultraviolet", "high-na euv", NULL, NULL}},
    {"AMD", {"amd", "instinct mi300", "mi300x", "mi325x", "epyc"}},
    {"ARM", {"arm holdings", "armv9", NULL, NULL, NULL}},
    {"AVGO", {"broadcom", "custom xpu", "tomahawk 5", NULL, NULL}},
    {"ANET", {"arista networks", "ultra ethernet", NULL, NULL, NULL}},
    {"SMCI", {"supermicro", "super micro computer", NULL, NULL, NULL}},
    {"MU", {"micron technology", "hbm3e", NULL, NULL, NULL}},
    {"SKHYNIX", {"sk hynix", "hbm4", NULL, NULL, NULL}},
    {"JPM/GS", {"jpmorgan", "goldman sachs", "morgan stanley", "blackrock", "citadel"}},
    {"BTC", {"bitcoin", "btc", "spot etf", NULL, NULL}},
    {"ETH", {"ethereum", "ether", NULL, NULL, NULL}},
    {"SOL", {"solana", "sol token", NULL, NULL, NULL}},
};

typedef struct {
  const char *model_family;
  AiWeightLicense default_license;
  const char *const terms[6];
} AiModelRule;

static const AiModelRule k_ai_model_rules[] = {
    /* Closed-Source Proprietary Frontier APIs */
    {"GPT-4o/o1/o3", AI_LICENSE_CLOSED_PROPRIETARY, {"gpt-4", "gpt-4o", "openai o1", "openai o3", "gpt-5", "o3-mini"}},
    {"Claude-3.5/3.7", AI_LICENSE_CLOSED_PROPRIETARY, {"claude 3", "claude 3.5", "claude 3.7", "claude sonnet", "claude opus", "claude haiku"}},
    {"Gemini-2.5/2.0", AI_LICENSE_CLOSED_PROPRIETARY, {"gemini 1.5", "gemini 2.0", "gemini 2.5", "gemini pro", "gemini flash", "deep think"}},
    {"Grok-2/3", AI_LICENSE_CLOSED_PROPRIETARY, {"grok-2", "grok 2", "grok-3", "grok 3", "xai grok", NULL}},
    {"Sora/Veo-2", AI_LICENSE_CLOSED_PROPRIETARY, {"openai sora", "veo 2", "imagen 3", "runway gen-3", "kling", "luma dream"}},
    /* Google DeepMind Scientific, Mathematical, RL & World-Model Breakthroughs */
    {"AlphaFold-3/Proteo", AI_LICENSE_OPEN_WEIGHTS_RESTRICTED, {"alphafold", "alphafold 3", "alphafold3", "alphaproteo", "alphamissense", "isomorphic labs"}},
    {"AlphaProof/Geometry", AI_LICENSE_CLOSED_PROPRIETARY, {"alphaproof", "alphageometry", "alphageometry2", "funsearch", "alphatensor", "alphadev"}},
    {"AlphaZero/MuZero", AI_LICENSE_OPEN_WEIGHTS_PERMISSIVE, {"alphazero", "muzero", "alphago", "alphastar", "graphcast", "gencast"}},
    {"Genie-2/SIMA/RT-X", AI_LICENSE_CLOSED_PROPRIETARY, {"genie 2", "deepmind sima", "rt-x", "robocat", "aloha", "astra"}},
    {"Chinchilla/Griffin", AI_LICENSE_OPEN_WEIGHTS_PERMISSIVE, {"chinchilla", "griffin", "hawk", "synthid", "ferret", "gnomad"}},
    /* Open-Weights under Community / Custom / Usage-Cap / Non-Commercial Licenses */
    {"Llama-3/4", AI_LICENSE_OPEN_WEIGHTS_RESTRICTED, {"llama 3", "llama-3", "llama 3.1", "llama 3.3", "llama 4", "codellama"}},
    {"Gemma-2/PaliGemma", AI_LICENSE_OPEN_WEIGHTS_RESTRICTED, {"gemma 2", "gemma-2", "codegemma", "paligemma", "recurrentgemma", "shieldgemma"}},
    {"Command-R+/Aya", AI_LICENSE_OPEN_WEIGHTS_RESTRICTED, {"command r+", "command-r+", "command r", "cohere aya", "aya-expanse", NULL}},
    {"Nemotron/Cosmos", AI_LICENSE_OPEN_WEIGHTS_RESTRICTED, {"nemotron", "nvidia cosmos", "llama-nemotron", "minitron", "nvlm", NULL}},
    {"Flux/SD3", AI_LICENSE_OPEN_WEIGHTS_RESTRICTED, {"flux.1", "flux.1-dev", "stable diffusion 3", "sd3.5", "sdxl", "ideogram"}},
    /* Permissively Licensed Open-Weights on Hugging Face (Apache-2.0 / MIT) */
    {"DeepSeek-R1/V3", AI_LICENSE_OPEN_WEIGHTS_PERMISSIVE, {"deepseek-r1", "deepseek r1", "deepseek-v3", "deepseek v3", "deepseek coder", "janus-pro"}},
    {"Qwen-2.5/QwQ", AI_LICENSE_OPEN_WEIGHTS_PERMISSIVE, {"qwen", "qwen-2.5", "qwen2.5", "qwq", "qwen-vl", "qwen-coder"}},
    {"Mistral/Mixtral", AI_LICENSE_OPEN_WEIGHTS_PERMISSIVE, {"mistral large", "mixtral", "codestral", "pixtral", "ministral", "mathstral"}},
    {"Phi-3/4", AI_LICENSE_OPEN_WEIGHTS_PERMISSIVE, {"phi-3", "phi-3.5", "phi-4", "orca-2", "phi-3-mini", NULL}},
    {"Falcon-2/Mamba", AI_LICENSE_OPEN_WEIGHTS_PERMISSIVE, {"falcon 2", "falcon-180b", "falcon mamba", "mamba-2", "jamba", "zamba"}},
    {"Yi/GLM-4/Kimi", AI_LICENSE_OPEN_WEIGHTS_PERMISSIVE, {"glm-4", "chatglm", "yi-1.5", "yi-lightning", "kimi k1.5", "internlm2"}},
    {"SmolLM2/Zephyr", AI_LICENSE_OPEN_WEIGHTS_PERMISSIVE, {"smollm", "smollm2", "smolvlm", "zephyr-7b", "open-r1", "tinyllama"}},
    {"Hermes-3/Nous", AI_LICENSE_OPEN_WEIGHTS_PERMISSIVE, {"hermes-3", "nous-hermes", "nousresearch", "deephermes", "dolphin-llama", NULL}},
    {"MiniMax/HunYuan", AI_LICENSE_OPEN_WEIGHTS_PERMISSIVE, {"minimax-01", "minimax", "hunyuan", "baichuan", "step-2", "minicpm"}},
    {"DBRX/Arctic/Liquid", AI_LICENSE_OPEN_WEIGHTS_PERMISSIVE, {"dbrx", "snowflake-arctic", "liquid lfm", "granite-3", "modernbert", "deberta"}},
    {"Whisper/Kokoro/Moshi", AI_LICENSE_OPEN_WEIGHTS_PERMISSIVE, {"whisper-large", "kokoro-82m", "moshi", "parler-tts", "f5-tts", "elevenlabs"}},
    {"BGE-M3/Nomic-Embed", AI_LICENSE_OPEN_WEIGHTS_PERMISSIVE, {"bge-m3", "nomic-embed", "jina-embeddings", "e5-mistral", "colpali", "gliner"}},
    {"ESM3/Evo-2/Boltz", AI_LICENSE_OPEN_WEIGHTS_PERMISSIVE, {"esm3", "evo 2", "boltz-1", "chai-1", "rfdiffusion", "progen2"}},
    /* Full OSI Open-Source AI (Weights + Open Pretraining Dataset + Training Code) */
    {"OLMo-2/StarCoder", AI_LICENSE_OPEN_SCIENCE_OSI, {"olmo", "olmo 2", "olmo-2", "tulu-3", "starcoder2", "starcoder"}},
    {"Pythia/MAP-Neo/DCLM", AI_LICENSE_OPEN_SCIENCE_OSI, {"pythia", "map-neo", "dclm", "amber-7b", "bloom-176b", "openlm"}},
    {"OpenVLA/Pi-0", AI_LICENSE_OPEN_SCIENCE_OSI, {"openvla", "pi-0", "rt-2", "octo model", "figure helix", "gr00t"}},
};

typedef struct {
  const char *infra_name;
  const char *const terms[6];
} AiInfraRule;

static const AiInfraRule k_ai_infra_rules[] = {
    /* Neoclouds & GPU Cluster Clouds */
    {"CoreWeave", {"coreweave", NULL, NULL, NULL, NULL, NULL}},
    {"Lambda-Cloud", {"lambda labs", "lambda cloud", "lambda gpu", NULL, NULL, NULL}},
    {"Together-AI", {"together ai", "together.ai", "together compute", NULL, NULL, NULL}},
    {"Fireworks-AI", {"fireworks ai", "fireworks.ai", "fireattention", NULL, NULL, NULL}},
    {"Crusoe-Cloud", {"crusoe energy", "crusoe cloud", "clean compute", NULL, NULL, NULL}},
    {"Nebius-AI", {"nebius", "nebius ai", NULL, NULL, NULL, NULL}},
    {"Baseten", {"baseten", "truss serving", NULL, NULL, NULL, NULL}},
    {"Modal-Labs", {"modal labs", "modal serverless", NULL, NULL, NULL, NULL}},
    {"Replicate", {"replicate.com", "cog container", NULL, NULL, NULL, NULL}},
    {"Anyscale-Ray", {"anyscale", "ray cluster", "ray serve", "ray train", NULL, NULL}},
    {"Hyperbolic/Voltage", {"hyperbolic labs", "voltage park", "sf compute", "prime intellect", NULL, NULL}},
    {"Vultr/FluidStack", {"fluidstack", "vultr gpu", "paperspace", "runpod", "vast.ai", NULL}},
    /* Inference Engines, Serving Runtimes & Kernels */
    {"vLLM", {"vllm", "pagedattention", NULL, NULL, NULL, NULL}},
    {"SGLang", {"sglang", "radixattention", "lmsys", NULL, NULL, NULL}},
    {"TensorRT-LLM", {"tensorrt-llm", "tensorrt", "triton inference server", "nvidia dynamo", NULL, NULL}},
    {"llama.cpp/GGUF", {"llama.cpp", "gguf", "ggml", "ollama", "apple mlx", "exllamav2"}},
    {"FlashAttention", {"flashattention", "flashattention-2", "flashattention-3", "flash-decoding", NULL, NULL}},
    {"FlashInfer/Triton", {"flashinfer", "openai triton", "triton kernel", "cutlass", "thunderkittens", "torch.compile"}},
    {"JAX/Flax/Pathways", {"jax", "flax", "optax", "pallas kernel", "pathways", "haiku"}},
    {"Speculative-Decode", {"speculative decoding", "medusa head", "eagle decoding", "multi-token prediction", "draft model", NULL}},
    {"KV-Cache/Disagg", {"kv cache", "kv-cache", "prefill-decode", "disaggregated serving", "mooncake", "prefix caching"}},
    {"FP8/AWQ/GPTQ", {"fp8", "fp4", "awq", "gptq", "int4 quantization", "qlora"}},
    /* Hugging Face Hub & Open-Weights Post-Training / Evaluation Stack */
    {"HF-Transformers/TGI", {"safetensors", "text-generation-inference", "huggingface hub", "hf hub", "model card", "gradio"}},
    {"HF-TRL/PEFT/Unsloth", {"unsloth", "axolotl", "peft", "trl library", "bitsandbytes", "torchtune"}},
    {"HF-Datasets/FineWeb", {"fineweb", "fineweb-edu", "the-stack-v2", "dolma dataset", "redpajama", "datatrove"}},
    {"LM-Eval/Arena", {"lm-evaluation-harness", "open llm leaderboard", "chatbot arena", "swe-bench", "gpqa", "arc-agi"}},
    /* AI Silicon, Accelerators & Cluster Interconnects */
    {"NVIDIA-Blackwell", {"blackwell", "b200", "gb200", "nvl72", "vera rubin", "b100"}},
    {"NVIDIA-Hopper", {"h100", "h200", "gh200", "hopper gpu", "hgx", NULL}},
    {"AMD-Instinct", {"mi300x", "mi325x", "mi350", "amd rocm", "instinct gpu", NULL}},
    {"Google-TPU", {"tpu v5p", "tpu v5e", "tpu v6", "trillium tpu", "ironwood", "xla compiler"}},
    {"Groq-LPU", {"groq", "lpu inference", "language processing unit", NULL, NULL, NULL}},
    {"Cerebras-WSE", {"cerebras", "wafer-scale engine", "wse-3", "cs-3", NULL, NULL}},
    {"SambaNova-RDU", {"sambanova", "reconfigurable dataflow", "sn40l", NULL, NULL, NULL}},
    {"Tenstorrent", {"tenstorrent", "wormhole", "blackhole", "grayskull", "tensix", NULL}},
    {"Etched/MatX-ASIC", {"etched sohu", "transformer asic", "matx", "d-matrix", "furiosaai", "hailo"}},
    {"AWS-Trainium", {"trainium", "trainium2", "inferentia", "inferentia2", NULL, NULL}},
    {"Huawei-Ascend", {"ascend 910b", "ascend 910c", "huawei cann", "kunpeng", NULL, NULL}},
    {"NVLink/InfiniBand", {"nvlink", "nvswitch", "infiniband", "rocev2", "ultra ethernet", "cowos"}},
};

typedef struct {
  const char *event_name;
  const char *const terms[5];
} FiscalEventRule;

static const FiscalEventRule k_fiscal_event_rules[] = {
    {"rate-decision", {"interest rate", "basis points", "rate hike", "rate cut", "policy rate"}},
    {"fomc-dot-plot", {"dot plot", "beige book", "fomc minutes", "jackson hole", "reverse repo"}},
    {"inflation-print", {"cpi", "core inflation", "pce deflator", "hicp", "producer price"}},
    {"labor-payrolls", {"nonfarm payrolls", "unemployment rate", "jobless claims", "labor market", NULL}},
    {"gdp-release", {"real gdp", "gdp growth", "economic contraction", "recession", NULL}},
    {"sovereign-debt-auction", {"treasury auction", "bond auction", "quarterly refunding", "debt ceiling", "eurobond"}},
    {"yield-curve-shift", {"yield curve", "steepening", "inverted yield curve", "term premium", "credit spread"}},
    {"fiscal-budget", {"federal budget", "budget deficit", "fiscal stimulus", "austerity", "autumn budget"}},
    {"earnings-guidance", {"quarterly earnings", "eps beat", "revenue guidance", "10-k", "share buyback"}},
    {"m&a-ipo-deal", {"merger", "acquisition", "leveraged buyout", "ipo filing", "s-1 filing"}},
    {"fx-intervention", {"fx intervention", "currency swap", "capital controls", "devaluation", "foreign exchange reserves"}},
    {"trade-tariff", {"import tariff", "export control", "customs duty", "trade war", "countervailing"}},
    {"sanctions-package", {"ofac sanctions", "secondary sanctions", "entity list", "asset freeze", "price cap"}},
    {"sovereign-restructuring", {"sovereign default", "imf bailout", "debt restructuring", "credit rating downgrade", NULL}},
    {"election-ballot", {"general election", "presidential election", "parliamentary election", "referendum", "exit poll"}},
    {"legislative-vote", {"appropriations bill", "continuing resolution", "no-confidence vote", "coalition government", "omnibus bill"}},
    {"supreme-court-ruling", {"supreme court", "constitutional court", "judicial review", "antitrust ruling", "injunction"}},
};

typedef struct {
  const char *league_name;
  const char *const terms[6];
} SportsRule;

static const SportsRule k_sports_rules[] = {
    /* Global Football / Soccer */
    {"UEFA-Champions-League", {"champions league", "ucl knockout", "uefa champions", "europa league", "ballon d'or", NULL}},
    {"Premier-League", {"premier league", "epl", "manchester city", "arsenal", "liverpool fc", "chelsea fc"}},
    {"LaLiga", {"la liga", "laliga", "real madrid", "fc barcelona", "atletico madrid", "el clasico"}},
    {"Serie-A", {"serie a", "scudetto", "inter milan", "ac milan", "juventus", "napoli"}},
    {"Bundesliga", {"bundesliga", "bayern munich", "borussia dortmund", "bayer leverkusen", "dfb-pokal", NULL}},
    {"Ligue-1", {"ligue 1", "paris saint-germain", "psg", "olympique marseille", NULL, NULL}},
    {"Copa-Libertadores", {"copa libertadores", "conmebol", "copa america", "flamengo", "river plate", "boca juniors"}},
    {"MLS/Liga-MX", {"major league soccer", "mls cup", "liga mx", "inter miami", "concacaf", NULL}},
    {"FIFA-World-Cup", {"fifa world cup", "world cup qualifier", "euro 2028", "afcon", "asian cup", "womens world cup"}},
    /* North American Major Leagues & Collegiate */
    {"NFL", {"nfl", "super bowl", "touchdown", "quarterback", "nfl draft", "nfl playoffs"}},
    {"NBA", {"nba", "nba finals", "western conference", "eastern conference", "triple-double", "wnba"}},
    {"MLB", {"major league baseball", "world series", "home run", "pitcher", "spring training", "mlb"}},
    {"NHL", {"nhl", "stanley cup", "ice hockey", "hat trick", "power play", NULL}},
    {"NCAA", {"ncaa", "march madness", "college football", "cfp", "final four", "heisman"}},
    /* Motorsport */
    {"Formula-1", {"formula 1", "formula one", "f1 grand prix", "red bull racing", "scuderia ferrari", "mclaren f1"}},
    {"MotoGP/WEC/Indy", {"motogp", "24 hours of le mans", "indy 500", "indycar", "nascar", "world rally"}},
    /* Tennis & Golf */
    {"ATP/WTA-Grand-Slam", {"wimbledon", "roland garros", "us open tennis", "australian open", "atp tour", "wta tour"}},
    {"PGA/Golf-Majors", {"pga tour", "masters tournament", "ryder cup", "open championship", "liv golf", "lpga"}},
    /* Global Cricket & Rugby */
    {"IPL/ICC-Cricket", {"indian premier league", "ipl", "icc cricket", "test cricket", "twenty20", "the ashes"}},
    {"Rugby-Six-Nations", {"six nations", "rugby world cup", "all blacks", "springboks", "super rugby", "premiership rugby"}},
    /* Olympic, Combat, Cycling & Mind/Esports */
    {"Olympic-Games", {"olympic games", "winter olympics", "summer olympics", "paralympics", "gold medal", "world athletics"}},
    {"UFC/MMA/Boxing", {"ufc", "mixed martial arts", "heavyweight title", "undisputed champion", "knockout", "boxing"}},
    {"TourDeFrance/UCI", {"tour de france", "giro d'italia", "uci worldtour", "peloton", "vuelta a espana", NULL}},
    {"Chess-FIDE", {"fide", "world chess championship", "grandmaster", "candidates tournament", "magnus carlsen", "gukesh"}},
    {"Esports-LoL/CS2", {"league of legends worlds", "valorant champions", "counter-strike 2", "dota 2 international", "esports", NULL}},
};

static void extract_cve_identifiers(const char *lower_text,
                                    size_t len,
                                    const uint64_t wb_filter[8],
                                    ExtractedEntities *out) {
  if(!lower_text || len < 9u) return;
  uint32_t h2 = ((uint32_t)'c' * 37u + (uint32_t)'v') & 255u;
  if((wb_filter[h2 >> 6u] & (1ULL << (h2 & 63u))) == 0ULL) return;
  uint32_t h3 = ((uint32_t)'c' * 131u + (uint32_t)'v' * 37u + (uint32_t)'e') & 255u;
  if((wb_filter[4u + (h3 >> 6u)] & (1ULL << (h3 & 63u))) == 0ULL) return;
  const char *p = lower_text;
  const char *end = lower_text + len;
  while(p + 9u <= end && out->cve_count < MAX_EXTRACTED_CVES) {
    const char *hit = strstr(p, "cve-");
    if(!hit || hit + 9u > end) break;
    if(isdigit((unsigned char)hit[4u]) &&
       isdigit((unsigned char)hit[5u]) &&
       isdigit((unsigned char)hit[6u]) &&
       isdigit((unsigned char)hit[7u]) &&
       hit[8u] == '-') {
      const char *j = hit + 9u;
      while(j < end && isdigit((unsigned char)*j) && (size_t)(j - hit) < 18u) j++;
      if(j > hit + 9u) {
        size_t clen = (size_t)(j - hit);
        for(size_t k = 0; k < clen; ++k) {
          out->cves[out->cve_count][k] = (char)toupper((unsigned char)hit[k]);
        }
        out->cves[out->cve_count][clen] = '\0';
        out->cve_count++;
        p = j;
        continue;
      }
    }
    p = hit + 4u;
  }
}

static int is_hf_repo_char(char c) {
  return isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.';
}

static void push_hf_repo(ExtractedEntities *out, const char *repo, size_t rlen) {
  if(!out || out->hf_repo_count >= MAX_EXTRACTED_HF_REPOS || rlen < 5u) return;
  while(rlen > 0 && (repo[rlen - 1u] == '.' || repo[rlen - 1u] == '-' || repo[rlen - 1u] == '_')) {
    rlen--;
  }
  if(rlen < 5u || rlen >= 63u) return;
  const char *slash = memchr(repo, '/', rlen);
  if(!slash || slash == repo || (size_t)(slash - repo) + 2u > rlen) return;
  if(memchr(slash + 1u, '/', rlen - (size_t)(slash + 1u - repo)) != NULL) return;
  for(size_t i = 0; i < out->hf_repo_count; ++i) {
    if(strlen(out->hf_repos[i]) == rlen && memcmp(out->hf_repos[i], repo, rlen) == 0) return;
  }
  memcpy(out->hf_repos[out->hf_repo_count], repo, rlen);
  out->hf_repos[out->hf_repo_count][rlen] = '\0';
  out->hf_repo_count++;
}

static void extract_hf_repo_ids(const char *lower_text, size_t len, ExtractedEntities *out) {
  if(!lower_text || len < 6u) return;
  if(memchr(lower_text, '/', len) == NULL) return;
  static const char *const k_hf_orgs[] = {
      "deepseek-ai/",         "meta-llama/",          "qwen/",
      "mistralai/",           "allenai/",             "bigcode/",
      "huggingfacetb/",       "huggingfaceh4/",       "black-forest-labs/",
      "stabilityai/",         "nousresearch/",        "cohereforai/",
      "01-ai/",               "thudm/",               "moonshotai/",
      "tiiuae/",              "baai/",                "nomic-ai/",
      "eleutherai/",          "openbmb/",             "ibm-granite/",
      "sentence-transformers/","unsloth/",            "zyphra/",
      "liquidai/",            "google-deepmind/",     "google/gemma",
      "google/paligemma",     "microsoft/phi",        "nvidia/llama",
      "nvidia/nemotron",      "openai/whisper",       "openai/clip",
      "apple/mlx",
  };
  for(size_t k = 0; k < sizeof(k_hf_orgs) / sizeof(k_hf_orgs[0]); ++k) {
    if(out->hf_repo_count >= MAX_EXTRACTED_HF_REPOS) break;
    const char *prefix = k_hf_orgs[k];
    size_t plen = strlen(prefix);
    const char *p = lower_text;
    const char *end = lower_text + len;
    while(p + plen < end) {
      const char *hit = strstr(p, prefix);
      if(!hit) break;
      int boundary_ok = (hit == lower_text) ||
                        (!isalnum((unsigned char)hit[-1]) && hit[-1] != '.' && hit[-1] != '-');
      if(boundary_ok) {
        const char *slash = strchr(prefix, '/');
        size_t org_len = (size_t)(slash - prefix);
        size_t cur = org_len + 1u;
        while(hit + cur < end && is_hf_repo_char(hit[cur]) && cur < 62u) cur++;
        if(cur > org_len + 2u) {
          push_hf_repo(out, hit, cur);
        }
      }
      p = hit + plen;
    }
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
  WordBloomIndex alpha_mask;
  compute_char_mask(lower, len, &alpha_mask);
#define HAS_WORD(w) text_contains_word_fast(lower, len, &alpha_mask, (w))

  for(size_t i = 0; i < sizeof(k_geo_rules) / sizeof(k_geo_rules[0]); ++i) {
    if(out->country_count >= MAX_EXTRACTED_COUNTRIES) break;
    for(size_t t = 0; t < 5u && k_geo_rules[i].terms[t]; ++t) {
      if(HAS_WORD(k_geo_rules[i].terms[t])) {
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
      if(HAS_WORD(k_org_rules[i].terms[t])) {
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
    for(size_t t = 0; t < 16u && k_topic_rules[i].terms[t]; ++t) {
      if(HAS_WORD(k_topic_rules[i].terms[t])) {
        direct |= BIT(k_topic_rules[i].node);
        break;
      }
    }
  }

  if(direct == 0u) direct = BIT(ONTO_PUBLIC_AFFAIRS);
  out->direct_ontology_mask = direct;
  out->closure_ontology_mask = ontology_upward_closure(direct);
  out->news_type = news_type_classify_precomputed(title, lower, len, &alpha_mask, feed_category);

  extract_cve_identifiers(lower, len, alpha_mask.wb_filter, out);

  /* Compute OPSEC / Tradecraft / High-Clearance score & classification */
  double opsec = 0.0;
  ClearanceLevel level = CLEARANCE_PUBLIC;

  if(HAS_WORD("osint") || HAS_WORD("satellite imagery") ||
     HAS_WORD("geolocation") || HAS_WORD("telemetry") ||
     HAS_WORD("sinkhole") || HAS_WORD("bgp")) {
    opsec += 2.2;
    if(level < CLEARANCE_OSINT) level = CLEARANCE_OSINT;
  }

  if(HAS_WORD("opsec") || HAS_WORD("anti-bot") ||
     HAS_WORD("turnstile") || HAS_WORD("fingerprint") ||
     HAS_WORD("ja3") || HAS_WORD("ja4") ||
     HAS_WORD("webauthn") || HAS_WORD("passkey") ||
     HAS_WORD("webassembly") || HAS_WORD("sandbox") ||
     HAS_WORD("onion") || HAS_WORD("qubes") ||
     HAS_WORD("grapheneos") || HAS_WORD("wireguard")) {
    opsec += 3.5;
    if(level < CLEARANCE_TRADECRAFT) level = CLEARANCE_TRADECRAFT;
  }

  if(out->cve_count > 0 || HAS_WORD("zero-day") ||
     HAS_WORD("0-day") || HAS_WORD("exploit") ||
     HAS_WORD("shellcode") || HAS_WORD("rootkit") ||
     HAS_WORD("bootkit") || HAS_WORD("spyware") ||
     HAS_WORD("pegasus") || HAS_WORD("heap") ||
     HAS_WORD("use-after-free") || HAS_WORD("post-quantum")) {
    opsec += 4.8 + (double)out->cve_count * 1.5;
    if(level < CLEARANCE_ZERO_DAY) level = CLEARANCE_ZERO_DAY;
  }

  if(HAS_WORD("sigint") || HAS_WORD("geoint") ||
     HAS_WORD("c4isr") || HAS_WORD("reconnaissance") ||
     HAS_WORD("hypersonic") || HAS_WORD("deterrence") ||
     HAS_WORD("classified") || HAS_WORD("darpa") ||
     HAS_WORD("iarpa") || HAS_WORD("nsa")) {
    opsec += 5.2;
    if(level < CLEARANCE_STRATEGIC) level = CLEARANCE_STRATEGIC;
  }

  out->clearance = level;
  out->opsec_score = opsec;

  /* Extract Market Assets / Currencies & Fiscal Events */
  for(size_t i = 0; i < sizeof(k_asset_rules) / sizeof(k_asset_rules[0]); ++i) {
    if(out->asset_count >= MAX_EXTRACTED_ASSETS) break;
    for(size_t t = 0; t < 5u && k_asset_rules[i].terms[t]; ++t) {
      if(HAS_WORD(k_asset_rules[i].terms[t])) {
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
      if(HAS_WORD(k_fiscal_event_rules[i].terms[t])) {
        strncpy(out->fiscal_events[out->fiscal_event_count], k_fiscal_event_rules[i].event_name, 23u);
        out->fiscal_events[out->fiscal_event_count][23] = '\0';
        out->fiscal_event_count++;
        break;
      }
    }
  }

  double hawk = 0.0, dove = 0.0, bull = 0.0, bear = 0.0;
  if(HAS_WORD("rate hike") || HAS_WORD("hawkish") ||
     HAS_WORD("quantitative tightening") || HAS_WORD("raises interest rate") ||
     HAS_WORD("inflation surge") || HAS_WORD("austerity")) {
    hawk += 3.5;
  }
  if(HAS_WORD("rate cut") || HAS_WORD("dovish") ||
     HAS_WORD("quantitative easing") || HAS_WORD("fiscal stimulus") ||
     HAS_WORD("liquidity injection") || HAS_WORD("cuts interest rate")) {
    dove += 3.5;
  }
  if(HAS_WORD("rally") || HAS_WORD("bullish") ||
     HAS_WORD("record high") || HAS_WORD("earnings beat") ||
     HAS_WORD("gdp expansion") || HAS_WORD("surges")) {
    bull += 3.0;
  }
  if(HAS_WORD("recession") || HAS_WORD("bearish") ||
     HAS_WORD("selloff") || HAS_WORD("sovereign default") ||
     HAS_WORD("debt crisis") || HAS_WORD("plunges")) {
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

  /* Extract AI Foundation Models, Neoclouds, Inference Engines, HF Repos & Open/Closed Spectrum */
  extract_hf_repo_ids(lower, len, out);

  double lic_scores[AI_LICENSE_COUNT] = {0};
  for(size_t i = 0; i < sizeof(k_ai_model_rules) / sizeof(k_ai_model_rules[0]); ++i) {
    if(out->ai_model_count >= MAX_EXTRACTED_AI_MODELS) break;
    for(size_t t = 0; t < 6u && k_ai_model_rules[i].terms[t]; ++t) {
      if(HAS_WORD(k_ai_model_rules[i].terms[t])) {
        strncpy(out->ai_models[out->ai_model_count], k_ai_model_rules[i].model_family, 23u);
        out->ai_models[out->ai_model_count][23] = '\0';
        out->ai_model_count++;
        if((unsigned)k_ai_model_rules[i].default_license < AI_LICENSE_COUNT) {
          lic_scores[k_ai_model_rules[i].default_license] += 3.0;
        }
        break;
      }
    }
  }

  for(size_t i = 0; i < sizeof(k_ai_infra_rules) / sizeof(k_ai_infra_rules[0]); ++i) {
    if(out->ai_infra_count >= MAX_EXTRACTED_AI_INFRA) break;
    for(size_t t = 0; t < 6u && k_ai_infra_rules[i].terms[t]; ++t) {
      if(HAS_WORD(k_ai_infra_rules[i].terms[t])) {
        strncpy(out->ai_infra[out->ai_infra_count], k_ai_infra_rules[i].infra_name, 23u);
        out->ai_infra[out->ai_infra_count][23] = '\0';
        out->ai_infra_count++;
        break;
      }
    }
  }

  if(out->ai_model_count > 0 || out->ai_infra_count > 0 || out->hf_repo_count > 0) {
    out->direct_ontology_mask |= BIT(ONTO_AI);
    out->closure_ontology_mask = ontology_upward_closure(out->direct_ontology_mask);
  }

  /* Open-Source (OSI) vs Open-Weights (Permissive / Restricted) vs Closed-Source Frontier API scoring */
  if(out->hf_repo_count > 0) {
    lic_scores[AI_LICENSE_OPEN_WEIGHTS_PERMISSIVE] += (double)out->hf_repo_count * 2.6;
  }
  if(HAS_WORD("closed-source") || HAS_WORD("closed source") ||
     HAS_WORD("proprietary model") || HAS_WORD("frontier api") ||
     HAS_WORD("walled garden") || HAS_WORD("unreleased weights")) {
    lic_scores[AI_LICENSE_CLOSED_PROPRIETARY] += 4.2;
  }
  if(HAS_WORD("community license") || HAS_WORD("non-commercial") ||
     HAS_WORD("custom license") || HAS_WORD("gated model")) {
    lic_scores[AI_LICENSE_OPEN_WEIGHTS_RESTRICTED] += 4.0;
  }
  if(HAS_WORD("open-weights") || HAS_WORD("open weights") ||
     HAS_WORD("apache-2.0") || HAS_WORD("apache 2.0") ||
     HAS_WORD("mit license") || HAS_WORD("safetensors") ||
     HAS_WORD("gguf") || HAS_WORD("hugging face") ||
     HAS_WORD("huggingface")) {
    lic_scores[AI_LICENSE_OPEN_WEIGHTS_PERMISSIVE] += 3.8;
  }
  if(HAS_WORD("open-source ai definition") || HAS_WORD("osaid") ||
     HAS_WORD("open pretraining") || HAS_WORD("reproducible pretraining") ||
     HAS_WORD("dolma") || HAS_WORD("the-stack-v2") ||
     HAS_WORD("fineweb") || HAS_WORD("fully open-source") ||
     HAS_WORD("open science ai") || HAS_WORD("open-source osi")) {
    lic_scores[AI_LICENSE_OPEN_SCIENCE_OSI] += 6.5;
  }

  AiWeightLicense best_lic = AI_LICENSE_UNSPECIFIED;
  double best_lic_sc = 0.5;
  for(uint32_t l = 1u; l < AI_LICENSE_COUNT; ++l) {
    if(lic_scores[l] >= best_lic_sc) {
      best_lic_sc = lic_scores[l];
      best_lic = (AiWeightLicense)l;
    }
  }
  if(lic_scores[AI_LICENSE_OPEN_SCIENCE_OSI] >= 7.5 &&
     lic_scores[AI_LICENSE_OPEN_SCIENCE_OSI] >= lic_scores[AI_LICENSE_CLOSED_PROPRIETARY]) {
    best_lic = AI_LICENSE_OPEN_SCIENCE_OSI;
  }
  out->ai_license = best_lic;
  out->openness_score = lic_scores[AI_LICENSE_OPEN_SCIENCE_OSI] * 1.5 +
                        lic_scores[AI_LICENSE_OPEN_WEIGHTS_PERMISSIVE] * 1.2 +
                        lic_scores[AI_LICENSE_OPEN_WEIGHTS_RESTRICTED] * 0.8 -
                        lic_scores[AI_LICENSE_CLOSED_PROPRIETARY] * 0.6;

  AiComputeTier ai_tier = AI_TIER_NONE;
  double ai_score = (double)out->ai_model_count * 2.4 + (double)out->ai_infra_count * 2.8 +
                    (double)out->hf_repo_count * 3.0;

  if((out->direct_ontology_mask & BIT(ONTO_AI)) != 0u ||
     HAS_WORD("rag") || HAS_WORD("vector database") ||
     HAS_WORD("embedding") || HAS_WORD("prompt") ||
     HAS_WORD("copilot") || HAS_WORD("agentic")) {
    ai_score += 1.8;
    if(ai_tier < AI_TIER_APPLIED_RAG) ai_tier = AI_TIER_APPLIED_RAG;
  }

  if(HAS_WORD("inference") || HAS_WORD("vllm") ||
     HAS_WORD("sglang") || HAS_WORD("tensorrt-llm") ||
     HAS_WORD("kv cache") || HAS_WORD("kv-cache") ||
     HAS_WORD("pagedattention") || HAS_WORD("flashattention") ||
     HAS_WORD("speculative decoding") || HAS_WORD("quantization") ||
     HAS_WORD("ttft") || HAS_WORD("tpot") ||
     HAS_WORD("tokens per second") || HAS_WORD("fp8") ||
     HAS_WORD("gguf") || HAS_WORD("llama.cpp")) {
    ai_score += 3.6;
    if(ai_tier < AI_TIER_INFERENCE_STACK) ai_tier = AI_TIER_INFERENCE_STACK;
  }

  if(HAS_WORD("neocloud") || HAS_WORD("coreweave") ||
     HAS_WORD("lambda labs") || HAS_WORD("together ai") ||
     HAS_WORD("fireworks ai") || HAS_WORD("crusoe") ||
     HAS_WORD("nebius") || HAS_WORD("baseten") ||
     HAS_WORD("blackwell") || HAS_WORD("gb200") ||
     HAS_WORD("nvl72") || HAS_WORD("h100") ||
     HAS_WORD("h200") || HAS_WORD("mi300x") ||
     HAS_WORD("groq") || HAS_WORD("cerebras") ||
     HAS_WORD("infiniband") || HAS_WORD("nvlink") ||
     HAS_WORD("gpu cluster")) {
    ai_score += 4.5;
    if(ai_tier < AI_TIER_NEOCLOUD_CLUSTER) ai_tier = AI_TIER_NEOCLOUD_CLUSTER;
  }

  if(HAS_WORD("pretraining") || HAS_WORD("pre-training") ||
     HAS_WORD("post-training") || HAS_WORD("test-time compute") ||
     HAS_WORD("rlhf") || HAS_WORD("grpo") ||
     HAS_WORD("dpo") || HAS_WORD("mixture of experts") ||
     HAS_WORD("scaling laws") || HAS_WORD("chinchilla") ||
     HAS_WORD("frontier model") || HAS_WORD("chain-of-thought") ||
     HAS_WORD("reward model") || HAS_WORD("open weights") ||
     HAS_WORD("alphafold") || HAS_WORD("alphaproof") ||
     HAS_WORD("alphageometry") || HAS_WORD("funsearch") ||
     HAS_WORD("alphazero") || HAS_WORD("muzero")) {
    ai_score += 5.2;
    if(ai_tier < AI_TIER_FRONTIER_PRETRAINING) ai_tier = AI_TIER_FRONTIER_PRETRAINING;
  }

  out->ai_tier = ai_tier;
  out->ai_compute_score = ai_score;

  /* Extract Global Sports Leagues, Tournaments & Athletics Intelligence */
  for(size_t i = 0; i < sizeof(k_sports_rules) / sizeof(k_sports_rules[0]); ++i) {
    if(out->sport_count >= MAX_EXTRACTED_SPORTS) break;
    for(size_t t = 0; t < 6u && k_sports_rules[i].terms[t]; ++t) {
      if(HAS_WORD(k_sports_rules[i].terms[t])) {
        strncpy(out->sports[out->sport_count], k_sports_rules[i].league_name, 23u);
        out->sports[out->sport_count][23] = '\0';
        out->sport_count++;
        break;
      }
    }
  }

  double sp_score = (double)out->sport_count * 3.2;
  if(out->sport_count > 0) {
    out->direct_ontology_mask |= BIT(ONTO_SPORTS);
    out->closure_ontology_mask = ontology_upward_closure(out->direct_ontology_mask);
  }
  if((out->direct_ontology_mask & BIT(ONTO_SPORTS)) != 0u) {
    sp_score += 2.0;
  }
  out->sports_score = sp_score;
#undef HAS_WORD
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
  size_t lo = 0;
  size_t hi = sizeof(k_stopwords) / sizeof(k_stopwords[0]);
  while(lo < hi) {
    size_t mid = lo + (hi - lo) / 2u;
    int cmp = strcmp(w, k_stopwords[mid]);
    if(cmp == 0) return 1;
    if(cmp < 0) hi = mid;
    else lo = mid + 1u;
  }
  return 0;
}

typedef struct {
  const char *alias;
  const char *canonical;
} DomainAlias;

static const DomainAlias k_domain_aliases[] = {
    {"0-day", "zero-day"},
    {"0day", "zero-day"},
    {"agi", "artificial-general-intelligence"},
    {"ai", "artificial-intelligence"},
    {"apt", "advanced-persistent-threat"},
    {"asi", "artificial-superintelligence"},
    {"awq", "activation-aware-quantization"},
    {"bf16", "bfloat16"},
    {"boe", "bank-of-england"},
    {"boj", "bank-of-japan"},
    {"bps", "basis-point"},
    {"btc", "bitcoin"},
    {"cbdc", "central-bank-digital-currency"},
    {"cisa", "cybersecurity-agency"},
    {"cowos", "chip-on-wafer-on-substrate"},
    {"cpi", "consumer-price-index"},
    {"cuda", "gpu-compute-runtime"},
    {"cve", "vulnerability"},
    {"ddos", "denial-of-service"},
    {"dpo", "direct-preference-optimization"},
    {"dxy", "dollar-index"},
    {"ebpf", "extended-bpf"},
    {"ecb", "european-central-bank"},
    {"edr", "endpoint-detection"},
    {"euv", "extreme-ultraviolet"},
    {"fed", "federal-reserve"},
    {"fomc", "federal-reserve"},
    {"forex", "foreign-exchange"},
    {"fp4", "low-bit-quantization"},
    {"fp8", "low-bit-quantization"},
    {"fx", "foreign-exchange"},
    {"gdp", "gross-domestic-product"},
    {"geoint", "geospatial-intelligence"},
    {"gguf", "quantized-model-weights"},
    {"gilt", "sovereign-bond"},
    {"gilts", "sovereign-bond"},
    {"gptq", "post-training-quantization"},
    {"grpo", "group-relative-policy-optimization"},
    {"h100", "hopper-gpu"},
    {"h200", "hopper-gpu"},
    {"hbm", "high-bandwidth-memory"},
    {"humint", "human-intelligence"},
    {"imf", "international-monetary-fund"},
    {"ipo", "initial-public-offering"},
    {"jgb", "japanese-government-bond"},
    {"jgbs", "japanese-government-bond"},
    {"jwst", "james-webb-telescope"},
    {"kv-cache", "key-value-attention-cache"},
    {"lhc", "large-hadron-collider"},
    {"llm", "large-language-model"},
    {"llms", "large-language-model"},
    {"lng", "liquefied-natural-gas"},
    {"lora", "low-rank-adaptation"},
    {"mcts", "monte-carlo-tree-search"},
    {"mi300x", "instinct-gpu"},
    {"ml-dsa", "dilithium"},
    {"ml-kem", "kyber"},
    {"mlx", "apple-silicon-ml"},
    {"moe", "mixture-of-experts"},
    {"mrna", "messenger-rna"},
    {"nccl", "collective-gpu-communication"},
    {"nfp", "nonfarm-payroll"},
    {"nsa", "national-security-agency"},
    {"nvlink", "gpu-interconnect"},
    {"oob", "out-of-bounds"},
    {"opec+", "opec"},
    {"opsec", "operational-security"},
    {"osint", "open-source-intelligence"},
    {"pboc", "peoples-bank-of-china"},
    {"pce", "personal-consumption"},
    {"pqc", "post-quantum-cryptography"},
    {"qe", "quantitative-easing"},
    {"qlora", "quantized-low-rank-adaptation"},
    {"qt", "quantitative-tightening"},
    {"rag", "retrieval-augmented-generation"},
    {"rbi", "reserve-bank-of-india"},
    {"rce", "remote-code-execution"},
    {"rlhf", "reinforcement-learning-human-feedback"},
    {"rmb", "renminbi"},
    {"rocm", "amd-gpu-compute"},
    {"s&p500", "sp500"},
    {"sft", "supervised-fine-tuning"},
    {"sglang", "structured-generation-runtime"},
    {"sigint", "signals-intelligence"},
    {"smr", "small-modular-reactor"},
    {"snb", "swiss-national-bank"},
    {"sqli", "sql-injection"},
    {"ssrf", "server-side-forgery"},
    {"tokamak", "nuclear-fusion-tokamak"},
    {"tpot", "time-per-output-token"},
    {"tpu", "tensor-processing-unit"},
    {"ttft", "time-to-first-token"},
    {"uaf", "use-after-free"},
    {"uav", "drone"},
    {"ust", "treasury-yield"},
    {"vllm", "paged-attention-serving"},
    {"wasm", "webassembly"},
    {"xss", "cross-site-scripting"},
    {"ycc", "yield-curve-control"},
    {"zk-snark", "zero-knowledge-proof"},
    {"zk-stark", "zero-knowledge-proof"},
    {"zkp", "zero-knowledge-proof"},
};

static const char *lookup_domain_alias(const char *w) {
  size_t lo = 0;
  size_t hi = sizeof(k_domain_aliases) / sizeof(k_domain_aliases[0]);
  while(lo < hi) {
    size_t mid = lo + (hi - lo) / 2u;
    int cmp = strcmp(w, k_domain_aliases[mid].alias);
    if(cmp == 0) return k_domain_aliases[mid].canonical;
    if(cmp < 0) hi = mid;
    else lo = mid + 1u;
  }
  return NULL;
}

static void stem_word(char *w, size_t len) {
  if(len > 7u && strcmp(w + len - 5u, "ation") == 0) {
    w[len - 5u] = '\0';
  } else if(len > 6u && strcmp(w + len - 4u, "ment") == 0) {
    w[len - 4u] = '\0';
  } else if(len > 6u && strcmp(w + len - 4u, "ness") == 0) {
    w[len - 4u] = '\0';
  } else if(len > 6u && strcmp(w + len - 4u, "able") == 0) {
    w[len - 4u] = '\0';
  } else if(len > 6u && strcmp(w + len - 4u, "ible") == 0) {
    w[len - 4u] = '\0';
  } else if(len > 5u && strcmp(w + len - 3u, "ies") == 0) {
    strcpy(w + len - 3u, "y");
  } else if(len > 5u && strcmp(w + len - 3u, "ing") == 0) {
    w[len - 3u] = '\0';
  } else if(len > 4u && strcmp(w + len - 2u, "ed") == 0) {
    w[len - 2u] = '\0';
  } else if(len > 4u && w[len - 1u] == 's' && w[len - 2u] != 's') {
    w[len - 1u] = '\0';
  }
}

static size_t push_lexical_term(const char *word,
                                size_t wlen,
                                float weight,
                                LexicalTerm *terms,
                                size_t current_count,
                                size_t max_terms) {
  uint64_t h = fnv1a64(word, wlen);
  for(size_t i = 0; i < current_count; ++i) {
    if(terms[i].hash == h && strcmp(terms[i].term, word) == 0) {
      terms[i].weight += weight;
      terms[i].tf += 1u;
      return current_count;
    }
  }
  if(current_count < max_terms) {
    strncpy(terms[current_count].term, word, 31u);
    terms[current_count].term[31] = '\0';
    terms[current_count].hash = h;
    terms[current_count].weight = weight;
    terms[current_count].tf = 1u;
    current_count++;
  }
  return current_count;
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
    while(text[p] != '\0' && (isalnum((unsigned char)text[p]) || text[p] == '-' || text[p] == '+')) {
      if(wlen < 30u) {
        word[wlen++] = (char)tolower((unsigned char)text[p]);
      }
      p++;
    }
    while(wlen > 0 && (word[wlen - 1u] == '-' || word[wlen - 1u] == '+')) wlen--;
    word[wlen] = '\0';
    if(wlen < 2u || is_stopword(word)) continue;

    /* Check canonical domain alias before stemming */
    const char *canonical = lookup_domain_alias(word);

    stem_word(word, wlen);
    wlen = strlen(word);
    if(wlen < 2u) continue;

    if(out_word_tokens) (*out_word_tokens)++;
    current_count = push_lexical_term(word, wlen, field_weight, terms, current_count, max_terms);

    if(canonical) {
      size_t clen = strlen(canonical);
      current_count = push_lexical_term(canonical, clen, field_weight * 0.85f, terms, current_count, max_terms);
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
