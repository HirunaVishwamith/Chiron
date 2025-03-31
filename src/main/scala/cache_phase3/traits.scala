package cache_phase3

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import cache_phase3.constants._

trait baseTrait extends Bundle {
  val valid = Bool()
  val address = UInt(addrWidth.W)
}
class baseWire extends baseTrait

trait  writeBackTrait extends baseTrait {
	val data = UInt((lineSize*8).W)
}
class writeBackWire extends writeBackTrait

trait loadCommitTrait extends baseTrait{
	val state = Bool()
}
class loadCommitWire extends loadCommitTrait

trait coreTrait extends Bundle {
  val instruction = UInt(insWidth.W)
  val robAddr = UInt(robAddrWidth.W)
  val prfDest = UInt(prfAddrWidth.W)
}

trait branchTrait extends Bundle {
  val valid = Bool()
  val mask = UInt(branchMaskWidth.W)
}

trait writeDataTrait extends Bundle {
  val valid = Bool()
  val data =UInt(dataWidth.W)
}

trait cacheLineTrait extends Bundle {
  val valid = Bool()
  val cacheLine = UInt((lineSize*8).W)
  val response = UInt(2.W)
  val required = Bool()
  val invalidated = Bool()
}

trait requestPipelineTrait extends baseTrait {
  val core = new coreTrait {}
  val branch = new branchTrait {}
  val writeData = new writeDataTrait {}
  val cacheLine = new cacheLineTrait {}
}

class requestPipelineWire extends requestPipelineTrait


trait  coherencyRequestTrait extends baseTrait{
	val response = UInt(cacheResponseWidth.W)
  //response(1) : Invalidate, response(0) : DataRequired
}
class coherencyRequestWire extends coherencyRequestTrait

trait  coherencyResponseTrait extends coherencyRequestTrait{
	val cacheLine = UInt((lineSize*8).W)
	val dataValid = Bool()
  //response(1) : isClean, response(0) : isUnique
}
class coherencyResponseWire extends coherencyResponseTrait