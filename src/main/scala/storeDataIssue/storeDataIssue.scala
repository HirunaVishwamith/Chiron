package storeDataIssue

import Chisel.log2Ceil
import constants._
import chisel3._
import chisel3.experimental.BundleLiterals._
import chisel3.util._
import chisel3.experimental.IO
import os.write


//Initiating the Fifo
class FifoIO extends Bundle {
  val enq = Flipped(new DecoupledIO(UInt(fifo_width.W)))
  val deq = new DecoupledIO(UInt(fifo_width.W))
}

abstract class Fifo( val depth: Int) extends Module  with RequireSyncReset{
  val io = IO(new FifoIO)
  assert(depth > 0, "Number of buffer elements needs to be larger than 0")
}

class sdiFifo( depth: Int) extends Fifo(depth: Int) {

  val readReg = RegInit(0.U(log2Ceil(depth).W))
  val nextRead = Mux(readReg === (depth - 1).U, 0.U, readReg + 1.U)
  val writeReg = RegInit(0.U(log2Ceil(depth).W))
  val nextWrite = Mux(writeReg === (depth - 1).U, 0.U, writeReg + 1.U)

  // the register based memory
  val memReg = Mem(depth, UInt(fifo_width.W))
  val readPtr = readReg
  val writePtr = writeReg
  val emptyReg = RegInit(true.B)
  val fullReg = RegInit(false.B)



  val incrRead = WireDefault(false.B)
  val incrWrite = WireDefault(false.B)


  val modifyVal = IO(Input(UInt(log2Ceil(depth).W)))

  val modify = IO(Input(Bool()))


  when (incrRead) {
    readReg := nextRead
  }


  when (modify && !emptyReg){
    //val nextval = Mux(modifyVal === (depth - 1).U, 0.U, modifyVal + 1.U)
    val nextval = modifyVal
    writeReg := nextval
    //fullReg := nextval === readPtr
    emptyReg := nextval === readPtr
  }.elsewhen(incrWrite){
    writeReg := nextWrite
  }
  val validBits = RegInit(VecInit(Seq.fill(depth)(false.B)))

  when(io.deq.ready && io.deq.valid && io.enq.valid && io.enq.ready) {
    memReg(writePtr) := io.enq.bits
    validBits(writePtr) := true.B
    validBits(readPtr) := false.B
    incrWrite := true.B
    incrRead := true.B
  }.elsewhen(io.enq.valid && io.enq.ready) {
    memReg(writePtr) := io.enq.bits
    validBits(writePtr) := true.B
    emptyReg := false.B
    fullReg := nextWrite === readPtr
    incrWrite := true.B
  }.elsewhen(io.deq.ready && io.deq.valid) {
    fullReg := false.B
    emptyReg := nextRead === writePtr
    incrRead := true.B
    validBits(readPtr) := false.B
  }

  io.deq.bits := memReg(readPtr)
  io.enq.ready := (!fullReg | (io.deq.valid & io.deq.ready)) & !modify
  io.deq.valid := !emptyReg & !modify

  //val memVals = Seq.fill(depth)(gen)


  val allocatedAddr = IO(Output(UInt(log2Ceil(depth).W)))

  allocatedAddr := writePtr

}

class composableInterface extends Bundle {
  val ready = Output(Bool())
  val valid = Input(Bool())
}

class fromROBUnit extends Bundle{
  val readyNow  = Input(Bool())
  // val prfAddr   = Input(UInt(prfAddrWidth.W))
}

class fromBranchUnit extends Bundle{
  val passOrFail  = Input(Bool())
  val robAddr   = Input(UInt(log2Ceil(fifo_depth).W))
  val valid       = Input(Bool())
}

class toPRFUnit extends Bundle{
  val instruction   = Output(UInt(32.W))
  val valid         = Output(Bool())
  val rs2Addr       = Output(UInt(prfAddrWidth.W))
  val branchMask    = Output(UInt(newBranchMaskWidth.W)) //leon coherency
}

class fromDecodeUnit extends composableInterface{
  val instruction   = Input(UInt(32.W))
  val rs2Addr       = Input(UInt(prfAddrWidth.W))
  val rs2Ready      = Input(Bool())
  val branchMask    = Input(UInt(newBranchMaskWidth.W)) //leon coherency
}

class fromDecodeRobAddr extends composableInterface {
  // Gives robaddr of all instuctions that leave decode
  val robAddr = Input(UInt(4.W))
}


class storeDataIssue extends Module{

  //Inputs and Outputs of the Module
  val fromROB    = IO(new fromROBUnit)
  val fromBranch  = IO(new fromBranchUnit)
  val fromDecode  = IO(new fromDecodeUnit)
  val toPRF       = IO(new toPRFUnit)
  val robMapUpdate = IO(new fromDecodeRobAddr)

  //Intermediate registers
  val toStore_reg = Reg(UInt(fifo_width.W))
  val branchMask_reg  = Reg(UInt(newBranchMaskWidth.W)) //leon coherency

  //Initiating the fifo
  val sdiFifo     = Module(new sdiFifo(fifo_depth) {
    val readPtrOut = IO(Output(readPtr.cloneType))
    readPtrOut := readPtr
  })

  // map each robaddr to a storedataqueue addr
  val map = Mem(16, UInt(log2Ceil(fifo_depth).W))
  // map(robMapUpdate.robAddr) := sdiFifo.allocatedAddr
  when(robMapUpdate.valid) {
    map(robMapUpdate.robAddr) := sdiFifo.allocatedAddr
  }
  robMapUpdate.ready := 1.B

  // Debug structure, map sdFifo readPtr to robAddr
  val sdAddrToRobAddr = Reg(Vec(fifo_depth, robMapUpdate.robAddr.cloneType))
  when(fromDecode.valid && fromDecode.ready) {
    sdAddrToRobAddr(sdiFifo.allocatedAddr) := robMapUpdate.robAddr
  }

  //Connecting the fifo
  sdiFifo.io.enq.valid      := fromDecode.valid
  fromDecode.ready          := sdiFifo.io.enq.ready
  sdiFifo.io.enq.bits       := Cat(fromDecode.branchMask, fromDecode.rs2Addr)
  sdiFifo.modifyVal := map(fromBranch.robAddr)
  sdiFifo.modify    := fromBranch.valid && !fromBranch.passOrFail

  sdiFifo.io.deq.ready      := fromROB.readyNow
  //toStore_reg               := sdiFifo.io.deq.bits

  //Connecting the register to the outputs to PRF
  toPRF.rs2Addr     := sdiFifo.io.deq.bits(5,0)
  toPRF.branchMask  := sdiFifo.io.deq.bits(10,6) //leon coherency changed the bits to 5 bits
  toPRF.instruction := fromDecode.instruction
  toPRF.valid       := sdiFifo.io.deq.valid

  // Debug structure
  val robOfStore = IO(Output(robMapUpdate.robAddr.cloneType))
  robOfStore := sdAddrToRobAddr(sdiFifo.readPtrOut)
}

object sdiVerilog extends App {
  (new chisel3.stage.ChiselStage).emitVerilog(new storeDataIssue())
}