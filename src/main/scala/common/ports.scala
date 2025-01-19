package common

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._

import configuration._

/**
  * Composable Interface: Used when connecting composable modules as explained
  * in: https://ieeexplore.ieee.org/document/8686204
  * 
  * ready - The interface of module is ready to fire.
  * fired - All the other interfaces involved in the rule is ready and the rule
  *   has fired.
  */
class composableInterface extends Bundle {
  val ready = Output(Bool())
  val fired = Input(Bool())
}

class instrIssueAllocate extends composableInterface {
  val instruction = Input(UInt(32.W))
  val branchMask = Input(UInt(newBranchMaskWidth.W))  //leon coherency
  val rs1 = Input(new Bundle{
    val ready = Bool()
    val prfAddr = UInt(prfAddrWidth.W)
  })
  val rs2 = Input(rs1.cloneType)
  val prfDest = Input(UInt(prfAddrWidth.W))
  val robAddr = Input(UInt(robAddrWidth.W))
}

class instrIssueRelease extends composableInterface {
  val instruction = Output(UInt(32.W))
  val branchMask = Output(UInt(newBranchMaskWidth.W))  //leon coherency
  val rs1prfAddr = Output(UInt(prfAddrWidth.W))
  val rs2prfAddr = Output(UInt(prfAddrWidth.W))
  val prfDest = Output(UInt(prfAddrWidth.W))
  val robAddr = Output(UInt(robAddrWidth.W))
}