"""Shared matplotlib style for the DAC 2027 manuscript.

Every figure in the paper imports from here so the whole set reads as one
system. Import and call `use_paper_style()` before plotting, and size figures
with `figsize(kind)` rather than hand-picking numbers.

Design rules this file enforces, and why:

* **Colour is assigned by the job it does, never cycled.** CATEGORICAL is a
  fixed order for identity (which tool / which configuration); SEQUENTIAL is for
  magnitude; STATUS is reserved for pass/fail and is never reused as "series 4".
* **Colourblind-safe.** CATEGORICAL is Okabe-Ito, which is designed for
  deuteranopia/protanopia and stays separable in greyscale print. DAC reviewers
  read on paper.
* **No dual-axis charts.** Two measures of different scale get two panels or an
  indexed common base. A dual y-axis is the single most common chart error and
  makes the reader infer a correlation the data does not contain.
* **Figures are sized for a two-column ACM page** so text is legible at final
  size without rescaling — rescaling is what makes conference figures unreadable.
* **Digits that line up use tabular figures**, and log axes are labelled as such
  explicitly, because a log axis that is not obviously log misleads.
"""

import matplotlib as mpl
import matplotlib.pyplot as plt

# ── Palettes ────────────────────────────────────────────────────────────────
# Okabe-Ito, in a fixed order. Assign by index and never re-order between
# figures: a given tool keeps its colour across the whole paper.
CATEGORICAL = [
    "#0072B2",  # blue      — ours / schedule exploration
    "#D55E00",  # vermillion— baseline / deterministic
    "#009E73",  # green     — random delay injection
    "#CC79A7",  # purple    — long-run brute force
    "#E69F00",  # orange    — spare
    "#56B4E9",  # sky       — spare
    "#F0E442",  # yellow    — spare (weak on white; use last)
    "#000000",  # black     — reference lines
]

# Named roles, so a figure says what it means rather than an index.
OURS      = CATEGORICAL[0]  # blue        — windowed (the policy that found bugs)
BASELINE  = CATEGORICAL[1]  # vermillion  — deterministic regression
RANDOM    = CATEGORICAL[2]  # green       — per-cycle random delay
BRUTE     = CATEGORICAL[3]  # purple      — long-run brute force / Linux boot
PCT       = CATEGORICAL[4]  # orange      — PCT transferred from software

# Policy identity is fixed across every figure in the paper.
POLICY_COLOR = {
    "deterministic": BASELINE,
    "random":        RANDOM,
    "pct":           PCT,
    "windowed":      OURS,
}
POLICY_LABEL = {
    "deterministic": "deterministic",
    "random":        "random delay",
    "pct":           "PCT",
    "windowed":      "windowed",
}

# Single-hue ramp for magnitude (light -> dark). Never a rainbow.
SEQUENTIAL = ["#DEEBF7", "#9ECAE1", "#4292C6", "#2171B5", "#08519C"]

# Reserved for correctness outcomes only.
STATUS = {"pass": "#009E73", "fail": "#D55E00", "unknown": "#999999"}

INK        = "#1A1A1A"   # primary text
INK_MUTED  = "#6B6B6B"   # secondary text, axis labels
GRID       = "#D9D9D9"


def use_paper_style():
    """Apply the manuscript style. Call once before plotting."""
    mpl.rcParams.update({
        # Type. Match the paper's body serif so figures do not look pasted in.
        "font.family": "serif",
        "font.serif": ["Times New Roman", "Nimbus Roman", "DejaVu Serif"],
        "font.size": 8,
        "axes.titlesize": 8,
        "axes.labelsize": 8,
        "xtick.labelsize": 7,
        "ytick.labelsize": 7,
        "legend.fontsize": 7,
        # Recessive grid and axes: the data is the figure, not the frame.
        "axes.grid": True,
        "axes.grid.axis": "y",
        "grid.color": GRID,
        "grid.linewidth": 0.5,
        "axes.axisbelow": True,
        "axes.edgecolor": INK_MUTED,
        "axes.linewidth": 0.6,
        "axes.spines.top": False,
        "axes.spines.right": False,
        "xtick.color": INK_MUTED,
        "ytick.color": INK_MUTED,
        "axes.labelcolor": INK,
        "text.color": INK,
        # Thin marks.
        "lines.linewidth": 1.2,
        "lines.markersize": 4,
        "patch.linewidth": 0.5,
        # Legend without a heavy box.
        "legend.frameon": False,
        # Output.
        "figure.dpi": 150,
        "savefig.dpi": 600,          # camera-ready needs 600 for line art
        "savefig.bbox": "tight",
        "savefig.pad_inches": 0.01,
        "pdf.fonttype": 42,
        "hatch.linewidth": 0.45,          # embed TrueType; ACM rejects Type 3
        "ps.fonttype": 42,
        "axes.prop_cycle": mpl.cycler(color=CATEGORICAL),
    })


def figsize(kind="single"):
    """Figure size in inches for the ACM two-column layout.

    single  — fits one column (3.33in wide)
    double  — spans both columns (7.0in wide)
    tall    — one column, taller, for stacked panels
    """
    return {
        "single": (3.33, 2.15),
        "double": (7.00, 2.40),
        "tall":   (3.33, 3.40),
        "square": (3.33, 3.00),
        "teaser": (7.00, 2.15),
        "arch":   (7.00, 2.72),
        "hook":   (3.33, 2.35),
        "explore":(7.00, 2.35),
        "find":   (7.00, 1.72),
        "pos":    (3.33, 2.55),
    }[kind]


def save(fig, path, exact=True):
    """Save as PDF (vector, for LaTeX) and PNG (for quick review).

    `exact` writes the figure at exactly its declared figsize instead of
    cropping to the ink. This matters more than it sounds: with a tight bbox a
    7.0in figure comes out ~5.7in, LaTeX then scales it back up to the text
    width, and every 8pt label silently renders at ~10pt. Figures in the same
    paper end up with different effective type sizes depending on how much
    whitespace each one happened to have. Declaring the size and filling it
    keeps one type size across the whole paper.
    """
    kw = dict(bbox_inches=None, pad_inches=0.0) if exact else {}
    fig.savefig(f"{path}.pdf", **kw)
    fig.savefig(f"{path}.png", **kw)
    plt.close(fig)
    return f"{path}.pdf"


def label_bars(ax, bars, fmt="{:.0f}", offset=1.02):
    """Direct-label bars instead of forcing a legend lookup.

    Selective labelling only — never a number on every point of a dense chart.
    """
    for b in bars:
        h = b.get_height()
        ax.text(b.get_x() + b.get_width() / 2, h * offset, fmt.format(h),
                ha="center", va="bottom", fontsize=6.5, color=INK)


def note_log_axis(ax, axis="y"):
    """Make a log axis unmistakable. A log scale that is not obviously log is
    the second most common way a chart misleads."""
    lab = ax.get_ylabel() if axis == "y" else ax.get_xlabel()
    suffix = " (log scale)"
    if suffix not in lab:
        (ax.set_ylabel if axis == "y" else ax.set_xlabel)(lab + suffix)
