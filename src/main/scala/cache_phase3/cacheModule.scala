package cache_phase3

import chisel3._ 
import chisel3.util._ 
import chisel3.experimental.BundleLiterals._
import cache_phase3.constants._
import cache_phase3.ChiselUtils.zeroInit

//TODO : Commit fifo need to cache branch resolve and remove loads as they might be invliadated while outside the cache module

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
  val branchOps = IO(new branchOps)
  val loadCommit = IO(new loadCommit)

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

  val scheduler = Module(new Scheduler)
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
  val commitFifo = Module(new fifoRecordInvalidate(
    depth = schedulerDepth*4,
    traitType = new baseWire
  ))

  //Scheduler connections
  scheduler.branchOps <> branchOps
  canAllocate := scheduler.canAllocate && commitFifo.isEmpty
  
  scheduler.requestIn <> request
  
  //Arbiter connections
  arbiter.writeDataIn <> writeDataIn
  arbiter.writeCommit <> writeCommit
  arbiter.responseOut := responseOut
  arbiter.branchOps <> branchOps
  
  // arbiter.request <> scheduler.requestOut
  scheduler.controlSignal.inorderReady := arbiter.request.inorderReady
  scheduler.controlSignal.speculativeReady := arbiter.request.speculativeReady
  arbiter.request.isSpeculative := scheduler.controlSignal.isSpeculative
  arbiter.request.request := scheduler.requestOut
  arbiter.replayRequest <> replayUnit.responseOut
  arbiter.coherencyRequest <> aceUnit.coherencyRequest

  //Cachelookup
  cacheLookup.branchOps <> branchOps

  cacheLookup.request <> arbiter.toCacheLookup
  
  //ReplayUnit
  replayUnit.branchOps <> branchOps

  replayUnit.requestIn <> cacheLookup.toReplay
  replayUnit.responseIn <> aceUnit.readResponse
  replayUnit.writeBackIn <> cacheLookup.toWriteBack

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

  responseOut.valid := Mux(cacheLookup.toResponse.request.valid, cacheLookup.toResponse.request.valid, peripheralUnit.responseOut.request.valid)
  responseOut.prfDest := Mux(cacheLookup.toResponse.request.valid, cacheLookup.toResponse.request.prfDest, peripheralUnit.responseOut.request.prfDest)
  responseOut.robAddr := Mux(cacheLookup.toResponse.request.valid, cacheLookup.toResponse.request.robAddr, peripheralUnit.responseOut.request.robAddr)
  responseOut.result := Mux(cacheLookup.toResponse.request.valid, cacheLookup.toResponse.request.result, peripheralUnit.responseOut.request.result)
  responseOut.instruction := Mux(cacheLookup.toResponse.request.valid, cacheLookup.toResponse.request.instruction, peripheralUnit.responseOut.request.instruction)

  //-----------------------Commit FIFO-----------------------------//
  zeroInit(commitFifo.write.data)
  commitFifo.read.ready := false.B
  commitFifo.invalidateAddr := 0.U
  commitFifo.invalidateEnable := false.B

  //Enqueue from responseOut of cacheLookup
  when(cacheLookup.toResponse.request.valid && cacheLookup.toResponse.request.instruction(6,0) === "b0000000".U){
    commitFifo.write.data.address := cacheLookup.toResponse.request.address
    commitFifo.write.data.valid := true.B
  }
  //Invalidate from the coherentRequest from aceUnit
  when(aceUnit.coherencyRequest.request.valid){
    commitFifo.invalidateAddr := aceUnit.coherencyRequest.request.address
    commitFifo.invalidateEnable := true.B
  }
  //Dequeue as requested from the loadCommit
  when(loadCommit.ready){
    commitFifo.read.ready := true.B
    loadCommit.state := commitFifo.read.data.valid
    loadCommit.valid := !commitFifo.isEmpty
  }

  //-----------------Initiate Fence----------------------//
  val fenceInititatedReg = RegInit(false.B)
  val canInititatedFenceReg = RegInit(false.B)
  val subModulesReady = WireDefault(
    scheduler.fenceReady && 
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
}

object CacheModuleMain extends App {
  println("Generating the CacheModule hardware")
  //Hardware files will be out into generated
  emitVerilog(new CacheModule(peripheral_id = 0, dPort_id = 0), Array("--target-dir", "generated"))
}

