#!/usr/bin/env bash
# Compile main.tex into main.pdf (pdflatex -> bibtex -> pdflatex x2).
set -e

cd "$(dirname "$0")"

SRC=main
LOGDIR=.build

mkdir -p "$LOGDIR"

echo "[1/4] pdflatex (pass 1)"
pdflatex -interaction=nonstopmode -halt-on-error "$SRC.tex" > "$LOGDIR/pdflatex1.log" 2>&1

echo "[2/4] bibtex"
# Non-fatal: bibtex exits non-zero when the draft has no \cite commands yet,
# and `set -e` would otherwise abort the build before the PDF is finished.
bibtex "$SRC" > "$LOGDIR/bibtex.log" 2>&1 || \
  echo "  (bibtex reported an issue -- see $LOGDIR/bibtex.log; expected while the draft has no citations)"

echo "[3/4] pdflatex (pass 2)"
pdflatex -interaction=nonstopmode -halt-on-error "$SRC.tex" > "$LOGDIR/pdflatex2.log" 2>&1

echo "[4/4] pdflatex (pass 3)"
pdflatex -interaction=nonstopmode -halt-on-error "$SRC.tex" > "$LOGDIR/pdflatex3.log" 2>&1

# Sanity checks
if grep -qi "undefined" "$LOGDIR/pdflatex3.log"; then
  echo "WARNING: undefined references/citations remain — see $LOGDIR/pdflatex3.log"
fi

PAGES=$(pdfinfo "$SRC.pdf" 2>/dev/null | awk '/^Pages:/{print $2}')
echo ""
LIMIT=${DAC_PAGE_LIMIT:-6}
echo ""
echo "Done: $SRC.pdf ($PAGES pages total, references included). Logs in $LOGDIR/."
if [ -n "$PAGES" ] && [ "$PAGES" -gt "$LIMIT" ]; then
  echo "OVER BUDGET: $PAGES pages > $LIMIT. DAC research papers have historically"
  echo "been 6 pages in ACM sigconf. Re-check the DAC 2027 CFP when it is posted;"
  echo "override with DAC_PAGE_LIMIT=n if it differs."
fi
