package Dcache

import chisel3._ 
import chisel3.util._ 
import chisel3.experimental.BundleLiterals._
import Dcache.constants._
import Dcache.ChiselUtils._
import Dcache.AddrChecker._

//* All atomic instructions, runs as a load followed by a store
//* Load(read) is passed with writeDataValid deasserted
//* Store(write) is passed with writeDataValid asserted
//TODO : Pipeline the module

class arbiter extends Module {
  val request = IO(new Bundle{
    val request = Input(new requestPipelineWire)
    val isSpeculative = Input(Bool())
    val inorderReady = Output(Bool())
    val speculativeReady = Output(Bool())
  })
  val toPeripheral = IO(new Bundle {
    val ready = Input(Bool())
    val request = Output(new requestPipelineWire)
  })
  val toCacheLookup = IO(new Bundle {
    val ready = Input(Bool())
    val holdInOrder = Input(Bool())
    val requestType = Output(UInt(2.W))
    val inorderPending = Input(Bool())
    val request = Output(new requestPipelineWire)
  })
  val replayRequest = IO(new Bundle {
    val ready = Output(Bool())
    val request = Input(new requestPipelineWire)
  })
  val coherencyRequest = IO(new Bundle {
    val ready = Output(Bool())
    val request = Input(new coherencyRequestWire)
  })
  // From cacheLookupUnit: bounded post-LR window during which snoops on the
  // reserved line are deferred (LR/SC forward-progress guarantee).
  val reservationGuard = IO(new Bundle {
    val active = Input(Bool())
    val address = Input(UInt(addrWidth.W))
  })
  val writeDataIn = IO(new writeDataIn)
  val writeCommit = IO(new composableInterface)
  val branchOps = IO(new branchOps)
  val responseOut = IO(Flipped(new responseOut))
  val fenceReady = IO(Output(Bool()))
  val writeInstuctionCommitFired = IO(Input(Bool()))
  
  request.speculativeReady := false.B
  request.inorderReady := false.B
  zeroInit(toPeripheral.request)
  zeroInit(toCacheLookup.request)
  toCacheLookup.requestType := 0.U
  replayRequest.ready := false.B
  coherencyRequest.ready := false.B
  writeCommit.ready := false.B
  fenceReady := false.B
  
  val speculativeBuffer = RegInit(0.U.asTypeOf(new requestPipelineWire))
  val inorderBuffer = RegInit(0.U.asTypeOf(new requestPipelineWire))
  val operationBuffer = RegInit(0.U.asTypeOf(new requestPipelineWire))
  
  val requestTypeWire = WireInit(0.U(arbiterReqTypesWidth.W))
  val speculativeBufferReadyWire = WireDefault(!speculativeBuffer.valid || (speculativeBuffer.valid && !speculativeBuffer.branch.valid))
  val operationBufferReadyWire = WireDefault((!operationBuffer.valid || (operationBuffer.valid && !operationBuffer.branch.valid)))
  val inorderBufferReadyWire = !inorderBuffer.valid || (inorderBuffer.valid && !inorderBuffer.branch.valid)

  request.speculativeReady := speculativeBufferReadyWire
  request.inorderReady :=  inorderBufferReadyWire && operationBufferReadyWire                        

  //---------------------Request Enqueue---------------------//
  // True on a cycle the corresponding buffer is (re)loaded. The ageing
  // blocks below must skip such cycles: regRecordUpdate's branch-PASS arm
  // ends in `buffer.valid := buffer.valid` (utils.scala:137), which writes
  // back the PREVIOUS occupant's valid bit / mask. That is the same last-
  // connect hole as ACEUnit.responseBuffer and peripheralUnit.requestBuffer.
  //
  // Ready allows a new request in when the occupant is empty OR already
  // squashed (valid && !branch.valid). The squashed-overwrite case is the
  // one that fires: a fresh speculative load is written, then ageing
  // clobbers its mask with the dead occupant's (often 0). The load looks
  // non-speculative, survives its own mispredict, and completes onto a ROB
  // slot speculation has already freed — READY-OUTSIDE-ROB-WINDOW on
  // ci-check's csaxpy-s5-q4 / filter-s5-q4.
  val specLoading = request.request.valid && request.request.branch.valid && request.isSpeculative
  val opLoading   = request.request.valid && request.request.branch.valid && !request.isSpeculative
  when(request.request.valid && request.request.branch.valid){
    when(request.isSpeculative){
      speculativeBuffer := request.request
      regWriteUpdate(speculativeBuffer.branch, branchOps, request.request.branch)
    } .otherwise {
      operationBuffer := request.request
      regWriteUpdate(operationBuffer.branch, branchOps, request.request.branch)
    }
  }

  // Age sitting occupants BEFORE the FSM / dequeue assign, so a later
  // `valid := false.B` last-connects over the PASS arm's resurrection.
  when(speculativeBuffer.valid && !specLoading){
    regRecordUpdate(speculativeBuffer.branch, branchOps)
  }
  when(operationBuffer.valid && !opLoading){
    regRecordUpdate(operationBuffer.branch, branchOps)
  }
  when(inorderBuffer.valid){
    regRecordUpdate(inorderBuffer.branch, branchOps)
  }

  //--------------------Operations State Machine-----------------//
  val idleState :: commitReadyState :: commitFiredState :: waitState :: writeInstructionFiredState :: Nil = Enum(5)
  val operationState = RegInit(idleState)

  val operationWires = Wire(new Bundle{
    val valid = Bool()
    val isRead = Bool()
    val isWrite = Bool()
    val isLR = Bool()
    val isSC = Bool()
    val rAtomics = Bool()
    val isPeriRead = Bool()
    val isPeriWrite = Bool()
  })
  operationWires := 0.U.asTypeOf(operationWires) 
  operationWires.valid := operationBuffer.valid
  operationWires.isRead := operationBuffer.core.instruction(6,0) === "b0000011".U
  operationWires.isWrite := operationBuffer.core.instruction(6,0) === "b0100011".U
  operationWires.rAtomics := operationBuffer.core.instruction(6,0) === "b0101111".U
  operationWires.isLR := operationBuffer.core.instruction(31,27) === "b00010".U && operationWires.rAtomics
  operationWires.isSC := operationBuffer.core.instruction(31,27) === "b00011".U && operationWires.rAtomics
  operationWires.isPeriRead := operationBuffer.core.instruction(6,0) === "b0000011".U && !isMainMemory(operationBuffer.address)
  operationWires.isPeriWrite := operationBuffer.core.instruction(6,0) === "b0100011".U && !isMainMemory(operationBuffer.address)

  switch(operationState){
    is(idleState){
      operationBuffer.writeData.valid:= false.B
      when(operationWires.valid){
        when(operationWires.isRead){

          inorderBuffer := operationBuffer
          operationBuffer.valid := false.B
        } .elsewhen(operationWires.isWrite){

          operationState := commitReadyState
        } .elsewhen(operationWires.isLR || operationWires.isSC || operationWires.rAtomics){

          inorderBuffer := operationBuffer
          inorderBuffer.writeData.valid := false.B
          operationState := waitState
        } .otherwise{

          operationBuffer.valid := false.B
        }
      }
    }
    is(commitReadyState){

      writeCommit.ready := true.B
      when(writeCommit.fired && writeDataIn.valid){
        inorderBuffer := operationBuffer
        inorderBuffer.writeData.data := writeDataIn.data
        inorderBuffer.writeData.valid := writeDataIn.valid
        operationBuffer.valid := false.B
        operationState := writeInstructionFiredState
      }.otherwise{
        operationState := Mux(writeCommit.fired, commitFiredState, commitReadyState)
      }
    }
    is(commitFiredState){
      when(writeDataIn.valid){

        inorderBuffer := operationBuffer
        inorderBuffer.writeData.data := writeDataIn.data
        inorderBuffer.writeData.valid := writeDataIn.valid
        operationBuffer.valid := false.B
        operationState := writeInstructionFiredState
      }
    }
    is(waitState){
      operationState := Mux(responseOut.valid && responseOut.instruction === operationBuffer.core.instruction, 
                                commitReadyState, waitState)
    }
    is(writeInstructionFiredState){
      operationState := Mux(writeInstuctionCommitFired, idleState, writeInstructionFiredState)
    }
  }

  val inorderBufferValidWire = WireDefault(inorderBuffer.valid && inorderBuffer.branch.valid)
  val speculativeBufferValidWire = WireDefault(speculativeBuffer.valid && speculativeBuffer.branch.valid)
  // val replayRequestValidWire = WireDefault(replayRequest.request.valid && replayRequest.request.branch.valid)

  //---------------------Request Dequeue---------------------//
  //* Priority Order
  //*    1.  Coherency
  //*    2.  Replay
  //*    3.  Inorder
  //*    4.  Speculative
  val atomicBusyState = RegInit(false.B)
  // Line address of the in-flight atomic (LR/SC/AMO). operationBuffer holds the
  // atomic for the whole read-pass -> commit -> write-pass window (waitState
  // keeps it valid), so this compare is stable while atomicBusyState is set.
  val snoopHitsAtomicLine =
    operationBuffer.address(addrWidth - 1, log2Ceil(lineSize)) ===
      coherencyRequest.request.address(addrWidth - 1, log2Ceil(lineSize))
  // LR/SC forward-progress guard (see cacheLookupUnit.reservationGuard): for
  // a bounded countdown after an LR, snoops on the reserved line are held
  // here — parked in the ACE unit's coherentRequestInState exactly like the
  // atomic-window deferral — so the owning hart's SC can complete before a
  // peer steals the line. Bounded by the countdown, so never a deadlock.
  val snoopHitsReservedLine = reservationGuard.active &&
    reservationGuard.address(addrWidth - 1, log2Ceil(lineSize)) ===
      coherencyRequest.request.address(addrWidth - 1, log2Ceil(lineSize))
  // Between an atomic's read pass and its post-commit write pass
  // (atomicBusyState && !inorderPending) only the atomic's line needs
  // protecting. Blocking ALL other dequeues here deadlocks: the atomic can
  // only commit after every older instruction, and an older speculative load
  // that has not dispatched yet needs this arbiter — while a blocked snoop
  // wedges the CCU (and with it every other hart). So the window keeps out
  // just (a) snoops on the atomic's own line and (b) further inorder ops;
  // speculative reads and different-line snoops flow (a read can't break
  // atomicity, a different line can't steal the reservation). Replay stays
  // blocked in this window, as before.
  when(toCacheLookup.ready) {
    when(atomicBusyState && !toCacheLookup.inorderPending && inorderBufferValidWire &&
         !(operationWires.isPeriRead || operationWires.isPeriWrite)){
      inorderBuffer.valid := false.B

      toCacheLookup.request := inorderBuffer
      requestTypeWire := "b01".U
      regReadUpdate(toCacheLookup.request.branch, branchOps, inorderBuffer.branch)

      atomicBusyState := false.B
    }.elsewhen(coherencyRequest.request.valid &&
               !(atomicBusyState && !toCacheLookup.inorderPending && snoopHitsAtomicLine) &&
               !snoopHitsReservedLine){
      coherencyRequest.ready := true.B

      toCacheLookup.request.valid := coherencyRequest.request.valid
      toCacheLookup.request.address := coherencyRequest.request.address
      toCacheLookup.request.cacheLine.response := coherencyRequest.request.response
      toCacheLookup.request.branch.valid := true.B
      requestTypeWire := "b11".U
    }.elsewhen(replayRequest.request.valid &&
               !(atomicBusyState && !toCacheLookup.inorderPending)){
      replayRequest.ready := true.B
      toCacheLookup.request := replayRequest.request
      requestTypeWire := "b10".U
      regReadUpdate(toCacheLookup.request.branch, branchOps, replayRequest.request.branch)

    } .elsewhen(inorderBufferValidWire && !atomicBusyState && !toCacheLookup.holdInOrder && !(operationWires.isPeriRead || operationWires.isPeriWrite)) {
      inorderBuffer.valid := false.B

      toCacheLookup.request := inorderBuffer
      requestTypeWire := "b01".U
      regReadUpdate(toCacheLookup.request.branch, branchOps, inorderBuffer.branch)
      
      atomicBusyState := Mux(operationWires.rAtomics && !inorderBuffer.writeData.valid, true.B, false.B)

    }.elsewhen(speculativeBufferValidWire) {
      speculativeBuffer.valid := false.B

      toCacheLookup.request := speculativeBuffer
      requestTypeWire := "b01".U
      regReadUpdate(toCacheLookup.request.branch, branchOps, speculativeBuffer.branch)

    }.otherwise{
      toCacheLookup.request.valid := false.B
      requestTypeWire := "b00".U
    }
    toCacheLookup.requestType := requestTypeWire
  }
  when(toPeripheral.ready && (operationWires.isPeriRead || operationWires.isPeriWrite) && inorderBufferValidWire) {
    inorderBuffer.valid := false.B
    toPeripheral.request := inorderBuffer

    regReadUpdate(toCacheLookup.request.branch, branchOps, inorderBuffer.branch)

  }
  fenceReady := (!speculativeBufferValidWire && !inorderBufferValidWire && !(operationBuffer.valid && operationBuffer.branch.valid) )

  //Resource optimization
  speculativeBuffer.cacheLine.cacheLine := 0.U
  speculativeBuffer.cacheLine.required := false.B
  speculativeBuffer.cacheLine.response := 0.U
  speculativeBuffer.writeData.data := 0.U
  speculativeBuffer.writeData.valid := false.B

  operationBuffer.cacheLine.cacheLine := 0.U
  operationBuffer.cacheLine.required := false.B
  operationBuffer.cacheLine.response := 0.U
  
  inorderBuffer.cacheLine.cacheLine := 0.U
  inorderBuffer.cacheLine.required := false.B
  inorderBuffer.cacheLine.response := 0.U
}