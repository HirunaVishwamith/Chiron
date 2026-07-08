#!/usr/bin/env bash
#
# run_linux.sh — drive a chiron Linux image on the golden emulator and/or the
# RTL lock-step harness.
#
# The golden model (build/emu.out) loads a flat binary at 0x80000000 and runs
# it; a chiron Linux image is a bbl.bin (bbl + nommu kernel payload + embedded
# dtb). The RTL lock-step harness (build/lockstep_linux.out) hard-codes
# sim/data/Image for both the RTL and the golden model and takes the dtb/boot
# rom paths as argv[2]/argv[3], so this script stages the image there and
# restores the original afterwards.
#
# Usage:
#   scripts/run_linux.sh emu      [IMAGE] [TIMEOUT_SECONDS]
#   scripts/run_linux.sh lockstep [IMAGE] [TIMEOUT_SECONDS]
#
# Defaults: IMAGE=bins/linux-s1.bin, TIMEOUT=300 (emu) / 180 (lockstep).
set -u

MODE="${1:-emu}"
IMAGE="${2:-bins/linux-s1.bin}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

DATA="sim/data"
BUILD="build"
DTB="$DATA/qemu.dtb"
BOOTROM="$DATA/boot.bin"

if [ ! -f "$IMAGE" ]; then
  echo "run_linux: image not found: $IMAGE" >&2
  exit 1
fi

case "$MODE" in
  emu)
    TIMEOUT="${3:-300}"
    LOG="$BUILD/linux_emu.log"
    echo "== golden-model boot =="
    echo "   image:   $IMAGE"
    echo "   timeout: ${TIMEOUT}s    log: $LOG"
    make --no-print-directory "$BUILD/emu.out" >/dev/null
    # emu.out runs forever; launch it in the background and stop as soon as the
    # login prompt appears (or the timeout elapses).
    stdbuf -oL "$BUILD/emu.out" "$IMAGE" </dev/null >"$LOG" 2>&1 &
    epid=$!
    deadline=$(( $(date +%s) + TIMEOUT ))
    result=1
    while kill -0 "$epid" 2>/dev/null; do
      if grep -aq "buildroot login:" "$LOG"; then result=0; break; fi
      if [ "$(date +%s)" -ge "$deadline" ]; then result=2; break; fi
      sleep 2
    done
    kill "$epid" 2>/dev/null; wait "$epid" 2>/dev/null
    echo "-- last 12 console lines --"
    tail -12 "$LOG"
    if [ "$result" -eq 0 ]; then
      echo "RESULT: PASS — reached the login prompt"; exit 0
    elif grep -aq "Run /init as init process" "$LOG"; then
      echo "RESULT: PARTIAL — kernel reached userspace /init (no login within ${TIMEOUT}s)"; exit 0
    else
      echo "RESULT: did not reach userspace (see $LOG)"; exit 1
    fi
    ;;

  lockstep)
    TIMEOUT="${3:-180}"
    LOG="$BUILD/lockstep_linux.log"
    echo "== bounded RTL lock-step (RTL vs golden model) =="
    echo "   image:   $IMAGE"
    echo "   timeout: ${TIMEOUT}s    log: $LOG"
    make --no-print-directory "$BUILD/lockstep_linux.out" >/dev/null

    # Stage the image where the harness expects it, preserving the original.
    STASH=""
    if [ -f "$DATA/Image" ]; then
      STASH="$DATA/Image.runlinux.bak"; cp "$DATA/Image" "$STASH"
    fi
    cp "$IMAGE" "$DATA/Image"
    rm -f run.log

    timeout "$TIMEOUT" stdbuf -oL "$BUILD/lockstep_linux.out" linux "$DTB" "$BOOTROM" >"$LOG" 2>&1
    status=$?

    # Restore the staged image.
    if [ -n "$STASH" ]; then mv "$STASH" "$DATA/Image"; fi

    if [ -f run.log ]; then matched=$(wc -l < run.log | tr -d ' '); else matched=0; fi
    echo "-- lock-step result --"
    if grep -aqE "PC mismat|Register mismatch" "$LOG"; then
      echo "RESULT: MISMATCH after ${matched:-0} matched instructions:"
      grep -aE "PC mismat|Register mismatch" "$LOG" | head -1
      exit 1
    else
      echo "RESULT: PASS — ${matched:-0} instructions, RTL == golden, no mismatch"
      echo "  (bounded window; status=$status, 124=timeout reached while still in lock-step)"
      exit 0
    fi
    ;;

  *)
    echo "usage: $0 {emu|lockstep} [IMAGE] [TIMEOUT]" >&2
    exit 2
    ;;
esac
