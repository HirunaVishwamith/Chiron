package cache_phase3

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import cache_phase3.constants._
import cache_phase3._
import cache_phase3.ChiselUtils._

//TODO : Add branchOps as seems fit to fifos

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
class fifoWithBranchOpsI[T <: requestTrait](depth: Int, traitType: T) extends fifoBaseModule(depth: Int, traitType: T) {
  val branchOps = IO(new branchOps())

  when(doWrite){
    //Branch operation for the moment of writing
    when(branchOps.valid){
      fifoWriteUpdate(memReg(writePtr),branchOps,write.data)
    }.otherwise{
      //No branch operation in action
      memReg(writePtr).branchMask := write.data.branchMask
      memReg(writePtr).valid := true.B
    }
  }
  //Branch operation for the records already written
  val startPointer = Mux(read.ready, readPtr + 1.U, readPtr)
  val endPointer = writePtr - 1.U

  when(branchOps.valid) {
    for (i <- 0 until depth) {
      when(startPointer <= i.U || i.U <= endPointer) {
        cacheBranchRegUpdate(memReg(i),branchOps)
      }
    }
  }
  //Output related branchMask
  fifoReadUpdate(memReg(readPtr), branchOps, read.data, emptyReg)
}

//Use in scheduler
class fifoWithAddrCheck[T <: requestTrait](depth: Int, traitType: T) extends fifoWithBranchOpsI(depth: Int, traitType: T) {
  val checkAddress = IO(Input(UInt(addrWidth.W)))
  val matchFound = IO(Output(Bool()))

  //Checking the address match for the double word range
  matchFound := memReg.map(
    entry => entry.valid && entry.address(addrWidth-1,3) === checkAddress(addrWidth-1,3)
    ).reduce(_ || _)
}

//Use branchInvalid signal : Used in ACEMSHR
class fifoWithBranchOpsII[T <: replayWithBranchInvalidTrait](depth: Int, traitType: T) extends fifoBaseModule(depth: Int, traitType: T) {
  val branchOps = IO(new branchOps())

  when(doWrite){
    //Branch operation for the moment of writing
    when(branchOps.valid){
      when(branchOps.passed){
        //BranchPass and match
        when((write.data.branchMask & branchOps.branchMask).orR){
          memReg(writePtr).branchMask := write.data.branchMask ^ branchOps.branchMask
        }.otherwise{
          //BranchPass and no match
          memReg(writePtr).branchMask := write.data.branchMask  
        }
        memReg(writePtr).valid := true.B
      } .otherwise {
        //BranchFail and match
        when((write.data.branchMask & branchOps.branchMask).orR){
          memReg(writePtr).branchMask := 0.U
          memReg(writePtr).branchInvalid := true.B
        }.otherwise{
          //BranchFail and no match
          memReg(writePtr).branchMask := write.data.branchMask
          memReg(writePtr).branchInvalid := false.B
        }
      }
    }.otherwise{
      //No branch operation in action
      memReg(writePtr).branchMask := write.data.branchMask
      memReg(writePtr).branchInvalid := false.B
    }
  }
  //Branch operation for the records already written
  val startPointer = Mux(read.ready, readPtr + 1.U, readPtr)
  val endPointer = writePtr - 1.U

  when(branchOps.valid) {
    for (i <- 0 until depth) {
      when(startPointer <= i.U || i.U <= endPointer) {
        when(branchOps.passed) {
          when((memReg(i).branchMask & branchOps.branchMask).orR) {
            memReg(i).branchMask := memReg(i).branchMask ^ branchOps.branchMask
          }
        }.otherwise {
          when((memReg(i).branchMask & branchOps.branchMask).orR) {
            memReg(i).branchInvalid := true.B
          }
        }
      }
    }
  }
  //Output related branchMask
  when(branchOps.valid) {
    when(branchOps.passed) {
      //BranchPass and match
      when((memReg(readPtr).branchMask & branchOps.branchMask).orR) {
        read.data.branchMask := memReg(readPtr).branchMask ^ branchOps.branchMask
      }.otherwise {
        //BranchPass and no match
        read.data.branchMask := memReg(readPtr).branchMask
      }
      read.data.valid := !emptyReg && memReg(readPtr).valid
    }.otherwise {
      //BranchFail and match
      when((memReg(readPtr).branchMask & branchOps.branchMask).orR) {
        read.data.branchMask := 0.U
        read.data.branchInvalid := true.B
      } .otherwise{
        //BranchFail and no match
        read.data.branchMask := memReg(readPtr).branchMask
        read.data.valid := !emptyReg && memReg(readPtr).valid
      }
    }
  }.otherwise {
    read.data.branchMask := memReg(readPtr).branchMask
    read.data.valid := !emptyReg && memReg(readPtr).valid
  }
}

class fifoRecordInvalidate[T <: baseTrait](depth: Int, traitType: T) extends fifoBaseModule(depth: Int, traitType: T){
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

