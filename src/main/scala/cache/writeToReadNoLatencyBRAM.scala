package cache

import chisel3._
import chisel3.util._
import chisel3.util.HasBlackBoxResource
import chisel3.experimental.BundleLiterals._
import common.configuration._
import common.configuration

class zeroWriteLatencyCache extends Module {
  val cacheSets = Seq.fill(cache.associativity)(SyncReadMem((1 << (cache.lineIndexWidth + cache.wordOffsetWidth)), UInt (64.W)))
  val tags = Seq.fill(cache.associativity)(SyncReadMem((1 << (cache.lineIndexWidth)), UInt ((32-cache.lineIndexWidth-cache.wordOffsetWidth-3).W)))

  val readAddress = IO(Input(UInt(32.W)))
  val readOut = IO(Output(Vec(cache.associativity, new Bundle {
    val data = UInt(64.W)
    val tag = UInt((32-cache.lineIndexWidth-cache.wordOffsetWidth-3).W)
    val valid = Bool()
  })))

  val writePorts = IO(Input(Vec(cache.associativity, new Bundle {
    val enable = Bool()
    val cacheAddress = UInt(32.W)
    val data = UInt(64.W)
  })))

  // writing to cache
  writePorts zip cacheSets foreach { case (writePort, cacheSet) => {
    when(writePort.enable) {
      cacheSet.write(writePort.cacheAddress >> 3, writePort.data)
    }
  }}

  val validBits = Seq.fill(cache.associativity)(RegInit(VecInit(Seq.fill(1 << cache.lineIndexWidth)(false.B))))

  (tags zip validBits)
  .zip(cacheSets)
  .zip(readOut zip writePorts)
  .foreach{ case(((tags, valids), cacheSet), (read, write)) => {
    val doForward = RegNext((readAddress(31,2) === write.cacheAddress(31,2)) && write.enable)

    read.data := Mux(doForward, RegNext(write.data), cacheSet.read(readAddress >> 3))
    read.tag := tags.read(readAddress >> (cache.wordOffsetWidth + 3))
    read.valid := RegNext(valids(readAddress >> (cache.wordOffsetWidth + 3)))
  }}

  // invalidating sets
  val invalidateSet = IO(Input(new Bundle {
    val valid = Bool()
    val cacheIndex = UInt(((cache.lineIndexWidth)).W)
    val invalidateVector = Vec(cache.associativity, Bool())
  }))
  invalidateSet.invalidateVector zip validBits foreach { case (invalidate, validBit) => {
    when(invalidateSet.valid && invalidate) { validBit(invalidateSet.cacheIndex) := false.B }
  }}

  // validating sets - after filling
  val cacheFillDone = IO(Input(new Bundle {
    val valid = Bool()
    val cacheIndex = UInt(((cache.lineIndexWidth)).W)
    val validateVector = Vec(cache.associativity, Bool())
    val tag = UInt((32-(configuration.cache.lineIndexWidth +cache.wordOffsetWidth + 3)).W)
  }))
  cacheFillDone.validateVector zip (validBits zip tags) foreach { case (validate, (validBit, tag)) => {
    when(cacheFillDone.valid && validate) { 
      validBit(cacheFillDone.cacheIndex) := true.B 
      tag.write(cacheFillDone.cacheIndex,cacheFillDone.tag)
    }
  }}
}

object zeroWriteLatencyCache extends App {
  emitVerilog(new zeroWriteLatencyCache)
}
