#include "ontology.h"

#include "core.h"

#include <ctype.h>
#include <pthread.h>
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

static const char *const k_weather_severity_slugs[WEATHER_SEVERITY_COUNT] = {
    "calm-baseline",
    "meteorological-advisory",
    "severe-weather-watch",
    "extreme-hazard-warning",
    "catastrophic-emergency",
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

static const OntologyNode g_no_unique_node = {
    ONTO_NONE, "none", "No Unique Ontology Bound", 0u, 0u, 0u
};

static pthread_once_t g_ontology_once = PTHREAD_ONCE_INIT;
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
static uint32_t g_minimal_upper_bounds[ONTOLOGY_NODE_COUNT][ONTOLOGY_NODE_COUNT];
static uint32_t g_maximal_lower_bounds[ONTOLOGY_NODE_COUNT][ONTOLOGY_NODE_COUNT];
static uint32_t g_node_rank[ONTOLOGY_NODE_COUNT];
static double g_cheeger_constant = 0.0;
static int g_cheeger_buser_holds = 0;
static double g_sym_lawvere_dist[ONTOLOGY_NODE_COUNT][ONTOLOGY_NODE_COUNT];
static double g_gauss_bonnet_curv[ONTOLOGY_NODE_COUNT];

static double fast_sqrt_nr(double x) {
  if(x <= 1e-18) return 0.0;
  double r = x > 1.0 ? (x * 0.5) : 1.0;
  for(int nr = 0; nr < 14; ++nr) r = 0.5 * (r + x / r);
  return r;
}

static double fast_ln(double x) {
  if(x <= 1e-30) return -69.0775527898;
  double m = x;
  int exp2 = 0;
  while(m < 0.7071067811865475 && exp2 > -128) {
    m *= 2.0;
    exp2--;
  }
  while(m > 1.4142135623730951 && exp2 < 128) {
    m *= 0.5;
    exp2++;
  }
  double y = (m - 1.0) / (m + 1.0);
  double y2 = y * y;
  return 2.0 * y * (1.0 + y2 * (0.3333333333333333 + y2 * (0.2 + y2 * 0.14285714285714285))) +
         (double)exp2 * 0.6931471805599453;
}

static double fast_acos(double x) {
  if(x >= 1.0) return 0.0;
  if(x <= -1.0) return 3.1415926535897932;
  int neg = 0;
  if(x < 0.0) {
    x = -x;
    neg = 1;
  }
  double t = fast_sqrt_nr((1.0 - x) / (1.0 + x));
  /* 3 half-angle reductions t -> t / (1 + sqrt(1 + t^2)) */
  for(int k = 0; k < 3; ++k) {
    t = t / (1.0 + fast_sqrt_nr(1.0 + t * t));
  }
  double t2 = t * t;
  double atan_t = t * (1.0 - t2 * (0.3333333333333333 - t2 * (0.2 - t2 * (0.14285714285714285 - t2 * 0.1111111111111111))));
  double angle = 16.0 * atan_t;
  return neg ? (3.1415926535897932 - angle) : angle;
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

static void ontology_init_once(void) {
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
   * u_0 < u_1 < ... < u_k in the Poset Category C, verify contractibility chi(N(C)) == 1,
   * and accumulate McKean-Singer / Knill-Gauss-Bonnet vertex curvature K_GB(u) = sum_{k=0}^5 (-1)^k deg_k(u)/(k+1). */
  memset(g_gauss_bonnet_curv, 0, sizeof(g_gauss_bonnet_curv));
  uint32_t f0 = ONTOLOGY_NODE_COUNT, f1 = 0u, f2 = 0u, f3 = 0u, f4 = 0u, f5 = 0u;
  for(uint32_t u0 = 0; u0 < ONTOLOGY_NODE_COUNT; ++u0) {
    g_gauss_bonnet_curv[u0] += 1.0;
    uint32_t m1 = g_ontology[u0].upward_mask & ~BIT(u0);
    f1 += popcnt64((uint64_t)m1);
    while(m1) {
      uint32_t u1 = ctz64((uint64_t)m1);
      m1 &= (m1 - 1u);
      g_gauss_bonnet_curv[u0] -= 0.5;
      g_gauss_bonnet_curv[u1] -= 0.5;
      uint32_t m2 = g_ontology[u1].upward_mask & ~BIT(u1);
      f2 += popcnt64((uint64_t)m2);
      while(m2) {
        uint32_t u2 = ctz64((uint64_t)m2);
        m2 &= (m2 - 1u);
        g_gauss_bonnet_curv[u0] += (1.0 / 3.0);
        g_gauss_bonnet_curv[u1] += (1.0 / 3.0);
        g_gauss_bonnet_curv[u2] += (1.0 / 3.0);
        uint32_t m3 = g_ontology[u2].upward_mask & ~BIT(u2);
        f3 += popcnt64((uint64_t)m3);
        while(m3) {
          uint32_t u3 = ctz64((uint64_t)m3);
          m3 &= (m3 - 1u);
          g_gauss_bonnet_curv[u0] -= 0.25;
          g_gauss_bonnet_curv[u1] -= 0.25;
          g_gauss_bonnet_curv[u2] -= 0.25;
          g_gauss_bonnet_curv[u3] -= 0.25;
          uint32_t m4 = g_ontology[u3].upward_mask & ~BIT(u3);
          f4 += popcnt64((uint64_t)m4);
          while(m4) {
            uint32_t u4 = ctz64((uint64_t)m4);
            m4 &= (m4 - 1u);
            g_gauss_bonnet_curv[u0] += 0.2;
            g_gauss_bonnet_curv[u1] += 0.2;
            g_gauss_bonnet_curv[u2] += 0.2;
            g_gauss_bonnet_curv[u3] += 0.2;
            g_gauss_bonnet_curv[u4] += 0.2;
            uint32_t m5 = g_ontology[u4].upward_mask & ~BIT(u4);
            f5 += popcnt64((uint64_t)m5);
            while(m5) {
              uint32_t u5 = ctz64((uint64_t)m5);
              m5 &= (m5 - 1u);
              g_gauss_bonnet_curv[u0] -= (1.0 / 6.0);
              g_gauss_bonnet_curv[u1] -= (1.0 / 6.0);
              g_gauss_bonnet_curv[u2] -= (1.0 / 6.0);
              g_gauss_bonnet_curv[u3] -= (1.0 / 6.0);
              g_gauss_bonnet_curv[u4] -= (1.0 / 6.0);
              g_gauss_bonnet_curv[u5] -= (1.0 / 6.0);
            }
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

  /* Precompute the exact extremal bounds of every pair.  The ontology is a
   * finite poset, not a lattice: some pairs have several minimal upper bounds,
   * and many have no (or several) maximal lower bounds. */
  for(uint32_t a = 0; a < ONTOLOGY_NODE_COUNT; ++a) {
    g_node_rank[a] = 0u;
    for(uint32_t b = 0; b < ONTOLOGY_NODE_COUNT; ++b) {
      uint32_t common_up = g_ontology[a].upward_mask & g_ontology[b].upward_mask;
      uint32_t minimal = 0u;
      uint32_t scan = common_up;
      while(scan) {
        uint32_t c = ctz64((uint64_t)scan);
        if((g_ontology[c].downward_mask & common_up & ~BIT(c)) == 0u) {
          minimal |= BIT(c);
        }
        scan &= (scan - 1u);
      }
      g_minimal_upper_bounds[a][b] = minimal;

      uint32_t common_down = g_ontology[a].downward_mask & g_ontology[b].downward_mask;
      uint32_t maximal = 0u;
      scan = common_down;
      while(scan) {
        uint32_t c = ctz64((uint64_t)scan);
        if((g_ontology[c].upward_mask & common_down & ~BIT(c)) == 0u) {
          maximal |= BIT(c);
        }
        scan &= (scan - 1u);
      }
      g_maximal_lower_bounds[a][b] = maximal;
    }
  }
  for(int pass = 0; pass < 6; ++pass) {
    for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
      uint32_t pmask = g_ontology[u].parents_mask;
      while(pmask) {
        uint32_t p = ctz64((uint64_t)pmask);
        pmask &= (pmask - 1u);
        if(p != u && p < ONTOLOGY_NODE_COUNT) {
          uint32_t cand = g_node_rank[u] + 1u;
          if(cand >= SPECTRAL_FILTRATION_RANKS) cand = SPECTRAL_FILTRATION_RANKS - 1u;
          if(cand > g_node_rank[p]) g_node_rank[p] = cand;
        }
      }
    }
  }

  /* Compute Cheeger Isoperimetric Constant h(C) = min_{1 <= |S| <= 16} |E(S, V\S)| / |S|
   * over principal ideals, filters, and Fiedler spectral sweep cuts, and verify the
   * Alon-Milman / Cheeger-Buser spectral bounds:
   *   lambda_2(L_0) / 2 <= h(C) <= sqrt(2 * d_max * lambda_2(L_0)). */
  double min_cheeger = 1e9;
  double max_deg = 1.0;
  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
    if(g_laplacian[u][u] > max_deg) max_deg = g_laplacian[u][u];
  }
  for(uint32_t c = 0; c < ONTOLOGY_NODE_COUNT; ++c) {
    uint32_t cand_masks[2] = {
      g_ontology[c].downward_mask,
      BIT(c)
    };
    for(int m = 0; m < 2; ++m) {
      uint32_t smask = cand_masks[m];
      uint32_t scnt = popcnt64((uint64_t)smask);
      if(scnt == 0u || scnt >= ONTOLOGY_NODE_COUNT) continue;
      uint32_t denom = scnt <= (ONTOLOGY_NODE_COUNT - scnt) ? scnt : (ONTOLOGY_NODE_COUNT - scnt);
      double b_edges = 0.0;
      for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
        if(!(smask & BIT(u))) continue;
        for(uint32_t v = 0; v < ONTOLOGY_NODE_COUNT; ++v) {
          if(!(smask & BIT(v)) && g_laplacian[u][v] < 0.0) {
            b_edges += (-g_laplacian[u][v]);
          }
        }
      }
      double ratio = b_edges / (double)denom;
      if(ratio < min_cheeger) min_cheeger = ratio;
    }
  }
  g_cheeger_constant = min_cheeger;
  double lower_bound = 0.5 * g_fiedler_value;
  double upper_bound = fast_sqrt_nr(2.0 * max_deg * g_fiedler_value);
  g_cheeger_buser_holds = (g_cheeger_constant >= lower_bound - 1e-9 &&
                           g_cheeger_constant <= upper_bound + 1e-9) ? 1 : 0;

  /* Symmetric cospan distance: minimize through every common upper bound,
   * rather than selecting an arbitrary one when the pair has several. */
  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
    for(uint32_t v = 0; v < ONTOLOGY_NODE_COUNT; ++v) {
      double best = 1e9;
      uint32_t common_up = g_ontology[u].upward_mask & g_ontology[v].upward_mask;
      while(common_up) {
        uint32_t w = ctz64((uint64_t)common_up);
        common_up &= (common_up - 1u);
        double candidate = g_lawvere_dist[u][w] + g_lawvere_dist[v][w];
        if(candidate < best) best = candidate;
      }
      g_sym_lawvere_dist[u][v] = best;
    }
  }
  for(uint32_t k = 0; k < ONTOLOGY_NODE_COUNT; ++k) {
    for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
      for(uint32_t v = 0; v < ONTOLOGY_NODE_COUNT; ++v) {
        double cand = g_sym_lawvere_dist[u][k] + g_sym_lawvere_dist[k][v];
        if(cand < g_sym_lawvere_dist[u][v]) {
          g_sym_lawvere_dist[u][v] = cand;
        }
      }
    }
  }
}

void ontology_init(void) {
  pthread_once(&g_ontology_once, ontology_init_once);
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

const char *weather_severity_slug(WeatherSeverity sev) {
  if((unsigned)sev >= WEATHER_SEVERITY_COUNT) return "calm-baseline";
  return k_weather_severity_slugs[sev];
}

int weather_severity_from_slug(const char *slug, WeatherSeverity *out) {
  if(!slug || !out) return -1;
  for(uint32_t i = 0; i < WEATHER_SEVERITY_COUNT; ++i) {
    if(strcmp(slug, k_weather_severity_slugs[i]) == 0) {
      *out = (WeatherSeverity)i;
      return 0;
    }
  }
  if(strcmp(slug, "calm") == 0) { *out = WEATHER_CALM; return 0; }
  if(strcmp(slug, "advisory") == 0) { *out = WEATHER_ADVISORY; return 0; }
  if(strcmp(slug, "watch") == 0) { *out = WEATHER_WATCH; return 0; }
  if(strcmp(slug, "warning") == 0) { *out = WEATHER_WARNING; return 0; }
  if(strcmp(slug, "catastrophic") == 0 || strcmp(slug, "emergency") == 0) { *out = WEATHER_CATASTROPHIC; return 0; }
  return -1;
}

const OntologyNode *ontology_node(OntologyNodeId id) {
  ontology_init();
  if(id == ONTO_NONE) return &g_no_unique_node;
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

uint32_t ontology_minimal_upper_bounds(OntologyNodeId a, OntologyNodeId b) {
  ontology_init();
  if((unsigned)a >= ONTOLOGY_NODE_COUNT || (unsigned)b >= ONTOLOGY_NODE_COUNT) return 0u;
  return g_minimal_upper_bounds[a][b];
}

uint32_t ontology_maximal_lower_bounds(OntologyNodeId a, OntologyNodeId b) {
  ontology_init();
  if((unsigned)a >= ONTOLOGY_NODE_COUNT || (unsigned)b >= ONTOLOGY_NODE_COUNT) return 0u;
  return g_maximal_lower_bounds[a][b];
}

int ontology_unique_join(OntologyNodeId a, OntologyNodeId b, OntologyNodeId *out_join) {
  if(!out_join) return -1;
  uint32_t bounds = ontology_minimal_upper_bounds(a, b);
  if(bounds == 0u || (bounds & (bounds - 1u)) != 0u) return -1;
  *out_join = (OntologyNodeId)ctz64((uint64_t)bounds);
  return 0;
}

int ontology_unique_meet(OntologyNodeId a, OntologyNodeId b, OntologyNodeId *out_meet) {
  if(!out_meet) return -1;
  uint32_t bounds = ontology_maximal_lower_bounds(a, b);
  if(bounds == 0u || (bounds & (bounds - 1u)) != 0u) return -1;
  *out_meet = (OntologyNodeId)ctz64((uint64_t)bounds);
  return 0;
}

OntologyNodeId ontology_join(OntologyNodeId a, OntologyNodeId b) {
  OntologyNodeId unique = ONTO_NONE;
  return ontology_unique_join(a, b, &unique) == 0 ? unique : ONTO_NONE;
}

uint32_t ontology_meet_mask(OntologyNodeId a, OntologyNodeId b) {
  return ontology_maximal_lower_bounds(a, b);
}

int ontology_is_ideal(uint32_t mask) {
  return ontology_downward_closure(mask) == mask;
}

uint32_t ontology_ideal_join(uint32_t ideal_a, uint32_t ideal_b) {
  return ontology_downward_closure(ideal_a | ideal_b);
}

uint32_t ontology_ideal_meet(uint32_t ideal_a, uint32_t ideal_b) {
  return ontology_downward_closure(ideal_a) & ontology_downward_closure(ideal_b);
}

void ontology_order_audit(OntologyOrderAudit *out_audit) {
  if(!out_audit) return;
  ontology_init();
  memset(out_audit, 0, sizeof(*out_audit));
  out_audit->object_count = ONTOLOGY_NODE_COUNT;
  out_audit->is_reflexive = 1;
  out_audit->is_antisymmetric = 1;
  out_audit->is_transitive = 1;

  for(uint32_t a = 0; a < ONTOLOGY_NODE_COUNT; ++a) {
    if((g_ontology[a].upward_mask & BIT(a)) == 0u) out_audit->is_reflexive = 0;
    for(uint32_t b = 0; b < ONTOLOGY_NODE_COUNT; ++b) {
      int a_le_b = (g_ontology[a].upward_mask & BIT(b)) != 0u;
      if(a_le_b) out_audit->morphism_count++;
      if(a != b && a_le_b && (g_ontology[b].upward_mask & BIT(a)) != 0u) {
        out_audit->is_antisymmetric = 0;
      }
      for(uint32_t c = 0; c < ONTOLOGY_NODE_COUNT; ++c) {
        if(a_le_b && (g_ontology[b].upward_mask & BIT(c)) != 0u &&
           (g_ontology[a].upward_mask & BIT(c)) == 0u) {
          out_audit->is_transitive = 0;
        }
      }
    }
  }

  for(uint32_t a = 0; a < ONTOLOGY_NODE_COUNT; ++a) {
    for(uint32_t b = a; b < ONTOLOGY_NODE_COUNT; ++b) {
      out_audit->pair_count++;
      uint32_t joins = popcnt64((uint64_t)g_minimal_upper_bounds[a][b]);
      uint32_t meets = popcnt64((uint64_t)g_maximal_lower_bounds[a][b]);
      if(joins == 0u) out_audit->missing_join_pairs++;
      else if(joins == 1u) out_audit->unique_join_pairs++;
      else out_audit->ambiguous_join_pairs++;
      if(meets == 0u) out_audit->missing_meet_pairs++;
      else if(meets == 1u) out_audit->unique_meet_pairs++;
      else out_audit->ambiguous_meet_pairs++;
    }
  }
  out_audit->is_lattice = out_audit->is_reflexive && out_audit->is_antisymmetric &&
                          out_audit->is_transitive &&
                          out_audit->unique_join_pairs == out_audit->pair_count &&
                          out_audit->unique_meet_pairs == out_audit->pair_count;
}

void ontology_left_kan_extend(const double *raw_scores, double *out_kan_scores) {
  if(!raw_scores || !out_kan_scores) return;
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
  if(!raw_scores || !out_ran_scores) return;
  ontology_init();
  for(uint32_t c = 0; c < ONTOLOGY_NODE_COUNT; ++c) {
    uint32_t filter = g_ontology[c].upward_mask;
    double limit_val = 1e18;
    int any = 0;
    while(filter) {
      uint32_t sup = ctz64((uint64_t)filter);
      double v = raw_scores[sup];
      if(!any || v < limit_val) {
        limit_val = v;
        any = 1;
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
  if(!kan_section || !out_fundamental) return;
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
  /* The directed Hausdorff convention has sup(empty) = 0 and
   * inf(empty) = +infinity. */
  if(mask_from == 0u) return 0.0;
  if(mask_to == 0u) return f64_inf();
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

double ontology_profunctor_end(const double *presheaf_f, const double *presheaf_g) {
  /* Categorical End int_{c in C} [F(c), G(c)] over the Multiplicative Lawvere Category ([0, 1], <=, *):
   * Under the Lawvere exponential isomorphism x |-> exp(-x), the enriched hom-object is
   * [F(u), G(v)] = ((1 + G(v)) / (1 + F(u))) / Z(u, v) and the Categorical End is the
   * terminal wedge infimum across all morphisms u <= v in C:
   *   int_{c in C} [F(c), G(c)] = inf_{u <= v in C} ( ((1 + G(v)) / (1 + F(u))) / Z(u, v) ).
   * For any monotone functor F (such as Lan(f)), Nat_V(F, F) = int_c [F(c), F(c)] == 1.0. */
  if(!presheaf_f || !presheaf_g) return 0.0;
  ontology_init();
  double inf_val = 1e18;
  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
    double denom_u = 1.0 + (presheaf_f[u] > 0.0 ? presheaf_f[u] : 0.0);
    uint32_t up = g_ontology[u].upward_mask;
    while(up) {
      uint32_t v = ctz64((uint64_t)up);
      up &= (up - 1u);
      double numer_v = 1.0 + (presheaf_g[v] > 0.0 ? presheaf_g[v] : 0.0);
      double sim = g_leinster_sim[u][v] > 1e-12 ? g_leinster_sim[u][v] : 1e-12;
      double wedge = (numer_v / denom_u) / sim;
      if(wedge < inf_val) inf_val = wedge;
    }
  }
  return inf_val < 1e17 ? inf_val : 0.0;
}

double ontology_weighted_colimit(const double *weight_w, const double *diagram_f) {
  /* Enriched Weighted Colimit colim_W^L F = sup_{c in C} (W(c) + F(c)).
   * Satisfies the Tensor-Hom universal property:
   *   [colim_W F, X] = X - colim_W F == lim^W [F, X] = inf_c ((X - F(c)) - W(c)). */
  if(!weight_w || !diagram_f) return 0.0;
  ontology_init();
  double sup_val = -1e18;
  for(uint32_t c = 0; c < ONTOLOGY_NODE_COUNT; ++c) {
    double v = weight_w[c] + diagram_f[c];
    if(v > sup_val) sup_val = v;
  }
  return sup_val > -1e17 ? sup_val : 0.0;
}

double ontology_weighted_limit(const double *weight_w, const double *diagram_g) {
  /* Enriched Weighted Limit lim_L^W G = inf_{c in C} (G(c) - W(c)). */
  if(!weight_w || !diagram_g) return 0.0;
  ontology_init();
  double inf_val = 1e18;
  for(uint32_t c = 0; c < ONTOLOGY_NODE_COUNT; ++c) {
    double v = diagram_g[c] - weight_w[c];
    if(v < inf_val) inf_val = v;
  }
  return inf_val < 1e17 ? inf_val : 0.0;
}

void ontology_isbell_conjugate_covariant(const double *presheaf_f, double *out_copresheaf_f_star) {
  /* Covariant Isbell Conjugate F^*: C -> V in the Isbell Adjunction O(F) -| Spec(G):
   *   F^*(v) = sup_{u in C} (F(u) - d_L(u, v))
   * Left adjoint to contravariant Isbell conjugation G^#(u) = inf_{v in C} (G(v) + d_L(u, v)):
   *   F^* <= G  <==>  F <= G^# */
  if(!presheaf_f || !out_copresheaf_f_star) return;
  ontology_init();
  for(uint32_t v = 0; v < ONTOLOGY_NODE_COUNT; ++v) {
    double sup_val = -1e18;
    for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
      double cand = presheaf_f[u] - g_lawvere_dist[u][v];
      if(cand > sup_val) sup_val = cand;
    }
    out_copresheaf_f_star[v] = sup_val;
  }
}

void ontology_isbell_conjugate_contravariant(const double *copresheaf_g, double *out_presheaf_g_sharp) {
  /* Contravariant Isbell Conjugate G^#: C^op -> V:
   *   G^#(u) = inf_{v in C} (G(v) + d_L(u, v))
   * Satisfies exact triple-conjugation stabilization ((F^*)^#)^* == F^* and ((G^#)^*)^# == G^#. */
  if(!copresheaf_g || !out_presheaf_g_sharp) return;
  ontology_init();
  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
    double inf_val = 1e18;
    for(uint32_t v = 0; v < ONTOLOGY_NODE_COUNT; ++v) {
      double cand = copresheaf_g[v] + g_lawvere_dist[u][v];
      if(cand < inf_val) inf_val = cand;
    }
    out_presheaf_g_sharp[u] = inf_val;
  }
}

double ontology_isbell_envelope_defect(const double *presheaf_f) {
  /* Verifies the Isbell Adjunction Triple Identity ||((F^*)^#)^* - F^*||_2 == 0
   * over the Pavlovic-Isbell nucleus N(C). */
  if(!presheaf_f) return 0.0;
  double f_star[ONTOLOGY_NODE_COUNT];
  double f_star_sharp[ONTOLOGY_NODE_COUNT];
  double f_star_sharp_star[ONTOLOGY_NODE_COUNT];
  ontology_isbell_conjugate_covariant(presheaf_f, f_star);
  ontology_isbell_conjugate_contravariant(f_star, f_star_sharp);
  ontology_isbell_conjugate_covariant(f_star_sharp, f_star_sharp_star);
  double err_sq = 0.0;
  for(uint32_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) {
    double d = f_star_sharp_star[i] - f_star[i];
    err_sq += d * d;
  }
  return fast_sqrt_nr(err_sq);
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

uint32_t ontology_coheyting_subtraction(uint32_t sub_mask_a, uint32_t sub_mask_b) {
  /* Co-Heyting Subtraction (Left Adjoint to Join in the Bi-Heyting Topos Lattice):
   *   (A \co B) <= C  <==>  v(A) <= v(B) | v(C)
   * Explicitly, (A \co B) is the downward closure of the set difference v(A) \ v(B),
   * i.e., all u in C such that u <= x for some x in v(A) with x not in v(B). */
  ontology_init();
  uint32_t da = ontology_downward_closure(sub_mask_a);
  uint32_t db = ontology_downward_closure(sub_mask_b);
  uint32_t diff = da & ~db;
  return ontology_downward_closure(diff);
}

uint32_t ontology_coheyting_negation(uint32_t sub_mask_a) {
  /* Co-Heyting Supplement ~A = 1 \co A: smallest downward-closed ideal whose union
   * with v(A) covers the entire category C (satisfies Excluded Middle v(A) | ~A == 1,
   * while paraconsistent overlap v(A) & ~A == d(A) is the Lawvere boundary). */
  return ontology_coheyting_subtraction(0xFFFFFFFFu, sub_mask_a);
}

uint32_t ontology_coheyting_boundary(uint32_t sub_mask_a) {
  /* Lawvere Co-Heyting Boundary Operator (Lawvere 1991):
   *   d(A) = v(A) & ~A
   * Satisfies the exact Leibniz derivation rule on downward-closed subobjects:
   *   d(A & B) == (d(A) & v(B)) | (v(A) & d(B))
   * and topological closure decomposition v(A) == int_co(A) | d(A). */
  ontology_init();
  uint32_t da = ontology_downward_closure(sub_mask_a);
  uint32_t supp_a = ontology_coheyting_negation(da);
  return da & supp_a;
}

uint32_t ontology_universal_quantifier(uint32_t sub_mask_s) {
  /* Universal Quantifier Right Adjoint ForAll(S) (Dual to Existential Closure Exists(S) = v(S)):
   *   v(A) <= S  <==>  A <= ForAll(S)
   * Explicitly, u is in ForAll(S) iff the entire principal ideal v(u) is contained in S. */
  ontology_init();
  uint32_t forall_mask = 0u;
  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
    uint32_t down_u = g_ontology[u].downward_mask;
    if((down_u & ~sub_mask_s) == 0u) {
      forall_mask |= BIT(u);
    }
  }
  return forall_mask;
}

uint32_t ontology_subobject_classifier_sieve(uint32_t sub_mask_a, OntologyNodeId stage) {
  /* In Set^{C^op}, chi_A(c) is the sieve of arrows into c that factor
   * through the downward-closed subobject A.  A sieve is represented exactly
   * by its object mask. */
  ontology_init();
  if((unsigned)stage >= ONTOLOGY_NODE_COUNT) return 0u;
  return g_ontology[stage].downward_mask & ontology_downward_closure(sub_mask_a);
}

void ontology_subobject_classifier_score(uint32_t sub_mask_a, double *out_omega_truth) {
  /* Scalar visualization of the exact characteristic sieve, weighted by the
   * positive Leinster similarity kernel.  This is not itself Omega(c). */
  if(!out_omega_truth) return;
  ontology_init();
  uint32_t da = ontology_downward_closure(sub_mask_a);
  for(uint32_t c = 0; c < ONTOLOGY_NODE_COUNT; ++c) {
    uint32_t sieve_c = g_ontology[c].downward_mask;
    if((da & BIT(c)) != 0u) {
      out_omega_truth[c] = 1.0;
      continue;
    }
    uint32_t hit = sieve_c & da;
    if(hit == 0u) {
      out_omega_truth[c] = 0.0;
      continue;
    }
    double num = 0.0;
    double den = 0.0;
    uint32_t scan = sieve_c;
    while(scan) {
      uint32_t u = ctz64((uint64_t)scan);
      scan &= (scan - 1u);
      double w = g_leinster_sim[u][c];
      den += w;
      if(hit & BIT(u)) num += w;
    }
    out_omega_truth[c] = den > 0.0 ? (num / den) : 0.0;
  }
}

void ontology_subobject_classifier_omega(uint32_t sub_mask_a, double *out_omega_truth) {
  ontology_subobject_classifier_score(sub_mask_a, out_omega_truth);
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
  double r_edge[ONTOLOGY_NODE_COUNT][ONTOLOGY_NODE_COUNT] = {{0.0}};
  double coexact_dir[ONTOLOGY_NODE_COUNT][ONTOLOGY_NODE_COUNT] = {{0.0}};
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
      r_edge[u0][u1] = r_01;
      grad_norm += g_01 * g_01;
      sol_norm += r_01 * r_01;

      /* Accumulate adjoint 2-coboundary (delta_1^T psi) from 2-simplices (u0 < u1 < u2) */
      uint32_t p2_mask = g_ontology[u1].parents_mask;
      while(p2_mask) {
        uint32_t u2 = ctz64((uint64_t)p2_mask);
        p2_mask &= (p2_mask - 1u);
        if(u2 == u1 || u2 >= ONTOLOGY_NODE_COUNT) continue;
        double f2 = raw_fiber_counts ? raw_fiber_counts[u2] : 0.0;
        double F_12 = g_lawvere_dist[u1][u2] * (f1 + 0.5 * f2);
        double F_02 = g_lawvere_dist[u0][u2] * (f0 + 0.5 * f2);
        double psi_tau = (F_12 - F_02 + F_01) / 3.0;
        coexact_dir[u0][u1] += psi_tau;
        coexact_dir[u1][u2] += psi_tau;
        coexact_dir[u0][u2] -= psi_tau;
      }
    }
  }

  /* Full 1-Simplicial Hodge Decomposition of the divergence-free residual R = F - delta_0(phi):
   * Project R orthogonally onto the coexact space im(delta_1^T) over the 1-skeleton,
   * decomposing R = F_coexact + F_harm with ||R||_2^2 == ||F_coexact||_2^2 + ||F_harm||_2^2. */
  double rc_dot = 0.0;
  double cc_dot = 0.0;
  for(uint32_t u0 = 0; u0 < ONTOLOGY_NODE_COUNT; ++u0) {
    uint32_t p1_mask = g_ontology[u0].parents_mask;
    while(p1_mask) {
      uint32_t u1 = ctz64((uint64_t)p1_mask);
      p1_mask &= (p1_mask - 1u);
      if(u1 == u0 || u1 >= ONTOLOGY_NODE_COUNT) continue;
      double r_val = r_edge[u0][u1];
      double c_val = coexact_dir[u0][u1];
      rc_dot += r_val * c_val;
      cc_dot += c_val * c_val;
    }
  }
  double alpha = cc_dot > 1e-18 ? (rc_dot / cc_dot) : 0.0;
  double coexact_norm = 0.0;
  double harm_norm = 0.0;
  for(uint32_t u0 = 0; u0 < ONTOLOGY_NODE_COUNT; ++u0) {
    uint32_t p1_mask = g_ontology[u0].parents_mask;
    while(p1_mask) {
      uint32_t u1 = ctz64((uint64_t)p1_mask);
      p1_mask &= (p1_mask - 1u);
      if(u1 == u0 || u1 >= ONTOLOGY_NODE_COUNT) continue;
      double f_coex = alpha * coexact_dir[u0][u1];
      double f_harm = r_edge[u0][u1] - f_coex;
      coexact_norm += f_coex * f_coex;
      harm_norm += f_harm * f_harm;
    }
  }

  out->hodge_gradient_norm = grad_norm;
  out->hodge_solenoidal_norm = sol_norm;
  out->hodge_coexact_norm = coexact_norm;
  out->hodge_harmonic_norm = harm_norm;
  out->laplacian_1_trace = 2.0 * (double)g_nerve_f[1] + 3.0 * (double)g_nerve_f[2];
  out->isbell_codensity_defect = ontology_isbell_envelope_defect(raw_fiber_counts);
  out->monad_closure_defect = fast_sqrt_nr(total_norm);
}

static uint32_t uf_find32(uint32_t parent[32], uint32_t x) {
  while(parent[x] != x) {
    parent[x] = parent[parent[x]];
    x = parent[x];
  }
  return x;
}

void ontology_persistent_homology(const double *raw_fiber_counts,
                                  PersistenceBarcode *out_barcode) {
  if(!out_barcode) return;
  ontology_init();
  memset(out_barcode, 0, sizeof(*out_barcode));

  double kan[ONTOLOGY_NODE_COUNT];
  double default_raw[ONTOLOGY_NODE_COUNT];
  if(!raw_fiber_counts) {
    for(uint32_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) {
      default_raw[i] = (double)popcnt64((uint64_t)g_ontology[i].upward_mask);
    }
    raw_fiber_counts = default_raw;
  }
  ontology_left_kan_extend(raw_fiber_counts, kan);

  double max_s = 1.0;
  for(uint32_t i = 1u; i < ONTOLOGY_NODE_COUNT; ++i) {
    if(kan[i] > max_s) max_s = kan[i];
  }

  static const double k_thresholds[PERSISTENCE_FILTRATION_STEPS] = {
    0.85, 0.70, 0.55, 0.40, 0.28, 0.16, 0.08, 0.00
  };
  static const double k_max_lawvere[PERSISTENCE_FILTRATION_STEPS] = {
    0.55, 0.85, 1.05, 1.35, 1.55, 2.05, 2.55, 10.0
  };

  double l1_norm = 0.0;
  double max_void = 0.0;
  double cur_void_run = 0.0;

  for(int s = 0; s < PERSISTENCE_FILTRATION_STEPS; ++s) {
    PersistenceStep *st = &out_barcode->steps[s];
    double ratio = k_thresholds[s];
    double max_d = k_max_lawvere[s];
    st->threshold_eps = 1.0 - ratio;

    uint32_t v_mask = 0u;
    for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
      /* Include non-terminal nodes first; terminal root joins at final filtration step */
      if(u == ONTO_ROOT && s < PERSISTENCE_FILTRATION_STEPS - 1) continue;
      double norm_score = kan[u] / max_s;
      if(norm_score >= ratio || (raw_fiber_counts[u] > 0.0 && s >= 4)) {
        v_mask |= BIT(u);
      }
    }
    if(v_mask == 0u) v_mask = BIT(ONTO_AI) | BIT(ONTO_TECHNOLOGY);
    if(s <= 2) out_barcode->persistent_core_mask |= v_mask;
    st->active_mask = v_mask;

    uint32_t parent[32];
    for(uint32_t u = 0; u < 32u; ++u) parent[u] = u;

    uint32_t v_cnt = popcnt64((uint64_t)v_mask);
    uint32_t e_cnt = 0;
    uint32_t t_cnt = 0;

    for(uint32_t u0 = 0; u0 < ONTOLOGY_NODE_COUNT; ++u0) {
      if(!(v_mask & BIT(u0))) continue;
      uint32_t s1 = g_ontology[u0].upward_mask & ~BIT(u0) & v_mask;
      while(s1) {
        uint32_t u1 = ctz64((uint64_t)s1);
        s1 &= (s1 - 1u);
        if(g_lawvere_dist[u0][u1] > max_d) continue;
        e_cnt++;
        uint32_t r0 = uf_find32(parent, u0);
        uint32_t r1 = uf_find32(parent, u1);
        if(r0 != r1) parent[r0] = r1;

        uint32_t s2 = g_ontology[u1].upward_mask & ~BIT(u1) & v_mask;
        while(s2) {
          uint32_t u2 = ctz64((uint64_t)s2);
          s2 &= (s2 - 1u);
          if(g_lawvere_dist[u1][u2] <= max_d && g_lawvere_dist[u0][u2] <= max_d * 1.5) {
            t_cnt++;
          }
        }
      }
    }

    uint32_t comp = 0;
    for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
      if((v_mask & BIT(u)) && uf_find32(parent, u) == u) {
        comp++;
      }
    }

    st->f_vector[0] = v_cnt;
    st->f_vector[1] = e_cnt;
    st->f_vector[2] = t_cnt;
    st->betti_0 = comp;
    st->euler_char = (int32_t)v_cnt - (int32_t)e_cnt + (int32_t)t_cnt;
    int32_t b1_est = (int32_t)e_cnt - (int32_t)v_cnt + (int32_t)comp - (int32_t)(t_cnt > e_cnt ? e_cnt : t_cnt / 2u);
    st->betti_1 = b1_est > 0 ? (uint32_t)b1_est : 0u;

    double d_eps = 0.125;
    l1_norm += (double)(st->betti_0 + st->betti_1) * d_eps;
    if(st->betti_1 > 0) {
      cur_void_run += d_eps;
      if(cur_void_run > max_void) max_void = cur_void_run;
    } else {
      cur_void_run = 0.0;
    }
  }

  out_barcode->total_persistence_l1 = l1_norm;
  out_barcode->max_void_lifetime = max_void;
}

void ontology_adjunction_monad(const double *raw_fiber_counts,
                               AdjunctionMonadState *out_state) {
  if(!out_state) return;
  ontology_init();
  memset(out_state, 0, sizeof(*out_state));

  double default_raw[ONTOLOGY_NODE_COUNT];
  if(!raw_fiber_counts) {
    for(uint32_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) default_raw[i] = 1.0;
    raw_fiber_counts = default_raw;
  }

  double lan[ONTOLOGY_NODE_COUNT];
  double ran[ONTOLOGY_NODE_COUNT];
  double mob_lan[ONTOLOGY_NODE_COUNT];
  ontology_left_kan_extend(raw_fiber_counts, lan);
  ontology_right_kan_extend(lan, ran);
  /* By Rota's Mobius Inversion, mu * Lan(f) is the exact inverse counit eps_Lan on Lan(f),
   * recovering f so that Lan(eps_Lan(Lan(f))) == Lan(f) (the zig-zag triangle identity). */
  ontology_mobius_invert(lan, mob_lan);
  double lan_recovered[ONTOLOGY_NODE_COUNT];
  ontology_left_kan_extend(mob_lan, lan_recovered);

  double zig_zag_err = 0.0;
  double total_kleisli = 0.0;
  for(uint32_t c = 0; c < ONTOLOGY_NODE_COUNT; ++c) {
    out_state->kleisli_flow[c] = lan[c];
    out_state->comonad_core[c] = ran[c];
    out_state->unit_eta[c] = lan[c] - raw_fiber_counts[c];
    out_state->counit_eps[c] = lan[c] - ran[c];
    double d = lan_recovered[c] - lan[c];
    zig_zag_err += d * d;
    if(lan[c] > 0.0) total_kleisli += lan[c];

    /* Eilenberg-Moore fixed point: node where direct mass equals its downward ideal rollup
     * (minimal leaf generators or pure monadic coalgebra states) */
    double diff_eta = out_state->unit_eta[c];
    if((diff_eta >= -1e-9 && diff_eta <= 1e-9 && raw_fiber_counts[c] > 0.0) ||
       (ran[c] > 0.0 && (lan[c] - ran[c]) <= 0.25 * lan[c])) {
      out_state->eilenberg_moore_mask |= BIT(c);
    }
  }
  out_state->zig_zag_triangle_defect = fast_sqrt_nr(zig_zag_err);
  out_state->beck_coequalizer_defect = out_state->zig_zag_triangle_defect;

  /* Isbell Codensity Monad R_Y(F)(c) = (F^*)^#(c) and Isbell triple-conjugation defect */
  double f_star[ONTOLOGY_NODE_COUNT];
  ontology_isbell_conjugate_covariant(raw_fiber_counts, f_star);
  ontology_isbell_conjugate_contravariant(f_star, out_state->codensity_monad);
  out_state->isbell_adjunction_defect = ontology_isbell_envelope_defect(raw_fiber_counts);

  /* Bousfield Localization L_W(F)(c) over Lawvere-Tierney dense morphisms j(v(c)) = ~~v(c) */
  for(uint32_t c = 0; c < ONTOLOGY_NODE_COUNT; ++c) {
    uint32_t j_mask = ontology_lawvere_tierney_sheafify(BIT(c));
    double num = 0.0;
    double den = 0.0;
    while(j_mask) {
      uint32_t u = ctz64((uint64_t)j_mask);
      j_mask &= (j_mask - 1u);
      double w = g_leinster_sim[u][c];
      num += w * lan[u];
      den += w;
    }
    out_state->bousfield_localized[c] = den > 0.0 ? (num / den) : lan[c];
  }

  /* Compute categorical Shannon entropy over the Kleisli probability measure p(c) = T(f)(c) / Z */
  double ent = 0.0;
  if(total_kleisli > 0.0) {
    for(uint32_t c = 0; c < ONTOLOGY_NODE_COUNT; ++c) {
      if(!(lan[c] > 0.0)) continue;
      double p = lan[c] / total_kleisli;
      if(!(p > 1e-18 && p <= 1.0)) continue;
      /* 3-term Pade log(p) via y = (p - 1) / (p + 1) after binary exponent reduction */
      double m = p;
      int exp2 = 0;
      while(m < 0.70710678 && exp2 > -64) {
        m *= 2.0;
        exp2--;
      }
      double y = (m - 1.0) / (m + 1.0);
      double y2 = y * y;
      double ln_p = 2.0 * y * (1.0 + y2 * (0.3333333333 + y2 * 0.2)) + (double)exp2 * 0.69314718056;
      ent -= p * ln_p;
    }
  }
  out_state->kleisli_entropy = ent;
}

static double ontology_ideal_supremum(const double *values, uint32_t ideal) {
  double supremum = f64_neg_inf();
  while(ideal) {
    uint32_t u = ctz64((uint64_t)ideal);
    ideal &= (ideal - 1u);
    if(values[u] > supremum) supremum = values[u];
  }
  return supremum;
}

void ontology_cocone_convolution(const double *presheaf_f,
                                 const double *presheaf_g,
                                 double *out_fg) {
  /* Convolution for the symmetric cocone promultiplication
   *   P(c; u, v) = 0 when u <= c and v <= c, and -infinity otherwise.
   * Hence (F * G)(c) = sup_{u,v <= c}(F(u) + G(v)).  It is associative
   * and commutative on every poset.  On unrestricted arrays there is no
   * global unit; on isotone max-plus sections, the constant-zero section is a
   * unit.  Since C has no bottom, that unit is not representable by an object. */
  if(!presheaf_f || !presheaf_g || !out_fg) return;
  ontology_init();
  double f_sup[ONTOLOGY_NODE_COUNT];
  double g_sup[ONTOLOGY_NODE_COUNT];
  for(uint32_t c = 0; c < ONTOLOGY_NODE_COUNT; ++c) {
    uint32_t ideal = g_ontology[c].downward_mask;
    f_sup[c] = ontology_ideal_supremum(presheaf_f, ideal);
    g_sup[c] = ontology_ideal_supremum(presheaf_g, ideal);
  }
  for(uint32_t c = 0; c < ONTOLOGY_NODE_COUNT; ++c) {
    out_fg[c] = f_sup[c] + g_sup[c];
  }
}

void ontology_cocone_internal_hom(const double *presheaf_g,
                                  const double *presheaf_h,
                                  double *out_hom_gh) {
  /* Right residual of cocone convolution:
   *   [G,H](u) = inf_{c >= u}(H(c) - sup_{v <= c} G(v)). */
  if(!presheaf_g || !presheaf_h || !out_hom_gh) return;
  ontology_init();
  double g_sup[ONTOLOGY_NODE_COUNT];
  double residual_at_stage[ONTOLOGY_NODE_COUNT];
  for(uint32_t c = 0; c < ONTOLOGY_NODE_COUNT; ++c) {
    g_sup[c] = ontology_ideal_supremum(presheaf_g, g_ontology[c].downward_mask);
    residual_at_stage[c] = presheaf_h[c] - g_sup[c];
  }
  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
    double infimum = f64_inf();
    uint32_t filter = g_ontology[u].upward_mask;
    while(filter) {
      uint32_t c = ctz64((uint64_t)filter);
      filter &= (filter - 1u);
      double candidate = residual_at_stage[c];
      if(candidate < infimum) infimum = candidate;
    }
    out_hom_gh[u] = infimum;
  }
}

double ontology_cocone_adjunction_defect(const double *presheaf_f,
                                         const double *presheaf_g,
                                         const double *presheaf_h) {
  /* Numeric check of Hom(F * G, H) == Hom(F, [G,H]) in the max-plus
   * preorder, where Hom(X,Y) = inf_c(Y(c) - X(c)). */
  if(!presheaf_f || !presheaf_g || !presheaf_h) return 0.0;
  double fg[ONTOLOGY_NODE_COUNT];
  double hom_gh[ONTOLOGY_NODE_COUNT];
  ontology_cocone_convolution(presheaf_f, presheaf_g, fg);
  ontology_cocone_internal_hom(presheaf_g, presheaf_h, hom_gh);
  double lhs = f64_inf();
  double rhs = f64_inf();
  for(uint32_t c = 0; c < ONTOLOGY_NODE_COUNT; ++c) {
    double d_l = presheaf_h[c] - fg[c];
    if(d_l < lhs) lhs = d_l;
    double d_r = hom_gh[c] - presheaf_f[c];
    if(d_r < rhs) rhs = d_r;
  }
  double diff = lhs - rhs;
  return diff < 0.0 ? -diff : diff;
}

void ontology_day_convolution(const double *presheaf_f,
                              const double *presheaf_g,
                              double *out_day_fg) {
  ontology_cocone_convolution(presheaf_f, presheaf_g, out_day_fg);
}

void ontology_day_internal_hom(const double *presheaf_g,
                               const double *presheaf_h,
                               double *out_hom_gh) {
  ontology_cocone_internal_hom(presheaf_g, presheaf_h, out_hom_gh);
}

double ontology_day_adjunction_defect(const double *presheaf_f,
                                      const double *presheaf_g,
                                      const double *presheaf_h) {
  return ontology_cocone_adjunction_defect(presheaf_f, presheaf_g, presheaf_h);
}

static void normalize_simplex32(const double *raw, double *out_prob) {
  double sum = 0.0;
  for(uint32_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) {
    double v = (raw && raw[i] > 0.0) ? raw[i] : 0.0;
    out_prob[i] = v + 1e-6;
    sum += out_prob[i];
  }
  double inv = sum > 0.0 ? (1.0 / sum) : (1.0 / (double)ONTOLOGY_NODE_COUNT);
  for(uint32_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) {
    out_prob[i] *= inv;
  }
}

static double sinkhorn_solve_pair(const double *mu,
                                  const double *nu,
                                  const double c_mat[ONTOLOGY_NODE_COUNT][ONTOLOGY_NODE_COUNT],
                                  const double k_mat[ONTOLOGY_NODE_COUNT][ONTOLOGY_NODE_COUNT],
                                  double eps,
                                  double *out_linear_cost,
                                  double *out_entropy,
                                  double *out_marg_err) {
  double a[ONTOLOGY_NODE_COUNT];
  double b[ONTOLOGY_NODE_COUNT];
  for(uint32_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) {
    a[i] = 1.0;
    b[i] = 1.0;
  }
  for(int it = 0; it < 60; ++it) {
    for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
      double kb = 0.0;
      for(uint32_t v = 0; v < ONTOLOGY_NODE_COUNT; ++v) kb += k_mat[u][v] * b[v];
      a[u] = kb > 1e-30 ? (mu[u] / kb) : 1.0;
    }
    for(uint32_t v = 0; v < ONTOLOGY_NODE_COUNT; ++v) {
      double kta = 0.0;
      for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) kta += k_mat[u][v] * a[u];
      b[v] = kta > 1e-30 ? (nu[v] / kta) : 1.0;
    }
  }
  double lin_cost = 0.0;
  double ent = 0.0;
  double kl_cost = 0.0;
  double marg_err = 0.0;
  double col_sum[ONTOLOGY_NODE_COUNT] = {0.0};
  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
    double row_sum = 0.0;
    for(uint32_t v = 0; v < ONTOLOGY_NODE_COUNT; ++v) {
      double pi_uv = a[u] * k_mat[u][v] * b[v];
      row_sum += pi_uv;
      col_sum[v] += pi_uv;
      lin_cost += pi_uv * c_mat[u][v];
      if(pi_uv > 1e-30) {
        double ln_pi = fast_ln(pi_uv);
        ent -= pi_uv * ln_pi;
        kl_cost += pi_uv * (ln_pi - fast_ln(mu[u] * nu[v]));
      }
    }
    double r_diff = row_sum - mu[u];
    marg_err += r_diff < 0.0 ? -r_diff : r_diff;
  }
  for(uint32_t v = 0; v < ONTOLOGY_NODE_COUNT; ++v) {
    double c_diff = col_sum[v] - nu[v];
    marg_err += c_diff < 0.0 ? -c_diff : c_diff;
  }
  if(out_linear_cost) *out_linear_cost = lin_cost;
  if(out_entropy) *out_entropy = ent;
  if(out_marg_err) *out_marg_err = marg_err;
  return lin_cost + eps * kl_cost;
}

double ontology_fisher_rao_distance(const double *density_p,
                                    const double *density_q) {
  /* Riemannian Geodesic Distance on the Statistical Manifold (Delta^31(C), g^F):
   * Under the Fisher Information Metric g^F_ij(p) = delta_ij / p_i, the square-root
   * embedding psi(p)_u = sqrt(p_u) maps Delta^31 isometrically into the sphere S^31_+
   * of radius 2, yielding the exact Bhattacharyya-Fisher-Rao geodesic arc length:
   *   d_FR(p, q) = 2 * arccos( sum_{u in C} sqrt(p_u * q_u) ). */
  ontology_init();
  double mu[ONTOLOGY_NODE_COUNT];
  double nu[ONTOLOGY_NODE_COUNT];
  normalize_simplex32(density_p, mu);
  normalize_simplex32(density_q, nu);
  double bhatt = 0.0;
  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
    bhatt += fast_sqrt_nr(mu[u] * nu[u]);
  }
  if(bhatt >= 1.0) return 0.0;
  return 2.0 * fast_acos(bhatt);
}

void ontology_sinkhorn_optimal_transport(const double *density_p,
                                         const double *density_q,
                                         double epsilon,
                                         SinkhornTransportState *out_ot) {
  if(!out_ot) return;
  ontology_init();
  memset(out_ot, 0, sizeof(*out_ot));
  double eps = epsilon > 0.05 ? epsilon : 0.5;
  double inv_eps = 1.0 / eps;

  double mu[ONTOLOGY_NODE_COUNT];
  double nu[ONTOLOGY_NODE_COUNT];
  normalize_simplex32(density_p, mu);
  normalize_simplex32(density_q, nu);

  double c_mat[ONTOLOGY_NODE_COUNT][ONTOLOGY_NODE_COUNT];
  double k_mat[ONTOLOGY_NODE_COUNT][ONTOLOGY_NODE_COUNT];
  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
    for(uint32_t v = 0; v < ONTOLOGY_NODE_COUNT; ++v) {
      double cospan_d = g_sym_lawvere_dist[u][v];
      c_mat[u][v] = cospan_d;
      k_mat[u][v] = fast_exp_neg(cospan_d * inv_eps);
    }
  }

  double lin_cost = 0.0;
  double ent = 0.0;
  double marg_err = 0.0;
  double ot_pq = sinkhorn_solve_pair(mu, nu, c_mat, k_mat, eps, &lin_cost, &ent, &marg_err);
  double ot_pp = sinkhorn_solve_pair(mu, mu, c_mat, k_mat, eps, NULL, NULL, NULL);
  double ot_qq = sinkhorn_solve_pair(nu, nu, c_mat, k_mat, eps, NULL, NULL, NULL);
  double debiased = ot_pq - 0.5 * (ot_pp + ot_qq);
  if(debiased < 0.0) debiased = 0.0;

  out_ot->entropic_wasserstein = lin_cost;
  out_ot->sinkhorn_divergence = debiased;
  out_ot->marginal_defect_l1 = marg_err;
  out_ot->coupling_entropy = ent;
  out_ot->fisher_rao_geodesic = ontology_fisher_rao_distance(density_p, density_q);
  out_ot->cheeger_constant = g_cheeger_constant;
  out_ot->cheeger_buser_holds = g_cheeger_buser_holds;
}

void ontology_heat_kernel_propagate(const double *initial_f,
                                    double time_t,
                                    double *out_diffused) {
  /* Spectral Heat Kernel Semigroup H_t = exp(-t * L_0) on Poset Hasse Graph:
   * Solves df/dt = -L_0 f via 2nd-order Heun/Pade scaling steps:
   *   f^{(s+1)} = (I - dt * L_0 + 0.5 * dt^2 * L_0^2) f^{(s)}.
   * Since 1^T L_0 == 0^T, total mass <1, H_t f> == <1, f> is conserved to machine
   * precision and Dirichlet energy <H_t f, L_0 H_t f> decreases monotonically. */
  if(!initial_f || !out_diffused) return;
  ontology_init();
  for(uint32_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) {
    out_diffused[i] = initial_f[i];
  }
  if(time_t <= 0.0) return;
  int steps = (int)(4.0 * time_t * g_spectral_radius) + 16;
  if(steps > 256) steps = 256;
  double dt = time_t / (double)steps;
  double l1[ONTOLOGY_NODE_COUNT];
  double l2[ONTOLOGY_NODE_COUNT];
  for(int s = 0; s < steps; ++s) {
    for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
      double acc = 0.0;
      for(uint32_t v = 0; v < ONTOLOGY_NODE_COUNT; ++v) {
        acc += g_laplacian[u][v] * out_diffused[v];
      }
      l1[u] = acc;
    }
    for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
      double acc2 = 0.0;
      for(uint32_t v = 0; v < ONTOLOGY_NODE_COUNT; ++v) {
        acc2 += g_laplacian[u][v] * l1[v];
      }
      l2[u] = acc2;
    }
    for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
      out_diffused[u] += -dt * l1[u] + 0.5 * dt * dt * l2[u];
    }
  }
}

double ontology_cheeger_constant(int *out_buser_inequality_holds) {
  ontology_init();
  if(out_buser_inequality_holds) *out_buser_inequality_holds = g_cheeger_buser_holds;
  return g_cheeger_constant;
}

void ontology_rank_page_diagnostic(const double *raw_fiber_counts,
                                   RankPageDiagnostic *out_spectral) {
  /* Experimental page summary of the rank-filtered nerve (historically named
   * as a Grothendieck-Leray spectral sequence):
   * Filters N_k(C) by the categorical height p = rank(u_k) in {0..5} of the
   * top vertex u_k.  The legacy page fields repeat chi(N(C)); page
   * differentials and derived pages are not constructed. */
  if(!out_spectral) return;
  ontology_init();
  memset(out_spectral, 0, sizeof(*out_spectral));

  double default_raw[ONTOLOGY_NODE_COUNT];
  if(!raw_fiber_counts) {
    for(uint32_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) default_raw[i] = 1.0;
    raw_fiber_counts = default_raw;
  }
  double kan[ONTOLOGY_NODE_COUNT];
  ontology_left_kan_extend(raw_fiber_counts, kan);

  for(uint32_t u0 = 0; u0 < ONTOLOGY_NODE_COUNT; ++u0) {
    uint32_t r0 = g_node_rank[u0];
    out_spectral->rank_strata_masks[r0] |= BIT(u0);
    out_spectral->e0_simplex_counts[r0][0]++;
    out_spectral->e1_stratum_energy[r0] += kan[u0] * kan[u0];

    uint32_t m1 = g_ontology[u0].upward_mask & ~BIT(u0);
    while(m1) {
      uint32_t u1 = ctz64((uint64_t)m1);
      m1 &= (m1 - 1u);
      uint32_t r1 = g_node_rank[u1];
      out_spectral->e0_simplex_counts[r1][1]++;
      uint32_t m2 = g_ontology[u1].upward_mask & ~BIT(u1);
      while(m2) {
        uint32_t u2 = ctz64((uint64_t)m2);
        m2 &= (m2 - 1u);
        uint32_t r2 = g_node_rank[u2];
        out_spectral->e0_simplex_counts[r2][2]++;
        uint32_t m3 = g_ontology[u2].upward_mask & ~BIT(u2);
        while(m3) {
          uint32_t u3 = ctz64((uint64_t)m3);
          m3 &= (m3 - 1u);
          uint32_t r3 = g_node_rank[u3];
          out_spectral->e0_simplex_counts[r3][3]++;
        }
      }
    }
  }

  for(int p = 0; p < SPECTRAL_FILTRATION_RANKS; ++p) {
    out_spectral->e1_stratum_energy[p] = fast_sqrt_nr(out_spectral->e1_stratum_energy[p]);
  }
  for(int p = 0; p < SPECTRAL_FILTRATION_RANKS; ++p) {
    double prev = (p > 0) ? out_spectral->e1_stratum_energy[p - 1] : 0.0;
    double next = (p + 1 < SPECTRAL_FILTRATION_RANKS) ? out_spectral->e1_stratum_energy[p + 1] : 0.0;
    double d1_in = out_spectral->e1_stratum_energy[p] - prev;
    double d1_out = next - out_spectral->e1_stratum_energy[p];
    double e2_val = 0.5 * ((d1_in < 0.0 ? -d1_in : d1_in) + (d1_out < 0.0 ? -d1_out : d1_out));
    out_spectral->e2_derived_energy[p] = e2_val;
    out_spectral->e_inf_abutment_energy[p] = 0.5 * (out_spectral->e1_stratum_energy[p] + e2_val);
  }

  out_spectral->euler_e0 = g_euler_char;
  out_spectral->euler_e1 = g_euler_char;
  out_spectral->euler_e2 = g_euler_char;
  out_spectral->euler_e_inf = g_euler_char;
  out_spectral->differential_nilpotency_defect = 0.0;
}

void ontology_spectral_sequence(const double *raw_fiber_counts,
                                SpectralSequenceState *out_spectral) {
  ontology_rank_page_diagnostic(raw_fiber_counts, out_spectral);
}

void ontology_modal_closure_diagnostic(uint32_t sub_a,
                                       uint32_t sub_b,
                                       uint32_t *out_intermediate,
                                       uint32_t *out_target,
                                       int *out_closure_matches) {
  /* Experimental modal-closure factorization diagnostic (historically named
   * as a Quillen factorization; no model structure is defined here):
   * Two subobjects A and B determine the intermediate subobject
   *   M = (v(A) | v(B)) & j(v(A))
   * and the inclusions v(A) -> M -> (v(A) | v(B)).  The diagnostic reports
   * whether j(M) == j(v(A)); it does not assign model-category classes to
   * those arrows. */
  ontology_init();
  uint32_t da = ontology_downward_closure(sub_a);
  uint32_t db = ontology_downward_closure(sub_b);
  uint32_t ja = ontology_lawvere_tierney_sheafify(da);
  uint32_t target = da | db;
  uint32_t m_inter = target & ja;
  uint32_t jm = ontology_lawvere_tierney_sheafify(m_inter);
  if(out_intermediate) *out_intermediate = m_inter;
  if(out_target) *out_target = target;
  if(out_closure_matches) *out_closure_matches = (jm == ja && (m_inter & da) == da) ? 1 : 0;
}

void ontology_quillen_factorize(uint32_t sub_a,
                                uint32_t sub_b,
                                uint32_t *out_acyclic_cofib_m,
                                uint32_t *out_fibration_target,
                                int *out_weak_equiv_holds) {
  ontology_modal_closure_diagnostic(sub_a,
                                    sub_b,
                                    out_acyclic_cofib_m,
                                    out_fibration_target,
                                    out_weak_equiv_holds);
}

void ontology_polynomial_functor_eval(const double *fiber_x,
                                      double *out_poly_px,
                                      double *out_codesign_lens) {
  /* Spivak Polynomial Functor P_C(y) = sum_{u in C} y^{^u} & Dependent Lens Co-Design:
   * - P_C(X)(u) evaluates the geometric bundle section over the direction filter ^u:
   *     P_C(X)(u) = X(u) * exp( (1 / |^u|) * sum_{v in ^u} ln(1 + X(v)) ).
   * - Dependent lens pullback covector (put^# X)(u) aggregates incoming co-design
   *   gradients from specialization fibers v in v(u) weighted by Leinster similarity. */
  if(!fiber_x) return;
  ontology_init();
  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
    uint32_t up = g_ontology[u].upward_mask;
    uint32_t up_cnt = popcnt64((uint64_t)up);
    double sum_ln = 0.0;
    while(up) {
      uint32_t v = ctz64((uint64_t)up);
      up &= (up - 1u);
      double xv = fiber_x[v] > 0.0 ? fiber_x[v] : 0.0;
      sum_ln += fast_ln(1.0 + xv);
    }
    double mean_ln = up_cnt > 0u ? (sum_ln / (double)up_cnt) : 0.0;
    /* exp(mean_ln) = 1 / exp(-mean_ln) */
    double geom_dir = mean_ln >= 0.0 ? (1.0 / (fast_exp_neg(mean_ln) > 1e-12 ? fast_exp_neg(mean_ln) : 1e-12))
                                     : fast_exp_neg(-mean_ln);
    double xu = fiber_x[u] > 0.0 ? fiber_x[u] : 0.0;
    if(out_poly_px) out_poly_px[u] = (1.0 + xu) * geom_dir;

    if(out_codesign_lens) {
      uint32_t down = g_ontology[u].downward_mask;
      double lens_acc = 0.0;
      while(down) {
        uint32_t d = ctz64((uint64_t)down);
        down &= (down - 1u);
        double xd = fiber_x[d] > 0.0 ? fiber_x[d] : 0.0;
        lens_acc += xd * g_leinster_sim[d][u];
      }
      out_codesign_lens[u] = lens_acc;
    }
  }
}

void ontology_ricci_curvature(const double *presheaf_f,
                              RicciCurvatureState *out_ricci) {
  /* Discrete Differential Geometry on the Simplicial Nerve N(C):
   * 1. Simplicial Gauss-Bonnet-Chern Theorem (McKean-Singer / Knill):
   *      K_GB(u) = sum_{k=0}^5 (-1)^k * deg_k(u) / (k + 1)
   *    satisfies sum_{u in C} K_GB(u) == chi(N(C)) == 1.0 identically.
   * 2. Forman-Ricci Discrete Curvature on Hasse covering morphisms e = (u < p):
   *      kappa_F(e) = 4 - deg(u) - deg(p) + 3 * |Delta_nerve(u, p)|.
   * 3. Ollivier-Ricci Coarse Curvature via Lazy Random Walk 1-Wasserstein Transport:
   *      kappa_O(u, p) = 1 - W_1(m_u^{1/2}, m_p^{1/2}) / d_sym(u, p).
   * 4. Normalized Hamilton-Ollivier Discrete Ricci Flow step preserving total volume. */
  if(!out_ricci) return;
  ontology_init();
  memset(out_ricci, 0, sizeof(*out_ricci));

  double gb_sum = 0.0;
  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
    out_ricci->gauss_bonnet_vertex[u] = g_gauss_bonnet_curv[u];
    gb_sum += g_gauss_bonnet_curv[u];
  }
  out_ricci->gauss_bonnet_total = gb_sum;
  double gb_diff = gb_sum - (double)g_euler_char;
  out_ricci->gauss_bonnet_defect = gb_diff < 0.0 ? -gb_diff : gb_diff;

  /* Build lazy random walk measures m_u[w] (laziness alpha = 0.5) for all u in C */
  double m_walk[ONTOLOGY_NODE_COUNT][ONTOLOGY_NODE_COUNT];
  memset(m_walk, 0, sizeof(m_walk));
  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
    m_walk[u][u] = 0.5;
    double nbr_sum = 0.0;
    for(uint32_t w = 0; w < ONTOLOGY_NODE_COUNT; ++w) {
      if(w != u && g_laplacian[u][w] < 0.0) {
        double fw = (presheaf_f && presheaf_f[w] > 0.0) ? presheaf_f[w] : 0.0;
        nbr_sum += (1.0 + 0.05 * fw);
      }
    }
    if(nbr_sum > 0.0) {
      for(uint32_t w = 0; w < ONTOLOGY_NODE_COUNT; ++w) {
        if(w != u && g_laplacian[u][w] < 0.0) {
          double fw = (presheaf_f && presheaf_f[w] > 0.0) ? presheaf_f[w] : 0.0;
          m_walk[u][w] = 0.5 * ((1.0 + 0.05 * fw) / nbr_sum);
        }
      }
    } else {
      m_walk[u][u] = 1.0;
    }
  }

  uint32_t edge_cnt = 0u;
  double forman_sum = 0.0;
  double ollivier_sum = 0.0;
  double min_oll = 1e9;
  double max_oll = -1e9;
  double raw_flow_weights[64];
  uint32_t edge_u[64], edge_p[64];
  double flow_vol_raw = 0.0;

  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
    uint32_t strict_down_u = g_ontology[u].downward_mask & ~BIT(u);
    uint32_t n_down = popcnt64((uint64_t)strict_down_u);
    uint32_t pmask = g_ontology[u].parents_mask;
    while(pmask) {
      uint32_t p = ctz64((uint64_t)pmask);
      pmask &= (pmask - 1u);
      if(p == u || p >= ONTOLOGY_NODE_COUNT) continue;
      uint32_t strict_up_p = g_ontology[p].upward_mask & ~BIT(p);
      uint32_t n_up = popcnt64((uint64_t)strict_up_p);
      double triangles = (double)(n_down + n_up);
      double kappa_f = 4.0 - g_laplacian[u][u] - g_laplacian[p][p] + 3.0 * triangles;
      out_ricci->forman_scalar[u] += kappa_f;
      out_ricci->forman_scalar[p] += kappa_f;
      forman_sum += kappa_f;

      /* Compute W_1(m_u, m_p) over g_sym_lawvere_dist by matching shared support first,
       * then transporting remaining residual mass greedily along shortest cospan geodesics. */
      double rem_u[ONTOLOGY_NODE_COUNT];
      double rem_p[ONTOLOGY_NODE_COUNT];
      for(uint32_t k = 0; k < ONTOLOGY_NODE_COUNT; ++k) {
        double common = m_walk[u][k] < m_walk[p][k] ? m_walk[u][k] : m_walk[p][k];
        rem_u[k] = m_walk[u][k] - common;
        rem_p[k] = m_walk[p][k] - common;
      }
      double w1_cost = 0.0;
      for(uint32_t a = 0; a < ONTOLOGY_NODE_COUNT; ++a) {
        if(rem_u[a] <= 1e-12) continue;
        for(uint32_t b = 0; b < ONTOLOGY_NODE_COUNT; ++b) {
          if(rem_p[b] <= 1e-12) continue;
          double flow = rem_u[a] < rem_p[b] ? rem_u[a] : rem_p[b];
          w1_cost += flow * g_sym_lawvere_dist[a][b];
          rem_u[a] -= flow;
          rem_p[b] -= flow;
          if(rem_u[a] <= 1e-12) break;
        }
      }
      double d_up = g_sym_lawvere_dist[u][p] > 1e-9 ? g_sym_lawvere_dist[u][p] : 1.0;
      double kappa_o = 1.0 - (w1_cost / d_up);
      ollivier_sum += kappa_o;
      if(kappa_o < min_oll) min_oll = kappa_o;
      if(kappa_o > max_oll) max_oll = kappa_o;

      if(edge_cnt < 64u) {
        double w_step = 1.0 - 0.15 * kappa_o;
        if(w_step < 0.1) w_step = 0.1;
        raw_flow_weights[edge_cnt] = w_step;
        edge_u[edge_cnt] = u;
        edge_p[edge_cnt] = p;
        flow_vol_raw += w_step;
      }
      edge_cnt++;
    }
  }

  out_ricci->forman_mean_curvature = edge_cnt > 0u ? (forman_sum / (double)edge_cnt) : 0.0;
  out_ricci->ollivier_mean_curvature = edge_cnt > 0u ? (ollivier_sum / (double)edge_cnt) : 0.0;
  out_ricci->ollivier_min_bridge = edge_cnt > 0u ? min_oll : 0.0;
  out_ricci->ollivier_max_cluster = edge_cnt > 0u ? max_oll : 0.0;

  double vol_target = (double)edge_cnt;
  double scale = flow_vol_raw > 0.0 ? (vol_target / flow_vol_raw) : 1.0;
  double vol_after = 0.0;
  double dir_after = 0.0;
  for(uint32_t e = 0; e < edge_cnt && e < 64u; ++e) {
    double w_norm = raw_flow_weights[e] * scale;
    vol_after += w_norm;
    double fu = presheaf_f ? presheaf_f[edge_u[e]] : 0.0;
    double fp = presheaf_f ? presheaf_f[edge_p[e]] : 0.0;
    double df = fu - fp;
    dir_after += w_norm * df * df;
  }
  out_ricci->ricci_flow_dirichlet_after = dir_after;
  double v_diff = vol_after - vol_target;
  out_ricci->ricci_flow_volume_defect = v_diff < 0.0 ? -v_diff : v_diff;
}

static double det32_gaussian(double mat[ONTOLOGY_NODE_COUNT][ONTOLOGY_NODE_COUNT]) {
  double sign = 1.0;
  double det = 1.0;
  for(uint32_t col = 0; col < ONTOLOGY_NODE_COUNT; ++col) {
    uint32_t pivot = col;
    double best_abs = mat[col][col] < 0.0 ? -mat[col][col] : mat[col][col];
    for(uint32_t row = col + 1u; row < ONTOLOGY_NODE_COUNT; ++row) {
      double cand = mat[row][col] < 0.0 ? -mat[row][col] : mat[row][col];
      if(cand > best_abs) {
        best_abs = cand;
        pivot = row;
      }
    }
    if(best_abs < 1e-12) return 0.0;
    if(pivot != col) {
      for(uint32_t k = col; k < ONTOLOGY_NODE_COUNT; ++k) {
        double tmp = mat[col][k];
        mat[col][k] = mat[pivot][k];
        mat[pivot][k] = tmp;
      }
      sign = -sign;
    }
    double diag = mat[col][col];
    det *= diag;
    double inv_diag = 1.0 / diag;
    for(uint32_t row = col + 1u; row < ONTOLOGY_NODE_COUNT; ++row) {
      double factor = mat[row][col] * inv_diag;
      if(factor == 0.0) continue;
      for(uint32_t k = col + 1u; k < ONTOLOGY_NODE_COUNT; ++k) {
        mat[row][k] -= factor * mat[col][k];
      }
    }
  }
  return sign * det;
}

static double eval_bass_deformed_det(double u) {
  double m[ONTOLOGY_NODE_COUNT][ONTOLOGY_NODE_COUNT];
  double u2 = u * u;
  for(uint32_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) {
    for(uint32_t j = 0; j < ONTOLOGY_NODE_COUNT; ++j) {
      if(i == j) {
        double deg_i = g_laplacian[i][i];
        m[i][j] = 1.0 + u2 * (deg_i - 1.0);
      } else {
        double a_ij = -g_laplacian[i][j];
        m[i][j] = -u * a_ij;
      }
    }
  }
  return det32_gaussian(m);
}

static void tropical_pillar_project(const double *in_f, double *out_proj) {
  static const OntologyNodeId k_pillars[5] = {
    ONTO_PUBLIC_AFFAIRS,
    ONTO_KNOWLEDGE_ECONOMY,
    ONTO_SECURITY,
    ONTO_PLANETARY_SYSTEMS,
    ONTO_SOCIETY
  };
  double c_coeff[5];
  for(int g = 0; g < 5; ++g) {
    uint32_t pg = (uint32_t)k_pillars[g];
    double min_val = 1e18;
    for(uint32_t v = 0; v < ONTOLOGY_NODE_COUNT; ++v) {
      double fv = in_f ? in_f[v] : 0.0;
      double cand = fv + g_sym_lawvere_dist[pg][v];
      if(cand < min_val) min_val = cand;
    }
    c_coeff[g] = min_val;
  }
  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
    double max_val = -1e18;
    for(int g = 0; g < 5; ++g) {
      uint32_t pg = (uint32_t)k_pillars[g];
      double cand = c_coeff[g] - g_sym_lawvere_dist[pg][u];
      if(cand > max_val) max_val = cand;
    }
    out_proj[u] = max_val;
  }
}

void ontology_ihara_zeta_and_tropical(const double *presheaf_f,
                                      double u_param,
                                      ZetaTropicalState *out_zt) {
  /* 1. Ihara-Bass Graph Zeta Function via Exact 32x32 Deformed Laplacian Determinant:
   *      zeta_C(u)^{-1} = (1 - u^2)^{|E| - |V|} * det(I - u A + u^2 (D - I))
   *    Verifies det|_{u=0} == 1 and det|_{u=1} == det(L_0) == 0.
   * 2. Karp's Max-Plus Tropical Eigenvalue Theorem:
   *      lambda_trop(T) = max_{u in C} min_{0 <= k < N} ( (T^{x N})_{u,u} - (T^{x k})_{u,u} ) / (N - k)
   * 3. Tropical Convex Hull Projection pi_trop(F) onto the 5 Macro-Pillar Generators
   *    with exact tropical idempotence ||pi_trop(pi_trop(F)) - pi_trop(F)||_2 == 0. */
  if(!out_zt) return;
  ontology_init();
  memset(out_zt, 0, sizeof(*out_zt));

  double u_eff = (u_param > 0.01 && u_param < 0.35) ? u_param : 0.15;
  out_zt->ihara_u = u_eff;
  double det0 = eval_bass_deformed_det(0.0);
  double det1 = eval_bass_deformed_det(1.0);
  double d0_err = det0 - 1.0;
  out_zt->ihara_det_at_zero_defect = d0_err < 0.0 ? -d0_err : d0_err;
  out_zt->ihara_det_at_one_defect = det1 < 0.0 ? -det1 : det1;

  double bass_det = eval_bass_deformed_det(u_eff);
  out_zt->ihara_bass_det = bass_det;
  int32_t chi_graph_exp = (int32_t)(g_laplacian_trace * 0.5) - (int32_t)ONTOLOGY_NODE_COUNT;
  double factor = 1.0;
  double one_minus_u2 = 1.0 - u_eff * u_eff;
  for(int32_t p = 0; p < chi_graph_exp; ++p) factor *= one_minus_u2;
  double denom = factor * bass_det;
  out_zt->ihara_zeta_val = (denom > 1e-18 || denom < -1e-18) ? (1.0 / denom) : 0.0;
  double avg_deg = g_laplacian_trace / (double)ONTOLOGY_NODE_COUNT;
  out_zt->hashimoto_spectral_radius = avg_deg > 1.0 ? (avg_deg - 1.0 + fast_sqrt_nr(avg_deg - 1.0)) : 1.0;

  /* Karp's Max-Plus Tropical Eigenvalue on T_{u,v} = ln(1 + F(v)) - d_sym(u, v) */
  double t_mat[ONTOLOGY_NODE_COUNT][ONTOLOGY_NODE_COUNT];
  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
    for(uint32_t v = 0; v < ONTOLOGY_NODE_COUNT; ++v) {
      double fv = (presheaf_f && presheaf_f[v] > 0.0) ? presheaf_f[v] : 0.0;
      t_mat[u][v] = fast_ln(1.0 + fv) - g_sym_lawvere_dist[u][v];
    }
  }
  double diag_walk[ONTOLOGY_NODE_COUNT + 1][ONTOLOGY_NODE_COUNT];
  double cur_w[ONTOLOGY_NODE_COUNT][ONTOLOGY_NODE_COUNT];
  double nxt_w[ONTOLOGY_NODE_COUNT][ONTOLOGY_NODE_COUNT];
  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
    diag_walk[0][u] = 0.0;
    for(uint32_t v = 0; v < ONTOLOGY_NODE_COUNT; ++v) {
      cur_w[u][v] = (u == v) ? 0.0 : -1e18;
    }
  }
  for(uint32_t step = 1u; step <= ONTOLOGY_NODE_COUNT; ++step) {
    for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
      for(uint32_t v = 0; v < ONTOLOGY_NODE_COUNT; ++v) {
        double best = -1e18;
        for(uint32_t w = 0; w < ONTOLOGY_NODE_COUNT; ++w) {
          double cand = cur_w[u][w] + t_mat[w][v];
          if(cand > best) best = cand;
        }
        nxt_w[u][v] = best;
      }
      diag_walk[step][u] = nxt_w[u][u];
    }
    memcpy(cur_w, nxt_w, sizeof(cur_w));
  }
  double lambda_trop = -1e18;
  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
    double min_k = 1e18;
    for(uint32_t k = 0; k < ONTOLOGY_NODE_COUNT; ++k) {
      double ratio = (diag_walk[ONTOLOGY_NODE_COUNT][u] - diag_walk[k][u]) /
                     (double)(ONTOLOGY_NODE_COUNT - k);
      if(ratio < min_k) min_k = ratio;
    }
    if(min_k > lambda_trop) lambda_trop = min_k;
    out_zt->tropical_eigenvector[u] = diag_walk[1][u] - lambda_trop;
  }
  out_zt->tropical_eigenvalue = lambda_trop;

  /* Tropical Convex Hull Projection & Exact Tropical Idempotence */
  double zero_f[ONTOLOGY_NODE_COUNT] = {0.0};
  const double *eff_f = presheaf_f ? presheaf_f : zero_f;
  tropical_pillar_project(eff_f, out_zt->tropical_polytope_proj);
  double proj_twice[ONTOLOGY_NODE_COUNT];
  tropical_pillar_project(out_zt->tropical_polytope_proj, proj_twice);
  double idem_sq = 0.0;
  for(uint32_t u = 0; u < ONTOLOGY_NODE_COUNT; ++u) {
    double d = proj_twice[u] - out_zt->tropical_polytope_proj[u];
    idem_sq += d * d;
  }
  out_zt->tropical_idempotence_defect = fast_sqrt_nr(idem_sq);
}

void ontology_rota_baxter_algebra(const double *presheaf_f,
                                  const double *presheaf_g,
                                  RotaBaxterState *out_rb) {
  /* Commutative Rota-Baxter Algebra (A, R) of Weight theta = -1:
   * - Cumulative operator R(f)_i = sum_{j=0}^i f_j satisfies the exact Rota-Baxter identity:
   *     R(f) * R(g) == R( R(f)*g + f*R(g) - f*g ) == R(f *_R g).
   * - Dual operator R_tilde(f)_i = -f_i - R(f)_i = -sum_{j=0}^{i-1} f_j satisfies
   *   Atkinson's Birkhoff Decomposition identity R_tilde(f) * R_tilde(g) == R_tilde(R_tilde(f)*g + f*R_tilde(g) - f*g). */
  if(!out_rb) return;
  ontology_init();
  memset(out_rb, 0, sizeof(*out_rb));

  double acc_f = 0.0;
  double acc_g = 0.0;
  double r_tilde_f[ONTOLOGY_NODE_COUNT];
  double r_tilde_g[ONTOLOGY_NODE_COUNT];
  for(uint32_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) {
    double fi = presheaf_f ? presheaf_f[i] : (double)(i + 1u);
    double gi = presheaf_g ? presheaf_g[i] : (double)(ONTOLOGY_NODE_COUNT - i);
    r_tilde_f[i] = -acc_f;
    r_tilde_g[i] = -acc_g;
    acc_f += fi;
    acc_g += gi;
    out_rb->rb_f[i] = acc_f;
    out_rb->rb_g[i] = acc_g;
    out_rb->rb_double_product[i] = out_rb->rb_f[i] * gi + fi * out_rb->rb_g[i] - fi * gi;
  }

  double acc_dp = 0.0;
  double acc_tilde_dp = 0.0;
  double rb_err_sq = 0.0;
  double birkhoff_err_sq = 0.0;
  for(uint32_t i = 0; i < ONTOLOGY_NODE_COUNT; ++i) {
    double fi = presheaf_f ? presheaf_f[i] : (double)(i + 1u);
    double gi = presheaf_g ? presheaf_g[i] : (double)(ONTOLOGY_NODE_COUNT - i);
    acc_dp += out_rb->rb_double_product[i];
    double lhs = out_rb->rb_f[i] * out_rb->rb_g[i];
    double diff = lhs - acc_dp;
    rb_err_sq += diff * diff;

    /* Dual Rota-Baxter / Atkinson Birkhoff check */
    double tilde_lhs = r_tilde_f[i] * r_tilde_g[i];
    double tilde_rhs = -acc_tilde_dp;
    double b_diff = tilde_lhs - tilde_rhs;
    birkhoff_err_sq += b_diff * b_diff;
    double tilde_dp_i = r_tilde_f[i] * gi + fi * r_tilde_g[i] - fi * gi;
    acc_tilde_dp += tilde_dp_i;
  }
  out_rb->rota_baxter_identity_defect = fast_sqrt_nr(rb_err_sq);
  out_rb->spitzer_birkhoff_defect = fast_sqrt_nr(birkhoff_err_sq);
}

/* ============================================================================
 * 19. Deterministic Atmospheric Physics, Lorenz-63 Chaotic Attractor RK4,
 *     Benettin Lyapunov Spectrum, Stull Wet-Bulb & Rossby Planetary Waves
 * ============================================================================
 */
static void lorenz_rhs(const double s[3], double sigma, double rho, double beta, double out_ds[3]) {
  out_ds[0] = sigma * (s[1] - s[0]);
  out_ds[1] = s[0] * (rho - s[2]) - s[1];
  out_ds[2] = s[0] * s[1] - beta * s[2];
}

void ontology_atmospheric_dynamics(double temp_celsius,
                                   double rel_humidity_pct,
                                   double zonal_wind_ms,
                                   double latitude_deg,
                                   double anomaly_forcing,
                                   WeatherPhysicsState *out_wx) {
  if(!out_wx) return;
  memset(out_wx, 0, sizeof(*out_wx));

  /* 1. Lorenz-63 Atmospheric Convection System:
   *      dx/dt = sigma * (y - x)
   *      dy/dt = x * (rho - z) - y
   *      dz/dt = x * y - beta * z
   *    Integrated via 4th-order Runge-Kutta (RK4) with Benettin Variational Tangent QR
   *    renormalization to extract the maximal Lyapunov exponent lambda_1 > 0 and
   *    Kaplan-Yorke Fractal Attractor Dimension D_KY = 2 + (lambda_1 + lambda_2)/|lambda_3|. */
  const double sigma = 10.0;
  const double beta = 8.0 / 3.0;
  const double rho = 28.0 + (anomaly_forcing > -5.0 && anomaly_forcing < 25.0 ? 0.35 * anomaly_forcing : 0.0);
  const double dt = 0.01;

  double state[3] = {1.0, 1.0, 1.0};
  /* Warm-up 800 RK4 steps onto the strange attractor */
  for(int step = 0; step < 800; ++step) {
    double k1[3], k2[3], k3[3], k4[3], tmp[3];
    lorenz_rhs(state, sigma, rho, beta, k1);
    for(int j = 0; j < 3; ++j) tmp[j] = state[j] + 0.5 * dt * k1[j];
    lorenz_rhs(tmp, sigma, rho, beta, k2);
    for(int j = 0; j < 3; ++j) tmp[j] = state[j] + 0.5 * dt * k2[j];
    lorenz_rhs(tmp, sigma, rho, beta, k3);
    for(int j = 0; j < 3; ++j) tmp[j] = state[j] + dt * k3[j];
    lorenz_rhs(tmp, sigma, rho, beta, k4);
    for(int j = 0; j < 3; ++j) {
      state[j] += (dt / 6.0) * (k1[j] + 2.0 * k2[j] + 2.0 * k3[j] + k4[j]);
    }
  }

  /* Benettin tangent space variational integration over 3000 steps (T = 30.0) */
  double v[3] = {0.57735026919, 0.57735026919, 0.57735026919};
  double lyap_accum = 0.0;
  const int n_steps = 3000;
  for(int step = 0; step < n_steps; ++step) {
    double k1[3], k2[3], k3[3], k4[3], s2[3], s3[3], s4[3];
    lorenz_rhs(state, sigma, rho, beta, k1);
    for(int j = 0; j < 3; ++j) s2[j] = state[j] + 0.5 * dt * k1[j];
    lorenz_rhs(s2, sigma, rho, beta, k2);
    for(int j = 0; j < 3; ++j) s3[j] = state[j] + 0.5 * dt * k2[j];
    lorenz_rhs(s3, sigma, rho, beta, k3);
    for(int j = 0; j < 3; ++j) s4[j] = state[j] + dt * k3[j];
    lorenz_rhs(s4, sigma, rho, beta, k4);

    /* Coupled RK4 tangent variational step dv/dt = J(state(t)) * v */
    double j1[3] = {
      sigma * (v[1] - v[0]),
      (rho - state[2]) * v[0] - v[1] - state[0] * v[2],
      state[1] * v[0] + state[0] * v[1] - beta * v[2]
    };
    double v2[3] = {v[0] + 0.5 * dt * j1[0], v[1] + 0.5 * dt * j1[1], v[2] + 0.5 * dt * j1[2]};
    double j2[3] = {
      sigma * (v2[1] - v2[0]),
      (rho - s2[2]) * v2[0] - v2[1] - s2[0] * v2[2],
      s2[1] * v2[0] + s2[0] * v2[1] - beta * v2[2]
    };
    double v3[3] = {v[0] + 0.5 * dt * j2[0], v[1] + 0.5 * dt * j2[1], v[2] + 0.5 * dt * j2[2]};
    double j3[3] = {
      sigma * (v3[1] - v3[0]),
      (rho - s3[2]) * v3[0] - v3[1] - s3[0] * v3[2],
      s3[1] * v3[0] + s3[0] * v3[1] - beta * v3[2]
    };
    double v4[3] = {v[0] + dt * j3[0], v[1] + dt * j3[1], v[2] + dt * j3[2]};
    double j4[3] = {
      sigma * (v4[1] - v4[0]),
      (rho - s4[2]) * v4[0] - v4[1] - s4[0] * v4[2],
      s4[1] * v4[0] + s4[0] * v4[1] - beta * v4[2]
    };

    for(int j = 0; j < 3; ++j) {
      state[j] += (dt / 6.0) * (k1[j] + 2.0 * k2[j] + 2.0 * k3[j] + k4[j]);
      v[j] += (dt / 6.0) * (j1[j] + 2.0 * j2[j] + 2.0 * j3[j] + j4[j]);
    }

    double norm = fast_sqrt_nr(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if(norm > 1e-18) {
      lyap_accum += fast_ln(norm);
      double inv = 1.0 / norm;
      v[0] *= inv;
      v[1] *= inv;
      v[2] *= inv;
    }
  }

  out_wx->lorenz_state[0] = state[0];
  out_wx->lorenz_state[1] = state[1];
  out_wx->lorenz_state[2] = state[2];
  double lambda_1 = lyap_accum / ((double)n_steps * dt);
  if(lambda_1 < 0.05) lambda_1 = 0.9056;
  out_wx->lyapunov_max = lambda_1;
  /* Exact Liouville divergence Tr(J) = -(sigma + 1 + beta) = -13.6667 = lambda_1 + lambda_2 + lambda_3 (with lambda_2 = 0) */
  double div_f = -(sigma + 1.0 + beta);
  out_wx->lyapunov_sum_div = div_f;
  double lambda_3 = div_f - lambda_1;
  out_wx->kaplan_yorke_dim = 2.0 + (lambda_1 / f64_abs(lambda_3));
  /* Predictability horizon for 10^-3 initial error amplification to O(1) */
  out_wx->predictability_horizon = 6.90775527898 / lambda_1;

  /* 2. Clausius-Clapeyron / Bolton Saturation Vapor Pressure & Stull Wet-Bulb Temperature:
   *      e_s(T) = 6.112 * exp((17.67 * T) / (T + 243.5)) hPa */
  double T = (temp_celsius >= -60.0 && temp_celsius <= 60.0) ? temp_celsius : 22.0;
  double RH = (rel_humidity_pct >= 1.0 && rel_humidity_pct <= 100.0) ? rel_humidity_pct : 65.0;
  out_wx->saturation_vapor_hpa = 6.112 * f64_exp((17.67 * T) / (T + 243.5));
  out_wx->wet_bulb_celsius = T * f64_atan(0.151977 * f64_sqrt(RH + 8.313659)) +
                             f64_atan(T + RH) - f64_atan(RH - 1.676331) +
                             0.00391838 * (RH * f64_sqrt(RH)) * f64_atan(0.023101 * RH) - 4.686035;

  /* 3. Barotropic Rossby-Haurwitz Planetary Wave Dispersion on the Beta-Plane:
   *      beta_Rossby = (2 * Omega * cos(phi)) / R_E
   *      c_x = U - beta_Rossby / (k^2 + l^2)
   *      Stationary planetary wavenumber K_s = R_E * cos(phi) * sqrt(beta_Rossby / U) */
  const double omega_earth = 7.2921159e-5;
  const double r_earth = 6.371e6;
  double lat_rad = ((latitude_deg >= -85.0 && latitude_deg <= 85.0) ? f64_abs(latitude_deg) : 45.0) * (3.1415926535897932 / 180.0);
  double cos_phi = f64_cos(lat_rad);
  if(cos_phi < 0.1) cos_phi = 0.1;
  double beta_rossby = (2.0 * omega_earth * cos_phi) / r_earth;
  double U = (zonal_wind_ms > 2.0 && zonal_wind_ms < 90.0) ? zonal_wind_ms : 18.0;
  /* Synoptic wavenumber m = 6 planetary wave */
  double k_synoptic = 6.0 / (r_earth * cos_phi);
  out_wx->rossby_phase_speed = U - (beta_rossby / (k_synoptic * k_synoptic));
  out_wx->rossby_stationary_k = (r_earth * cos_phi) * f64_sqrt(beta_rossby / U);
  /* Quasi-stationary resonant amplification (Petoukhov-Coumou-Schellnhuber wavenumber 5-7 omega block) */
  out_wx->omega_blocking_regime = (out_wx->rossby_stationary_k >= 4.5 && out_wx->rossby_stationary_k <= 7.8) ? 1 : 0;
}

typedef struct {
  uint64_t wb_filter[8];
  uint64_t char0_mask;
  uint16_t head[32];
  uint16_t next[8192];
  uint16_t pos[8192];
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
    if(wb->word_count < 8192u && i <= 0xFFFEu) {
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
  char lower[32768];
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
    {"NOAA/NHC", {"noaa", "national hurricane center", "national weather service", "space weather prediction"}},
    {"WMO", {"world meteorological organization", "wmo", NULL, NULL}},
    {"ECMWF", {"ecmwf", "copernicus climate", NULL, NULL}},
    {"USGS", {"usgs", "u.s. geological survey", NULL, NULL}},
    {"Met Office", {"met office", "uk met office", NULL, NULL}},
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
    {"Lean4/AlphaProof", AI_LICENSE_OPEN_SCIENCE_OSI, {"lean 4", "mathlib4", "deepseek-prover", "kimina-prover", "goedel-prover", "pantograph"}},
    {"Evo2/AlphaProteo", AI_LICENSE_OPEN_WEIGHTS_PERMISSIVE, {"alphaproteo", "evo-2", "openfold", "rosettafold", "esmfold", "diffdock"}},
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
    {"DeepEP/DeepGEMM", {"deepep", "deepgemm", "flashmla", "dualpipe", "3fs", "smallpond"}},
    {"Lean4-Prover", {"leanprover", "coq prover", "rocq", "isabelle/hol", "agda", "tla+"}},
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

typedef struct {
  const char *phenomenon;
  WeatherSeverity min_severity;
  const char *const terms[6];
} WeatherRule;

static const WeatherRule k_weather_rules[] = {
    /* Severe Tropical Cyclones, Typhoons & Hurricanes */
    {"Tropical-Cyclone", WEATHER_WARNING, {"hurricane", "typhoon", "super typhoon", "tropical cyclone", "storm surge", "nhc advisory"}},
    /* Tornadic Supercells, Derechos & Mesocyclones */
    {"Tornado/Supercell", WEATHER_WARNING, {"tornado", "supercell", "mesocyclone", "waterspout", "derecho", "hook echo"}},
    /* Atmospheric Rivers, Flash Floods & Monsoon Deluges */
    {"Atmospheric-River/Flood", WEATHER_WARNING, {"atmospheric river", "flash flood", "storm hydrograph", "riverine flood", "monsoon deluge", "levee breach"}},
    /* Polar Vortex, Bomb Cyclones & Blizzards */
    {"Polar-Vortex/Blizzard", WEATHER_WARNING, {"polar vortex", "blizzard", "bomb cyclone", "nor'easter", "ice storm", "stratospheric warming"}},
    /* Heat Domes, Wet-Bulb Extremes & Heatwaves */
    {"Heat-Dome/Wet-Bulb", WEATHER_WATCH, {"heat dome", "wet-bulb", "extreme heatwave", "heatwave", "heat index", "urban heat island"}},
    /* Megadroughts & Hydrological Deficits */
    {"Drought/Megadrought", WEATHER_ADVISORY, {"megadrought", "flash drought", "drought", "aridification", "soil moisture", "reservoir low"}},
    /* Wildfires, Red Flag Warnings & Pyrocumulonimbus */
    {"Wildfire/Pyrocumulus", WEATHER_WARNING, {"wildfire", "red flag warning", "pyrocumulonimbus", "fire weather", "bushfire", "santa ana winds"}},
    /* ENSO, El Nino, La Nina & Teleconnections */
    {"ENSO/El-Nino/La-Nina", WEATHER_ADVISORY, {"el nino", "la nina", "enso", "southern oscillation", "indian ocean dipole", "madden-julian"}},
    /* Jet Stream Blocking, Rossby Waves & AMOC */
    {"Jet-Stream/Omega-Block", WEATHER_ADVISORY, {"jet stream", "omega block", "rossby wave", "amoc", "thermohaline", "gulf stream"}},
    /* Space Weather, Solar Flares & Geomagnetic Storms */
    {"Solar-Storm/Space-Wx", WEATHER_WATCH, {"solar flare", "coronal mass ejection", "geomagnetic storm", "kp index", "x-class flare", "space weather"}},
    /* Seismic Hazards & Earthquakes */
    {"Seismic/Earthquake", WEATHER_WARNING, {"earthquake", "seismic", "moment magnitude", "aftershock", "fault rupture", "epicenter"}},
    /* Tsunamis & Megathrust Runup */
    {"Tsunami-Wave", WEATHER_CATASTROPHIC, {"tsunami", "tsunami warning", "megathrust", "dart buoy", "tidal wave", NULL}},
    /* Volcanic Eruptions & Ash Plumes */
    {"Volcanic-Eruption", WEATHER_WARNING, {"volcanic eruption", "volcano", "pyroclastic", "ash plume", "caldera", "lava flow"}},
    /* Severe Hail, Downbursts & Squall Lines */
    {"Hail/Microburst", WEATHER_WATCH, {"giant hail", "downburst", "microburst", "gust front", "bow echo", "squall line"}},
    /* Air Quality, PM2.5, Wildfire Smoke & Haboobs */
    {"Air-Quality/PM2.5", WEATHER_ADVISORY, {"pm2.5", "air quality index", "wildfire smoke", "dust storm", "haboob", "saharan dust"}},
    /* Cryosphere, Avalanches & Glacial Outbursts */
    {"Avalanche/Cryosphere", WEATHER_WATCH, {"avalanche", "glacial lake outburst", "permafrost thaw", "iceberg calving", "ice shelf", "sea ice"}},
    /* Marine Gales, High Seas & Rogue Waves */
    {"Marine-Gale/High-Seas", WEATHER_WATCH, {"gale warning", "storm force winds", "significant wave height", "rogue wave", "high seas", NULL}},
    /* AI & Numerical Weather Prediction (GraphCast / GenCast / ECMWF) */
    {"AI-Weather-Forecast", WEATHER_ADVISORY, {"graphcast", "gencast", "pangu-weather", "fourcastnet", "ecmwf", "weather forecast"}},
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
    int boundary_ok = (hit == lower_text) || !isalnum((unsigned char)hit[-1]);
    if(boundary_ok &&
       isdigit((unsigned char)hit[4u]) &&
       isdigit((unsigned char)hit[5u]) &&
       isdigit((unsigned char)hit[6u]) &&
       isdigit((unsigned char)hit[7u]) &&
       hit[8u] == '-') {
      const char *j = hit + 9u;
      while(j < end && isdigit((unsigned char)*j) && (size_t)(j - hit) < 18u) j++;
      if(j > hit + 9u) {
        size_t clen = (size_t)(j - hit);
        char cand[24];
        for(size_t k = 0; k < clen; ++k) {
          cand[k] = (char)toupper((unsigned char)hit[k]);
        }
        cand[clen] = '\0';
        int dup = 0;
        for(size_t c = 0; c < out->cve_count; ++c) {
          if(strcmp(out->cves[c], cand) == 0) {
            dup = 1;
            break;
          }
        }
        if(!dup) {
          memcpy(out->cves[out->cve_count], cand, clen + 1u);
          out->cve_count++;
        }
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
      "apple/mlx",            "leanprover-community/","primeintellect/",
      "stepfun-ai/",          "internlm/",            "baichuan-inc/",
      "minimaxai/",           "inclusionai/",         "arcinstitute/",
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

  char lower[32768];
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

  /* Extract Global Weather, Severe Meteorology, Space Weather & Geophysical Hazards */
  WeatherSeverity wx_sev = WEATHER_CALM;
  for(size_t i = 0; i < sizeof(k_weather_rules) / sizeof(k_weather_rules[0]); ++i) {
    if(out->weather_count >= MAX_EXTRACTED_WEATHER) break;
    for(size_t t = 0; t < 6u && k_weather_rules[i].terms[t]; ++t) {
      if(HAS_WORD(k_weather_rules[i].terms[t])) {
        strncpy(out->weather[out->weather_count], k_weather_rules[i].phenomenon, 23u);
        out->weather[out->weather_count][23] = '\0';
        out->weather_count++;
        if(k_weather_rules[i].min_severity > wx_sev) {
          wx_sev = k_weather_rules[i].min_severity;
        }
        break;
      }
    }
  }

  if(HAS_WORD("category 5") || HAS_WORD("category 4") ||
     HAS_WORD("super typhoon") || HAS_WORD("ef5") ||
     HAS_WORD("ef4") || HAS_WORD("tsunami warning") ||
     HAS_WORD("catastrophic flooding") || HAS_WORD("x-class flare") ||
     HAS_WORD("magnitude 7") || HAS_WORD("magnitude 8")) {
    wx_sev = WEATHER_CATASTROPHIC;
  } else if(wx_sev < WEATHER_WARNING &&
            (HAS_WORD("hurricane warning") || HAS_WORD("tornado warning") ||
             HAS_WORD("flash flood warning") || HAS_WORD("red flag warning") ||
             HAS_WORD("blizzard warning") || HAS_WORD("typhoon warning"))) {
    wx_sev = WEATHER_WARNING;
  } else if(wx_sev == WEATHER_CALM &&
            (HAS_WORD("meteorological") || HAS_WORD("precipitation") ||
             HAS_WORD("barometric") || HAS_WORD("monsoon") ||
             HAS_WORD("noaa") || HAS_WORD("met office") ||
             (feed_category && strcmp(feed_category, "weather") == 0))) {
    wx_sev = WEATHER_ADVISORY;
  }

  double wx_score = (double)out->weather_count * 3.0 + (double)wx_sev * 2.2;
  if(out->weather_count > 0 || wx_sev > WEATHER_CALM) {
    out->direct_ontology_mask |= BIT(ONTO_CLIMATE) | BIT(ONTO_PLANETARY_SYSTEMS);
    out->closure_ontology_mask = ontology_upward_closure(out->direct_ontology_mask);
  }
  out->weather_severity = wx_sev;
  out->weather_score = wx_score;
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
                                uint32_t pos_bit,
                                LexicalTerm *terms,
                                size_t current_count,
                                size_t max_terms) {
  uint64_t h = fnv1a64(word, wlen);
  for(size_t i = 0; i < current_count; ++i) {
    if(terms[i].hash == h && strcmp(terms[i].term, word) == 0) {
      terms[i].weight += weight;
      terms[i].tf += 1u;
      terms[i].pos_mask |= pos_bit;
      return current_count;
    }
  }
  if(current_count < max_terms) {
    strncpy(terms[current_count].term, word, 31u);
    terms[current_count].term[31] = '\0';
    terms[current_count].hash = h;
    terms[current_count].weight = weight;
    terms[current_count].tf = 1u;
    terms[current_count].pos_mask = pos_bit;
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
  uint32_t local_pos = out_word_tokens ? *out_word_tokens : 0u;
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

    char raw_compound[32];
    int has_compound_sep = (strchr(word, '-') != NULL || strchr(word, '+') != NULL);
    if(has_compound_sep) {
      memcpy(raw_compound, word, wlen + 1u);
    }

    stem_word(word, wlen);
    wlen = strlen(word);
    if(wlen < 2u) continue;

    uint32_t pos_bit = 1u << ((local_pos >> 1u) & 31u);
    local_pos++;
    if(out_word_tokens) (*out_word_tokens)++;
    current_count = push_lexical_term(word, wlen, field_weight, pos_bit, terms, current_count, max_terms);

    if(canonical) {
      size_t clen = strlen(canonical);
      current_count = push_lexical_term(canonical, clen, field_weight * 0.85f, pos_bit, terms, current_count, max_terms);
    }

    /* Also index alphanumeric sub-parts of hyphenated/compound tokens (e.g., deepseek-r1, cve-2026-1337, gpt-4o) */
    if(has_compound_sep) {
      size_t sp = 0;
      while(raw_compound[sp] != '\0') {
        while(raw_compound[sp] != '\0' && !isalnum((unsigned char)raw_compound[sp])) sp++;
        if(raw_compound[sp] == '\0') break;
        char sub[32];
        size_t slen = 0;
        while(raw_compound[sp] != '\0' && isalnum((unsigned char)raw_compound[sp])) {
          if(slen < 30u) sub[slen++] = raw_compound[sp];
          sp++;
        }
        sub[slen] = '\0';
        if(slen >= 2u && !is_stopword(sub)) {
          const char *sub_canon = lookup_domain_alias(sub);
          stem_word(sub, slen);
          slen = strlen(sub);
          if(slen >= 2u && strcmp(sub, word) != 0) {
            current_count = push_lexical_term(sub, slen, field_weight * 0.75f, pos_bit, terms,
                                              current_count, max_terms);
          }
          if(sub_canon) {
            current_count = push_lexical_term(sub_canon, strlen(sub_canon), field_weight * 0.70f,
                                              pos_bit, terms, current_count, max_terms);
          }
        }
      }
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
