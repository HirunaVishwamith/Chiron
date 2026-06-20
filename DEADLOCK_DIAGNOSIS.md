# csaxpy s2–s5 deadlock — diagnosis & handoff notes

## TL;DR
- **Symptom:** `csaxpy-s1` passes; `csaxpy-s2 … s5` hang with the per-instruction
  `STEP_TIMEOUT` ("TIMEOUT IN SIMULATOR!!!" / "Test failed: Time-out!").
- **It is NOT a regression** from the TAGE / IPC / restructuring work. The exact
  same hang reproduces on commit **`47d67d4`** (the believed-good commit) with the
  committed binary. Only `csaxpy-s1` is in the `REGRESSION` list (`mk/benchmarks.mk:31`),
  so s2–s5 were simply never exercised by regression — that's why it looked new.
- **It is working-set dependent**, not data-corruption: freshly regenerated
  s3/s4/s5 datasets (different random data) also hang. Only small working sets
  (s1, and a fresh small s2) finish before the deadlock window.

## Verified failure chain (root → symptom)
Captured by per-stage Chisel `printf` probes on the committed `csaxpy-s2`
(hangs deterministically at tick **40962**):

1. At ~40k cycles the **front-end redirects fetch to `0x80000000`** (the reset
   vector / `instructionBase`). `decode.expectedPC == 0x80000000`
   (`decode/decode.scala`; `expectedPC` reset value is `initialPC+4 = 0x80000000`).
   Mid-program this is almost certainly a *spurious* redirect (you never jump to
   program start inside the csaxpy loop). This is the **trigger**.
2. The I-cache **cold-misses** on `0x80000000` and issues a single read:
   `iCache → iPort(ACE) → Interconnect → CCU → L2(LLC) → mainMemory`.
3. That **one read never returns**. Downstream everything stalls:
   - CCU read-return FSM `stateReg_8` is stuck in **SYNC (state 2)**
     (`interconnect/ccu.scala`) waiting on the read data.
     `crpbuf_3_0/1 == 0` (clean snoop, no CD expected), `select_buff == L2`,
     so it is purely waiting for the L2 read.
   - The L2 has **one** read in flight (`arSentTot == 1`, `rRcvTot == 0`) and is
     otherwise idle; the read is a cold miss → `MSHR → mainMemory`.
4. Front-end starves → ROB drains empty (`fifoV=0`) → no commit for
   `STEP_TIMEOUT` cycles → timeout.

So a *single* cold instruction read of `0x80000000` wedges, and because the
front end can make no progress without it, the whole core deadlocks.

## Ruled OUT (don't re-investigate these)
- **Timer interrupt (MTIP):** interrupt-inject FSM is idle (`waitForMTIP`),
  front end not blocked by it.
- **Coherency snoop / CD path:** `crpbuf_3_0 == crpbuf_3_1 == 0` at the hang —
  both snoops were clean misses, no CD beat is owed. The deadlock does **not**
  involve the D-cache snoop.
- **L2 `replace_full` / writeback saturation:** `replace_count == 0`,
  `replace_full == 0`.
- **L2 front-Rob input buffer ready/latch (`l2_Rob.scala`):** tried 6 variants
  (`ARREADY := readyInputBuffer && !AWVALID`, `&& !stall`, combinational on
  `inputBufferState===empty`, gating the AR latch, etc.) — **every one produced
  byte-identical behaviour (hang at the same tick 40962)**, so the AR is not
  dropped here.
- **`mainMemory.programmed`:** a red herring. `programmed==1` throughout
  execution (read accepts climb steadily); the one `programmed==0` sample was the
  *kernel-load phase*, not the execution hang. During execution mainMemory serves
  reads fine; a `programmed`-gated stuck-probe did **not** fire.
- **Benchmark NUM_CORES:** `crt.S` is `NUM_CORES 1`; binaries pass single-core
  init. The Interconnect/CCU is single-core: `acePort0 = D$`, `acePort1 = I$`
  (the CCU's "core1" snoop port is the I-cache, not a second core).

## Where the remaining bug must be (not yet isolated to one line)
The single cold read of `0x80000000` does not complete somewhere in:
`CCU stateReg_8` ↔ `L2 MSHR (MSHR.scala)` ↔ `mainMemory` read-burst handshake.
mainMemory serves *other* reads during the run, so it is specific to this
transaction / this point in the run (lots of preceding store/writeback traffic).

Two concrete leads, in priority order:
1. **The spurious redirect to `0x80000000`** (the trigger). Find what sets
   `decode.expectedPC` to the reset value mid-run — candidates:
   `core.scala` `branchEvals.nextPC := Mux(coherentLoadInvalid, rob.commit.pc,
   nextCorrectPC)` and the trap path (`mtvec`/`mepc`). If this redirect is
   eliminated, the cold read never happens and the hang is avoided. Likely the
   most localized / lowest-risk fix.
2. **The L2 read-completion path for a cold miss.** Trace one read with an
   in-flight tracker in `non_block_l2_cache/l2.scala`:
   ```
   readInFlight set on (io.cache_axi.ARVALID && io.cache_axi.ARREADY)
   cleared on (RVALID && RREADY && RLAST)
   ```
   then dump `MSHR.io.axi` (ARVALID/ARREADY/RVALID/RREADY/RLAST), the
   `mem_read_axi` ↔ mainMemory burst, and whether `cache_hit_out`/`Rob_in.fired`
   ever fire for that address. The mainMemory read/write **arbiter**
   (`mainMemory.scala` ~line 149, `reading`/`writing` states — `writing` forces
   `ARREADY := false`) and the MSHR `RREADY := (state===rdata)` beat accounting
   are the spots to scrutinize for a read-after-write or burst-length mismatch.

## How to reproduce / drive the investigation
- Stable images (committed-data reproducers) were copied to
  `/tmp/csaxpy-s{1,2,3,5}-stable.bin`. Use a **fixed path** image, not `bins/…`:
  the reorg made `bins/*.bin` tracked, so a `git checkout` of an older commit
  **deletes** the untracked image and lockstep then runs on empty memory and
  exits 0 (false pass — this corrupted an earlier git-bisect).
- Run: `make sim && make build/lockstep.out && \
  ./build/lockstep.out --image /tmp/csaxpy-s2-stable.bin $(csaxpy_DONE) --logdir /tmp`
  where `csaxpy_DONE = --done-pc 0x800009a4 --done-pc 0x80000998 --done-a0 0`.
- **Build gotcha:** `make sim` returns 0 even when `sbt` fails (it reuses a stale
  `system.v`). Always `rm -f system.v` first and verify a probe string with
  `grep … system.v` before trusting a run.
- All debug probes used to produce this diagnosis have been reverted; the tree is
  back to the committed state.

## To rebuild csaxpy datasets (single-core, current source)
Sizes by scale: s1=1000, s2=2000, s3=5000, s4=7000, s5=10000 (data is ~13 B/elem).
`workloads/benchmarks/mt-csaxpy/csaxpy_gendata.py` is Python-2; a Python-3 port
that writes `dataset1.h` for a given size was used. Build via
`PATH=/media/hv/D1/OOO_Processor/riscv/bin:$PATH make -C workloads/benchmarks riscv bmarks="mt-csaxpy"`
then `objcopy`'d `mt-csaxpy.bin` → `bins/mt-csaxpy-sN.bin`.
