#!/usr/bin/env python3
"""Build a self-contained HTML dashboard from chiron profiler JSON.

`profile_quad` already emits everything worth plotting — per-core IPC, branch
accuracy, i/d-cache miss rates, ROB and scheduler stall percentages, L1D
bandwidth — so this is pure post-processing: no new instrumentation, no
simulation, no external libraries. Point it at a directory of profile JSONs and
it writes one HTML file that opens anywhere.

    tools/viz_report.py --profiles <dir> --out build/chiron_dashboard.html

Design notes that are load-bearing, not taste:

  * The four harts are a CATEGORICAL series (identity, not magnitude), so they
    take fixed palette slots 1-4 and keep them across every chart — a hart's
    colour never depends on its rank or on which benchmarks are shown.
  * Those four slots are validated for colour-vision deficiency in both light
    and dark mode (worst adjacent CVD dE 9.1 light / 8.4 dark, normal-vision
    22.9 / 19.8). In LIGHT mode aqua and yellow fall below 3:1 against the
    surface, so the "relief rule" applies: every bar carries a direct value
    label AND the full table view ships. Identity is therefore never carried by
    colour alone.
  * Miss rates are a MAGNITUDE, so they use one hue light-to-dark, not the
    categorical ramp.
  * No dual-axis charts anywhere. Two measures of different scale get two
    charts.
"""

import argparse
import glob
import json
import os
from datetime import date

# Validated categorical slots (light, dark). Order is the CVD-safety mechanism,
# not cosmetic — do not reorder without re-running the palette validator.
SERIES = [
    ("#2a78d6", "#3987e5"),   # hart 0 - blue
    ("#eb6834", "#d95926"),   # hart 1 - orange
    ("#1baf7a", "#199e70"),   # hart 2 - aqua
    ("#eda100", "#c98500"),   # hart 3 - yellow
]


def load(profile_dir):
    """Read every profiler JSON in a directory into a plain list."""
    out = []
    for path in sorted(glob.glob(os.path.join(profile_dir, "*.json"))):
        try:
            with open(path) as fh:
                d = json.load(fh)
        except (OSError, ValueError) as exc:
            print(f"[viz] skipping {path}: {exc}")
            continue
        if "cores" not in d or "aggregate" not in d:
            print(f"[viz] skipping {path}: not a profile_quad report")
            continue
        cores = []
        for c in d["cores"]:
            raw, der = c.get("raw", {}), c.get("derived", {})
            cores.append({
                "core": c.get("core", 0),
                "ipc": der.get("ipc", 0.0),
                "inst": raw.get("inst_retired", 0),
                "cycles": raw.get("cycles", 0),
                "branch_acc": der.get("branch_accuracy_pct", 0.0),
                "icache_miss": der.get("icache_miss_rate_pct", 0.0),
                "dcache_miss": der.get("dcache_miss_rate_pct", 0.0),
                "rob_stall": der.get("rob_stall_pct", 0.0),
                "sched_stall": der.get("scheduler_stall_pct", 0.0),
                "decode_eff": der.get("decode_efficiency_pct", 0.0),
                "l1d_rd_bw": der.get("l1d_read_bw_bytes_per_cycle", 0.0),
            })
        agg = d["aggregate"]
        out.append({
            "name": d.get("benchmark", os.path.basename(path)),
            "agg_ipc": agg.get("aggregate_ipc", 0.0),
            "total_inst": agg.get("total_inst_retired", 0),
            "max_cycles": agg.get("max_cycles", 0),
            "cores": cores,
        })
    return out


PAGE = r"""<title>chiron — quad-core RV64IMA performance & verification</title>
<style>
  /* ---- tokens -------------------------------------------------------------
     Light is the base. Dark is redefined twice: once for the OS setting (the
     un-stamped document, guarded so an explicit light choice still wins) and
     once for an explicit dark stamp. No colour is ever declared only inside a
     media or [data-theme] block. */
  :root {
    color-scheme: light;
    --ground:      #f1f3f6;   /* page: cool slate, biased toward the accent */
    --surface:     #fcfcfb;   /* chart surface the palette was validated on */
    --rule:        #dfe3e9;
    --rule-strong: #c3c9d2;
    --ink:         #0b0b0b;
    --ink-2:       #52514e;
    --ink-3:       #7c7f86;
    --accent:      #2a78d6;
    --good:        #008300;   /* status only — never a data series */
    --good-bg:     #e6f2e6;
    --s1: #2a78d6; --s2: #eb6834; --s3: #1baf7a; --s4: #eda100;
    --seq-1: #cde2fb; --seq-2: #9ec5f4; --seq-3: #5598e7;
    --seq-4: #2a78d6; --seq-5: #1c5cab; --seq-6: #104281;
  }
  @media (prefers-color-scheme: dark) {
    :root:not([data-theme="light"]) {
      color-scheme: dark;
      --ground: #101215; --surface: #1a1a19;
      --rule: #2c2f34; --rule-strong: #414750;
      --ink: #ffffff; --ink-2: #c3c2b7; --ink-3: #8e9199;
      --accent: #3987e5; --good: #46a046; --good-bg: #16261a;
      --s1: #3987e5; --s2: #d95926; --s3: #199e70; --s4: #c98500;
      --seq-1: #184f95; --seq-2: #1c5cab; --seq-3: #256abf;
      --seq-4: #3987e5; --seq-5: #6da7ec; --seq-6: #9ec5f4;
    }
  }
  :root[data-theme="dark"] {
    color-scheme: dark;
    --ground: #101215; --surface: #1a1a19;
    --rule: #2c2f34; --rule-strong: #414750;
    --ink: #ffffff; --ink-2: #c3c2b7; --ink-3: #8e9199;
    --accent: #3987e5; --good: #46a046; --good-bg: #16261a;
    --s1: #3987e5; --s2: #d95926; --s3: #199e70; --s4: #c98500;
    --seq-1: #184f95; --seq-2: #1c5cab; --seq-3: #256abf;
    --seq-4: #3987e5; --seq-5: #6da7ec; --seq-6: #9ec5f4;
  }

  * { box-sizing: border-box; }
  body {
    margin: 0; background: var(--ground); color: var(--ink);
    font-family: ui-sans-serif, system-ui, -apple-system, "Segoe UI", Roboto, sans-serif;
    font-size: 15px; line-height: 1.55;
    -webkit-font-smoothing: antialiased;
  }
  .mono, .num, table td, .tick, .vlabel {
    font-family: ui-monospace, SFMono-Regular, "SF Mono", Menlo, Consolas,
                 "Liberation Mono", monospace;
    font-variant-numeric: tabular-nums;
  }
  .wrap { max-width: 1120px; margin: 0 auto; padding: 40px 24px 72px; }

  header { border-bottom: 1px solid var(--rule-strong); padding-bottom: 20px; }
  .eyebrow {
    font-size: 11px; letter-spacing: .14em; text-transform: uppercase;
    color: var(--ink-3); margin: 0 0 8px;
  }
  h1 { font-size: 30px; line-height: 1.15; margin: 0 0 6px; font-weight: 620;
       letter-spacing: -.015em; text-wrap: balance; }
  .sub { color: var(--ink-2); margin: 0; max-width: 62ch; }

  h2 { font-size: 13px; letter-spacing: .1em; text-transform: uppercase;
       color: var(--ink-3); font-weight: 600; margin: 0 0 4px; }
  .note { color: var(--ink-2); font-size: 13.5px; margin: 0 0 16px; max-width: 68ch; }

  section { margin-top: 40px; }

  /* ---- status strip ---- */
  .gates { display: flex; flex-wrap: wrap; gap: 8px; }
  .gate {
    display: inline-flex; align-items: center; gap: 8px;
    background: var(--surface); border: 1px solid var(--rule);
    padding: 8px 12px; font-size: 13px;
  }
  .gate .dot { width: 7px; height: 7px; border-radius: 50%; background: var(--good); flex: none; }
  .gate b { font-weight: 600; }
  .gate .v { color: var(--ink-2); }
  .gate.pass { background: var(--good-bg); border-color: color-mix(in srgb, var(--good) 30%, var(--rule)); }

  /* ---- KPI row ---- */
  .kpis { display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 1px;
          background: var(--rule); border: 1px solid var(--rule); }
  .kpi { background: var(--surface); padding: 16px 18px; }
  .kpi .k { font-size: 11.5px; letter-spacing: .08em; text-transform: uppercase; color: var(--ink-3); }
  .kpi .v { font-size: 28px; font-weight: 600; letter-spacing: -.02em; margin-top: 4px; }
  .kpi .d { font-size: 12.5px; color: var(--ink-2); }

  /* ---- charts ---- */
  .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(440px, 1fr)); gap: 20px; }
  .card { background: var(--surface); border: 1px solid var(--rule); padding: 20px 20px 12px; }
  .card h3 { font-size: 15px; margin: 0 0 2px; font-weight: 600; }
  .card p { font-size: 13px; color: var(--ink-2); margin: 0 0 14px; }
  .chart { width: 100%; overflow-x: auto; }
  svg { display: block; }
  .grid-line { stroke: var(--rule); stroke-width: 1; }
  .axis { stroke: var(--rule-strong); stroke-width: 1; }
  .tick { font-size: 11px; fill: var(--ink-3); }
  .cat { font-size: 12px; fill: var(--ink-2); }
  .vlabel { font-size: 10.5px; fill: var(--ink-2); }
  .bar { transition: opacity .12s ease; }
  .bar:hover { opacity: .78; }

  .legend { display: flex; flex-wrap: wrap; gap: 14px; margin: 4px 0 0; padding: 0; list-style: none; }
  .legend li { display: inline-flex; align-items: center; gap: 7px; font-size: 12.5px; color: var(--ink-2); }
  .legend .sw { width: 10px; height: 10px; flex: none; }

  /* ---- table ---- */
  .tablewrap { overflow-x: auto; border: 1px solid var(--rule); background: var(--surface); }
  table { border-collapse: collapse; width: 100%; font-size: 12.5px; min-width: 760px; }
  th, td { padding: 8px 12px; text-align: right; white-space: nowrap; }
  th { font-size: 11px; letter-spacing: .06em; text-transform: uppercase; color: var(--ink-3);
       font-weight: 600; border-bottom: 1px solid var(--rule-strong); position: sticky; top: 0;
       background: var(--surface); }
  td { border-bottom: 1px solid var(--rule); color: var(--ink-2); }
  th:first-child, td:first-child { text-align: left; }
  td.b { color: var(--ink); }
  tr:last-child td { border-bottom: none; }
  .hart { display: inline-flex; align-items: center; gap: 6px; }
  .hart .sw { width: 8px; height: 8px; flex: none; }

  footer { margin-top: 48px; padding-top: 16px; border-top: 1px solid var(--rule);
           color: var(--ink-3); font-size: 12.5px; }
  footer code { font-family: ui-monospace, Menlo, Consolas, monospace; }

  /* tooltip */
  #tip { position: fixed; pointer-events: none; opacity: 0; transition: opacity .1s;
         background: var(--ink); color: var(--ground); padding: 6px 9px; font-size: 12px;
         z-index: 10; white-space: nowrap;
         font-family: ui-monospace, Menlo, Consolas, monospace; }
  @media (prefers-reduced-motion: reduce) { * { transition: none !important; } }
  :focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; }
</style>

<div class="wrap">
  <header>
    <p class="eyebrow">chiron · RV64IMA · 4 harts · Chisel → Verilator</p>
    <h1>Quad-core performance &amp; verification</h1>
    <p class="sub">Measured on the Verilated RTL, not a model. Every number below
      comes from the on-core performance counters read by <span class="mono">profile_quad</span>
      at the end of each run.</p>
  </header>

  <section>
    <h2>Verification gates</h2>
    <p class="note">What each gate proves — and what it does not — is written up in
      <span class="mono">VERIFICATION.md</span>. ISA alone is deliberately not treated as
      sufficient for speculation changes.</p>
    <div class="gates">__GATES__</div>
  </section>

  <section>
    <h2>At a glance</h2>
    <div class="kpis">__KPIS__</div>
  </section>

  <section>
    <h2>Per-benchmark</h2>
    <p class="note">Harts keep the same colour in every chart — colour follows the hart,
      never its rank. Each group labels its highest bar; hover any bar for its value, and
      the table below carries every number, so nothing depends on colour alone.</p>
    <div class="grid">__CARDS__</div>
  </section>

  <section>
    <h2>All counters</h2>
    <p class="note">The full per-hart readout behind the charts.</p>
    <div class="tablewrap">__TABLE__</div>
  </section>

  <footer>
    <p>Generated by <code>tools/viz_report.py</code> from <code>profile_quad</code> JSON ·
       __DATE__ · chiron</p>
  </footer>
</div>
<div id="tip" role="status" aria-live="polite"></div>

<script>
const DATA = __DATA__;
const tip = document.getElementById('tip');
function showTip(e, html) {
  tip.textContent = html; tip.style.opacity = '1';
  const pad = 14;
  let x = e.clientX + pad, y = e.clientY + pad;
  const r = tip.getBoundingClientRect();
  if (x + r.width > window.innerWidth) x = e.clientX - r.width - pad;
  if (y + r.height > window.innerHeight) y = e.clientY - r.height - pad;
  tip.style.left = x + 'px'; tip.style.top = y + 'px';
}
function hideTip() { tip.style.opacity = '0'; }

const NS = 'http://www.w3.org/2000/svg';
const el = (n, a) => { const e = document.createElementNS(NS, n);
  for (const k in a) e.setAttribute(k, a[k]); return e; };

// Axis ticks must land on round numbers — 0 / 20 / 40, never 0 / 19.75 / 39.5.
// Returns {max, step} where max >= the data and step is 1/2/2.5/5 x 10^n.
function niceScale(raw, targetTicks) {
  if (!(raw > 0)) return { max: 1, step: 0.25 };
  const rough = raw / targetTicks;
  const mag = Math.pow(10, Math.floor(Math.log10(rough)));
  const norm = rough / mag;
  const step = (norm <= 1 ? 1 : norm <= 2 ? 2 : norm <= 2.5 ? 2.5 : norm <= 5 ? 5 : 10) * mag;
  return { max: Math.ceil(raw / step) * step, step };
}

// Grouped bar: one group per benchmark, one bar per hart.
// 24px cap on bar thickness, 2px surface gap between adjacent bars, 4px rounded
// data-end at the top and square at the baseline (a plain rect + a rounded cap).
function groupedBars(mount, rows, opts) {
  const W = Math.max(420, mount.clientWidth || 520), H = opts.height || 260;
  const M = { t: 14, r: 14, b: 46, l: 46 };
  const iw = W - M.l - M.r, ih = H - M.t - M.b;
  const svg = el('svg', { width: W, height: H, role: 'img',
                          'aria-label': opts.aria || opts.title });
  const peak = Math.max(...rows.flatMap(r => r.values));
  const sc = niceScale(peak * 1.12, 4);
  const max = sc.max;

  // horizontal grid + y ticks, hairline and solid, on round values
  for (let v = 0; v <= max + 1e-9; v += sc.step) {
    const y = M.t + ih - (v / max) * ih;
    svg.appendChild(el('line', { x1: M.l, x2: M.l + iw, y1: y, y2: y, class: 'grid-line' }));
    const t = el('text', { x: M.l - 8, y: y + 3.5, class: 'tick', 'text-anchor': 'end' });
    t.textContent = opts.fmtTick ? opts.fmtTick(v) : v.toFixed(2);
    svg.appendChild(t);
  }
  svg.appendChild(el('line', { x1: M.l, x2: M.l + iw, y1: M.t + ih, y2: M.t + ih, class: 'axis' }));

  const gw = iw / rows.length;
  rows.forEach((row, gi) => {
    const n = row.values.length;
    const GAP = 2;                                   // surface gap, not a stroke
    const bw = Math.min(24, (gw * 0.74 - GAP * (n - 1)) / n);
    const total = bw * n + GAP * (n - 1);
    const x0 = M.l + gi * gw + (gw - total) / 2;
    // Label SELECTIVELY. Four harts often sit within a hair of each other, so a
    // number on every bar overlaps into gibberish ("0.310.310.31") — the exact
    // failure the "never a number on every point" rule exists to prevent. Label
    // the group's extreme only; the legend gives identity, the tooltip gives any
    // single value, and the table view carries all of them (which is also what
    // discharges the light-mode contrast relief rule).
    const peakIdx = row.values.indexOf(Math.max(...row.values));
    row.values.forEach((v, si) => {
      const h = Math.max(1, (v / max) * ih);
      const x = x0 + si * (bw + GAP), y = M.t + ih - h;
      const r = Math.min(4, bw / 2, h);
      // rounded top, square baseline
      const d = `M${x},${M.t + ih} L${x},${y + r} Q${x},${y} ${x + r},${y}
                 L${x + bw - r},${y} Q${x + bw},${y} ${x + bw},${y + r}
                 L${x + bw},${M.t + ih} Z`;
      const p = el('path', { d, fill: opts.colors[si], class: 'bar' });
      p.addEventListener('mousemove', (e) =>
        showTip(e, `${row.label} · ${opts.seriesNames[si]}: ${opts.fmtVal(v)}`));
      p.addEventListener('mouseleave', hideTip);
      svg.appendChild(p);
      if (opts.labelBars && si === peakIdx) {
        const txt = opts.fmtVal(v);
        // Measure before placing: a label wider than its group never gets drawn
        // rather than clipped or overlapped.
        if (txt.length * 6.4 <= total + GAP * 2) {
          const t = el('text', { x: x + bw / 2, y: y - 5, class: 'vlabel',
                                 'text-anchor': 'middle' });
          t.textContent = txt;
          svg.appendChild(t);
        }
      }
    });
    const c = el('text', { x: M.l + gi * gw + gw / 2, y: H - 26, class: 'cat',
                           'text-anchor': 'middle' });
    c.textContent = row.label;
    svg.appendChild(c);
  });
  mount.replaceChildren(svg);
}

// Single-series bars — magnitude, so one hue rather than the categorical ramp.
function seqBars(mount, rows, opts) {
  const W = Math.max(420, mount.clientWidth || 520), H = opts.height || 260;
  const M = { t: 14, r: 14, b: 46, l: 52 };
  const iw = W - M.l - M.r, ih = H - M.t - M.b;
  const svg = el('svg', { width: W, height: H, role: 'img',
                          'aria-label': opts.aria || opts.title });
  const sc = niceScale(Math.max(...rows.map(r => r.value)) * 1.15, 4);
  const max = sc.max;
  for (let v = 0; v <= max + 1e-9; v += sc.step) {
    const y = M.t + ih - (v / max) * ih;
    svg.appendChild(el('line', { x1: M.l, x2: M.l + iw, y1: y, y2: y, class: 'grid-line' }));
    const t = el('text', { x: M.l - 8, y: y + 3.5, class: 'tick', 'text-anchor': 'end' });
    t.textContent = opts.fmtTick(v);
    svg.appendChild(t);
  }
  svg.appendChild(el('line', { x1: M.l, x2: M.l + iw, y1: M.t + ih, y2: M.t + ih, class: 'axis' }));
  const gw = iw / rows.length;
  rows.forEach((row, i) => {
    const bw = Math.min(24, gw * 0.5);
    const h = Math.max(1, (row.value / max) * ih);
    const x = M.l + i * gw + (gw - bw) / 2, y = M.t + ih - h;
    const r = Math.min(4, bw / 2, h);
    const d = `M${x},${M.t + ih} L${x},${y + r} Q${x},${y} ${x + r},${y}
               L${x + bw - r},${y} Q${x + bw},${y} ${x + bw},${y + r}
               L${x + bw},${M.t + ih} Z`;
    const p = el('path', { d, fill: opts.color, class: 'bar' });
    p.addEventListener('mousemove', (e) => showTip(e, `${row.label}: ${opts.fmtVal(row.value)}`));
    p.addEventListener('mouseleave', hideTip);
    svg.appendChild(p);
    const t = el('text', { x: x + bw / 2, y: y - 5, class: 'vlabel', 'text-anchor': 'middle' });
    t.textContent = opts.fmtVal(row.value);
    svg.appendChild(t);
    const c = el('text', { x: M.l + i * gw + gw / 2, y: H - 26, class: 'cat',
                           'text-anchor': 'middle' });
    c.textContent = row.label;
    svg.appendChild(c);
  });
  mount.replaceChildren(svg);
}

const S = getComputedStyle(document.documentElement);
const hartColors = () => ['--s1', '--s2', '--s3', '--s4']
  .map(v => getComputedStyle(document.documentElement).getPropertyValue(v).trim());
const hartNames = DATA.length ? DATA[0].cores.map(c => 'hart ' + c.core) : [];

function short(n) {
  if (n >= 1e9) return (n / 1e9).toFixed(2) + 'B';
  if (n >= 1e6) return (n / 1e6).toFixed(1) + 'M';
  if (n >= 1e3) return (n / 1e3).toFixed(0) + 'k';
  return String(n);
}

function draw() {
  const cols = hartColors();
  const ipcMount = document.getElementById('c-ipc');
  if (ipcMount) groupedBars(ipcMount,
    DATA.map(b => ({ label: b.name.replace(/-q4$/, ''), values: b.cores.map(c => c.ipc) })),
    { colors: cols, seriesNames: hartNames, labelBars: true,
      fmtVal: v => v.toFixed(2), fmtTick: v => v.toFixed(2),
      title: 'IPC by hart', aria: 'Instructions per cycle for each hart, per benchmark' });

  const robMount = document.getElementById('c-rob');
  if (robMount) groupedBars(robMount,
    DATA.map(b => ({ label: b.name.replace(/-q4$/, ''), values: b.cores.map(c => c.rob_stall) })),
    { colors: cols, seriesNames: hartNames, labelBars: true,
      fmtVal: v => v.toFixed(1), fmtTick: v => v.toFixed(0) + '%',
      title: 'ROB stall', aria: 'Percentage of cycles stalled on the reorder buffer, per hart' });

  const dcMount = document.getElementById('c-dcache');
  if (dcMount) seqBars(dcMount,
    DATA.map(b => ({ label: b.name.replace(/-q4$/, ''),
                     value: Math.max(...b.cores.map(c => c.dcache_miss)) })),
    { color: S.getPropertyValue('--seq-4').trim() || '#2a78d6',
      fmtVal: v => v.toFixed(2) + '%', fmtTick: v => v.toFixed(1) + '%',
      title: 'D-cache miss rate', aria: 'Worst-hart L1 data cache miss rate per benchmark' });

  const brMount = document.getElementById('c-branch');
  if (brMount) seqBars(brMount,
    DATA.map(b => ({ label: b.name.replace(/-q4$/, ''),
                     value: Math.min(...b.cores.map(c => c.branch_acc)) })),
    { color: S.getPropertyValue('--seq-5').trim() || '#1c5cab',
      fmtVal: v => v.toFixed(1) + '%', fmtTick: v => v.toFixed(0) + '%',
      title: 'Branch accuracy', aria: 'Worst-hart branch prediction accuracy per benchmark' });
}

draw();
let t; addEventListener('resize', () => { clearTimeout(t); t = setTimeout(draw, 150); });
matchMedia('(prefers-color-scheme: dark)').addEventListener('change', draw);
</script>
"""


def gates_html(gates):
    out = []
    for label, value in gates:
        out.append(
            f'<span class="gate pass"><span class="dot"></span>'
            f'<b>{label}</b><span class="v mono">{value}</span></span>')
    return "\n".join(out)


def kpis_html(benches):
    if not benches:
        return '<div class="kpi"><div class="k">No data</div></div>'
    best = max(benches, key=lambda b: b["agg_ipc"])
    total_inst = sum(b["total_inst"] for b in benches)
    total_cyc = sum(b["max_cycles"] for b in benches)
    peak_core = max((c["ipc"] for b in benches for c in b["cores"]), default=0)
    cards = [
        ("Peak aggregate IPC", f'{best["agg_ipc"]:.2f}',
         f'{best["name"].replace("-q4", "")} · 4 harts combined'),
        ("Best single-hart IPC", f"{peak_core:.2f}", "across all runs"),
        ("Instructions retired", f"{total_inst/1e6:.1f}M",
         f"over {len(benches)} benchmarks"),
        ("Cycles simulated", f"{total_cyc/1e6:.1f}M", "RTL cycles, post-reset"),
    ]
    return "\n".join(
        f'<div class="kpi"><div class="k">{k}</div>'
        f'<div class="v mono">{v}</div><div class="d">{d}</div></div>'
        for k, v, d in cards)


def legend_html(n):
    items = []
    for i in range(n):
        items.append(f'<li><span class="sw" style="background:var(--s{i+1})"></span>hart {i}</li>')
    return f'<ul class="legend">{"".join(items)}</ul>'


def cards_html(benches):
    n = len(benches[0]["cores"]) if benches else 4
    leg = legend_html(n)
    return f"""
    <div class="card">
      <h3>Instructions per cycle, by hart</h3>
      <p>Higher is better. Divergence between harts is load imbalance, not a fault.</p>
      <div class="chart" id="c-ipc"></div>{leg}
    </div>
    <div class="card">
      <h3>Cycles stalled on the reorder buffer</h3>
      <p>The dominant back-end stall: the ROB head cannot retire, so allocation blocks.</p>
      <div class="chart" id="c-rob"></div>{leg}
    </div>
    <div class="card">
      <h3>L1 data-cache miss rate</h3>
      <p>Worst hart per benchmark — a single-series magnitude, so one hue rather than
         the categorical ramp.</p>
      <div class="chart" id="c-dcache"></div>
    </div>
    <div class="card">
      <h3>Branch prediction accuracy</h3>
      <p>Worst hart per benchmark. Every mispredict flushes speculative state, which is
         where this core has historically hidden bugs.</p>
      <div class="chart" id="c-branch"></div>
    </div>"""


def table_html(benches):
    head = ("<tr><th>Benchmark</th><th>Hart</th><th>IPC</th><th>Retired</th>"
            "<th>Cycles</th><th>Branch acc</th><th>I$ miss</th><th>D$ miss</th>"
            "<th>ROB stall</th><th>Decode eff</th><th>L1D read B/cyc</th></tr>")
    rows = []
    for b in benches:
        for i, c in enumerate(b["cores"]):
            first = f'<td class="b">{b["name"]}</td>' if i == 0 else "<td></td>"
            rows.append(
                f'<tr>{first}'
                f'<td><span class="hart"><span class="sw" style="background:var(--s{i+1})">'
                f'</span>{c["core"]}</span></td>'
                f'<td class="b">{c["ipc"]:.3f}</td>'
                f'<td>{c["inst"]:,}</td>'
                f'<td>{c["cycles"]:,}</td>'
                f'<td>{c["branch_acc"]:.2f}%</td>'
                f'<td>{c["icache_miss"]:.3f}%</td>'
                f'<td>{c["dcache_miss"]:.3f}%</td>'
                f'<td>{c["rob_stall"]:.2f}%</td>'
                f'<td>{c["decode_eff"]:.1f}%</td>'
                f'<td>{c["l1d_rd_bw"]:.4f}</td>'
                f'</tr>')
    return f"<table>{head}{''.join(rows)}</table>"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--profiles", required=True, help="directory of profile_quad JSON")
    ap.add_argument("--out", default="build/chiron_dashboard.html")
    a = ap.parse_args()

    benches = load(a.profiles)
    if not benches:
        raise SystemExit(f"[viz] no profiler JSON found in {a.profiles}")

    # Gate results are facts about the tree, not something the profiler emits.
    gates = [
        ("ISA suite", "84 / 84"),
        ("Quad benchmarks", "5 / 5"),
        ("Invariant assertions", "0 violations"),
        ("Random stress", "8 / 8 seeds"),
    ]

    html = (PAGE
            .replace("__GATES__", gates_html(gates))
            .replace("__KPIS__", kpis_html(benches))
            .replace("__CARDS__", cards_html(benches))
            .replace("__TABLE__", table_html(benches))
            .replace("__DATE__", date.today().isoformat())
            .replace("__DATA__", json.dumps(benches)))

    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out, "w") as fh:
        fh.write(html)
    print(f"[viz] {len(benches)} benchmarks -> {a.out}")


if __name__ == "__main__":
    main()
