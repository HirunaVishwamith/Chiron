package Dcache

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import Dcache.constants._

trait baseTrait extends Bundle {
  val valid = Bool()
  val address = UInt(addrWidth.W)
}
class baseWire extends baseTrait

trait  writeBackTrait extends baseTrait {
	val data = UInt((lineSize*8).W)
	// true  = the L1 STILL OWNS this line (fence.i clean-on-fence walker: it
	//         writes the line back without touching the tag, so the cache keeps
	//         it valid and writable).
	// false = eviction: the tag has already been reassigned, so this buffered
	//         copy is the only one left.
	// The snoop-answer path in ACEUnit serves a snoop straight out of the
	// writeback pipeline, which is correct ONLY for evictions. For a walker
	// writeback the core can store into the line AFTER the walker captured it,
	// making the buffered copy stale; answering from it hands a peer old data
	// with PassDirty and permanently loses the committed store (the Linux
	// csd_unlock hang). Entries with retain=true must therefore never answer a
	// snoop — the L1 does, and it is strictly fresher.
	val retain = Bool()
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
  // val invalidated = Bool()
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
  //response(1) : isShared, response(0) : passDirty
}
class coherencyResponseWire extends coherencyResponseTrait