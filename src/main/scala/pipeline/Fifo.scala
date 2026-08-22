package pipeline.fifo

import Chisel.log2Ceil
import chisel3._
import pipeline.ports._
import chisel3.util._

class regFifo[T <: Data ]( gen: T, depth: Int) extends Fifo(gen:
  T, depth: Int) {

  def counter(depth: Int , incr: Bool): (UInt , UInt) = {
    val cntReg = RegInit (0.U(log2Ceil(depth).W))
    val nextVal = Mux(cntReg === (depth -1).U, 0.U, cntReg + 1.U)
    when (incr) {
      cntReg := nextVal
    }
    (cntReg , nextVal)
  }

  // the register based memory
  val memReg = Mem(depth , gen)
  val incrRead = WireDefault (false.B)
  val incrWrite = WireDefault (false.B)
  val (readPtr , nextRead) = counter(depth , incrRead)
  val (writePtr , nextWrite ) = counter(depth , incrWrite )
  val emptyReg = RegInit(true.B)
  val fullReg = RegInit(false.B)


  when(io.deq.ready && io.deq.valid && io.enq.valid && io.enq.ready) {
    memReg(writePtr) := io.enq.bits
    incrWrite := true.B
    incrRead := true.B
  }.elsewhen(io.enq.valid && io.enq.ready) {
    memReg(writePtr) := io.enq.bits
    emptyReg := false.B
    fullReg := nextWrite === readPtr
    incrWrite := true.B
  }.elsewhen(io.deq.ready && io.deq.valid) {
    fullReg := false.B
    emptyReg := nextRead === writePtr
    incrRead := true.B
  }

  io.deq.bits := memReg(readPtr)
  io.enq.ready := !fullReg | (io.deq.valid & io.deq.ready)
  io.deq.valid := !emptyReg
  //printf(p"$io\n")
  val isEmpty = IO(Output(Bool()))
  isEmpty := emptyReg
}

/**
  * FIFO IO with enqueue and dequeue ports using the ready/valid interface.
  */
class FifoIO[T <: Data](private val gen: T) extends Bundle {
  val enq = Flipped(new DecoupledIO(gen))
  val deq = new DecoupledIO(gen)
}

abstract class Fifo[T <: Data ]( gen: T, val depth: Int) extends Module  with RequireSyncReset{
  val io = IO(new FifoIO(gen))
  assert(depth > 0, "Number of buffer elements needs to be larger than 0")
}

class robFifo[T <: Data ]( gen: T, depth: Int) extends Fifo(gen:
  T, depth: Int) {

  val incrRead = WireDefault(false.B)
  val incrWrite = WireDefault(false.B)

  val modify = IO(Input(Bool()))
  val modifyVal = IO(Input(UInt(log2Ceil(depth).W)))

  val readReg = RegInit (0.U(log2Ceil(depth).W))
  val nextRead = Mux(readReg === (depth -1).U, 0.U, readReg + 1.U)
  when (incrRead) {
    readReg := nextRead
  }

  val writeReg = RegInit(0.U(log2Ceil(depth).W))
  val nextWrite = Mux(writeReg === (depth - 1).U, 0.U, writeReg + 1.U)

  val fullReg = RegInit(false.B)
  // the register based memory
  val memReg = Mem(depth, gen)
  val readPtr = readReg
  val writePtr = writeReg
  val emptyReg = RegInit(true.B)

  val nextval = Mux(modifyVal === (depth - 1).U, 0.U, modifyVal + 1.U)

  // A rollback may only target an entry that is still live, i.e. inside
  // [readPtr, writePtr). The block below assumes that ("the branch itself ...
  // cannot have committed yet"), but the assumption does not always hold: a
  // branch can resolve AFTER its ROB entry has retired, and then modifyVal is
  // behind readPtr and `writeReg := modifyVal + 1` moves the write pointer
  // FORWARD, so the ROB GROWS instead of shrinking. Measured on mt-ipimux
  // hart1 (robmodify_probe, pre-edge sampling) — exactly ONE such rollback in
  // 218,231, and it is the one that wedges the core:
  //   cyc 26814119  pre{rd=14 wr=11 occ=13} modify=1 mVal=12 rel=14  <- retired
  //                 post{rd=14 wr=13 occ=15}                          <- grew
  //   cyc 26814132  post{rd=14 wr=14 full=1 occ=depth}                <- FULL
  // A permanently full ROB blocks allocation, so decode stalls, the scheduler
  // drains, branchCounter can never reach 0, and the interrupt-injection FSM
  // parks in waitToInjectInterr with fetch frozen (core.scala:1197) — the hart
  // never takes its pending IPI.
  //
  // Occupancy is (writePtr - readPtr) mod depth, with empty/full disambiguated
  // by the flags. A target at distance >= occupancy has already retired, so the
  // rollback has nothing left to discard and is dropped. This cannot mask a
  // real rollback: anything still in flight is by definition within occupancy.
  private val relTarget = modifyVal - readPtr
  private val relWrite  = writePtr - readPtr
  private val ptrW      = log2Ceil(depth)
  val occupancy = Mux(emptyReg, 0.U((ptrW + 1).W),
                  Mux(fullReg,  depth.U((ptrW + 1).W), 0.U(1.W) ## relWrite))
  val modifyInWindow = (0.U(1.W) ## relTarget) < occupancy
  val doModify = modify && modifyInWindow

  when (doModify){
    //val nextval = modifyVal
    writeReg := nextval
    fullReg := nextval === readPtr
    // emptyReg MUST be updated here too. A branch-mispredict rollback keeps
    // entries [readPtr .. modifyVal] — including the branch itself, which has
    // resolved but cannot have committed yet (deq is gated by !modify, and
    // commit.ready needs the ready bit this resolution is what sets). So the
    // FIFO always retains at least one entry and the correct disambiguation of
    // the nextval === readPtr case on THIS path is "kept everything" (full),
    // never "kept nothing" (empty) — hence the constant below rather than the
    // old commented-out `nextval === readPtr`.
    //
    // Leaving emptyReg stale here is what produced two long-standing SMP
    // wedges, because it desynchronises emptyReg from readPtr/writePtr:
    //   * stale emptyReg=true after a rollback -> deq.valid stays low while
    //     readPtr =/= writePtr, then the next enq clears emptyReg and the head
    //     presents a SLOT THAT WAS NEVER REWRITTEN. That stale entry retires
    //     and lands its register writeback: mt-ipitmr's csd-clear loop counter
    //     a4 gets overwritten with an older trap frame's value (0x290 =
    //     timer_irqs) mid-loop, so `bne a4,a6` never sees 4 and the software
    //     IPI handler spins forever.
    //   * stale emptyReg=false with nextval === readPtr -> fullReg=true and the
    //     ROB is permanently full: allocation blocked, branchCounter never
    //     drains, and the interrupt-injection FSM sits in waitToInjectInterr
    //     with fetch frozen (core.scala:1197) never taking its IPI — the
    //     mt-ipimux wedge.
    // The coherent-load squash needs the opposite resolution ("keep nothing")
    // and gets it from flushAll below, which last-connect overrides this.
    emptyReg := false.B
  }.elsewhen(incrWrite){
    writeReg := nextWrite
  }




  when(io.deq.ready && io.deq.valid && io.enq.valid && io.enq.ready) {
    memReg(writePtr) := io.enq.bits
    incrWrite := true.B
    incrRead := true.B
  }.elsewhen(io.enq.valid && io.enq.ready) {
    memReg(writePtr) := io.enq.bits
    emptyReg := false.B
    fullReg := nextWrite === readPtr
    incrWrite := true.B
  }.elsewhen(io.deq.ready && io.deq.valid) {
    fullReg := false.B
    emptyReg := nextRead === writePtr
    incrRead := true.B
  }

  io.deq.bits := memReg(readPtr)
  io.enq.ready := (!fullReg | (io.deq.valid & io.deq.ready)) & !doModify
  io.deq.valid := !emptyReg & !doModify

  // Coherent-load squash: the rollback target is commit.robAddr-1, which the
  // modify block above cannot represent — nextval === readPtr is ambiguous
  // between "keep nothing" (this case) and "keep everything" (full FIFO
  // rolling back to its newest slot), and resolving it as full leaves the
  // stale head presenting at commit forever while blocking all allocation.
  // An explicit flush empties the FIFO with last-connect priority.
  val flushAll = IO(Input(Bool()))
  when(flushAll) {
    writeReg := readReg
    fullReg := false.B
    emptyReg := true.B
  }
}

class robResultsFifo[T <: Data ]( gen: T, depth: Int, numWritePorts: Int) extends robFifo(gen: T, depth: Int){
  class robWriteport extends Bundle{
    val valid = Input(Bool())
    val data = Input(gen)
    val addr = Input(UInt(log2Ceil(depth).W))
  }

  // Result write ports
  val writeports = IO(Vec(numWritePorts,new robWriteport))


  for (i <- 0 until writeports.length){
    when(writeports(i).valid) {
      memReg(writeports(i).addr) := writeports(i).data
    }
  }

  val allocatedAddr = IO(Output(UInt(log2Ceil(depth).W)))

  allocatedAddr := writePtr

  val secondPtr  = Mux(readPtr === (depth - 1).U, 0.U, readPtr + 1.U)
  val twoOrMore  = fullReg || (!emptyReg && (readPtr +% 1.U =/= writePtr))
  val secondReady = IO(Output(Bool()))
  secondReady := twoOrMore && memReg(secondPtr).asUInt(0)

}
