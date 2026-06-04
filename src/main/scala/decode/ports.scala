package decode

import chisel3._
import chisel3.util._
import decode.constants._
import common.configuration

class composableInterface extends Bundle {
  val ready = Output(Bool())
  val fired = Input(Bool())
}

class RecivInstrFrmFetch extends composableInterface {
  val pc          = Input(UInt(dataWidth.W))
  val instruction = Input(UInt(insAddrWidth.W))
  val expected    = Output(new Bundle {
    val valid     = Bool()
    val pc        = UInt(dataWidth.W)
    val coherency = Bool()
  })
}

class PushInsToPipeline extends composableInterface {
  val instruction = Output(UInt(insAddrWidth.W))
  val pc          = Output(UInt(dataWidth.W))
  val PRFDest     = Output(UInt(PRFAddrWidth.W))
  val rs1Addr     = Output(UInt(PRFAddrWidth.W))
  val rs1Ready    = Output(Bool())
  val rs2Addr     = Output(UInt(PRFAddrWidth.W))
  val rs2Ready    = Output(Bool())
  val immediate   = Output(UInt(dataWidth.W))
  val robAddr     = Input(UInt(robAddrWidth.W))
  val branchMask  = Output(UInt(configuration.newBranchMaskWidth.W))
}

class PullCommitFrmRob extends composableInterface {
  val pc          = Input(UInt(dataWidth.W))
  val instruction = Input(UInt(insAddrWidth.W))
  val rdAddr      = Input(UInt(rdWidth.W))
  val PRFDest     = Input(UInt(PRFAddrWidth.W))
  val robAddr     = Input(UInt(robAddrWidth.W))
  val data        = Input(UInt(dataWidth.W))
}

class PrfAddrFrmExec extends Bundle {
  val exec1Addr  = Input(UInt(PRFAddrWidth.W))
  val exec2Addr  = Input(UInt(PRFAddrWidth.W))
  val exec3Addr  = Input(UInt(PRFAddrWidth.W))
  val exec1Valid = Input(Bool())
  val exec2Valid = Input(Bool())
  val exec3Valid = Input(Bool())
}

class JumpPRFWrite extends composableInterface {
  val PRFDest  = Output(UInt(PRFAddrWidth.W))
  val linkAddr = Output(UInt(dataWidth.W))
}

class BranchPCs extends composableInterface {
  val branchPCReady    = Output(Bool())
  val branchPC         = Output(UInt(dataWidth.W))
  val predictedPCReady = Output(Bool())
  val predictedPC      = Output(UInt(dataWidth.W))
  val branchMask       = Output(UInt(configuration.newBranchMaskWidth.W))
}

class BranchEvalIn extends composableInterface {
  val passFail   = Input(Bool())
  val branchMask = Input(UInt(configuration.newBranchMaskWidth.W))
  val targetPC   = Input(UInt(dataWidth.W))
}

class BranchEvalOut extends composableInterface {
  val passFail   = Output(Bool())
  val branchMask = Output(UInt(4.W))
}

class RetiredRenamedTable extends Bundle {
  val table = Output(Vec(regCount, UInt(PRFAddrWidth.W)))
}
