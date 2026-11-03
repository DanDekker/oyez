# Oyez

Oyez! Oyez! Oyez! A news API and RSS aggregator for the people.

**Oyez** is a zero-dependency, first-party **C23** global news aggregator API, RSS/Atom/JSON Feed parser, readability content extractor, category-theoretic ontology engine, and in-memory BM25 + 64-bit BitSet search engine.

Compiled strictly with `-std=c23 -Wall -Wextra -Werror -Wpedantic -O3` and only `-lm -lpthread`.

---

## Architecture & Proprietary C23 Stack

| Module | File | Description |
| :--- | :--- | :--- |
| **Core & Hashing** | [`src/core.h`](src/core.h), [`src/core.c`](src/core.c) | Dynamic/fixed `Buffer`, JSON string escaping, monotonic nanosecond clocks, `fnv1a64`, `mix64`, 128-bit multiplicative `wyhash64`, hardware `popcnt64`, `ctz64` & `clz64`. |
| **Checksums & Wire** | [`src/checksum.h`](src/checksum.h), [`src/checksum.c`](src/checksum.c), [`src/wire.h`](src/wire.h) | Little-endian binary wire encoding, reflected nibble CRC-32, and incremental SHA-256 (`Sha256Digest`). |
| **Byte-Level BPE Tokenizer** | [`src/token.h`](src/token.h), [`src/token.c`](src/token.c) | Doubly-linked symbol list + open-addressed `PairTable` + `MaxHeap`/`MinHeap` BPE trainer/encoder with `OYEZBP01` CRC-32 binary checkpoints. |
| **M1 Structured Tokenizer** | [`src/m1_tokenizer.h`](src/m1_tokenizer.h), [`src/m1_tokenizer.c`](src/m1_tokenizer.c) | Fixed-vocabulary pre-segmenter (256 raw bytes + 26 indent tokens + 200 syntax/crypto/scientific/AI-kernel primitives + 144 news/arXiv/DeepMind/cybersec/macro/politics/sports landmarks + 16 control tokens = `642` base IDs, `2,048` default trained vocabulary) with **O(1) First-Byte Length-Sorted Dispatch Table (`g_m1_dispatch[256]`)**, **64-bit M1 Subword Structural SimHash (`m1_structural_sketch64`)**, and `OYEZMT02` SHA-256 + CRC-32 sealed serialization. |
| **Markup & Readability** | [`src/markup.h`](src/markup.h), [`src/markup.c`](src/markup.c) | Unified RSS 2.0, Atom 1.0, RSS 1.0/RDF, and JSON Feed 1.1 parser + Readability DOM block scorer and HTML-to-Markdown converter. |
| **Category Theory & Gazetteer** | [`src/ontology.h`](src/ontology.h), [`src/ontology.c`](src/ontology.c) | **32-node Bitmask Poset Category $\mathcal{C}$** (`upward_mask` $\uparrow c$, `downward_mask` $\downarrow c$), Lattice Join $\vee$ & Meet $\wedge$, **Bi-Heyting Algebra** (Heyting Implication $(A \Rightarrow B) \dashv (A \wedge -)$, Intuitionistic Negation $\neg A$, **Co-Heyting Subtraction $(A \setminus_{\text{co}} B) \dashv (B \cup -)$, Co-Heyting Supplement $\sim A = 1 \setminus_{\text{co}} A$, and Lawvere Co-Heyting Boundary $\partial A = \downarrow A \cap \sim A$ with Exact Leibniz Derivation Rule $\partial(I \cap J) = (\partial I \cap J) \cup (I \cap \partial J)$**), **Universal Quantifier Right Adjoint $\forall(S)$**, **Subobject Classifier Characteristic Map $\chi_A: 1 \to \Omega$**, **Lawvere-Tierney Double-Negation Topology $j(A) = \neg\neg A$ & Grothendieck Dense Sieves**, **Quillen Model Category Factorization ($A \stackrel{\sim}{\hookrightarrow} M \twoheadrightarrow A \vee B$ with $j(M) = j(A)$)**, **Brian Day's Monoidal Convolution $(F \star_{\text{Day}} G)(c) = \int^{u,v} \mathcal{C}(c, u \vee v) \otimes F(u) \otimes G(v)$, Day Closed Internal Hom $[G, H]_{\text{Day}}(u)$ & Exact Day Tensor-Hom Adjunction ($\text{Hom}(F \star_{\text{Day}} G, H) \cong \text{Hom}(F, [G, H]_{\text{Day}})$)**, **Spivak Polynomial Endofunctor $P_{\mathcal{C}}(X)(u) = \sum_{\sigma} \prod_v X_{\text{cod}(\sigma)}(v)$ & Dependent Lens Co-Design Pullback**, **Simplicial Gauss–Bonnet–Chern Curvature $K_{\text{GB}}(u) = \sum_{k=0}^{5} (-1)^k \frac{\deg_k(u)}{k+1}$ ($\sum_{u \in \mathcal{C}} K_{\text{GB}}(u) = \chi(N(\mathcal{C})) = 1$), Forman–Ricci Simplicial Curvature $\kappa_F(e)$, Ollivier–Ricci Lazy Random Walk Coarse Curvature $\kappa_O(u, v) = 1 - W_1(m_u, m_v)/d(u, v)$ & Normalized Hamilton–Ollivier Discrete Ricci Flow**, **Ihara–Bass Graph Zeta Function $\zeta_{\mathcal{C}}(u) = ((1-u^2)^{|E|-|V|} \det(I - u A + u^2(D-I)))^{-1}$ via Exact $32 \times 32$ Deformed Laplacian Determinant ($\det|_{u=0} = 1, \det|_{u=1} = \det(L_0) = 0$) & Hashimoto Non-Backtracking Spectral Radius $\rho(B)$**, **Karp's Max-Plus Tropical Eigenvalue $\lambda_{\text{trop}}(T)$ & Tropical Convex Hull Projection $\pi_{\text{trop}}(F)$ with Exact Tropical Idempotence $\pi_{\text{trop}}^2 = \pi_{\text{trop}}$**, **Commutative Rota–Baxter Algebra $(\mathcal{A}, R)$ of Weight $\theta = -1$ ($R(f)R(g) = R(R(f)g + f R(g) - fg)$) & Atkinson's Multiplicative Birkhoff Factorization**, **Deterministic Atmospheric Physics (`ontology_atmospheric_dynamics`): 4th-Order Runge–Kutta (RK4) Lorenz-63 Chaotic Convection Attractor + Benettin Variational Tangent QR Lyapunov Spectrum ($\lambda_1 > 0$, Liouville Divergence $-13.6667$, Kaplan–Yorke Fractal Dimension $D_{\text{KY}} \approx 2.06$), Clausius–Clapeyron Saturation Vapor Pressure $e_s(T)$, Stull Psychrometric Wet-Bulb Temperature $T_w$, and Barotropic Rossby–Haurwitz Planetary Wave Dispersion $K_s$ & Omega Blocking**, **Enriched Isbell Conjugacy ($F^\star \dashv G^\sharp$) & Pavlovic-Isbell Nucleus ($((F^\star)^\sharp)^\star = F^\star$)**, **Lawvere Enriched Quasimetric Space $d_{\mathcal{L}}(u, v)$ via Tropical Min-Plus Semiring Kleene Star**, **Entropic Sinkhorn-Knopp Optimal Transport ($W_\varepsilon(\mu, \nu)$), Self-Debiased Sinkhorn Divergence ($S_\varepsilon(\mu, \nu) \ge 0$), Coupling Shannon Entropy $H(\Pi^*)$ & Riemannian Fisher-Rao Statistical Geodesic $d_{\text{FR}}(\mu, \nu) = 2\arccos(\sum_u \sqrt{\mu_u \nu_u})$ on $\Delta^{31}(\mathcal{C})$**, **Spectral Heat Kernel Semigroup $H_t = \exp(-t L_0)$ with Exact Mass Conservation $\langle \mathbf{1}, H_t f \rangle = \langle \mathbf{1}, f \rangle$**, **Cheeger Isoperimetric Bottleneck Constant $h(\mathcal{C})$ & Cheeger-Buser Spectral Inequality Verification ($\lambda_2(L_0)/2 \le h(\mathcal{C}) \le \sqrt{2 d_{\max} \lambda_2(L_0)}$)**, **Grothendieck–Leray Spectral Sequence $(E_r^{p,q}, d_r)$ of the Rank-Filtered Simplicial Nerve $F_p N(\mathcal{C})$ with Exact Euler Invariance $\chi(E_0) = \chi(E_1) = \chi(E_2) = \chi(E_\infty) = 1$**, **Leinster Magnitude $|\mathcal{C}|_{\mathcal{L}} = \sum_u w(u)$ & Maximum-Entropy Weighting**, **Yoneda Embedding $\mathcal{Y}: \mathcal{C} \hookrightarrow \mathbf{Set}^{\mathcal{C}^{\text{op}}}$, Constructive Yoneda Lemma Isomorphism $\text{Nat}(\mathcal{Y}(c), F) \cong F(c)$ & Enriched Lawvere-Yoneda Kan Transform**, **Monad & Comonad Algebras ($T = \iota^* \text{Lan}_\iota$, $G = \iota^* \text{Ran}_\iota$) + Zig-Zag Triangle Identity, Beck Monadicity Coequalizer, Isbell Codensity Monad & Bousfield Localization**, **Persistent Homology Filtration Barcodes ($\beta_0(\epsilon_k), \beta_1(\epsilon_k)$ across 8 Lawvere/Density Scales)**, Left & Right Kan Extensions ($\text{Lan}_\iota \dashv \iota^* \dashv \text{Ran}_\iota$), **Rota's Incidence Algebra Möbius Inversion** $\mu(u, v)$, **Profunctor Coend $\int^{c \in \mathcal{C}} F(c) G(c)$, Multiplicative Lawvere-Leinster End $\int_{c \in \mathcal{C}} [F(c), G(c)]$, and Enriched Weighted Colimit / Limit ($\text{colim}_W^{\mathcal{L}} F$, $\lim_{\mathcal{L}}^W G$) with Exact Tensor-Hom Adjunction**, **Simplicial Nerve $N(\mathcal{C})$ $f$-vector, Betti Numbers $(\beta_0, \beta_1) = (1, 0)$ & Euler Characteristic $\chi(N(\mathcal{C})) = 1$**, **Simplicial Cochain Complex $\delta_1 \circ \delta_0 = 0$, Full 1-Simplicial Helmholtz-Hodge Decomposition ($C^1(\mathcal{C}) = \text{im}(\delta_0) \oplus \text{im}(\delta_1^\top) \oplus \ker(L_1)$ into Gradient, Coexact 2-Coboundary & Harmonic Flows), 0- & 1-Hodge Laplacian Traces ($\text{Tr}(L_0), \text{Tr}(L_1) = 951$), Fiedler Algebraic Connectivity $\lambda_2(L_0)$ & Dirichlet Energy**, **Poset Wasserstein / Radon Distance**, **Word-Start Bucket Index (`WordBloomIndex`) with 512-bit Cache-Line Bloom Filter & 32-Bucket Offset Chains**, 103-entry domain acronym/synonym expander, **138-country ISO-2 gazetteer**, **33 Frontier, Google DeepMind Research, Formal Theorem Proving & Open-Weights AI Model families**, **39 Neocloud / Inference / JAX / Hugging Face / AI Silicon families**, **39 Hugging Face Hub `<org>/<model>` repository namespace extractors**, **5-tier Open-Source (OSI) vs Open-Weights vs Closed-Source Frontier API license spectrum (`AiWeightLicense`)**, **25 Global Sports Leagues & Tournaments**, **18 Severe Weather, Tropical Cyclone, Tornado, Atmospheric River, Space Weather & Seismic Hazard extractors (`WeatherSeverity`)**, and 64-bit Charikar SimHash. |
| **Global Feed Catalog** | [`src/catalog.h`](src/catalog.h), [`src/catalog.c`](src/catalog.c) | **750 curated unique global news, category theory / algebraic topology / HoTT / formal verification, frontier science, Google DeepMind research, AI labs, Hugging Face Hub, open-weights/local inference, GPU neoclouds, inference engines, robotics, aerospace, quantum, biotech, fusion, constitutional law, maritime trade, systems/compilers/ZK crypto, 0-day cybersec, central banks, equities, FX, fiscal/political events, global sports, and severe weather / hurricane / tornado / space weather / seismic feeds** across all 11 world regions and 100+ countries/territories. |
| **BM25+ & BitSet Index Engine** | [`src/index_engine.h`](src/index_engine.h), [`src/index_engine.c`](src/index_engine.c) | Thread-safe (`pthread_rwlock_t`) database-free in-memory + `OYEZIX01` binary snapshot engine with **Contiguous 64 KB Block String Arena (`StringArenaBlock`)**, O(1) `wyhash64` open-addressed `dedup_table`, 64-bit word `BitSet` presheaf fibers (`bitset_andnot_inplace`, `bitset_intersect_count`, `bitset_jaccard`), **4-Band Multi-Probe SimHash64 LSH** index, **64-entry Generation-Invalidated Query Cache**, **BM25+ ($\delta = 1.0$) with 32-Bit Positional Proximity Phrase Boosting** and categorical specificity boost, Submodular MMR & **Fast Greedy Cholesky Determinantal Point Process (`engine_dpp_rerank` / `?diversify=dpp`) MAP parallelotope volume diversification**, **Autonomous Watchpoint Rule Engine, Alert Ring Buffer (`AutomationRule`, `AutomationAlert`) & Auer–Cesa-Bianchi–Fischer UCB1 Multi-Armed Bandit Feed Scheduler (`engine_bandit_select_feeds`)**, **Galois Formal Concept Lattice Hasse Diagrams**, **BitSet-accelerated Categorical Pullbacks ($X \times_Z Y$) & Pushout Colimits ($X \sqcup_Z Y$)**, **Presheaf Natural Transformations ($\eta: F \Rightarrow G$)**, **Bi-Heyting Topos, Day Convolution, Quillen Model Factorization, Entropic Sinkhorn Optimal Transport, Fisher-Rao Geodesic, Ricci Curvature Flow, Ihara Zeta, Tropical Convex Hull, Rota–Baxter Algebra, Spectral Sequence & Isbell Conjugacy Engine (`ToposState` / `engine_topos_state`)**, **Grothendieck Construction / Category of Elements $\int_{\mathcal{C}} F$ & Cartesian Lifting (`CartesianLift`)**, **Temporal Cellular Sheaves over Time Intervals (`TemporalSheafState`)**, **Symmetric Monoidal Operad 3-Stage Narrative Composition (`OperadChain`)**, and Bipartite Entity-Article PageRank. |
| **POSIX HTTP Server & CLI** | [`src/http_server.h`](src/http_server.h), [`src/http_server.c`](src/http_server.c), [`src/main.c`](src/main.c) | Zero-dependency multi-threaded (`pthread`) HTTP/1.1 Keep-Alive server with **Embedded HTML5 Topos Intelligence Console (`GET /console`)**, **Topos Bi-Heyting, Day Convolution, Sinkhorn OT & Spectral Sequence Endpoint (`GET /api/v1/topos`)**, **Weather & Atmospheric Physics Telemetry (`GET /api/v1/weather`)**, **Autonomous Watchpoint & UCB1 Bandit Automation API (`GET/POST/DELETE /api/v1/automation`)**, **Atom 1.0 XML Syndication (`GET /api/v1/feed.xml`)**, **Server-Sent Events (`GET /api/v1/stream`)**, background autonomous daemon thread, concurrent `pthread` feed poller, and CLI (`serve`, `seed`, `poll`, `automate`, `weather`, `bench`, `stats`, `tui`). |

---

## Performance Benchmarks (Apple Silicon M-Series, `-O3`)

```text
================================================================
  Oyez Pure C23 Proprietary Engine Benchmark Suite
================================================================
1. Ingest + Entity/Poset/SimHash64 Index (2000 articles across 750 feeds):
   Total Time:  75.08 ms (26638 articles/sec)
2. BM25 + Poset Presheaf Fiber + BitSet + Cache Search (1000 queries):
   Avg Latency: 0.62 us/query (1602564 queries/sec, hits=549)
3. Bipartite Entity-Article PageRank (15 iterations over 2000 articles):
   Total Time:  1.34 ms (top entity: geo:US score=0.0527)
4. 4-Band Multi-Probe SimHash64 LSH Story Clustering (2000 articles):
   Total Time:  0.02 ms (clusters formed=32)
5. 32-Node Poset Kan + Yoneda + Coend + Helmholtz-Hodge Complex (10,000 runs):
   Avg Latency: 12.132 us/pass (|C|_L=15.67, grad=57120.6, curl=2169.4, d1*d0=0.00e+00, coend=40239)
6. Pushout, Fibration, Temporal Sheaf, Topos Bi-Heyting, Sinkhorn OT & Spectral Seq:
   Total Time:  0.758 ms (amalgams=5, lifts=32, operads=16, W_eps=0.845, S_eps=0.007, d_FR=0.254, day_err=0.0e+00)
7. Lorenz-63 Chaos RK4 + Benettin QR Lyapunov & UCB1 Bandit Automation:
   Total Time:  0.436 ms (lambda_1=0.8181, D_KY=2.0565, T_w=24.80C, K_s=5.55, alerts=256, bandit_k=16)
8. M1 Structured BPE Tokenizer Training + SHA-256 Seal:
   Vocab Size:  2048 (trained in 96.42 ms)
9. Consolidated One-Machine Appliance Self-Verification & Memory Audit:
   Total Time:  1.197 ms (theorems=12/12, resident=9.29 MB, arena=1055.4 KB, max_defect=1.33e-14)
```

---

## Quick Start

```bash
# Build binary and run test suite
make test

# Run the high-performance benchmark suite
make bench

# Launch the ANSI One-Machine Autonomous Intelligence Appliance TUI Dashboard
./build/oyez tui

# Audit single-machine resident RAM, string arena, and 12/12 theorem self-verification
./build/oyez appliance

# Seed the 750 global news, category theory, scientific, cybersec, AI, weather & sports feeds into data/oyez.ix
./build/oyez seed

# Run an autonomous UCB1 Multi-Armed Bandit feed scheduling & watchpoint alert cycle
./build/oyez automate

# Inspect live Lorenz-63 chaotic convection, wet-bulb & Rossby wave atmospheric telemetry
./build/oyez weather

# Poll live RSS/Atom feeds
./build/oyez poll 15

# Start the HTTP/1.1 REST API & Web Console server on port 8787
./build/oyez serve 8787
```

---

## REST API & Web Endpoints

- `GET /console` — Zero-dependency embedded HTML5/JS One-Machine Autonomous Intelligence Console
- `GET /health` — Engine health, feed/article counts, and inverted index vocabulary size
- `GET /api/v1/appliance` (alias `GET /api/v1/machine`) — Consolidated One-Machine Appliance telemetry: exact single-machine resident RAM breakdown (`total_resident_bytes`, 64 KB `StringArenaBlock` used/capacity, inverted index, 64-bit `BitSet` fibers, 4-band SimHash64 LSH & dedup tables, query cache), **12/12 algebraic, topological, differential-geometric & dynamical self-verification theorems** (`all_theorems_verified`, `max_numerical_defect`), live Lorenz-63 / Stull wet-bulb / Rossby wave atmospheric physics, cross-domain situation room counts, and UCB1 bandit scheduler priorities
- `GET /api/v1/stats` — Region, epistemic `NewsType`, `weather_severities`, automation rule/alert counts, and Poset presheaf fiber counts
- `GET /api/v1/ontology?sub=ai&sup=knowledge-economy&a=cyber&b=ai` — Poset Category $\mathcal{C}$ morphisms, Möbius $\mu(u,v)$, Lattice Join $\vee$ / Meet $\wedge$, Bi-Heyting Implication / Negation / Co-Heyting Subtraction / Co-Heyting Supplement / Lawvere Boundary $\partial A$ / Universal Quantifier $\forall$, Lawvere-Tierney Sheafification $j(A) = \neg\neg A$, Lawvere Quasimetric $d_{\mathcal{L}}(A, B)$, Leinster Magnitude $|\mathcal{C}|_{\mathcal{L}}$, Isbell Codensity Defect, Day Convolution Adjunction Defect, Cheeger Isoperimetric Constant $h(\mathcal{C})$ & Cheeger-Buser Spectral Inequality, Enriched Lawvere-Yoneda Kan Transform, Simplicial Nerve $f$-vector, Betti numbers, Fiedler eigenvalue $\lambda_2(L_0)$, 0- & 1-Hodge Laplacian Traces (`laplacian_trace`, `laplacian_1_trace`), Full 1-Simplicial Helmholtz-Hodge Decomposition (`hodge_gradient_norm`, `hodge_coexact_norm`, `hodge_harmonic_norm`, `hodge_solenoidal_norm`), Cochain Exactness $\|\delta_1\delta_0 f\|_2^2$, Profunctor End & Coend, Weighted Colimit & Limit, and Left/Right Kan Extensions
- `GET /api/v1/topos?source_region=east-asia&target_region=europe&a=ai&b=technology` — Presheaf Topos $\mathbf{Set}^{\mathcal{C}^{\text{op}}}$ Bi-Heyting subobject calculus, Lawvere boundary Leibniz rule verification $\partial(I \cap J) = (\partial I \cap J) \cup (I \cap \partial J)$, graded Subobject Classifier truth profile $\chi_A: 1 \to \Omega$, regional presheaf Equalizer subobject $\text{Eq}(F_{\text{src}}, F_{\text{dst}})$, Day Convolution & Day Internal Hom with Tensor-Hom Adjunction verification, Quillen Model Category Factorization ($A \stackrel{\sim}{\hookrightarrow} M \twoheadrightarrow A \vee B$), Entropic Sinkhorn-Knopp Optimal Transport ($W_\varepsilon$, debiased $S_\varepsilon$, coupling entropy $H(\Pi^*)$) & Riemannian Fisher-Rao Geodesic $d_{\text{FR}}$, Simplicial Gauss–Bonnet–Chern ($\sum_u K_{\text{GB}}(u) = 1$), Forman–Ricci & Ollivier–Ricci Curvature + Discrete Ricci Flow, Ihara–Bass Graph Zeta Function $\zeta_{\mathcal{C}}(u)$ & Tropical Convex Hull Projection ($\pi_{\text{trop}}^2 = \pi_{\text{trop}}$), Commutative Rota–Baxter Algebra & Atkinson Birkhoff Factorization, Grothendieck–Leray Spectral Sequence $(E_r^{p,q}, d_r)$ with Euler invariance $\chi(E_0) = \chi(E_1) = \chi(E_2) = \chi(E_\infty) = 1$, Spivak Polynomial Endofunctor & Dependent Lens Co-Design pullback, Isbell Conjugacy hull & triple-conjugation defect, Profunctor End/Coend, and Weighted Limit/Colimit Tensor-Hom adjunction verification
- `GET /api/v1/temporal` — Persistent Homology Filtration Barcode ($\beta_0(\epsilon_k), \beta_1(\epsilon_k)$), Adjunction Monad/Comonad State ($\eta, \varepsilon$, Zig-Zag Triangle Identity defect, Beck Monadicity Coequalizer defect, Isbell Adjunction defect, Kleisli entropy), and Temporal Cellular Sheaf velocity/acceleration over time stalks
- `GET /api/v1/operad?r1=east-asia&r2=north-america&r3=europe` — Symmetric Monoidal Operad 3-stage narrative composition ($A \xrightarrow{w_{12}} B \xrightarrow{w_{23}} C$)
- `GET /api/v1/concepts?min_support=2` — Formal Concept Analysis (Galois connection $\mathcal{P}(\mathcal{G}) \leftrightarrows \mathcal{P}(\mathcal{M})^{\text{op}}$, Lawvere-Tierney closure, intensional entropy, and Concept Lattice Hasse Diagram covering morphisms)
- `GET /api/v1/pullback?left_region=east-asia&right_region=europe&over=mentioned_country` — Categorical fibered product $X \times_Z Y$ across regional bureaus
- `GET /api/v1/pushout?left_region=east-asia&right_region=europe&over=mentioned_country` — Categorical pushout colimit $X \sqcup_Z Y$ amalgamated equivalence classes and contravariant Presheaf Natural Transformation $\eta: F_{\text{left}} \Rightarrow F_{\text{right}}$
- `GET /api/v1/fibration?category=all-news` — Grothendieck Construction / Category of Elements $\int_{\mathcal{C}} F$ Cartesian lifts $(u, a_u) \to (v, a_v)$ over covering morphisms $u \to v$ in $\mathcal{C}$
- `GET /api/v1/feed.xml?category=ai` — Live Atom 1.0 XML syndication stream for downstream feed readers
- `GET /api/v1/stream` — Server-Sent Events (`text/event-stream`) topological & article telemetry stream
- `GET /api/v1/centrality` — Bipartite Entity-Article PageRank centrality
- `GET /api/v1/markets` — Macro & fiscal stance fiber counts, extracted FX/equity/commodity asset counts, and fiscal event counts
- `GET /api/v1/security` — Clearance level fiber counts, active `CVE-YYYY-NNNN` vulnerability fiber counts, and global organization fiber counts
- `GET /api/v1/ai` — AI Compute Tier fiber counts (`frontier-model-and-rl`, `neocloud-and-cluster`, `inference-and-serving`, `applied-and-rag`), Open-Source vs Closed-Source `ai_license_spectrum` (`open-source-osi-full`, `open-weights-permissive`, `open-weights-custom-license`, `closed-frontier-api`), `open_vs_closed_summary`, extracted frontier/open-weights AI model fibers, neocloud/inference/GPU infrastructure fibers, Hugging Face `<org>/<model>` repository fibers, and top AI compute articles
- `GET /api/v1/sports` — Global sports articles count, 25 extracted league/tournament fiber counts (`UEFA-Champions-League`, `Premier-League`, `Formula-1`, `NBA`, `NFL`, `FIFA-World-Cup`, `ATP/WTA-Grand-Slam`, `Olympic-Games`, etc.), and top sports articles
- `GET /api/v1/weather?temp_c=28.0&humidity=75.0&lat_deg=38.0&u_wind=18.0` — Global weather & seismic hazard fiber counts (`Tropical-Cyclone`, `Tornado/Supercell`, `Atmospheric-River`, `Polar-Vortex`, `Heat-Dome/Heatwave`, `Space-Weather/Solar`, `Earthquake/Seismic`, `Tsunami-Warning`, `Volcanic-Eruption`, etc.), `weather_severities` (`calm-baseline`, `meteorological-advisory`, `severe-weather-watch`, `extreme-hazard-warning`, `catastrophic-emergency`), live `atmospheric_physics` (**Lorenz-63 chaotic convection attractor**, **maximal Lyapunov exponent $\lambda_1$**, **Kaplan–Yorke fractal dimension $D_{\text{KY}}$**, **predictability horizon**, **Clausius–Clapeyron saturation vapor pressure $e_s$**, **Stull wet-bulb temperature $T_w$**, **Rossby stationary wavenumber $K_s$ & omega blocking**), and top weather articles
- `GET /api/v1/automation` — Autonomous Watchpoint Rule Engine state, active watchpoint rules, recent ring-buffer alerts (`AutomationAlert`), and **Auer–Cesa-Bianchi–Fischer UCB1 Multi-Armed Bandit** top-16 feed polling schedule (`bandit_schedule`)
- `POST /api/v1/automation/rules` — Create or update an autonomous watchpoint rule (`name`, `query`, `category`, `region`, `weather`, `min_clearance`, `fiscal_stance`, `min_ai_tier`, `min_weather_severity`, `min_score`, `action`, `target`)
- `DELETE /api/v1/automation/rules?id=6` — Delete an autonomous watchpoint rule by ID
- `POST /api/v1/automation/run` — Trigger an immediate automation rule sweep and optional UCB1 bandit feed polling batch (`poll_batch`, `auto_poll_interval_sec`)
- `GET /api/v1/catalog?region=east-asia` — Browse the 750-feed global news catalog
- `POST /api/v1/catalog/seed` — Seed feeds from the global catalog
- `GET /api/v1/feeds` — List indexed feeds
- `POST /api/v1/feeds/poll` — Poll live feeds and persist `OYEZIX01` snapshot
- `GET /api/v1/articles?q=semiconductor&category=knowledge-economy&exclude_category=politics&category_morphism=1&diversify=dpp` — BM25+ + Poset Presheaf restriction + Heyting/BitSet difference exclusion + Submodular MMR (`diversify=1`) or Fast Greedy Cholesky Determinantal Point Process (`diversify=dpp`) search (supports `?ai_model=`, `?ai_infra=`, `?ai_tier=`, `?ai_license=`, `?hf_repo=`, `?sport=`, `?weather=`, `?weather_severity=`, `?cve=`, `?organization=`, `?asset=`, `?fiscal_event=`)
- `GET /api/v1/articles/:id` — Retrieve full article record
- `GET /api/v1/articles/:id/similar` — 64-bit Charikar SimHash LSH (`popcnt64`) near-duplicate search
- `POST /api/v1/extract` — Extract clean text, Markdown, and entities from raw HTML or URL
- `POST /api/v1/tokenize` — Encode text with the M1 structured BPE tokenizer
