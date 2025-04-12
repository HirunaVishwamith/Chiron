package cache_phase3

import chisel3._ 
import chisel3.util._ 
import chisel3.experimental.BundleLiterals._
import cache_phase3.constants._
import cache_phase3.ChiselUtils.zeroInit

class CacheModule (
  peripheral_id : Int,
  dPort_id : Int
) extends Module {
  val request = IO(new request)
  val dPort = IO(new ACE(
    busWidth = dPort_WIDTH
  ))
  val peripheral = IO(new AXI(
    busWidth = peripheral_WIDTH
  ))
  val responseOut = IO(new responseOut)
  val canAllocate = IO(Output(Bool()))
  val writeDataIn = IO(new writeDataIn)
  val initiateFence = IO(Input(Bool()))
  val fenceInstructions = IO(new composableInterface)
  val writeCommit = IO(new composableInterface)
  val writeInstructionCommit = IO(new composableInterface)
  val branchOps = IO(new branchOps)
  val loadCommit = IO(new loadCommit)
  //!Debug only
  val debug = IO(new debug)

  canAllocate := false.B  
  
  responseOut.valid := false.B
  responseOut.prfDest := 0.U
  responseOut.robAddr := 0.U
  responseOut.result := 0.U
  responseOut.instruction := 0.U

  fenceInstructions.ready := false.B
  writeCommit.ready := false.B

  loadCommit.valid := false.B
  loadCommit.state := false.B

  writeInstructionCommit.ready := false.B

  val requestScheduler = Module(new requestScheduler)
  val arbiter = Module(new arbiter)
  val cacheLookup = Module(new cacheLookupUnit)
  val replayUnit = Module(new replayUnit)
  val peripheralUnit = Module(new peripheralUnit(
    dataWidth = dataWidth,
    addrWidth = addrWidth,
    id = peripheral_id,
    length = peripheral_LEN,
    size = peripheral_SIZE
  ))
  val aceUnit = Module(new ACEUnit(
    dataWidth = dataWidth,
    addrWidth = addrWidth,
    id = dPort_id,
    length = dPort_LEN,
    size = dPort_SIZE
  ))
  val commitFifo = Module(new fifoRecordInvalidateI(
    depth = schedulerDepth*4,
    traitType = new requestPipelineWire
  ))

  //Scheduler connections
  requestScheduler.branchOps := branchOps
  canAllocate := requestScheduler.canAllocate && !commitFifo.isFull
  
  requestScheduler.requestIn.valid := request.valid
  requestScheduler.requestIn.address := request.address
  requestScheduler.requestIn.core.instruction := request.instruction
  requestScheduler.requestIn.core.robAddr := request.robAddr
  requestScheduler.requestIn.core.prfDest := request.prfDest
  requestScheduler.requestIn.branch.mask := request.branchMask

  requestScheduler.requestIn.branch.valid := true.B
  requestScheduler.requestIn.writeData.valid := false.B
  requestScheduler.requestIn.writeData.data := 0.U
  // requestScheduler.requestIn.cacheLine.valid := false.B
  requestScheduler.requestIn.cacheLine.cacheLine := 0.U
  requestScheduler.requestIn.cacheLine.response := 0.U
  requestScheduler.requestIn.cacheLine.required := false.B
  // requestScheduler.requestIn.cacheLine.invalidated := false.B
  
  //Arbiter connections
  arbiter.writeDataIn <> writeDataIn
  arbiter.writeCommit <> writeCommit
  arbiter.responseOut := responseOut
  arbiter.branchOps <> branchOps
  
  // arbiter.request <> requestScheduler.requestOut
  requestScheduler.controlSignal.inorderReady := arbiter.request.inorderReady
  requestScheduler.controlSignal.speculativeReady := arbiter.request.speculativeReady
  arbiter.request.isSpeculative := requestScheduler.controlSignal.isSpeculative
  arbiter.request.request := requestScheduler.requestOut
  arbiter.replayRequest <> replayUnit.responseOut
  arbiter.coherencyRequest <> aceUnit.coherencyRequest
  arbiter.writeInstuctionCommitFired := writeInstructionCommit.fired

  //Cachelookup
  cacheLookup.branchOps := branchOps

  cacheLookup.request <> arbiter.toCacheLookup
  cacheLookup.writeInstructionCommit.fired := false.B

  //!Debug only
  debug := cacheLookup.debug 
  
  //ReplayUnit
  replayUnit.branchOps <> branchOps

  replayUnit.requestIn <> cacheLookup.toReplay
  replayUnit.responseIn <> aceUnit.readResponse
  replayUnit.writeBackIn <> cacheLookup.toWriteBack
  replayUnit.coherencyRequest := aceUnit.coherencyRequest.request

  //ACEUnit
  aceUnit.branchOps <> branchOps
  aceUnit.bus <> dPort

  aceUnit.readRequest <> replayUnit.requestOut
  aceUnit.coherencyResponse <> cacheLookup.toCoherency
  aceUnit.writeRequest <> replayUnit.writeBackOut

  //PeripheralUnit
  peripheralUnit.branchOps <> branchOps
  peripheralUnit.bus <> peripheral

  peripheralUnit.request <>arbiter.toPeripheral 
  peripheralUnit.responseOut.ready := !cacheLookup.toResponse.request.valid
  peripheralUnit.writeInstructionCommit.fired := false.B

  responseOut.valid := Mux(cacheLookup.toResponse.request.valid, cacheLookup.toResponse.request.valid && cacheLookup.toResponse.request.branch.valid, 
                  peripheralUnit.responseOut.request.valid)
  responseOut.prfDest := Mux(cacheLookup.toResponse.request.valid, cacheLookup.toResponse.request.core.prfDest, peripheralUnit.responseOut.request.core.prfDest)
  responseOut.robAddr := Mux(cacheLookup.toResponse.request.valid, cacheLookup.toResponse.request.core.robAddr, peripheralUnit.responseOut.request.core.robAddr)
  responseOut.result := Mux(cacheLookup.toResponse.request.valid, cacheLookup.toResponse.request.writeData.data, peripheralUnit.responseOut.request.writeData.data)
  responseOut.instruction := Mux(cacheLookup.toResponse.request.valid, cacheLookup.toResponse.request.core.instruction, peripheralUnit.responseOut.request.core.instruction)

  when(cacheLookup.writeInstructionCommit.ready){
    cacheLookup.writeInstructionCommit <> writeInstructionCommit
  } .otherwise{
    peripheralUnit.writeInstructionCommit <> writeInstructionCommit
  }

  //-----------------------Commit FIFO-----------------------------//
  zeroInit(commitFifo.write.data)
  commitFifo.read.ready := false.B
  commitFifo.invalidateAddr := 0.U
  commitFifo.invalidateEnable := false.B

  //Enqueue from responseOut of cacheLookup
  when(cacheLookup.toResponse.request.valid && cacheLookup.toResponse.request.branch.valid && cacheLookup.toResponse.request.core.instruction(6,0) === "b0000011".U){
    commitFifo.write.data := cacheLookup.toResponse.request
  } .elsewhen(peripheralUnit.responseOut.request.valid){
    commitFifo.write.data := peripheralUnit.responseOut.request
  }
  //BranchOps
  commitFifo.branchOps := branchOps
  //Invalidate from the coherentRequest from aceUnit
  when(aceUnit.coherencyRequest.request.valid && aceUnit.coherencyRequest.request.response(1)){
    commitFifo.invalidateAddr := aceUnit.coherencyRequest.request.address
    commitFifo.invalidateEnable := true.B
  }
  //Dequeue as requested from the loadCommit
  when(loadCommit.ready){
    commitFifo.read.ready := true.B
    loadCommit.state := commitFifo.read.data.valid
    loadCommit.valid := !commitFifo.isEmpty && commitFifo.read.data.branch.valid
    when(commitFifo.isEmpty){
      loadCommit.state := false.B
      loadCommit.valid := true.B
    }
  }

  //-----------------Initiate Fence----------------------//
  val fenceInititatedReg = RegInit(false.B)
  val canInititatedFenceReg = RegInit(false.B)
  val subModulesReady = WireDefault(
    requestScheduler.fenceReady && 
    arbiter.fenceReady &&
    replayUnit.fenceReady &&
    aceUnit.fenceReady &&
    RegNext(RegNext(!cacheLookup.request.holdInOrder))
    //* inorder signal is delayed by two clock cycles so all operations are done
  )
  canInititatedFenceReg := Mux(!canInititatedFenceReg, initiateFence, canInititatedFenceReg)

  fenceInititatedReg := canInititatedFenceReg && subModulesReady
  
  when(fenceInititatedReg){
    fenceInstructions.ready := true.B
    canAllocate := false.B
    fenceInititatedReg := Mux(fenceInstructions.fired, false.B, true.B)
    canInititatedFenceReg := Mux(fenceInstructions.fired, false.B, true.B)
  }

  commitFifo.write.data.cacheLine.cacheLine := 0.U
  commitFifo.write.data.cacheLine.required := false.B
  commitFifo.write.data.cacheLine.response := 0.U
  commitFifo.write.data.writeData.valid := false.B
  commitFifo.write.data.writeData.data := 0.U
}

object CacheModuleMain extends App {
  println("Generating the CacheModule hardware")
  //Hardware files will be out into generated
  emitVerilog(new CacheModule(peripheral_id = 0, dPort_id = 0), Array("--target-dir", "generated"))
}

