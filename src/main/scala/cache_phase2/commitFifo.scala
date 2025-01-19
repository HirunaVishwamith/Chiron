package DataCache

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import DataCache.constantsDCache._

class commitRecordIO extends Bundle {
  val valid = Input(Bool())
  val addr = Input(UInt(addrWidth.W))
}

class commitFifo extends Module{
  val dataIn = IO(new commitRecordIO())
  // val canAllocate = IO(Input(Bool()))
  val dataOut = IO(Flipped(new commitRecordIO()))
  val canAccept = IO(Input(Bool()))
  val isEmpty = IO(Output(Bool()))
  val isFull = IO(Output(Bool()))

  val invalidateAddr = IO(Input(UInt(addrWidth.W)))
  val invalidateEnable = IO(Input(Bool()))

  //CanAllocate will not be used

  dataOut.valid := false.B
  dataOut.addr := 0.U
  // canAllocate := false.B
  isEmpty := false.B 

  def counter(depth: Int, incr: Bool): (UInt, UInt) = {
    val cntReg = RegInit(0.U(log2Ceil(depth).W))
    val nextVal = Mux(cntReg === (depth-1).U, 0.U, cntReg + 1.U)
    when (incr) {
      cntReg := nextVal
    }
    (cntReg, nextVal)
  }

  //Here the writeDataIn bundle is defined for IO, will need to define a wire only class to use here
  class commitRecord extends Bundle {
    val valid = Bool()
    val addr = UInt(addrWidth.W)
  }
  val memReg = Reg(Vec(schedulerDepth, new commitRecord))

  val incrRead = WireInit(false.B)
  val incrWrite = WireInit(false.B)
  val (readPtr, nextRead) = counter(schedulerDepth, incrRead)
  val (writePtr, nextWrite) = counter(schedulerDepth, incrWrite)

  val emptyReg = RegInit(true.B)
  val fullReg = RegInit(false.B)

  val op = dataIn.valid ## canAccept
  val doWrite = WireDefault (false.B)
  switch (op) {
    is("b00".U) {}
    is("b01".U) { // read
      when (! emptyReg ) {
        fullReg := false.B
        emptyReg := nextRead === writePtr
        incrRead := true.B
      }
    }
    is("b10".U) { // write
      when (! fullReg ) {
        doWrite := true.B
        emptyReg := false.B
        fullReg := nextWrite === readPtr
        incrWrite := true.B
      }
    }
    is("b11".U) { // write and read
      when (! fullReg ) {
        doWrite := true.B
        emptyReg := false.B
        when( emptyReg ) {
          fullReg := false.B
        }. otherwise {
          fullReg := nextWrite === nextRead
      }
        incrWrite := true.B
      }
      when (! emptyReg ) {
        fullReg := false.B
      when( fullReg ) {
        emptyReg := false.B
      }. otherwise {
        emptyReg := nextRead === nextWrite
      }
        incrRead := true.B
      }
    }
  }
  
  when( doWrite ) {
    memReg(writePtr) <> dataIn
  }
  dataOut <> memReg(readPtr)
  // canAllocate := !(writePtr - readPtr > 3.U)
  // dataOut.valid := !emptyReg

  when(invalidateEnable) {
    for (i <- 0 until schedulerDepth) {
      when(memReg(i).addr === invalidateAddr) {
        memReg(i).valid := false.B
      }
    }
  }

  isEmpty := emptyReg
  isFull := fullReg


}