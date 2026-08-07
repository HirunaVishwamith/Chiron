package Dcache

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import Dcache.constants._
import Dcache._
import Dcache.ChiselUtils._

// Base class only -- every FIFO in the D-cache now extends one of the
// branch-aware subclasses below. (It used to be instantiated directly as the
// peripheral MSHR, which meant an MMIO read on the bus could not be squashed;
// see peripheralUnit.) Do not instantiate this for anything carrying a
// speculative request.
class fifoBaseModule[T <: baseTrait](depth: Int, traitType: T) extends Module {
  val write = IO(new Bundle{
    val ready = Output(Bool())
    val data = Input(traitType.cloneType)
  })
  val read = IO(new Bundle{
    val ready = Input(Bool())
    val data = Output(traitType.cloneType)
  })
  val isEmpty = IO(Output(Bool()))

  // Zero initialize requestOut and internal memory
  zeroInit(read.data)
  isEmpty := false.B
  write.ready := false.B

  protected val memReg = RegInit(0.U.asTypeOf(Vec(depth, traitType.cloneType))) // Internal memory storage

  val incrRead = WireInit(false.B)
  val incrWrite = WireInit(false.B)

  def counter(size: Int, inc: Bool): (UInt, UInt) = {
    val cntReg = RegInit(0.U(log2Ceil(size).W))
    val nextVal = Mux(cntReg === (size - 1).U, 0.U, cntReg + 1.U)
    when(inc) { cntReg := nextVal }
    (cntReg, nextVal)
  }

  //----------------------Input-----------------------//
  val (readPtr, nextRead) = counter(depth, incrRead)
  val (writePtr, nextWrite) = counter(depth, incrWrite)

  val emptyReg = RegInit(true.B)
  protected val fullReg = RegInit(false.B)

  val op = write.data.valid ## read.ready
  protected val doWrite = WireDefault(false.B)

  switch(op) {
    is("b00".U) {}
    is("b01".U) { // read
      when(!emptyReg) {
        fullReg := false.B
        emptyReg := nextRead === writePtr
        incrRead := true.B
      }
    }
    is("b10".U) { // write
      when(!fullReg) {
        doWrite := true.B
        emptyReg := false.B
        fullReg := nextWrite === readPtr
        incrWrite := true.B
      }
    }
    is("b11".U) { // write and read
      when(!fullReg) {
        doWrite := true.B
        emptyReg := false.B
        fullReg := Mux(emptyReg, false.B, nextWrite === nextRead)
        incrWrite := true.B
      }
      when(!emptyReg) {
        fullReg := false.B
        emptyReg := Mux(fullReg, false.B, nextRead === nextWrite)
        incrRead := true.B
      }
    }
  }

  when(doWrite) {
    memReg(writePtr) := write.data
  }

  //----------------------Output-----------------------//
  read.data := memReg(readPtr) // Bulk assign from memory
  read.data.valid := !emptyReg
  write.ready := !fullReg
  isEmpty := emptyReg
}

//Use in replayUnit
//Use branchInvalid signal : Used in ACEMSHR
class fifoWithBranchOps[T <: requestPipelineTrait](depth: Int, traitType: T) extends fifoBaseModule(depth: Int, traitType: T) {
  val branchOps = IO(new branchOps())

  when(doWrite){
    //Branch operation for the moment of writing
    regWriteUpdate(memReg(writePtr).branch, branchOps, write.data.branch)
  }
  //Branch operation for the records already written
  val startPointer = Mux(read.ready, readPtr + 1.U, readPtr)
  val endPointer = writePtr - 1.U

  when(branchOps.valid) {
    for (i <- 0 until depth) {
      // Never touch the slot being WRITTEN this cycle: its update comes from
      // regWriteUpdate above (fresh data + this cycle's branchOps). This loop
      // reads the slot's OLD register value, and last-connect would clobber
      // the fresh entry with stale-mask garbage (branch pass) or kill it
      // outright (branch fail) — a committed store dropped from the inorder
      // queue this way served a stale dword to a peer's snoop on the Linux
      // SMP boot (CPU1 read a zero task pointer in complete()).
      when((startPointer <= i.U || i.U <= endPointer) &&
           !(doWrite && i.U === writePtr)) {
        when(branchOps.passed) {
          when((memReg(i).branch.mask & branchOps.branchMask).orR) {
            memReg(i).branch.mask := memReg(i).branch.mask ^ branchOps.branchMask
          }
        }.otherwise {
          when((memReg(i).branch.mask & branchOps.branchMask).orR) {
            memReg(i).branch.valid := false.B
          }
        }
      }
    }
  }
  //Output related branchMask
  regReadUpdate(read.data.branch, branchOps, memReg(readPtr).branch)
  read.data.valid := !emptyReg && memReg(readPtr).valid
}

//Use in scheduler
class fifoWithAddrCheck[T <: requestPipelineTrait](depth: Int, traitType: T, width: Int) extends fifoWithBranchOps(depth: Int, traitType: T) {
  val checkAddress = IO(Input(UInt(addrWidth.W)))
  val matchFound = IO(Output(Bool()))

  //Checking the address match for the double word range
  matchFound := memReg.map(
    entry => entry.valid && entry.address(addrWidth-1,width) === checkAddress(addrWidth-1,width)
    ).reduce(_ || _)
}

class fifoRecordInvalidateI[T <: requestPipelineTrait](depth: Int, traitType: T) extends fifoWithBranchOps(depth: Int, traitType: T){
  val isFull = IO(Output(Bool()))
  val invalidateAddr = IO(Input(UInt(addrWidth.W)))
  val invalidateEnable = IO(Input(Bool()))

  when(invalidateEnable) {
    for (i <- 0 until depth) {
      //Line-granular: the snoop carries a line address; any pending load
      //commit reading that line holds data the snoop just took ownership of
      when(memReg(i).address(addrWidth - 1, log2Ceil(lineSize)) === invalidateAddr(addrWidth - 1, log2Ceil(lineSize))) {
        memReg(i).valid := false.B
      }
    }
  }
  isFull := fullReg
}

class fifoBypassModule[T <: baseTrait](depth: Int, traitType: T) extends Module {
  val write = IO(new Bundle{
    val ready = Output(Bool())
    val data = Input(traitType.cloneType)
  })
  val read = IO(new Bundle{
    val ready = Input(Bool())
    val data = Output(traitType.cloneType)
  })
  val isEmpty = IO(Output(Bool()))

  // Line-address CAM for coherence: a dirty line in-flight in this FIFO is
  // no longer in the L1 tags (replacement already dropped it) and is not yet
  // in ACE writeBuffer. Without a snoop hit here the core answers "no data"
  // while still owning the only current copy — CCU then serves stale L2/DRAM
  // and the later writeback can race (proven spinlock-line corruption under
  // Linux SMP: DRAM at a kernel data PA holding another line's text bytes).
  val snoopAddr = IO(Input(UInt(addrWidth.W)))
  val snoopHit = IO(Output(Bool()))
  val snoopData = IO(Output(traitType.cloneType))

  zeroInit(read.data)
  isEmpty := false.B
  write.ready := false.B
  snoopHit := false.B
  zeroInit(snoopData)

  protected val memReg = RegInit(0.U.asTypeOf(Vec(depth, traitType.cloneType)))

  val incrRead = WireInit(false.B)
  val incrWrite = WireInit(false.B)

  def counter(size: Int, inc: Bool): (UInt, UInt) = {
    val cntReg = RegInit(0.U(log2Ceil(size).W))
    val nextVal = Mux(cntReg === (size - 1).U, 0.U, cntReg + 1.U)
    when(inc) { cntReg := nextVal }
    (cntReg, nextVal)
  }

  val (readPtr, nextRead) = counter(depth, incrRead)
  val (writePtr, nextWrite) = counter(depth, incrWrite)

  val emptyReg = RegInit(true.B)
  protected val fullReg = RegInit(false.B)

  val op = write.data.valid ## read.ready
  val bypass = emptyReg && (op === "b11".U)
  protected val doWrite = WireDefault(false.B)

  switch(op) {
    is("b00".U) {}
    is("b01".U) { // read
      when(!emptyReg) {
        fullReg := false.B
        emptyReg := nextRead === writePtr
        incrRead := true.B
      }
    }
    is("b10".U) { // write
      when(!fullReg) {
        doWrite := true.B
        emptyReg := false.B
        fullReg := nextWrite === readPtr
        incrWrite := true.B
      }
    }
    is("b11".U) { // write and read
      when(!fullReg) {
        doWrite := true.B
        emptyReg := false.B
        fullReg := Mux(emptyReg, false.B, nextWrite === nextRead)
        incrWrite := true.B
      }
      when(!emptyReg) {
        fullReg := false.B
        emptyReg := Mux(fullReg, false.B, nextRead === nextWrite)
        incrRead := true.B
      }
    }
  }

  // Override control signals if bypassing
  when(bypass) {
    doWrite := false.B
    incrRead := false.B
    incrWrite := false.B
    emptyReg := true.B
    fullReg := false.B
  }

  when(doWrite) {
    memReg(writePtr) := write.data
  }

  // Invalidate a slot as it is dequeued: both CAMs below scan raw memReg
  // entries, and a stale (already-drained) writeback left valid would (a)
  // serve OLD line data to a peer's snoop and (b) wedge the owner's own
  // drain-before-refetch gate forever (proven: boot hart froze in percpu
  // setup). The doWrite guard keeps a same-cycle write to the same slot
  // (read while full) from being clobbered, regardless of connect order.
  when(incrRead && !(doWrite && writePtr === readPtr)) {
    memReg(readPtr).valid := false.B
  }

  // Bypass write data to read when FIFO is empty and both read/write are ready
  read.data := Mux(bypass, write.data, memReg(readPtr))
  read.data.valid := Mux(bypass, true.B, !emptyReg)
  write.ready := !fullReg
  isEmpty := emptyReg

  // Snoop CAM: hit any valid entry (or the bypassing write) at the same line.
  // Priority: lowest index first (deterministic); bypass beats only when empty.
  val lineHi = log2Ceil(lineSize)
  val snoopLine = snoopAddr(addrWidth - 1, lineHi)
  val entryHits = VecInit(memReg.map { e =>
    e.valid && e.address(addrWidth - 1, lineHi) === snoopLine
  })
  val bypassHit = bypass && write.data.valid &&
    write.data.address(addrWidth - 1, lineHi) === snoopLine
  snoopHit := entryHits.asUInt.orR || bypassHit
  snoopData := Mux(bypassHit, write.data,
    MuxCase(memReg(0), entryHits.zipWithIndex.map { case (h, i) =>
      h -> memReg(i)
    }))

  // Second, independent CAM port: the owning core's OWN read-miss path must
  // not overtake a queued writeback of the same line (refill would return the
  // pre-eviction version from L2 and the line splits into two partial
  // copies — proven on the Linux SMP boot as a stale task pointer served to
  // a peer). Hit = any valid entry or the bypassing write on the same line.
  val selfAddr = IO(Input(UInt(addrWidth.W)))
  val selfHit = IO(Output(Bool()))
  val selfLine = selfAddr(addrWidth - 1, lineHi)
  selfHit := memReg.map { e =>
    e.valid && e.address(addrWidth - 1, lineHi) === selfLine
  }.reduce(_ || _) || (bypass && write.data.valid &&
    write.data.address(addrWidth - 1, lineHi) === selfLine)
}