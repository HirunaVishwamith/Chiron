import chisel3._
import chisel3.util._

/**
 * F3b — TAGE direction predictor + BTB + return-address stack.
 *
 * Drop-in replacement for gshareV2_predictor: identical core IO
 * (io.branchres / io.curr_pc / io.next_pc + requestSent + mispredicted) plus
 * one new `predecode` input that fetch.scala drives from the instruction
 * buffer head. Correctness is owned by the misprediction-recovery path; this
 * unit only sets accuracy.
 *
 * Design constraints from this pipeline:
 *  - Prediction is PC/history-indexed only (no instruction bytes at next-PC
 *    time), so returns are recognised by a learned return-PC tag table.
 *  - branchResToFetch carries no prediction metadata, so the provider table
 *    is recomputed at train time from the committed history instead of being
 *    carried with the branch. ghrSpec/ghrCommit drift between mispredicts is
 *    the accepted cost; every mispredict resynchronises them.
 *
 * TAGE: bimodal base + four tagged tables with geometric history lengths.
 * Provider = longest-history tag hit. On a mispredict, allocate in the
 * shortest longer table whose u-bit is clear, else age (clear) those u-bits.
 * u is set/cleared when the provider disagrees with the alternate prediction.
 *
 * RAS: predecode pushes pc+4 on calls (jal/jalr, rd ∈ {x1,x5}) and tags
 * return PCs (jalr, rd=x0, rs1 ∈ {x1,x5}). A predict-time hit in the return
 * table overrides the BTB with the stack top and pops. Wrong-path pushes and
 * pops are not repaired (simple RAS); the stack rebalances naturally.
 */
class tage_predictor(
  val btbSize:      Int = 512,
  val baseDepth:    Int = 4096,
  val tableDepth:   Int = 1024,
  val tagW:         Int = 9,
  val histLens:     Seq[Int] = Seq(5, 13, 32, 80),
  val rasDepth:     Int = 16,
  val retTableSize: Int = 256
) extends Module {
  require(isPow2(btbSize) && isPow2(baseDepth) && isPow2(tableDepth) &&
          isPow2(rasDepth) && isPow2(retTableSize))

  val io = IO(new Bundle {
    val branchres = new branchResToFetch
    val curr_pc   = Input(UInt(64.W))
    val next_pc   = Output(UInt(64.W))
  })
  val requestSent  = IO(Input(Bool()))
  val mispredicted = IO(Input(Bool()))
  // Driven from the fetch instruction-buffer head (pre-decode time).
  val predecode = IO(Input(new Bundle {
    val fired       = Bool()
    val pc          = UInt(64.W)
    val instruction = UInt(32.W)
  }))

  val nTables = histLens.length
  val histLen = histLens.max
  val idxW    = log2Ceil(tableDepth)
  val baseW   = log2Ceil(baseDepth)

  // ---------------- Global history ----------------
  val ghrCommit = RegInit(0.U(histLen.W))
  val ghrSpec   = RegInit(0.U(histLen.W))

  // XOR-fold the low `len` bits of h down to `w` bits.
  def fold(h: UInt, len: Int, w: Int): UInt = {
    val chunks = (len + w - 1) / w
    (0 until chunks).map { i =>
      h(math.min((i + 1) * w, len) - 1, i * w).pad(w)
    }.reduce(_ ^ _)
  }
  def tIdx(pc: UInt, h: UInt, len: Int): UInt = pc(idxW + 1, 2) ^ fold(h, len, idxW)
  def tTag(pc: UInt, h: UInt, len: Int): UInt =
    (pc(tagW + 1, 2) ^ fold(h, len, tagW) ^ Cat(fold(h, len, tagW - 1), 0.U(1.W)))(tagW - 1, 0)

  // ---------------- Tables ----------------
  val baseCtr = Mem(baseDepth, UInt(2.W))
  case class TageTable(ctr: Mem[UInt], tag: Mem[UInt], u: Mem[UInt], valid: Vec[Bool])
  val tables = Seq.fill(nTables)(TageTable(
    Mem(tableDepth, UInt(3.W)),
    Mem(tableDepth, UInt(tagW.W)),
    Mem(tableDepth, UInt(1.W)),
    RegInit(VecInit(Seq.fill(tableDepth)(false.B)))
  ))

  // ---------------- BTB (direct-mapped) ----------------
  val btbIdxW   = log2Ceil(btbSize)
  val btbAddr   = io.curr_pc(btbIdxW + 1, 2)
  val btbTag    = io.curr_pc(63, btbIdxW + 2)
  val resAddr   = io.branchres.pc(btbIdxW + 1, 2)
  val resTag    = io.branchres.pc(63, btbIdxW + 2)
  val btb       = Mem(btbSize, UInt(64.W))
  val btbValid  = RegInit(VecInit(Seq.fill(btbSize)(false.B)))
  val btbTags   = Mem(btbSize, UInt((62 - btbIdxW).W))
  val btbHit    = btbValid(btbAddr) && (btbTags(btbAddr) === btbTag)

  // ---------------- Return-address stack ----------------
  val ras    = Reg(Vec(rasDepth, UInt(64.W)))
  val rasPtr = RegInit(0.U(log2Ceil(rasDepth).W))
  val rasTop = ras(rasPtr - 1.U)

  val retIdxW  = log2Ceil(retTableSize)
  val retValid = RegInit(VecInit(Seq.fill(retTableSize)(false.B)))
  val retTags  = Mem(retTableSize, UInt((62 - retIdxW).W))
  val retAddr  = io.curr_pc(retIdxW + 1, 2)
  val retHit   = retValid(retAddr) && (retTags(retAddr) === io.curr_pc(63, retIdxW + 2))

  // Pre-decode call/return detection (jal=1101111, jalr=1100111).
  val pdOp     = predecode.instruction(6, 0)
  val pdRd     = predecode.instruction(11, 7)
  val pdRs1    = predecode.instruction(19, 15)
  val pdIsJalr = pdOp === "b1100111".U
  val pdIsJal  = pdOp === "b1101111".U
  def isLink(r: UInt) = (r === 1.U) || (r === 5.U)
  val pdIsCall   = predecode.fired && (pdIsJal || pdIsJalr) && isLink(pdRd)
  val pdIsReturn = predecode.fired && pdIsJalr && (pdRd === 0.U) && isLink(pdRs1)

  when(pdIsReturn) {
    val a = predecode.pc(retIdxW + 1, 2)
    retValid(a) := true.B
    retTags(a)  := predecode.pc(63, retIdxW + 2)
  }

  val doPop  = requestSent && retHit
  val doPush = pdIsCall
  when(doPush) { ras(Mux(doPop, rasPtr - 1.U, rasPtr)) := predecode.pc + 4.U }
  rasPtr := rasPtr + doPush.asUInt - doPop.asUInt

  // ---------------- Predict ----------------
  val pHits = tables.zip(histLens).map { case (t, len) =>
    val i = tIdx(io.curr_pc, ghrSpec, len)
    (t.valid(i) && (t.tag(i) === tTag(io.curr_pc, ghrSpec, len)), t.ctr(i)(2))
  }
  val basePred = baseCtr(io.curr_pc(baseW + 1, 2))(1)
  // Longest-history hit provides; fall back to the base bimodal.
  val takenPred = pHits.reverse.foldRight(basePred) { case ((hit, dir), alt) => Mux(hit, dir, alt) }

  io.next_pc := MuxCase(io.curr_pc + 4.U, Seq(
    retHit                 -> rasTop,
    (btbHit && takenPred)  -> btb(btbAddr)
  ))

  // Speculative history: shift the predicted direction for PCs known to be
  // control flow (BTB or return-table hit); restore on mispredict.
  when(mispredicted) {
    ghrSpec := ghrCommit
  }.elsewhen(requestSent && (btbHit || retHit)) {
    ghrSpec := Cat(ghrSpec(histLen - 2, 0), (takenPred || retHit).asUInt)
  }

  // ---------------- Train (at branch resolution, committed history) ----------------
  val trainIdx  = tables.zip(histLens).map { case (_, len) => tIdx(io.branchres.pc, ghrCommit, len) }
  val trainTag  = histLens.map(len => tTag(io.branchres.pc, ghrCommit, len))
  val trainHit  = tables.zip(trainIdx).zip(trainTag).map { case ((t, i), g) =>
    t.valid(i) && (t.tag(i) === g)
  }
  val trainBase     = baseCtr(io.branchres.pc(baseW + 1, 2))
  val trainBaseDir  = trainBase(1)
  // provider = longest hit; altpred = next-longest hit below it, else base.
  val provOH   = PriorityEncoderOH(trainHit.reverse).reverse // one-hot, longest hit
  val anyHit   = trainHit.reduce(_ || _)
  val provDir  = tables.zip(trainIdx).zip(provOH).map { case ((t, i), s) =>
    Mux(s, t.ctr(i)(2), false.B)
  }.reduce(_ || _)
  val belowHit = trainHit.zip(provOH).scanRight(false.B) { case ((h, s), seen) => seen || s }.tail
    // belowHit(i) = a provider exists at a LONGER table than i
  // altpred = longest hit strictly below the provider, else the base bimodal.
  val altDir = tables.zip(trainIdx).zip(trainHit.zip(belowHit)).reverse.foldRight(trainBaseDir) {
    case (((t, i), (h, below)), acc) => Mux(h && below, t.ctr(i)(2), acc)
  }
  val predDir  = Mux(anyHit, provDir, trainBaseDir)
  val outcome  = io.branchres.branchTaken

  def bump(ctr: UInt, up: Bool, max: Int): UInt =
    Mux(up, Mux(ctr === max.U, ctr, ctr + 1.U), Mux(ctr === 0.U, ctr, ctr - 1.U))

  when(io.branchres.fired) {
    when(io.branchres.isBranch) {
      // BTB always learns the resolved target.
      btbValid(resAddr) := true.B
      btbTags(resAddr)  := resTag
      btb(resAddr)      := io.branchres.pcAfterBrnach

      // Base bimodal always trains.
      baseCtr(io.branchres.pc(baseW + 1, 2)) := bump(trainBase, outcome, 3)

      // Provider trains; u tracks whether the provider beat the altpred.
      tables.zip(trainIdx).zip(provOH).foreach { case ((t, i), sel) =>
        when(sel) {
          t.ctr(i) := bump(t.ctr(i), outcome, 7)
          when(provDir =/= altDir) { t.u(i) := (provDir === outcome).asUInt }
        }
      }

      // On a direction mispredict, allocate in the shortest table longer
      // than the provider with u==0; if none, age those u-bits instead.
      when(predDir =/= outcome) {
        val longer = trainHit.zip(provOH).scanLeft(!anyHit) { case (seen, (_, s)) => seen || s }
          // longer(i) = table i is strictly longer than the provider (or no provider)
        val canAlloc = tables.zip(trainIdx).zipWithIndex.map { case ((t, i), k) =>
          longer(k) && (t.u(i) === 0.U)
        }
        when(canAlloc.reduce(_ || _)) {
          val allocOH = PriorityEncoderOH(canAlloc) // shortest eligible
          tables.zip(trainIdx).zip(trainTag).zip(allocOH).foreach { case (((t, i), g), sel) =>
            when(sel) {
              t.valid(i) := true.B
              t.tag(i)   := g
              t.ctr(i)   := Mux(outcome, 4.U, 3.U)
              t.u(i)     := 0.U
            }
          }
        }.otherwise {
          tables.zip(trainIdx).zipWithIndex.foreach { case ((t, i), k) =>
            when(longer(k)) { t.u(i) := 0.U }
          }
        }
      }

      ghrCommit := Cat(ghrCommit(histLen - 2, 0), outcome)
    }.otherwise {
      // Stale BTB entry (not actually a branch): invalidate.
      when(io.branchres.branchTaken) { btbValid(resAddr) := false.B }
    }
  }

  io.branchres.ready := true.B

  val btbhitOut = IO(Output(Bool()))
  btbhitOut := btbHit || retHit
}
