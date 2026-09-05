"""A small schematic-diagram engine for the DAC 2027 figures.

Why this exists. The first version of these diagrams placed boxes and labels by
hand-picked coordinates. That works until a label is one word longer than you
guessed, and then text runs over a neighbouring box and the figure reads as
clutter. You cannot eyeball your way out of it either: the overlap depends on
the font, the DPI and the final scale factor in LaTeX.

So this module does three things instead:

  1. **Text is wrapped to fit its box, by measurement.** We render the string,
     ask matplotlib for its actual extent, and shrink the wrap width until it
     fits. No guessing at characters-per-inch.
  2. **Boxes grow to fit their text**, never the other way round.
  3. **`Canvas.check()` raises** if any two boxes overlap or any text escapes
     its box. A figure that would look clogged fails the build rather than
     landing in the paper.

Everything is plain matplotlib. Coordinates are a user-chosen grid (0..W, 0..H)
so a diagram is described by its structure, not by inches.
"""

import textwrap

import matplotlib.patches as mpatches
import matplotlib.pyplot as plt
from matplotlib.path import Path

from chiron_style import CATEGORICAL, GRID, INK, INK_MUTED, STATUS

# ── Semantic roles ──────────────────────────────────────────────────────────
# A box's colour says what KIND of thing it is, so the reader learns the
# encoding once and it holds across every diagram in the paper.
ROLE = {
    # role          face        edge          text
    "ours":       ("#E8F1F8", CATEGORICAL[0], INK),   # Kairos components
    "dut":        ("#FBEDE4", CATEGORICAL[1], INK),   # the design under test
    "external":   ("#F2F2F2", INK_MUTED,      INK),   # pre-existing infra
    "artifact":   ("#FFFFFF", INK_MUTED,      INK),   # files / outputs
    "good":       ("#E6F4EF", STATUS["pass"], INK),
    "bad":        ("#FBE9E0", STATUS["fail"], INK),
    "ghost":      ("#FFFFFF", GRID,           INK_MUTED),
}


class Box:
    __slots__ = ("x", "y", "w", "h", "name")

    def __init__(self, x, y, w, h, name):
        self.x, self.y, self.w, self.h, self.name = x, y, w, h, name

    # Anchor points, so arrows attach to a side rather than a magic number.
    @property
    def left(self):   return (self.x, self.y + self.h / 2)
    @property
    def right(self):  return (self.x + self.w, self.y + self.h / 2)
    @property
    def top(self):    return (self.x + self.w / 2, self.y + self.h)
    @property
    def bottom(self): return (self.x + self.w / 2, self.y)
    @property
    def center(self): return (self.x + self.w / 2, self.y + self.h / 2)

    def port(self, side, frac=0.5):
        """A point along one side; frac runs 0..1 left-to-right / bottom-to-top."""
        if side == "top":    return (self.x + self.w * frac, self.y + self.h)
        if side == "bottom": return (self.x + self.w * frac, self.y)
        if side == "left":   return (self.x, self.y + self.h * frac)
        if side == "right":  return (self.x + self.w, self.y + self.h * frac)
        raise ValueError(side)

    def overlaps(self, o, pad=0.0):
        return not (self.x + self.w + pad <= o.x or o.x + o.w + pad <= self.x or
                    self.y + self.h + pad <= o.y or o.y + o.h + pad <= self.y)


class Canvas:
    """A diagram drawn on a W x H grid inside one matplotlib axes."""

    def __init__(self, ax, w, h):
        self.ax, self.W, self.H = ax, w, h
        self.boxes = []
        self._texts = []          # (artist, owning Box or None)
        self._free = []           # unowned text: notes and arrow labels
        ax.set_xlim(0, w)
        ax.set_ylim(0, h)
        ax.set_aspect("auto")
        ax.axis("off")
        for s in ax.spines.values():
            s.set_visible(False)

    # ── measurement ─────────────────────────────────────────────────────────
    def _extent(self, artist):
        """Artist extent in data coordinates."""
        fig = self.ax.figure
        fig.canvas.draw()
        bb = artist.get_window_extent(fig.canvas.get_renderer())
        p0 = self.ax.transData.inverted().transform((bb.x0, bb.y0))
        p1 = self.ax.transData.inverted().transform((bb.x1, bb.y1))
        return p0[0], p0[1], p1[0] - p0[0], p1[1] - p0[1]   # x, y, w, h

    def _fit_wrap(self, s, max_w, size, weight="normal"):
        """Longest wrap width (in characters) whose rendered text fits max_w."""
        probe = self.ax.text(0, 0, s, fontsize=size, fontweight=weight, alpha=0)
        _, _, w, _ = self._extent(probe)
        probe.remove()
        if w <= max_w or not s.strip():
            return s
        # Characters scale roughly linearly with width; start from that estimate
        # and walk down until it genuinely fits.
        n = max(6, int(len(s) * max_w / w))
        for cand in range(n, 3, -1):
            wrapped = "\n".join(textwrap.wrap(s, cand)) or s
            probe = self.ax.text(0, 0, wrapped, fontsize=size,
                                 fontweight=weight, alpha=0)
            _, _, w, _ = self._extent(probe)
            probe.remove()
            if w <= max_w:
                return wrapped
        return "\n".join(textwrap.wrap(s, 8)) or s

    # ── primitives ──────────────────────────────────────────────────────────
    def box(self, x, y, w, h=None, title="", body="", role="ours",
            title_size=8, body_size=6.8, pad=0.12, radius=0.10, dashed=False,
            name=None, mono_body=False):
        """A titled box. Height auto-grows to hold the (wrapped) text."""
        face, edge, ink = ROLE[role]
        inner = w - 2 * pad

        t_txt = self._fit_wrap(title, inner, title_size, "bold") if title else ""
        b_txt = self._fit_wrap(body, inner, body_size) if body else ""

        # Measure the two blocks to size the box.
        th = bh = 0.0
        if t_txt:
            a = self.ax.text(0, 0, t_txt, fontsize=title_size,
                             fontweight="bold", alpha=0)
            th = self._extent(a)[3]; a.remove()
        if b_txt:
            a = self.ax.text(0, 0, b_txt, fontsize=body_size, alpha=0,
                             family="monospace" if mono_body else None)
            bh = self._extent(a)[3]; a.remove()
        gap = 0.09 if (t_txt and b_txt) else 0.0
        need = th + bh + gap + 2 * pad
        h = need if h is None else max(h, need)

        self.ax.add_patch(mpatches.FancyBboxPatch(
            (x, y), w, h,
            boxstyle=f"round,pad=0,rounding_size={radius}",
            linewidth=0.9, edgecolor=edge, facecolor=face,
            linestyle=(0, (2.4, 1.6)) if dashed else "solid", zorder=2))

        b = Box(x, y, w, h, name or title or f"box{len(self.boxes)}")
        self.boxes.append(b)

        # Text block is vertically centred as a unit.
        top = y + h / 2 + (th + bh + gap) / 2
        if t_txt:
            a = self.ax.text(x + w / 2, top, t_txt, fontsize=title_size,
                             fontweight="bold", ha="center", va="top",
                             color=ink, zorder=3, linespacing=1.25)
            self._texts.append((a, b))
            top -= th + gap
        if b_txt:
            a = self.ax.text(x + w / 2, top, b_txt, fontsize=body_size,
                             ha="center", va="top", color=INK_MUTED, zorder=3,
                             linespacing=1.3,
                             family="monospace" if mono_body else None)
            self._texts.append((a, b))
        return b

    def group(self, x, y, w, h, label="", color=None):
        """A translucent grouping frame drawn behind everything else."""
        color = color or INK_MUTED
        self.ax.add_patch(mpatches.FancyBboxPatch(
            (x, y), w, h, boxstyle="round,pad=0,rounding_size=0.14",
            linewidth=0.8, edgecolor=color, facecolor="none",
            linestyle=(0, (3.0, 2.0)), zorder=1))
        if label:
            self.ax.text(x + 0.14, y + h - 0.055, label, fontsize=7,
                         fontweight="bold", ha="left", va="top", color=color,
                         zorder=3)
        return Box(x, y, w, h, label or "group")

    def arrow(self, a, b, label="", style="-|>", color=None, dashed=False,
              waypoints=None, label_side="above", label_size=6.5, lw=0.9):
        """An arrow from point a to point b, optionally via waypoints."""
        color = color or INK_MUTED
        pts = [a] + list(waypoints or []) + [b]
        verts, codes = [pts[0]], [Path.MOVETO]
        for p in pts[1:]:
            verts.append(p); codes.append(Path.LINETO)
        self.ax.add_patch(mpatches.FancyArrowPatch(
            path=Path(verts, codes), arrowstyle=style, mutation_scale=8,
            linewidth=lw, color=color, zorder=4,
            linestyle=(0, (2.4, 1.6)) if dashed else "solid",
            shrinkA=0, shrinkB=0))
        if label:
            mid = pts[len(pts) // 2] if len(pts) > 2 else (
                ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2))
            dy = 0.09 if label_side == "above" else -0.09
            va = "bottom" if label_side == "above" else "top"
            t = self.ax.text(mid[0], mid[1] + dy, label, fontsize=label_size,
                             ha="center", va=va, color=color, zorder=5)
            # owner stays None, but mark it free so check() tests it against
            # every box -- an arrow label landing on a box title is exactly
            # the "clogged" failure this class exists to make impossible.
            self._texts.append((t, None))
            self._free.append(t)
        return self

    def note(self, x, y, s, size=6.5, ha="left", va="top", color=None,
             width=None, italic=False, mono=False):
        color = color or INK_MUTED
        if width:
            s = self._fit_wrap(s, width, size)
        t = self.ax.text(x, y, s, fontsize=size, ha=ha, va=va, color=color,
                         zorder=5, linespacing=1.35,
                         style="italic" if italic else "normal",
                         family="monospace" if mono else None)
        self._texts.append((t, None))
        return t

    # ── the guard ───────────────────────────────────────────────────────────
    def check(self, pad=0.03, tol=0.02):
        """Fail loudly on the two things that make a diagram unreadable."""
        bad = []
        for i, a in enumerate(self.boxes):
            for b in self.boxes[i + 1:]:
                if a.overlaps(b, pad):
                    bad.append(f"boxes overlap: {a.name!r} / {b.name!r}")
        for artist, owner in self._texts:
            x, y, w, h = self._extent(artist)
            if owner is not None:
                if (x < owner.x - tol or y < owner.y - tol or
                        x + w > owner.x + owner.w + tol or
                        y + h > owner.y + owner.h + tol):
                    bad.append(f"text escapes its box: {owner.name!r}")
            if x < -tol or y < -tol or x + w > self.W + tol or y + h > self.H + tol:
                bad.append(f"text outside the canvas: "
                           f"{artist.get_text()[:34]!r}")
        # Unowned text (notes, arrow labels) must not land on a box. This was
        # the one gap in the original check, and it is how a label ended up
        # printed across a box title in the mechanism figure.
        for artist in self._free:
            x, y, w, h = self._extent(artist)
            for b in self.boxes:
                if (x < b.x + b.w - tol and x + w > b.x + tol and
                        y < b.y + b.h - tol and y + h > b.y + tol):
                    bad.append(f"free text {artist.get_text()[:24]!r} "
                               f"overlaps box {b.name!r}")
        if bad:
            raise AssertionError("schematic layout check failed:\n  " +
                                 "\n  ".join(sorted(set(bad))))
        return self
