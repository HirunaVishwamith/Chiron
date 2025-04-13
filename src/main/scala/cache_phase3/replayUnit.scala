package cache_phase3

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import cache_phase3.constants._
import cache_phase3._
import cache_phase3.ChiselUtils._

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

  val requestWaitFIFO = Module(new fifoWithBranchOps(
    depth = schedulerDepth,
    traitType = new requestPipelineWire
  ))
  val writeBackFIFO = Module(new fifoBypassModule(
    depth = schedulerDepth,
    traitType = new writeBackWire
  ))

  requestIn.ready := false.B
  responseIn.ready := false.B
  writeBackIn.ready := false.B
  requestWaitFIFO.read.ready := false.B

  writeBackFIFO.read.ready := false.B
  
  zeroInit(requestWaitFIFO.write.data)
  zeroInit(writeBackFIFO.write.data)

  requestWaitFIFO.branchOps <> branchOps
    
  requestIn.ready := requestWaitFIFO.write.ready
  requestWaitFIFO.read.ready := requestOut.ready

  when(requestIn.request.valid && requestIn.request.branch.valid){
    requestWaitFIFO.write.data := requestIn.request
    regWriteUpdate(requestWaitFIFO.write.data.branch, branchOps, requestIn.request.branch)
  }
  requestOut.request := requestWaitFIFO.read.data

  responseIn <> responseOut

  writeBackIn.ready := writeBackFIFO.write.ready
  writeBackFIFO.read.ready := writeBackOut.ready

  when(writeBackIn.request.valid){
    writeBackFIFO.write.data := writeBackIn.request
  }
  writeBackOut.request := writeBackFIFO.read.data

  fenceReady := requestWaitFIFO.isEmpty

  //Resource Utilization
  requestWaitFIFO.write.data.cacheLine.cacheLine := 0.U
  requestWaitFIFO.write.data.cacheLine.required := false.B
  // requestWaitFIFO.write.data.cacheLine.response := 0.U
}