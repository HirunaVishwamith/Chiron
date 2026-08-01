package Dcache

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import Dcache.constants._
import Dcache._
import Dcache.ChiselUtils._

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
  // Coherence snoop into the in-flight writeback FIFO (see fifoBypassModule).
  val writeBackSnoop = IO(new Bundle {
    val addr = Input(UInt(addrWidth.W))
    val hit = Output(Bool())
    val data = Output(new writeBackWire)
  })
  // Writeback held in the ACE write FSM (past this unit's FIFO) — the last
  // stage a same-line read miss must not overtake.
  val aceWriteInFlight = IO(new Bundle {
    val valid = Input(Bool())
    val address = Input(UInt(addrWidth.W))
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

  // Drain-before-refetch: a read miss whose line still has a writeback
  // queued (writeBackFIFO CAM) or on the bus (ACE writeBuffer) must wait for
  // that writeback to reach L2. Otherwise the refill can overtake it through
  // the interconnect, return the PRE-eviction version from L2, and the line
  // forks into two partial copies (post-refetch stores in BRAM, pre-eviction
  // stores in the queued writeback). Proven on the Linux SMP boot: the snoop
  // CAM then served a peer the version missing wait->private=current, and
  // CPU1 woke a NULL task ("spinlock bad magic", lock 0x6f0).
  val selfWbBlock = writeBackFIFO.selfHit ||
    (aceWriteInFlight.valid &&
     aceWriteInFlight.address(addrWidth - 1, log2Ceil(lineSize)) ===
       requestWaitFIFO.read.data.address(addrWidth - 1, log2Ceil(lineSize)))
  requestWaitFIFO.read.ready := requestOut.ready && !selfWbBlock

  when(requestIn.request.valid && requestIn.request.branch.valid){
    requestWaitFIFO.write.data := requestIn.request
    regWriteUpdate(requestWaitFIFO.write.data.branch, branchOps, requestIn.request.branch)
  }
  requestOut.request := requestWaitFIFO.read.data
  when(selfWbBlock) { requestOut.request.valid := false.B }

  responseIn <> responseOut

  writeBackIn.ready := writeBackFIFO.write.ready
  writeBackFIFO.read.ready := writeBackOut.ready

  when(writeBackIn.request.valid){
    writeBackFIFO.write.data := writeBackIn.request
  }
  writeBackOut.request := writeBackFIFO.read.data

  writeBackFIFO.snoopAddr := writeBackSnoop.addr
  writeBackSnoop.hit := writeBackFIFO.snoopHit
  writeBackSnoop.data := writeBackFIFO.snoopData
  writeBackFIFO.selfAddr := requestWaitFIFO.read.data.address

  fenceReady := requestWaitFIFO.isEmpty && writeBackFIFO.isEmpty

  //Resource Utilization
  requestWaitFIFO.write.data.cacheLine.cacheLine := 0.U
  requestWaitFIFO.write.data.cacheLine.required := false.B
  // requestWaitFIFO.write.data.cacheLine.response := 0.U
}