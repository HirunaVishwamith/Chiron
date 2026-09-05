# Deliverables: figures, testing, comparison, manuscript

2026-09-05. Infrastructure is in place; this is the plan it feeds.

## Manuscript

`paper/dac27/` — ACM `sigconf` via `acmart.cls`, copied from the ASP-DAC
build. `./compile.sh` runs pdflatex→bibtex→pdflatex×2 and **fails loudly if the
PDF exceeds the page budget**.

**Page budget: 6, not 7 or 15.** DAC research papers have historically been
6 pages in this format. The DAC 2027 CFP is not posted yet — re-check it and
override with `DAC_PAGE_LIMIT=n` if it differs. Six pages is a hard constraint
on scope: it fits roughly 5 figures, 2 tables, and ~35 references. Plan the
evaluation to that, not to a 15-page narrative.

Submission is **double-blind** (`[anonymous,review]` is set); remove for
camera-ready.

## Figure infrastructure

`tools/plots/chiron_style.py` — one shared style, imported by every figure.

- **Okabe-Ito categorical palette**, colourblind-safe and separable in
  greyscale, because DAC reviewers print papers.
- **Colour assigned by role, fixed across the paper**: `OURS`, `BASELINE`,
  `RANDOM`, `BRUTE`. A tool keeps its colour in every figure.
- Status colours (pass/fail) are **reserved** and never reused as a data series.
- Sized for the two-column layout (`figsize("single"|"double")`) so nothing is
  rescaled at import — rescaling is what makes conference figures unreadable.
- Type 42 fonts (ACM rejects Type 3); 600 dpi; PDF for LaTeX + PNG for review.
- **No dual-axis charts.** Two measures of different scale get two panels.

Validated end to end: `tools/plots/fig_determinism.py` →
`paper/dac27/figures/fig_determinism.pdf`, built from data measured today.

## Planned figures

| # | figure | status |
|---|---|---|
| 1 | Chiron architecture + where the stall hook attaches | to draw |
| 2 | **Determinism wall** — identical tag-transition counts, buggy vs fixed | **done** (real data) |
| 3 | Schedule-controller design | to draw |
| 4 | **Bugs found per simulated cycle** — headline | needs experiment |
| 5 | Interleaving coverage vs schedules explored | needs experiment |
| 6 | Detection latency per corpus bug (strip plot) | needs experiment |
| 7 | Sensitivity to perturbation points / bug depth | needs experiment |

Tables: (T1) the bug corpus; (T2) comparison against existing practice.

## Testing plan (run at the end, all of it, and report failures)

1. `make isa` — 84 ISA tests
2. `make ci-bench` — 5 quad benchmarks
3. `make ci-check` — per-cycle invariants
4. `make ci-smp` — 8 SMP micros
5. `make stress-sweep` — 8 seeds
6. `make lockstep` / `make lockstep-q4` — architectural oracle, 1 and 4 harts
7. `build/swmr_probe.out` — coherence invariants
8. Linux SMP boot — the user runs this
9. **Bit-identical check**: with perturbation disabled, the modified RTL must
   produce byte-identical traces to the unmodified design. This is the gate that
   protects the Linux-booting RTL, and it is non-negotiable.

## Comparison plan — ablations, not unfair cross-tool claims

Compare against **our own configurations plus current practice**:

| arm | what it represents |
|---|---|
| repeated deterministic runs | what the project does today |
| ad-hoc random delay injection | standard UVM testbench practice |
| long-run brute force (Linux boot) | how CO-1 was actually found |
| **PCT-style bounded schedule exploration** | ours |

**Do not manufacture a head-to-head against Cascade / DifuzzRTL / TheHuzz.**
They are single-core, target different DUTs, and vary the *program* rather than
the *schedule*. Cite them as complementary and say plainly why a direct
comparison would mislead — that is more credible than a rigged table.

TSOtool and McVerSi are the honest closest relatives; position as orthogonal
axes, never as baselines we beat.


---

## Figures — built, from measured data (2026-09-05)

Regenerate everything with `make kairos-figs`. Nothing is hand-drawn: a figure
that lives in a binary file drifts out of date the first time the design changes
and nobody notices.

| Figure | Job it does |
|---|---|
| `fig_mechanism` | The RTL hook (one bit gating the fetch→decode handshake), the loop Kairos runs around it, which oracle observes what, and a footer band carrying the two gates: **0** netlist differences compiled out, **8/8** SMP tests unchanged compiled in, ISA 84/84, benchmarks 5/5. This is the "why believe any of this" figure and it is Figure 1. |
| `fig_exploration` | The premise, **audited run by run**: a fingerprint matrix of policies × 8 seeds, filled where a run produced an interleaving never seen before, hollow (with its class number) where it repeated one. The `deterministic` row is one filled cell and seven hollow `1`s, on all three workloads. Bottom row: cross-hart order-pair coverage, which exposes PCT producing *more* schedules but *fewer* distinct orderings on mt-crosscall (48 vs 140). |
| `fig_finding` | One finding end to end — (a) where the 343-cycle delay lands and where the unperturbed run finishes, (b) the 100× cost, (c) the delta-debugging trajectory with every trial marked reproduces/does not, (d) the five-line `.ksched` that replays it. |
| `table_findings.tex` | Every finding with its reproduction command. |

**Two charts were deliberately retired.** A bar of novelty rates restated one
column of the fingerprint matrix, and a two-bar before/after restated one number
of `fig_finding(c)`. A figure that repeats another figure costs a page and buys
nothing — DAC gives six.

Tooling: `tools/plots/diagram.py` (box/arrow primitives so diagrams share the
plots' palette and type), `tools/kairos/fig_*.py`, `tools/kairos/figures.py` as
the single driver.
