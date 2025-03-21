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

trait requestTrait extends baseTrait {
  val instruction = UInt(insWidth.W)
  val branchMask = UInt(branchMaskWidth.W)
  val robAddr = UInt(robAddrWidth.W)
  val prfDest = UInt(prfAddrWidth.W)
}
class requestWire extends requestTrait

trait requestWithDataTrait extends requestTrait{
  val writeEn = Bool()
  val writeData = UInt(dataWidth.W)
}
class requestWithDataWire extends requestWithDataTrait

trait replayWithCacheLineTrait extends requestWithDataWire{
  val cacheLine = UInt((lineSize*8).W)
  val response = UInt(2.W)
}
class replayWithCacheLineWire extends replayWithCacheLineTrait

trait requestWithBranchInvalid extends requestWithDataTrait{
  val branchInvalid = Bool()
}
class requestWithBranchInvalidWire extends requestWithBranchInvalid

trait replayWithBranchInvalidTrait extends replayWithCacheLineTrait{
  val branchInvalid = Bool()
}
class replayWithBranchInvalidWire extends replayWithBranchInvalidTrait

trait  coherencyRequestTrait extends Bundle{
	val valid = Bool()
	val address = UInt(addrWidth.W)
	val response = UInt(2.W)
  //response(1) : Invalidate, response(0) : DataRequired
}
class coherencyRequestWire extends coherencyRequestTrait

trait  coherencyResponseTrait extends Bundle{
	val valid = Bool()
	val data = UInt((lineSize*8).W)
	val dataValid = Bool()
	val response = UInt(2.W)
  //response(1) : isClean, response(0) : isUnique
}
class coherencyResponseWire extends coherencyResponseTrait

trait cacheLookupTrait extends replayWithCacheLineTrait{
  val requestType = UInt(2.W)
}
class cacheLookupWire extends cacheLookupTrait

trait responseOutTrait extends Bundle{
  val valid = Bool()
  val prfDest = UInt(prfAddrWidth.W)
  val robAddr = UInt(robAddrWidth.W)
  val result = UInt(dataWidth.W)
  val instruction = UInt(insWidth.W)
}

trait responseOutWithAddrTrait extends requestTrait{
  val result = UInt(dataWidth.W)
}
class responseOutWithAddrWire extends responseOutWithAddrTrait

trait  writeBackTrait extends baseTrait {
	val data = UInt((lineSize*8).W)
}
class writeBackWire extends writeBackTrait

trait loadCommitTrait extends baseTrait{
	val state = Bool()
}
class loadCommitWire extends loadCommitTrait