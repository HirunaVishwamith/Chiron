package cache_phase3

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import cache_phase3.constants._
import cache_phase3._
import cache_phase3.ChiselUtils._

//Use in Peripheral MSHR, replayUnit
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
      when(startPointer <= i.U || i.U <= endPointer) {
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
      when(memReg(i).address === invalidateAddr) {
        memReg(i).valid := false.B
      }
    }
  }
  isFull := fullReg
}

class fifoRecordInvalidateII[T <: requestPipelineTrait](depth: Int, traitType: T) extends fifoWithBranchOps(depth: Int, traitType: T){
  val invalidateAddr = IO(Input(UInt(addrWidth.W)))
  val invalidateEnable = IO(Input(Bool()))

  when(invalidateEnable) {
    for (i <- 0 until depth) {
      when(memReg(i).address(addrWidth - 1, log2Ceil(lineSize)) === invalidateAddr(addrWidth - 1, log2Ceil(lineSize))) {
        memReg(i).cacheLine.invalidated := true.B
      }
    }
  }
}