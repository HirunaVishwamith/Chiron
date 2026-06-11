import chisel3._
import chisel3.util._

/**
 * F3a — Cleaned-up gshare predictor with a real (longer) global-history
 * register and a standard PC^history index. Drop-in replacement for the
 * original `gshare_predictor`: identical IO surface
 *   io.branchres / io.curr_pc / io.next_pc   (+ requestSent, mispredicted)
 * so `fetch.scala` and the lock-step harness are untouched. Correctness is
 * preserved by the existing misprediction-recovery path regardless of what
 * this unit predicts — only accuracy (and therefore IPC) changes.
 *
 * The original gshare used only ~log2(depth)/2 (=5) bits of history with an
 * odd Reverse() fold, which collapsed to a barely-better-than-bimodal ~60%
 * accuracy on real loops. This version keeps a proper `histLen`-bit history.
 *
 * History handling:
 *   ghrCommit — architectural global history; updated when a branch resolves
 *               (branchres.fired && isBranch) with the true outcome.
 *   ghrSpec   — speculative history used for prediction. Shifts in the
 *               predicted direction as taken-capable fetches leave the unit,
 *               and is restored to ghrCommit on a misprediction. The frontend
 *               is fully drained on recovery, so ghrSpec==ghrCommit is exact
 *               at that point and the stream resynchronises cleanly.
 */
class gshareV2_predictor(
  val counterDepth: Int = 4096,
  val btbSize:      Int = 512,
  val histLen:      Int = 24
) extends Module {
  require(isPow2(counterDepth) && isPow2(btbSize))

  val io = IO(new Bundle {
    val branchres = new branchResToFetch
    val curr_pc   = Input(UInt(64.W))
    val next_pc   = Output(UInt(64.W))
  })
  val requestSent  = IO(Input(Bool()))
  val mispredicted = IO(Input(Bool()))

  val idxW = log2Ceil(counterDepth)

  val ghrCommit = RegInit(0.U(histLen.W))
  val ghrSpec   = RegInit(0.U(histLen.W))

  // Fold the global history down to the counter index width, then XOR with the
  // low PC bits — the classic gshare hash, but over a real history length.
  def foldHist(h: UInt): UInt = {
    val chunks = (histLen + idxW - 1) / idxW
    val folded = (0 until chunks)
      .map(i => h(math.min((i + 1) * idxW, histLen) - 1, i * idxW))
      .reduce(_ ^ _)
    (folded | 0.U(idxW.W))(idxW - 1, 0) // pad/truncate to exactly idxW bits
  }
  def gIdx(pc: UInt, h: UInt): UInt = pc(idxW + 1, 2) ^ foldHist(h)

  val counters = Mem(counterDepth, UInt(2.W))

  // BTB (direct-mapped) ------------------------------------------------------
  val btbIdxW   = log2Ceil(btbSize)
  val btbAddr   = io.curr_pc(btbIdxW + 1, 2)
  val tag       = io.curr_pc(63, btbIdxW + 2)
  val resAddr   = io.branchres.pc(btbIdxW + 1, 2)
  val resTag    = io.branchres.pc(63, btbIdxW + 2)
  val btb       = Mem(btbSize, UInt(64.W))
  val validBits = RegInit(VecInit(Seq.fill(btbSize)(false.B)))
  val tagStore  = Mem(btbSize, UInt((62 - btbIdxW).W))

  // Predict ------------------------------------------------------------------
  val predIdx   = gIdx(io.curr_pc, ghrSpec)
  val btbHit    = validBits(btbAddr) && (tagStore(btbAddr) === tag)
  val takenPred = counters(predIdx)(1)
  io.next_pc := Mux(btbHit && takenPred, btb(btbAddr), io.curr_pc + 4.U)

  // Speculative history ------------------------------------------------------
  when(mispredicted) {
    ghrSpec := ghrCommit
  }.elsewhen(requestSent && btbHit) {
    ghrSpec := Cat(ghrSpec(histLen - 2, 0), takenPred)
  }

  // Train --------------------------------------------------------------------
  val trainIdx = gIdx(io.branchres.pc, ghrCommit)
  when(io.branchres.fired) {
    when(io.branchres.isBranch) {
      validBits(resAddr) := true.B
      tagStore(resAddr)  := resTag
      btb(resAddr)       := io.branchres.pcAfterBrnach
      val c = counters(trainIdx)
      when(io.branchres.branchTaken) {
        when(c =/= 3.U) { counters(trainIdx) := c + 1.U }
      }.otherwise {
        when(c =/= 0.U) { counters(trainIdx) := c - 1.U }
      }
      ghrCommit := Cat(ghrCommit(histLen - 2, 0), io.branchres.branchTaken)
    }.otherwise {
      // Stale BTB entry (instruction memory changed under us): invalidate.
      when(io.branchres.branchTaken) { validBits(resAddr) := false.B }
    }
  }

  io.branchres.ready := true.B

  val btbhitOut = IO(Output(Bool()))
  btbhitOut := btbHit
}
