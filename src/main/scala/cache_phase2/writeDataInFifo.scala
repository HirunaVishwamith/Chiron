package DataCache

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import DataCache.constantsDCache._

class writeDataInFifo extends Module{
  val dataIn = IO(new writeDataIn())
  // val canAllocate = IO(Input(Bool()))
  val dataOut = IO(Flipped(new writeDataIn()))
  val canAccept = IO(Input(Bool()))
  val isEmpty = IO(Output(Bool()))

  //CanAllocate will not be used

  dataOut.valid := false.B
  dataOut.data := 0.U
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
  class writeDataInWire extends Bundle {
    val valid = Bool()
    val data = UInt(dataWidth.W)
  }
  val memReg = Reg(Vec(schedulerDepth, new writeDataInWire))

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
    memReg(writePtr) := dataIn
  }
  dataOut <> memReg(readPtr)
  // canAllocate := !(writePtr - readPtr > 3.U)
  dataOut.valid := !emptyReg
  isEmpty := emptyReg


}