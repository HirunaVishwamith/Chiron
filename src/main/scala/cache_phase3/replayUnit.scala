package cache_phase3

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import cache_phase3.constants._
import cache_phase3._

//? After compiling
//TODO : Check with old requests to see if the memory address is already in a previous request
//TODO : Will need to keep a track of the requests already send in to check with.
//TODO : If so assert data not required field high
//TODO : At the dequeu of the requestOut fifo, add directly to the responseIn fifo.
//TODO : For the situation where a dependent request sets data not required and the coherency request invalidate
//TODO : -the data present original response, then the dependent request need to be serviced through the
//TODO : -ACE Unit to get the cache line
//TODO : The effect of this can be reduced by checking the dependency as it is about to go to the aceunit

class replayUnit extends Module{
  val requestIn = IO(new Bundle {
    val ready = Output(Bool())
    val request = Input(new requestWithDataWire)
  })
  val requestOut = IO(new Bundle {
    val ready = Input(Bool())
    val request = Output(new requestWithDataWire)
  })
  val responseIn = IO(new Bundle {
    val ready = Output(Bool())
    val request = Input(new replayWithCacheLineWire)
  })
  val responseOut = IO(new Bundle {
    val ready = Input(Bool())
    val request = Output(new replayWithCacheLineWire)
  })
  val writeBackIn = IO(new Bundle {
    val ready = Output(Bool())
    val request = Input(new writeBackWire)
  })
  val writeBackOut = IO(new Bundle {
    val ready = Input(Bool())
    val request = Output(new writeBackWire)
  })
  val branchOps = IO(new branchOps)
  val fenceReady = IO(Output(Bool()))

  val requestWaitFIFO = Module(new fifoWithBranchOpsI(
    depth = schedulerDepth,
    traitType = new requestWithDataWire
  ))
  requestWaitFIFO.branchOps <> branchOps
  requestWaitFIFO.write.data <> requestIn.request
  requestWaitFIFO.write.ready <> requestIn.ready
  requestWaitFIFO.read.data <> requestOut.request
  requestWaitFIFO.read.ready <> requestOut.ready

  val responseWaitFIFO = Module(new fifoWithBranchOpsI(
    depth = schedulerDepth,
    traitType = new replayWithCacheLineWire
  ))
  responseWaitFIFO.branchOps <> branchOps
  responseWaitFIFO.write.data <> responseIn.request
  responseWaitFIFO.write.ready <> responseIn.ready
  responseWaitFIFO.read.data <> responseOut.request
  responseWaitFIFO.read.ready <> responseOut.ready

  val writeBackFIFO = Module(new fifoBaseModule(
    depth = schedulerDepth,
    traitType = new writeBackWire
  ))
  writeBackFIFO.write.ready <> writeBackIn.ready
  writeBackFIFO.write.data <> writeBackIn.request
  writeBackFIFO.read.data <> writeBackOut.request
  writeBackFIFO.read.ready <> writeBackOut.ready

  fenceReady := requestWaitFIFO.isEmpty && responseWaitFIFO.isEmpty
}