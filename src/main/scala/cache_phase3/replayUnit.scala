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

  requestIn.ready := false.B
  responseIn.ready := false.B
  writeBackIn.ready := false.B
  
  //!Debug only
  val isPauseForBoolean = WireDefault(pauseForBranch.B)
  
  val requestWaitFIFO = Module(new fifoWithBranchOpsI(
    depth = schedulerDepth,
    traitType = new requestWithDataWire
    ))
    requestWaitFIFO.branchOps <> branchOps
    
    //! Debug only
  requestWaitFIFO.read.ready := false.B
  when(!(isPauseForBoolean && branchOps.valid)){
    requestWaitFIFO.write.ready <> requestIn.ready
    requestWaitFIFO.read.ready <> requestOut.ready
  }
  requestWaitFIFO.write.data <> requestIn.request
  requestWaitFIFO.read.data <> requestOut.request

  val responseWaitFIFO = Module(new fifoWithBranchOpsI(
    depth = schedulerDepth,
    traitType = new replayWithCacheLineWire
  ))

  //! Debug only
  responseWaitFIFO.read.ready := false.B
  when(!(isPauseForBoolean && branchOps.valid)){
    responseWaitFIFO.write.ready <> responseIn.ready
    responseWaitFIFO.read.ready <> responseOut.ready
  }
  responseWaitFIFO.write.data <> responseIn.request
  responseWaitFIFO.read.data <> responseOut.request
  responseWaitFIFO.branchOps <> branchOps

  val writeBackFIFO = Module(new fifoBaseModule(
    depth = schedulerDepth,
    traitType = new writeBackWire
  ))

  //! Debug only
  writeBackFIFO.read.ready := false.B
  when(!(isPauseForBoolean && branchOps.valid)){
    writeBackFIFO.write.ready <> writeBackIn.ready
    writeBackFIFO.read.ready <> writeBackOut.ready
  }
  writeBackFIFO.write.data <> writeBackIn.request
  writeBackFIFO.read.data <> writeBackOut.request

  fenceReady := requestWaitFIFO.isEmpty && responseWaitFIFO.isEmpty
}