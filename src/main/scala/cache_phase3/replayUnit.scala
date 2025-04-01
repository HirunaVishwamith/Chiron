package cache_phase3

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import cache_phase3.constants._
import cache_phase3._
import cache_phase3.ChiselUtils._

//TODO : At the dequeu of the requestOut fifo, add directly to the responseIn fifo.
//TODO : For the situation where a dependent request sets data not required and the coherency request invalidate
//TODO : -the data present original response, then the dependent request need to be serviced through the
//TODO : -ACE Unit to get the cache line
//TODO : The effect of this can be reduced by checking the dependency as it is about to go to the aceunit

class replayUnit extends Module{
  val requestIn = IO(new Bundle {
    val ready = Output(Bool())
    val request = Input(new requestPipelineWire)
  })
  val requestOut = IO(new Bundle {
    val ready = Input(Bool())
    val request = Output(new requestPipelineWire)
  })
  val responseIn = IO(new Bundle {
    val ready = Output(Bool())
    val request = Input(new requestPipelineWire)
  })
  val responseOut = IO(new Bundle {
    val ready = Input(Bool())
    val request = Output(new requestPipelineWire)
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
  val coherencyRequest = IO(Input(new coherencyRequestWire))
  val fenceReady = IO(Output(Bool()))

  //!Debug only
  val isPauseForBoolean = WireDefault(pauseForBranch.B)

  val requestWaitFIFO = Module(new fifoWithBranchOps(
    depth = schedulerDepth,
    traitType = new requestPipelineWire
  ))
  val responseWaitFIFO = Module(new fifoRecordInvalidateII(
    depth = schedulerDepth,
    traitType = new requestPipelineWire
  ))
  val writeBackFIFO = Module(new fifoBaseModule(
    depth = schedulerDepth,
    traitType = new writeBackWire
  ))

  requestIn.ready := false.B
  responseIn.ready := false.B
  writeBackIn.ready := false.B
  requestWaitFIFO.read.ready := false.B
  responseWaitFIFO.read.ready := false.B
  responseWaitFIFO.invalidateEnable := false.B
  responseWaitFIFO.invalidateAddr := 0.U
  writeBackFIFO.read.ready := false.B
  
  zeroInit(requestWaitFIFO.write.data)
  zeroInit(responseWaitFIFO.write.data)
  zeroInit(writeBackFIFO.write.data)

  requestWaitFIFO.branchOps <> branchOps
  responseWaitFIFO.branchOps <> branchOps
    
  //! Debug only
  when(!(isPauseForBoolean && branchOps.valid)){
    requestIn.ready := requestWaitFIFO.write.ready
    requestWaitFIFO.read.ready := requestOut.ready
  }
  when(requestIn.request.valid && requestIn.request.branch.valid){
    requestWaitFIFO.write.data := requestIn.request
    regWriteUpdate(requestWaitFIFO.write.data.branch, branchOps, requestIn.request.branch)
  }
  requestOut.request := requestWaitFIFO.read.data

  //! Debug only
  when(!(isPauseForBoolean && branchOps.valid)){
    responseIn.ready := responseWaitFIFO.write.ready
    responseWaitFIFO.read.ready := responseOut.ready
  }
  when(responseIn.request.valid && responseIn.request.branch.valid){
    responseWaitFIFO.write.data := responseIn.request
    regWriteUpdate(responseWaitFIFO.write.data.branch, branchOps, responseIn.request.branch)
  }
  responseOut.request := responseWaitFIFO.read.data
  when(coherencyRequest.valid){
    responseWaitFIFO.invalidateEnable := true.B
    responseWaitFIFO.invalidateAddr := coherencyRequest.address
  }

  //! Debug only
  when(!(isPauseForBoolean && branchOps.valid)){
    writeBackIn.ready := writeBackFIFO.write.ready
    writeBackFIFO.read.ready := writeBackOut.ready
  }
  when(writeBackIn.request.valid){
    writeBackFIFO.write.data := writeBackIn.request
  }
  writeBackOut.request := writeBackFIFO.read.data

  fenceReady := requestWaitFIFO.isEmpty && responseWaitFIFO.isEmpty
}