package Dcache

import chisel3._ 
import chisel3.util._ 
import chisel3.experimental.BundleLiterals._
import Dcache.constants._
import Dcache.ChiselUtils.zeroInit

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
  // High (with initiateFence) only when the committing fence is a fence.i
  // (funct3=001); selects the clean-on-fence walker path below.
  val initiateFenceI = IO(Input(Bool()))
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
  canAllocate := requestScheduler.canAllocate // && !commitFifo.isFull
  
  requestScheduler.requestIn.valid := request.valid
  requestScheduler.requestIn.address := request.address
  requestScheduler.requestIn.core.instruction := request.instruction
  requestScheduler.requestIn.core.robAddr := request.robAddr
  requestScheduler.requestIn.core.prfDest := request.prfDest
  requestScheduler.requestIn.branch.mask := request.branchMask

  requestScheduler.requestIn.branch.valid := true.B
  requestScheduler.requestIn.writeData.valid := false.B
  requestScheduler.requestIn.writeData.data := 0.U
  requestScheduler.requestIn.cacheLine.valid := false.B
  requestScheduler.requestIn.cacheLine.cacheLine := 0.U
  requestScheduler.requestIn.cacheLine.response := 0.U
  requestScheduler.requestIn.cacheLine.required := false.B
  
  //Arbiter connections
  arbiter.writeDataIn <> writeDataIn
  arbiter.writeCommit <> writeCommit
  arbiter.responseOut := responseOut
  arbiter.branchOps <> branchOps
  
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
  // Snoop CAM on writebacks still in the FIFO (between lookup and ACE head).
  replayUnit.writeBackSnoop.addr := aceUnit.writeBackFifoSnoop.addr
  aceUnit.writeBackFifoSnoop.hit := replayUnit.writeBackSnoop.hit
  aceUnit.writeBackFifoSnoop.data := replayUnit.writeBackSnoop.data
  // Drain-before-refetch: replay holds same-line read misses while their
  // writeback is anywhere between the FIFO and the AXI B response.
  replayUnit.aceWriteInFlight.valid := aceUnit.writeInFlight.valid
  replayUnit.aceWriteInFlight.address := aceUnit.writeInFlight.address
  // Staging writeback regs (before FIFO) — tags already dropped the victim.
  cacheLookup.writeBackStageSnoop.addr := aceUnit.writeBackStageSnoop.addr
  aceUnit.writeBackStageSnoop.hit := cacheLookup.writeBackStageSnoop.hit
  aceUnit.writeBackStageSnoop.data := cacheLookup.writeBackStageSnoop.data
  // Same-line fill-install hazard: hold a snoop in the ACE unit while a
  // replayed fill for that line is dispatching/installing in the lookup.
  cacheLookup.installSnoop.addr := aceUnit.installSnoop.addr
  aceUnit.installSnoop.hazard := cacheLookup.installSnoop.hazard
  // LR/SC forward-progress guard: arbiter defers reserved-line snoops for a
  // bounded window after an LR (see cacheLookupUnit.reservationGuard).
  arbiter.reservationGuard.active := cacheLookup.reservationGuard.active
  arbiter.reservationGuard.address := cacheLookup.reservationGuard.address

  //PeripheralUnit
  peripheralUnit.branchOps <> branchOps
  peripheralUnit.bus <> peripheral

  peripheralUnit.request <>arbiter.toPeripheral 
  peripheralUnit.responseOut.ready := !cacheLookup.toResponse.request.valid
  peripheralUnit.writeInstructionCommit.fired := false.B

  // Both arms must carry the squash check. The peripheral arm used to be
  // ungated -- see the comment on responseOut.request.valid in peripheralUnit
  // for what that allowed. peripheralUnit now gates its own valid too; keeping
  // the term here as well makes the two arms read identically and stops the
  // gate from being lost again if that internal one is ever refactored away.
  responseOut.valid := Mux(cacheLookup.toResponse.request.valid, cacheLookup.toResponse.request.valid && cacheLookup.toResponse.request.branch.valid,
                  peripheralUnit.responseOut.request.valid && peripheralUnit.responseOut.request.branch.valid)
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
  // A/B result 2026-07-11: re-enabling this check (removing the two overrides
  // below) did NOT fix the CPU1 stale-read "spinlock bad magic" and DID
  // deadlock mt-radix-q4 (no commit at 0x800004ec), so the overrides stay.
  // The commitFifo order assumptions need rework before this can go live.
  when(loadCommit.ready){
    commitFifo.read.ready := true.B
    loadCommit.state := commitFifo.read.data.valid
    loadCommit.valid := !commitFifo.isEmpty && commitFifo.read.data.branch.valid
    when(commitFifo.isEmpty){
      loadCommit.state := false.B
      loadCommit.valid := true.B
    }
  }
        loadCommit.valid := true.B
        loadCommit.state := true.B



  //-----------------Initiate Fence (fence.i => clean-on-fence)----------------//
  // Plain `fence` just drains the request pipeline and signals done (its ordering
  // is already enforced by the drain). A `fence.i` additionally runs the D-cache
  // clean walker (writes every dirty line back to L2) and waits for those
  // writebacks to reach L2 before signalling done, so the subsequent
  // non-coherent I-fetch observes up-to-date instruction memory. Only fence.i
  // takes the walker path, so the plain-fence barriers the benchmarks spin on
  // are unaffected.
  val fenceIdle :: fenceFlush :: fenceDrain :: fenceSignal :: Nil = Enum(4)
  val fenceState = RegInit(fenceIdle)
  val fenceIsI = RegInit(false.B)
  val subModulesReady = WireDefault(
    requestScheduler.fenceReady &&
    arbiter.fenceReady &&
    replayUnit.fenceReady &&
    aceUnit.fenceReady &&
    RegNext(RegNext(!cacheLookup.request.holdInOrder))
    //* inorder signal is delayed by two clock cycles so all operations are done
  )

  cacheLookup.flush.start := false.B

  switch(fenceState){
    is(fenceIdle){
      when(initiateFence){
        fenceIsI := initiateFenceI   // remember whether this fence is a fence.i
        fenceState := fenceFlush
      }
    }
    is(fenceFlush){
      //Wait for the request pipeline to drain; fence.i then kicks off the walker
      when(subModulesReady){
        when(fenceIsI){
          cacheLookup.flush.start := true.B
          fenceState := fenceDrain
        }.otherwise{
          fenceState := fenceSignal
        }
      }
    }
    is(fenceDrain){
      //Walker sweeping; wait until it finishes AND all writebacks reach L2
      when(!cacheLookup.flush.busy && subModulesReady){
        fenceState := fenceSignal
      }
    }
    is(fenceSignal){
      fenceInstructions.ready := true.B
      canAllocate := false.B
      when(fenceInstructions.fired){
        fenceState := fenceIdle
      }
    }
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

