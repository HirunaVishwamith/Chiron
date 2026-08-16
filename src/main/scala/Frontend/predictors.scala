package Frontend

import chisel3._
import chisel3.util._
import common.configuration

/**
  * Next-PC prediction for the fetch unit.
  *
  * SAFETY CONTRACT
  * ---------------
  * Everything in this file only chooses *which address fetch asks the I-cache
  * for next*. It never touches branch resolution (`branchEvals` in core.scala)
  * nor the redirect/squash machinery. A wrong prediction is therefore always
  * corrected by the existing redirect path: it costs cycles, never correctness.
  * Keep it that way — do not add anything here that feeds the commit path.
  *
  * Structures
  * ----------
  *  - CFI table   : PC-indexed control-flow-instruction classifier, filled by
  *                  pre-decoding instructions as fetch hands them to decode.
  *                  This is what lets the predictor tell a conditional branch
  *                  from a call from a return *at fetch time*, when all it has
  *                  is a PC.
  *  - BTB         : PC-indexed taken-target cache, filled at branch resolution.
  *  - TAGE        : tagged geometric-history direction predictor for
  *                  conditional branches, with a bimodal base predictor.
  *                  With `enableTageOverride`, same-cycle next-PC uses only
  *                  the bimodal bit; TAGE is computed the same cycle but only
  *                  registered, and overrides fetch the next cycle on disagreement.
  *  - RAS         : return-address stack for JALR returns, with a per-fetch
  *                  checkpoint so a redirect can roll the stack pointer back.
  */

/** Classes of control-flow instruction the frontend cares about. */
object cfiType {
  val NONE     = 0 // not a control transfer (or not yet pre-decoded)
  val COND     = 1 // conditional branch  — direction from TAGE
  val JUMP     = 2 // JAL, rd not a link register — unconditional, BTB target
  val CALL     = 3 // JAL/JALR writing a link register — push return address
  val RET      = 4 // JALR reading a link register — pop the RAS
  val INDIRECT = 5 // JALR, neither operand a link register — BTB target
  val RETCALL  = 6 // JALR link->link, rd != rs1 — pop then push (coroutine)
  val width    = 3

  /**
    * RISC-V return-address-stack hints, per the unprivileged spec's JALR table.
    * `rd`/`rs1` are "link" when they name x1 or x5.
    */
  def classify(instruction: UInt): UInt = {
    val opcode  = instruction(6, 0)
    val rd      = instruction(11, 7)
    val rs1     = instruction(19, 15)
    val rdLink  = (rd === 1.U) || (rd === 5.U)
    val rs1Link = (rs1 === 1.U) || (rs1 === 5.U)

    val isCond = opcode === "b1100011".U
    val isJal  = opcode === "b1101111".U
    val isJalr = opcode === "b1100111".U

    MuxCase(NONE.U(width.W), Seq(
      isCond -> COND.U(width.W),
      isJal  -> Mux(rdLink, CALL.U(width.W), JUMP.U(width.W)),
      isJalr -> MuxCase(INDIRECT.U(width.W), Seq(
        (rdLink && rs1Link && (rd =/= rs1)) -> RETCALL.U(width.W),
        rdLink                              -> CALL.U(width.W),
        rs1Link                             -> RET.U(width.W)
      ))
    ))
  }

  /** Unconditional transfers are always taken; COND asks the direction predictor. */
  def alwaysTaken(t: UInt): Bool =
    (t === JUMP.U) || (t === CALL.U) || (t === RET.U) ||
    (t === INDIRECT.U) || (t === RETCALL.U)
}

/** Pre-decode observation: an instruction fetch is handing to decode. */
class predecodeInfo extends Bundle {
  val valid       = Bool()
  val pc          = UInt(64.W)
  val instruction = UInt(32.W)
}

/** Roll the RAS pointer back to the value checkpointed when `pc` was fetched. */
class rasRestorePort extends Bundle {
  val valid = Bool()
  val state = UInt(configuration.frontend.rasCheckpointWidth.W)
}

/** State captured on a direction-predicted fetch so TAGE can override next cycle. */
class tageOverrideSnap extends Bundle {
  val valid     = Bool()
  val pc        = UInt(64.W)
  val wasCond   = Bool()
  val fastTaken = Bool()
  val tageTaken = Bool()
  val btbHit    = Bool()
  val btbTarget = UInt(64.W)
  val ghrUsed   = UInt(configuration.frontend.ghrLength.W)
}

/**
  * Common interface every next-PC predictor presents to `fetch`, so the two
  * implementations can be A/B'd from one build without rewiring fetch.
  */
abstract class nextPCPredictor extends Module {
  val io = IO(new Bundle {
    val branchres = new branchResToFetch
    val curr_pc   = Input(UInt(64.W))
    val next_pc   = Output(UInt(64.W))
  })
  /** High on the cycle fetch actually issues a request for `curr_pc`. */
  val requestSent  = IO(Input(Bool()))
  /** High on the cycle fetch discovers it fetched down the wrong path. */
  val mispredicted = IO(Input(Bool()))
  /**
    * Any fetch redirect, whether a branch mispredict or a coherency squash.
    * A superset of `mispredicted`: both flush the pipeline, so both invalidate
    * the speculative global history.
    */
  val pipelineFlush = IO(Input(Bool()))
  val predecode     = IO(Input(new predecodeInfo))
  val rasCheckpoint = IO(Output(UInt(configuration.frontend.rasCheckpointWidth.W)))
  val rasRestore    = IO(Input(new rasRestorePort))
  /**
    * Delayed TAGE override. High the cycle after a direction-predicted fetch
    * if TAGE disagrees with the same-cycle bimodal steer. `next_pc` is always
    * valid the same cycle — fetch must not wait on these.
    */
  val overrideValid = IO(Output(Bool()))
  val overridePC    = IO(Output(UInt(64.W)))
}

/**
  * The original BTB + gshare, wrapped in the common interface. Behaviourally
  * identical to the pre-TAGE frontend; the pre-decode and RAS ports are tied
  * off. Selected by `configuration.frontend.enableAdvancedPredictor = false`.
  */
class legacyPredictor(counterDepth: Int, btbSize: Int) extends nextPCPredictor {
  val inner = Module(new gshare_predictor(counterDepth, btbSize))
  inner.io.branchres <> io.branchres
  inner.io.curr_pc  := io.curr_pc
  io.next_pc        := inner.io.next_pc
  inner.requestSent  := requestSent
  inner.mispredicted := mispredicted
  rasCheckpoint := 0.U
  overrideValid := false.B
  overridePC    := 0.U
  // predecode / rasRestore / override intentionally unused
}

/**
  * BTB + CFI classifier + TAGE + RAS.
  *
  * History bookkeeping. Two global history registers are kept:
  *
  *   ghrSpec   — updated at *fetch* with the predicted direction
  *   ghrCommit — updated at *resolution* with the actual direction
  *
  * TAGE looks up with ghrSpec and trains with ghrCommit, and those two agree
  * for any branch that actually reaches resolution: a branch only resolves if
  * every older branch was predicted correctly (otherwise it was squashed), so
  * the predicted history it was fetched with *is* the committed history by the
  * time it resolves. On a mispredict ghrSpec is reloaded from ghrCommit, which
  * re-establishes the invariant. No per-branch history queue is needed.
  *
  * Same-cycle next-PC is bimodal+BTB+RAS so the TAGE mux is not on the fetch
  * request path. TAGE is still evaluated from `curr_pc`/`ghrSpec` that cycle
  * (so it sees the same tables as a combinational TAGE would) but the result
  * is only registered; if it disagrees and the BTB can realize the other
  * direction, fetch is redirected before the wrong successor is requested
  * (one bubble). A TAGE override repairs only the last ghrSpec bit — it must
  * not use pipelineFlush, which would smash speculative history back to commit.
  *
  * Both history registers are advanced on exactly one predicate — "the CFI
  * table says this PC is a conditional branch" — so the two stay in step.
  */
class advancedPredictor extends nextPCPredictor {
  private val cfg = configuration.frontend

  io.branchres.ready := 1.B

  // ─────────────────── packed bit-vector predictor state ──────────────────
  // The valid/useful arrays below are read AND written at a dynamic index, so
  // `Reg(Vec(N, Bool()))` is the wrong container for them: FIRRTL's LowerTypes
  // pass explodes such a Vec into N individual registers, and each dynamic read
  // then elaborates as an N-deep mux tree that Verilator re-evaluates every
  // cycle. Measured on the quad-core Linux boot, the four arrays here (4864
  // bits) accounted for ~42k of system.v's 269k lines and halved host
  // throughput -- 29k -> 16k cycles/s -- without changing behaviour by a single
  // cycle. Held as packed UInts each array is ONE register: a read is a dynamic
  // bit-extract, a write a one-hot mask.
  //
  // Read sites are unchanged (`vec(idx)` on a UInt is a bit-extract). Every
  // write site targets exactly one index of one array per cycle, so the
  // read-modify-write form below preserves last-connect semantics: a later
  // `when` still overrides an earlier one for the same bit, and the bulk clear
  // at the end of the update path still wins over all of them.
  private def bitClear(v: UInt, oh: UInt): UInt = v & (~oh).asUInt
  private def bitPut(v: UInt, oh: UInt, set: Bool): UInt =
    Mux(set, v | oh, bitClear(v, oh))

  // ───────────────────────── CFI classifier table ─────────────────────────
  // Direct-mapped, tagged, written from pre-decode. Only control transfers are
  // allocated, so plain arithmetic does not evict branch entries.
  private val cfiIdxW = log2Ceil(cfg.cfiEntries)
  private def cfiIdx(pc: UInt) = pc(cfiIdxW + 1, 2)
  private def cfiTag(pc: UInt) = pc(63, cfiIdxW + 2)

  val cfiValid = RegInit(0.U(cfg.cfiEntries.W))
  val cfiTags  = Mem(cfg.cfiEntries, UInt((62 - cfiIdxW).W))
  val cfiTypes = Mem(cfg.cfiEntries, UInt(cfiType.width.W))

  private val pdType = cfiType.classify(predecode.instruction)
  when(predecode.valid && (pdType =/= cfiType.NONE.U)) {
    cfiValid := cfiValid | UIntToOH(cfiIdx(predecode.pc), cfg.cfiEntries)
    cfiTags(cfiIdx(predecode.pc))  := cfiTag(predecode.pc)
    cfiTypes(cfiIdx(predecode.pc)) := pdType
  }

  private def lookupType(pc: UInt): UInt = {
    val hit = cfiValid(cfiIdx(pc)) && (cfiTags(cfiIdx(pc)) === cfiTag(pc))
    Mux(hit, cfiTypes(cfiIdx(pc)), cfiType.NONE.U(cfiType.width.W))
  }

  /** Type of the PC being predicted, and of the branch being resolved. */
  val typeP = lookupType(io.curr_pc)
  // Prefer the resolved instruction over the CFI table so a cold miss cannot
  // train TAGE/BTB as "not a branch". Predecode still fills the table so a
  // loop body that is fetched again before the first resolve still hits.
  val typeR = Mux(io.branchres.fired,
    cfiType.classify(io.branchres.instruction),
    lookupType(io.branchres.pc))

  // ───────────────────────────────── BTB ──────────────────────────────────
  private val btbIdxW = log2Ceil(cfg.btbEntries)
  private def btbIdx(pc: UInt) = pc(btbIdxW + 1, 2)
  private def btbTag(pc: UInt) = pc(63, btbIdxW + 2)

  val btb      = Mem(cfg.btbEntries, UInt(64.W))
  val btbValid = RegInit(0.U(cfg.btbEntries.W))
  val btbTags  = Mem(cfg.btbEntries, UInt((62 - btbIdxW).W))

  // `branchres.branchTaken` is only meaningful for conditional branches — the
  // condition evaluator in core.scala indexes on funct3, which is immediate
  // bits for JAL and always-zero for JALR. Use the CFI class instead, so an
  // unconditional transfer trains as taken rather than as a coin flip.
  val takenR = Mux(typeR === cfiType.COND.U, io.branchres.branchTaken,
                   Mux(typeR === cfiType.NONE.U, io.branchres.branchTaken, true.B))

  // Only taken outcomes define a BTB target. Writing pc+4 for a not-taken
  // branch (as the original did) makes the BTB actively wrong the moment the
  // direction predictor says "taken".
  when(io.branchres.fired && takenR) {
    btbValid := btbValid | UIntToOH(btbIdx(io.branchres.pc), cfg.btbEntries)
    btbTags(btbIdx(io.branchres.pc))  := btbTag(io.branchres.pc)
    btb(btbIdx(io.branchres.pc))      := io.branchres.pcAfterBrnach
  }

  val btbHitP    = btbValid(btbIdx(io.curr_pc)) && (btbTags(btbIdx(io.curr_pc)) === btbTag(io.curr_pc))
  val btbTargetP = btb(btbIdx(io.curr_pc))

  // ─────────────────────────── global history ────────────────────────────
  private val histLen = cfg.ghrLength
  val ghrSpec   = RegInit(0.U(histLen.W))
  val ghrCommit = RegInit(0.U(histLen.W))

  val condResolved = io.branchres.fired && (typeR === cfiType.COND.U)
  val ghrCommitNext = Mux(condResolved,
    Cat(ghrCommit(histLen - 2, 0), io.branchres.branchTaken), ghrCommit)
  ghrCommit := ghrCommitNext

  // ──────────────────── TAGE: tables and index functions ──────────────────
  private val nTables = cfg.tageHistoryLengths.length
  private val selW    = log2Ceil(nTables)
  private val tblIdxW = log2Ceil(cfg.tageTableEntries)
  private val tagW    = cfg.tageTagWidth

  /** XOR-fold the low `len` bits of `hist` down to `outW` bits. */
  private def fold(hist: UInt, len: Int, outW: Int): UInt = {
    val bits = hist(len - 1, 0)
    val nChunks = (len + outW - 1) / outW
    (0 until nChunks).map { i =>
      val lo = i * outW
      val hi = math.min(len - 1, lo + outW - 1)
      bits(hi, lo).pad(outW)
    }.reduce(_ ^ _)
  }

  private def tageIdx(pc: UInt, hist: UInt, t: Int): UInt =
    pc(tblIdxW + 1, 2) ^ fold(hist, cfg.tageHistoryLengths(t), tblIdxW)

  // Tag draws on PC bits above the index so aliasing in one does not imply
  // aliasing in the other.
  private def tageTag(pc: UInt, hist: UInt, t: Int): UInt =
    pc(tagW + tblIdxW + 1, tblIdxW + 2) ^ fold(hist, cfg.tageHistoryLengths(t), tagW)

  val tageTags  = Seq.fill(nTables)(Mem(cfg.tageTableEntries, UInt(tagW.W)))
  val tageCtrs  = Seq.fill(nTables)(Mem(cfg.tageTableEntries, UInt(3.W)))
  val tageValid = Seq.fill(nTables)(RegInit(0.U(cfg.tageTableEntries.W)))
  val tageU     = Seq.fill(nTables)(RegInit(0.U(cfg.tageTableEntries.W)))

  // Bimodal base predictor.
  private val baseIdxW = log2Ceil(cfg.bimodalEntries)
  val bimodal = Mem(cfg.bimodalEntries, UInt(2.W))
  private def baseIdx(pc: UInt) = pc(baseIdxW + 1, 2)

  /** Original-TAGE "use alternate prediction on newly-allocated" arbiter. */
  val useAltOnNa = RegInit(8.U(4.W))

  case class TageSel(
    provider: UInt, providerValid: Bool, provPred: Bool, altPred: Bool,
    provWeak: Bool, provNew: Bool, finalPred: Bool)

  private def select(hits: Seq[Bool], ctrs: Seq[UInt], us: Seq[Bool], basePred: Bool): TageSel = {
    // Highest-numbered hitting table wins — that is the longest history match.
    val provider = hits.zipWithIndex.foldLeft(0.U(selW.W)) {
      case (acc, (h, i)) => Mux(h, i.U(selW.W), acc)
    }
    val providerValid = hits.reduce(_ || _)
    val below   = (0 until nTables).map(i => hits(i) && (i.U(selW.W) < provider))
    val altHit  = below.reduce(_ || _)
    val altSel  = below.zipWithIndex.foldLeft(0.U(selW.W)) {
      case (acc, (h, i)) => Mux(h, i.U(selW.W), acc)
    }
    val ctrVec  = VecInit(ctrs)
    val uVec    = VecInit(us)
    val provCtr = ctrVec(provider)
    val provPred = provCtr(2)
    val altPred  = Mux(altHit, ctrVec(altSel)(2), basePred)
    val provWeak = (provCtr === 3.U) || (provCtr === 4.U)
    val provNew  = !uVec(provider)
    val finalPred = Mux(providerValid,
      Mux(provWeak && provNew && useAltOnNa(3), altPred, provPred), basePred)
    TageSel(provider, providerValid, provPred, altPred, provWeak, provNew, finalPred)
  }

  // ───────────────────────── TAGE: prediction path ────────────────────────
  // TAGE is computed from curr_pc/ghrSpec every cycle so it matches the
  // combinational predictor. With override it only feeds a register — next_pc
  // uses the bimodal bit so the TAGE mux is not on the I$ request path.
  private val baseP = bimodal(baseIdx(io.curr_pc))(1)
  private val idxP  = (0 until nTables).map(t => tageIdx(io.curr_pc, ghrSpec, t))
  private val tagsP = (0 until nTables).map(t => tageTag(io.curr_pc, ghrSpec, t))
  private val hitP  = (0 until nTables).map(t =>
    tageValid(t)(idxP(t)) && (tageTags(t)(idxP(t)) === tagsP(t)))
  private val ctrsP = (0 until nTables).map(t => tageCtrs(t)(idxP(t)))
  private val usP   = (0 until nTables).map(t => tageU(t)(idxP(t)))
  private val selP  = select(hitP, ctrsP, usP, baseP)
  val tageDirTaken  = if (cfg.enableTAGE) selP.finalPred else baseP
  val dirTaken      = if (cfg.enableTAGE && !cfg.enableTageOverride) tageDirTaken else baseP

  // ───────────────────────── RAS (return-address stack) ───────────────────
  private val spW  = cfg.rasSpWidth
  private val cntW = cfg.rasCntWidth

  val rasMem = Mem(cfg.rasDepth, UInt(64.W))
  val rasSp  = RegInit(0.U(spW.W))   // index of the next free slot; top is sp-1
  val rasCnt = RegInit(0.U(cntW.W))  // valid entries, saturating at rasDepth

  val rasTop   = rasMem(rasSp - 1.U)
  val rasEmpty = rasCnt === 0.U

  private val doPop  = requestSent && ((typeP === cfiType.RET.U) || (typeP === cfiType.RETCALL.U))
  private val doPush = requestSent && ((typeP === cfiType.CALL.U) || (typeP === cfiType.RETCALL.U))

  private val spAfterPop  = Mux(doPop && !rasEmpty, rasSp - 1.U, rasSp)
  private val cntAfterPop = Mux(doPop && !rasEmpty, rasCnt - 1.U, rasCnt)
  private val nextSp  = Mux(doPush, spAfterPop + 1.U, spAfterPop)
  private val nextCnt = Mux(doPush,
    Mux(cntAfterPop === cfg.rasDepth.U, cntAfterPop, cntAfterPop + 1.U), cntAfterPop)

  when(doPush) { rasMem(spAfterPop) := io.curr_pc + 4.U }
  rasSp  := nextSp
  rasCnt := nextCnt

  // Checkpoint reflects the state *after* this PC's own push/pop, so restoring
  // it on a redirect keeps the effect of the mispredicting instruction and
  // discards only what wrong-path fetches did behind it. Stack *contents* may
  // still have been clobbered by wrong-path pushes — that costs accuracy, not
  // correctness.
  rasCheckpoint := Cat(nextCnt, nextSp)
  when(rasRestore.valid) {
    rasSp  := rasRestore.state(spW - 1, 0)
    rasCnt := rasRestore.state(spW + cntW - 1, spW)
  }

  // ──────────────────────────── next-PC selection ─────────────────────────
  // Unconditional transfers are taken by definition; COND (and not-yet-classified
  // PCs, which fall back to the old behaviour) ask the direction predictor.
  val takenP = Mux(cfiType.alwaysTaken(typeP), true.B, dirTaken)
  val useRAS = cfg.enableRAS.B && !rasEmpty &&
               ((typeP === cfiType.RET.U) || (typeP === cfiType.RETCALL.U))

  // A "taken" direction the BTB has no target for cannot actually be followed —
  // fetch falls through to pc+4. The history must record what the frontend *did*,
  // not what the direction predictor wished for, or a BTB miss on a not-taken
  // branch desynchronises ghrSpec from ghrCommit without any redirect to repair
  // it. With this definition the two agree whenever no redirect occurred, and a
  // redirect reloads ghrSpec anyway.
  val fetchedTaken = btbHitP && takenP

  io.next_pc := Mux(useRAS, rasTop,
                Mux(fetchedTaken, btbTargetP, io.curr_pc + 4.U))

  // ──────────────────── TAGE override (next cycle, disagreement only) ─────
  val ov = RegInit(0.U.asTypeOf(new tageOverrideSnap))
  private val doOverride = WireDefault(false.B)

  if (cfg.enableTAGE && cfg.enableTageOverride) {
    // Capture the same-cycle TAGE result, not a re-lookup: intervening
    // table updates must not change the decision fetch already "would have"
    // made. JUMP/CALL/RET stay same-cycle via BTB/RAS.
    when(pipelineFlush) {
      ov.valid := false.B
    }.elsewhen(requestSent && (typeP === cfiType.COND.U)) {
      // Only classified conditionals. A NONE+BTB alias must not let TAGE
      // redirect sequential fetch; that desynchronises ghrSpec (NONE does
      // not shift history) from the path we actually follow.
      ov.valid     := true.B
      ov.pc        := io.curr_pc
      ov.wasCond   := true.B
      ov.fastTaken := dirTaken
      ov.tageTaken := tageDirTaken
      ov.btbHit    := btbHitP
      ov.btbTarget := btbTargetP
      ov.ghrUsed   := ghrSpec
    }.otherwise {
      ov.valid := false.B
    }

    // No BTB target means both predictors fall through to pc+4, so a
    // direction mismatch cannot be realized and must not redirect.
    doOverride := ov.valid && ov.btbHit && (ov.tageTaken =/= ov.fastTaken)
  }

  overrideValid := doOverride && !pipelineFlush
  overridePC    := Mux(ov.tageTaken, ov.btbTarget, ov.pc + 4.U)

  // Speculative history advances on exactly the predicate the commit side uses.
  // A TAGE override repairs only the bit written from bimodal; it must not
  // reload ghrSpec from ghrCommit (that is pipelineFlush's job).
  when(pipelineFlush) {
    ghrSpec := ghrCommitNext
  }.elsewhen(doOverride && ov.wasCond) {
    ghrSpec := Cat(ov.ghrUsed(histLen - 2, 0), ov.btbHit && ov.tageTaken)
  }.elsewhen(requestSent && (typeP === cfiType.COND.U)) {
    ghrSpec := Cat(ghrSpec(histLen - 2, 0), fetchedTaken)
  }

  // ─────────────────────────── TAGE: update path ──────────────────────────
  // Indices are recomputed from the committed history, which equals the history
  // this branch was predicted with (see the class comment).
  private val idxU  = (0 until nTables).map(t => tageIdx(io.branchres.pc, ghrCommit, t))
  // One decoder per table, shared by every write site below.
  private val ohU   = (0 until nTables).map(t => UIntToOH(idxU(t), cfg.tageTableEntries))
  private val tagsU = (0 until nTables).map(t => tageTag(io.branchres.pc, ghrCommit, t))
  private val hitU  = (0 until nTables).map(t => tageValid(t)(idxU(t)) && (tageTags(t)(idxU(t)) === tagsU(t)))
  private val ctrsU = (0 until nTables).map(t => tageCtrs(t)(idxU(t)))
  private val usU   = (0 until nTables).map(t => tageU(t)(idxU(t)))
  private val baseU = bimodal(baseIdx(io.branchres.pc))

  private val selU     = select(hitU, ctrsU, usU, baseU(1))
  val doUpdate = condResolved && cfg.enableTAGE.B
  val takenU   = io.branchres.branchTaken
  val mispU    = selU.finalPred =/= takenU

  private def satUp3(c: UInt)   = Mux(c === 7.U, c, c + 1.U)
  private def satDown3(c: UInt) = Mux(c === 0.U, c, c - 1.U)

  // Bimodal always trains: it is both the fallback and the alt-prediction floor,
  // and with enableTAGE = false it is the whole direction predictor, so it is
  // gated on the resolution itself rather than on the TAGE update.
  when(condResolved) {
    bimodal(baseIdx(io.branchres.pc)) :=
      Mux(takenU, Mux(baseU === 3.U, baseU, baseU + 1.U),
                  Mux(baseU === 0.U, baseU, baseU - 1.U))
  }

  for (t <- 0 until nTables) {
    when(doUpdate && selU.providerValid && (selU.provider === t.U)) {
      val c = ctrsU(t)
      tageCtrs(t)(idxU(t)) := Mux(takenU, satUp3(c), satDown3(c))
      // Usefulness only carries information when provider and alt disagree.
      when(selU.provPred =/= selU.altPred) {
        tageU(t) := bitPut(tageU(t), ohU(t), selU.provPred === takenU)
      }
    }
  }

  when(doUpdate && selU.providerValid && selU.provWeak && selU.provNew &&
       (selU.altPred =/= selU.provPred)) {
    when(selU.altPred === takenU) {
      useAltOnNa := Mux(useAltOnNa === 15.U, useAltOnNa, useAltOnNa + 1.U)
    }.otherwise {
      useAltOnNa := Mux(useAltOnNa === 0.U, useAltOnNa, useAltOnNa - 1.U)
    }
  }

  // Allocation: on a mispredict, claim an entry in a table with a longer
  // history than the provider, preferring one whose usefulness bit is clear.
  private val tblSelW  = selW + 1 // must hold nTables, i.e. "past the last table"
  private val startTbl = Mux(selU.providerValid, selU.provider +& 1.U, 0.U(tblSelW.W))
  private val cand     = (0 until nTables).map(t => (t.U(tblSelW.W) >= startTbl) && !usU(t))
  private val anyCand  = cand.reduce(_ || _)
  private val allocSel = PriorityEncoder(cand)

  when(doUpdate && mispU && (startTbl < nTables.U)) {
    when(anyCand) {
      for (t <- 0 until nTables) {
        when(allocSel === t.U) {
          tageValid(t)          := tageValid(t) | ohU(t)
          tageTags(t)(idxU(t))  := tagsU(t)
          tageCtrs(t)(idxU(t))  := Mux(takenU, 4.U, 3.U) // weakest of the right sign
          tageU(t)              := bitClear(tageU(t), ohU(t))
        }
      }
    }.otherwise {
      // Nothing free: age the candidates so the next mispredict can allocate.
      for (t <- 0 until nTables) {
        when(t.U(tblSelW.W) >= startTbl) { tageU(t) := bitClear(tageU(t), ohU(t)) }
      }
    }
  }

  // Periodic usefulness reset, so entries that were useful long ago cannot
  // lock out allocation forever.
  val ageCounter = RegInit(0.U(cfg.tageAgeCounterWidth.W))
  when(doUpdate) {
    ageCounter := ageCounter + 1.U
    when(ageCounter.andR) { tageU.foreach(_ := 0.U) }
  }
}
