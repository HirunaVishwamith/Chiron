import chisel3._
import chisel3.util._

/**
 * F2 — Block-fetch line streamer with deep sequential prefetch.
 *
 * Sits between the fetch unit (per-word request/response, unchanged) and the
 * iCache (returns a whole line per access). It holds a FIFO of up to `depth`
 * sequential lines and keeps several iCache requests outstanding, so the
 * iCache's long per-access latency is hidden by pipelining: while fetch
 * consumes the head line at 1 instr/cycle, later lines are being filled.
 *
 *   head    — line fetch is currently consuming (served at 1/cycle)
 *   fillPtr — next slot to receive an iCache response (fills in request order)
 *   tail    — next slot to allocate / next prefetch request
 *
 * The prefetch stream is strictly sequential (prefAddr += lineSz). When fetch
 * follows it, lines are already in flight / buffered. A non-sequential request
 * (taken branch / redirect to a line not at head or head+1) flushes the FIFO
 * and restarts the stream from there. Responses are matched to the requested
 * line by base address, so stale/lagging responses (including those left in the
 * iCache pipeline after a flush) are drained and ignored.
 *
 * Exactly one response is produced to fetch per accepted request, in order, so
 * the fetch unit's PC_fifo pairing remains valid.
 */
class lineStreamer(val offsetWidth: Int, val depth: Int = 8) extends Module {
  require(isPow2(depth), "lineStreamer depth must be a power of two")
  val words   = 1 << offsetWidth
  val lineW   = 32 * words
  val alignLo = offsetWidth + 2          // byte-offset bits within a line
  val lineSz  = (BigInt(1) << alignLo).U // bytes per line
  val pW      = log2Ceil(depth)

  val fromFetch = IO(new Bundle {
    val req  = Flipped(DecoupledIO(UInt(64.W))) // word address
    val resp = DecoupledIO(UInt(32.W))          // instruction word
  })
  val toCache = IO(new Bundle {
    val req  = DecoupledIO(UInt(64.W))
    val resp = Flipped(DecoupledIO(new Bundle {
      val line = UInt(lineW.W)
      val base = UInt(64.W)
    }))
  })

  def lineBase(a: UInt): UInt = Cat(a(63, alignLo), 0.U(alignLo.W))
  def wordSel(line: UInt, a: UInt): UInt =
    VecInit.tabulate(words)(i => line(32 * i + 31, 32 * i))(a(alignLo - 1, 2))

  val base   = Reg(Vec(depth, UInt(64.W)))
  val data   = Reg(Vec(depth, UInt(lineW.W)))
  val valid  = RegInit(VecInit(Seq.fill(depth)(false.B))) // slot allocated (requested)
  val filled = RegInit(VecInit(Seq.fill(depth)(false.B))) // line data present

  val head    = RegInit(0.U(pW.W))
  val fillPtr = RegInit(0.U(pW.W))
  val tail    = RegInit(0.U(pW.W))
  val count   = RegInit(0.U((pW + 1).W))
  val nxt     = head + 1.U // wraps in pW bits (depth is a power of two)

  val prefAddr    = Reg(UInt(64.W)) // line-aligned address of the next prefetch
  val streamValid = RegInit(false.B)

  // ---- 1-deep registered output (breaks the req/resp comb loop with fetch) ----
  val outValid = RegInit(false.B)
  val outWord  = Reg(UInt(32.W))
  fromFetch.resp.valid := outValid
  fromFetch.resp.bits  := outWord
  val outConsumed = outValid && fromFetch.resp.ready
  when(outConsumed) { outValid := false.B }
  val outFree = !outValid || outConsumed

  // ---- Defaults ----
  fromFetch.req.ready := false.B
  toCache.req.valid   := false.B
  toCache.req.bits    := prefAddr
  toCache.resp.ready  := true.B // always drain responses

  val rb        = lineBase(fromFetch.req.bits)
  val headHit   = (count > 0.U) && valid(head) && filled(head)  && (base(head) === rb)
  val headWait  = (count > 0.U) && valid(head) && !filled(head) && (base(head) === rb)
  val crossHit  = (count > 1.U) && valid(nxt)  && filled(nxt)   && (base(nxt) === rb)
  val crossWait = (count > 1.U) && valid(nxt)  && !filled(nxt)  && (base(nxt) === rb)
  // The stream is already pointed at the requested line and about to allocate it
  // (e.g. the cycle right after a flush). Don't re-flush — let the prefetcher
  // allocate slot[tail]==rb; otherwise we'd flush forever and never fill.
  val streamPending = streamValid && (count < depth.U) && (lineBase(prefAddr) === rb)
  val miss      = fromFetch.req.valid && !headHit && !headWait && !crossHit && !crossWait && !streamPending

  val doFlush  = miss && outFree
  val doRetire = WireDefault(false.B)
  val doAlloc  = WireDefault(false.B)
  val doFill   = WireDefault(false.B)

  // ---- Serve ----
  when(!doFlush && outFree && fromFetch.req.valid) {
    when(headHit) {
      outWord := wordSel(data(head), fromFetch.req.bits); outValid := true.B
      fromFetch.req.ready := true.B
    }.elsewhen(crossHit) {
      // Fetch crossed into the next line: serve it and retire the head line.
      outWord := wordSel(data(nxt), fromFetch.req.bits); outValid := true.B
      fromFetch.req.ready := true.B
      doRetire := true.B
    }
    // headWait / crossWait: stall (waiting on an in-flight fill)
  }

  // ---- Fill (responses arrive in request order) ----
  when(toCache.resp.fire && valid(fillPtr) && !filled(fillPtr) &&
       (base(fillPtr) === lineBase(toCache.resp.bits.base))) {
    data(fillPtr)   := toCache.resp.bits.line
    filled(fillPtr) := true.B
    doFill := true.B
  }

  // ---- Allocate + issue prefetch requests (keep the iCache pipeline full) ----
  when(!doFlush && streamValid && (count < depth.U)) {
    toCache.req.valid := true.B
    toCache.req.bits  := prefAddr
    when(toCache.req.fire) {
      base(tail)   := prefAddr
      valid(tail)  := true.B
      filled(tail) := false.B
      prefAddr     := prefAddr + lineSz
      doAlloc      := true.B
    }
  }

  // ---- Pointer / count updates ----
  when(doFlush) {
    for (i <- 0 until depth) { valid(i) := false.B; filled(i) := false.B }
    head := 0.U; fillPtr := 0.U; tail := 0.U; count := 0.U
    prefAddr := rb; streamValid := true.B
  }.otherwise {
    when(doRetire) { valid(head) := false.B; filled(head) := false.B; head := nxt }
    when(doFill)   { fillPtr := fillPtr + 1.U }
    when(doAlloc)  { tail := tail + 1.U }
    count := count + doAlloc.asUInt - doRetire.asUInt
  }

  // Debug bus (kept compatible with the testbench counters)
  val dbg = IO(Output(new Bundle {
    val filling   = Bool()
    val curValid  = Bool()
    val reqValid  = Bool()
    val reqHit    = Bool()
    val baseMatch = Bool()
    val reqBaseLo = UInt(16.W)
    val curBaseLo = UInt(16.W)
  }))
  dbg.filling   := fromFetch.req.valid && !headHit && !crossHit // cannot serve this cycle
  dbg.curValid  := count > 0.U
  dbg.reqValid  := fromFetch.req.valid
  dbg.reqHit    := headHit || crossHit
  dbg.baseMatch := (count > 0.U) && (base(head) === rb)
  dbg.reqBaseLo := rb(15, 0)
  dbg.curBaseLo := base(head)(15, 0)
}
