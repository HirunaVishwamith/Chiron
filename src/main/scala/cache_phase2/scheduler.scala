package DataCache

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import DataCache.constantsDCache._

class Scheduler extends Module{
  val requestIn = IO(new request)
  val canAllocate = IO(Output(Bool()))
  val requestOut = IO(Flipped(new request))
  val canAccept = IO(Input(Bool()))
  val isEmpty = IO(Output(Bool()))
  val branchOps = IO(new branchOps())

  requestOut.valid := false.B
  canAllocate := false.B
  requestOut.valid := false.B
  requestOut.address := 0.U
  requestOut.instruction := 0.U
  requestOut.branchMask := 0.U
  requestOut.robAddr := 0.U
  requestOut.prfDest := 0.U
  isEmpty := false.B
  

  def counter(depth: Int, incr: Bool): (UInt, UInt) = {
    val cntReg = RegInit(0.U(log2Ceil(depth).W))
    val nextVal = Mux(cntReg === (depth-1).U, 0.U, cntReg + 1.U)
    when (incr) {
      cntReg := nextVal
    }
    (cntReg, nextVal)
  }
  //Here the request bundle is defined for IO, will need to define a wire only class to use here
  class requestWire extends Bundle {
    val valid = Bool()
    val address = UInt(addrWidth.W)
    val instruction = UInt(insWidth.W)
    val branchMask = UInt(branchMaskWidth.W)
    val robAddr = UInt(robAddrWidth.W)
    val prfDest = UInt(prfAddrWidth.W)
    val branchValid = Bool()
  }
  val memReg = Reg(Vec(schedulerDepth, new requestWire))

  val incrRead = WireInit(false.B)
  val incrWrite = WireInit(false.B)
  val (readPtr, nextRead) = counter(schedulerDepth, incrRead)
  val (writePtr, nextWrite) = counter(schedulerDepth, incrWrite)

  val emptyReg = RegInit(true.B)
  val fullReg = RegInit(false.B)

  val op = requestIn.valid ## canAccept
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
    memReg(writePtr).valid := requestIn.valid
    memReg(writePtr).address := requestIn.address
    memReg(writePtr).instruction := requestIn.instruction
    memReg(writePtr).robAddr := requestIn.robAddr
    memReg(writePtr).prfDest := requestIn.prfDest
    when(branchOps.valid){
      when(branchOps.passed){
        when((requestIn.branchMask & branchOps.branchMask).orR){
          memReg(writePtr).branchMask := requestIn.branchMask ^ branchOps.branchMask
        }.otherwise{
          memReg(writePtr).branchMask := requestIn.branchMask  
        }
        memReg(writePtr).branchValid := true.B
      } .otherwise {
        memReg(writePtr).branchMask := requestIn.branchMask
        memReg(writePtr).branchValid := false.B
      }
    }.otherwise{
      memReg(writePtr).branchMask := requestIn.branchMask
      memReg(writePtr).branchValid := true.B
    }
  }
  requestOut.valid := memReg(readPtr).valid
  requestOut.address := memReg(readPtr).address
  requestOut.instruction := memReg(readPtr).instruction
  requestOut.branchMask := memReg(readPtr).branchMask
  requestOut.robAddr := memReg(readPtr).robAddr
  requestOut.prfDest := memReg(readPtr).prfDest

  val startPointer = Mux(canAccept, readPtr + 1.U, readPtr)
  val endPointer = writePtr - 1.U
  when(branchOps.valid) {
    for (i <- 0 until schedulerDepth) {
      when(startPointer <= i.U || i.U <= endPointer){
        when(branchOps.passed){
          when((memReg(i).branchMask & branchOps.branchMask).orR){
            memReg(i).branchMask := memReg(i).branchMask ^ branchOps.branchMask
          }
        } .otherwise{
          when((memReg(i).branchMask & branchOps.branchMask).orR){
            memReg(i).branchValid := false.B
          }
        }
      }
    }
  }

  canAllocate := !(writePtr - readPtr > 3.U)
  requestOut.valid := !emptyReg && memReg(readPtr).branchValid
  isEmpty := emptyReg

}