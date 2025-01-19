package pipeline.ports

/**
  * * * * * IMPORTANT * * * * * 
  * There is only one maintainer of this file. Only the maintainer is only allowed
  * to make changes to this file in the *main* branch.
  * 
  * Maintainer: Kaveesha Yalegama
  */

import chisel3._
import chisel3.util._


class composableInterface extends Bundle {
  val ready = Output(Bool())
  val fired = Input(Bool())
}

class robAllocate(addr_w :Int) extends composableInterface {
  val pc = Input(UInt(64.W))
  val instruction = Input(UInt(32.W))
  val prfDest = Input(UInt(6.W))
  val robAddr = Output(UInt(addr_w.W))
  val isReady = Input(Bool())
}

class pullExecResult(addr_w :Int) extends Bundle {
  val robAddr = Input(UInt(addr_w.W))
  val exceptionOccurred = Input(Bool())
  val mcause = Input(UInt(64.W))
  val mtval = Input(UInt(64.W))
  val valid = Input(Bool())
}

class retireInstruction(addr_w :Int) extends composableInterface {
  val prfDest = Output(UInt(6.W))
  val pc = Output(UInt(64.W))
  val instruction = Output(UInt(32.W))
  val exceptionOccurred = Output(Bool())
  val mcause = Output(UInt(64.W))
  val mtval = Output(UInt(64.W))
  val isStore = Output(Bool())
  val is_fence = Output(Bool())
}

class branchCheck(addr_w :Int) extends Bundle {
  val valid = Input(Bool())
  val pass = Input(Bool())
  val robAddr = Input(UInt(addr_w.W))
}