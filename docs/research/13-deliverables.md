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
