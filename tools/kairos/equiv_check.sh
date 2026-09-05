#!/usr/bin/env bash
# Prove that the Kairos RTL hook changes nothing when it is not used.
#
# This is the gate that protects a design people already trust. Chiron boots
# quad-core Linux; a verification tool that quietly perturbs the core it is
# meant to verify is worthless, and a reviewer is right to ask for proof rather
# than a promise. There are two claims and this script checks the first, which
# is the strong one:
#
#   1. COMPILED OUT  -> with `enableScheduleControl = false` the generated
#                       Verilog is IDENTICAL to a build that never heard of
#                       Kairos -- every wire, every assignment, every module.
#
#                       One caveat, stated up front because hiding it would be
#                       worse than the caveat: Chisel stamps each emitted line
#                       with the SOURCE LOCATION that produced it (`// @[core.
#                       scala 76:25]`). Adding four lines to core.scala shifts
#                       those line numbers, so a byte-for-byte cmp fails on
#                       comment text alone. Comments are not hardware. This
#                       script therefore reports BOTH: the raw comparison, and
#                       the comparison with `// @[...]` provenance stripped.
#                       The second one is the claim; the first is reported so
#                       nobody can accuse the check of quietly normalising away
#                       something real. Any surviving difference is a real
#                       netlist difference and a FAIL.
#
#   2. COMPILED IN, IDLE -> with the knob true and the stall mask held at zero,
#                       every SMP test behaves exactly as it does without
#                       Kairos. That is `make kairos-gate`, which runs on the
#                       real model rather than on the netlist.
#
# Method. The pre-Kairos RTL is whatever HEAD says, so the baseline is
# elaborated in a throwaway git worktree at HEAD. Nothing in your working tree
# is checked out, stashed, or reverted -- if this script is interrupted you lose
# nothing. The one thing it does touch is the knob in configuration.scala, and
# it restores that (and the original system.v, with its original mtime, so the
# Verilator library is not needlessly invalidated) on every exit path.
#
# Usage: tools/kairos/equiv_check.sh   [from the repo root]
set -u

CFG=src/main/scala/common/configuration.scala
WORK=build/kairos-equiv          # kept, so a FAIL can actually be inspected
mkdir -p "$WORK"
BASE_TREE=$(mktemp -d)/baseline
KEEP=$WORK/system.v.orig

cleanup() {
  echo "[equiv] restoring working tree"
  sed -i 's/^  val enableScheduleControl = false$/  val enableScheduleControl = true/' "$CFG"
  # Restore the original system.v *with its original mtime*. If it comes back
  # looking newer than sim/rtl/obj_dir_fast/Vsystem__ALL.a, the next build
  # re-verilates the whole model for nothing -- and, worse, a partial rebuild is
  # exactly how this tree has previously ended up running a stale netlist.
  if [ -f "$KEEP" ]; then cp "$KEEP" sim/rtl/system.v; touch -r "$KEEP" sim/rtl/system.v; fi
  git worktree remove --force "$BASE_TREE" 2>/dev/null
  rmdir "$(dirname "$BASE_TREE")" 2>/dev/null
}
trap cleanup EXIT

command -v git >/dev/null || { echo "git required"; exit 2; }
[ -f "$CFG" ] || { echo "run from the repo root"; exit 2; }
cp -p sim/rtl/system.v "$KEEP" 2>/dev/null

echo "[equiv] 1/3 elaborating the pre-Kairos baseline at HEAD (throwaway worktree)"
git worktree add --detach "$BASE_TREE" HEAD >/dev/null || exit 2
( cd "$BASE_TREE" && make sim/rtl/system.v ) >"$WORK/base.log" 2>&1
if [ ! -s "$BASE_TREE/sim/rtl/system.v" ]; then
  echo "[equiv] baseline elaboration FAILED — see $WORK/base.log"; tail -20 "$WORK/base.log"; exit 1
fi
cp "$BASE_TREE/sim/rtl/system.v" "$WORK/baseline.v"

echo "[equiv] 2/3 elaborating with enableScheduleControl = false"
sed -i 's/^  val enableScheduleControl = true$/  val enableScheduleControl = false/' "$CFG"
grep -q 'enableScheduleControl = false' "$CFG" || { echo "[equiv] could not flip the knob"; exit 2; }
make sim/rtl/system.v >"$WORK/off.log" 2>&1
if [ ! -s sim/rtl/system.v ]; then
  echo "[equiv] knob-off elaboration FAILED — see $WORK/off.log"; tail -20 "$WORK/off.log"; exit 1
fi
cp sim/rtl/system.v "$WORK/off.v"

echo "[equiv] 3/3 comparing"

raw=$(diff "$WORK/baseline.v" "$WORK/off.v" | grep -c '^[<>]')
# Strip Chisel provenance comments (`// @[file line:col]`) and trailing space.
strip() { sed -e 's:// @\[[^]]*\]::g' -e 's/[[:space:]]*$//' "$1"; }
strip "$WORK/baseline.v" > "$WORK/baseline.norm.v"
strip "$WORK/off.v"      > "$WORK/off.norm.v"
norm=$(diff "$WORK/baseline.norm.v" "$WORK/off.norm.v" | grep -c '^[<>]')

echo "[equiv]   raw differing lines ............ $raw  (source-location comments included)"
echo "[equiv]   differing lines, comments stripped $norm"

if [ "$norm" -eq 0 ]; then
  if [ "$raw" -eq 0 ]; then
    echo "[equiv] PASS — byte-identical to the pre-Kairos design."
  else
    echo "[equiv] PASS — netlist identical; the $raw raw differences are Chisel"
    echo "[equiv]        source-location comments (// @[core.scala N:M]) shifted by"
    echo "[equiv]        the four added lines. No wire, port or assignment differs."
  fi
  rc=0
else
  echo "[equiv] FAIL — $norm real netlist difference(s):"
  diff "$WORK/baseline.norm.v" "$WORK/off.norm.v" | head -40
  echo "[equiv] artefacts kept in $WORK/ for inspection"
  rc=1
fi
exit $rc
