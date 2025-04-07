package cache_phase3

import chisel3._ 
import chisel3.util._ 
import chisel3.experimental.BundleLiterals._
import cache_phase3.constants._
import cache_phase3.ChiselUtils._

//* A hollow load signal is sent to cacheLookup unit as a SC with writeDataValue.valid deasserted 
//* -To satisfy requirements of atomics
//* The rAtmoics situation cannot be optmizied with simple redesign with current work breakdown-
//* -First read response must come for write data to be released, hence core request pipeline stop
//* -Is the best way to follow
//? After testing
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
  val writeDataIn = IO(new writeDataIn)
  val writeCommit = IO(new composableInterface)
  val branchOps = IO(new branchOps)
  val responseOut = IO(Flipped(new responseOut))
  val fenceReady = IO(Output(Bool()))
  val writeInstuctionCommitFired = IO(Input(Bool()))

  //!Debug only
  val isPauseForBoolean = WireDefault(pauseForBranch.B)
  
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
  val coherencyRequestBuffer = RegInit(0.U.asTypeOf(new coherencyRequestWire))
  val replayRequestBuffer = RegInit(0.U.asTypeOf(new requestPipelineWire))
  
  val requestTypeWire = WireInit(0.U(arbiterReqTypesWidth.W))
  val speculativeBufferReadyWire = WireDefault(!speculativeBuffer.valid || (speculativeBuffer.valid && !speculativeBuffer.branch.valid))
  val operationBufferReadyWire = WireDefault((!operationBuffer.valid || (operationBuffer.valid && !operationBuffer.branch.valid)))
  val inorderBufferReadyWire = !inorderBuffer.valid || (inorderBuffer.valid && !inorderBuffer.branch.valid)

  request.speculativeReady := speculativeBufferReadyWire
  request.inorderReady :=  inorderBufferReadyWire && operationBufferReadyWire                        

  //---------------------Request Enqueue---------------------//
  when(request.request.valid && request.request.branch.valid){
    when(request.isSpeculative){
      speculativeBuffer:= request.request
    } .otherwise {   
      operationBuffer := request.request
    }
  } 
  when(!coherencyRequestBuffer.valid && coherencyRequest.request.valid){
    coherencyRequestBuffer := coherencyRequest.request
  }
  coherencyRequest.ready := !coherencyRequestBuffer.valid

  when(!replayRequestBuffer.valid && replayRequest.request.valid && replayRequest.request.branch.valid){
    replayRequestBuffer := replayRequest.request
  }
  replayRequest.ready := !replayRequestBuffer.valid

  //--------------------Operations State Machine-----------------//
  val idleState :: commitReadyState :: commitFiredState :: waitState :: hollowState :: writeInstructionFiredState :: Nil = Enum(6)
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
  operationWires.isPeriRead := operationBuffer.core.instruction(6,0) === "b0000011".U && operationBuffer.address === FIFO_ADDR_RX.U
  operationWires.isPeriWrite := operationBuffer.core.instruction(6,0) === "b0100011".U && operationBuffer.address === FIFO_ADDR_TX.U

  switch(operationState){
    is(idleState){
      operationBuffer.writeData.valid:= false.B
      when(operationWires.valid){
        when(operationWires.isRead){

          inorderBuffer := operationBuffer
          operationBuffer.valid := false.B
        } .elsewhen(operationWires.isWrite){

          operationState := commitReadyState
        } .elsewhen(operationWires.isLR){

          inorderBuffer := operationBuffer
          inorderBuffer.writeData.valid := false.B
          operationState := waitState
        } .elsewhen(operationWires.isSC){

          inorderBuffer := operationBuffer
          inorderBuffer.writeData.valid := false.B
          operationState := hollowState
        } .elsewhen(operationWires.rAtomics){

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
      operationState := Mux(writeCommit.fired, commitFiredState, commitReadyState)
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
    is(hollowState){ 
      operationState := Mux(responseOut.valid && responseOut.instruction === operationBuffer.core.instruction, 
                                commitReadyState, hollowState)
    }
    is(waitState){
      operationState := Mux(responseOut.valid && responseOut.instruction === operationBuffer.core.instruction, 
                                commitReadyState, waitState)
    }
    is(writeInstructionFiredState){
      operationState := Mux(writeInstuctionCommitFired, idleState, writeInstructionFiredState)
    }
  }

  when(requestTypeWire =/= 1.U){ 
    when(speculativeBuffer.valid){
      regRecordUpdate(speculativeBuffer.branch, branchOps)
    }
    when(operationBuffer.valid){
      regRecordUpdate(operationBuffer.branch, branchOps)
    }
    when(inorderBuffer.valid){
      regRecordUpdate(inorderBuffer.branch, branchOps)
    }
  }
  when(requestTypeWire =/= 2.U && replayRequestBuffer.valid){
    regRecordUpdate(replayRequestBuffer.branch, branchOps)
  }

  //---------------------Request Dequeue---------------------//
  //* Priority Order
  //*    1.  Coherency
  //*    2.  Replay
  //*    3.  Inorder
  //*    4.  Speculative
  val rAtmoicsWritePending = RegInit(false.B)
  when(toCacheLookup.ready && !(isPauseForBoolean && branchOps.valid)) {
    when(rAtmoicsWritePending){
      when(!toCacheLookup.holdInOrder && !(operationWires.isPeriRead || operationWires.isPeriWrite)){
        inorderBuffer.valid := false.B
        
        toCacheLookup.request := inorderBuffer
        requestTypeWire := "b01".U
        regReadUpdate(toCacheLookup.request.branch, branchOps, inorderBuffer.branch)
        
        rAtmoicsWritePending := false.B
      }
    }.elsewhen(coherencyRequestBuffer.valid) {
      coherencyRequestBuffer.valid := false.B

      toCacheLookup.request.valid := coherencyRequestBuffer.valid
      toCacheLookup.request.address := coherencyRequestBuffer.address
      toCacheLookup.request.cacheLine.response := coherencyRequestBuffer.response
      toCacheLookup.request.branch.valid := true.B
      requestTypeWire := "b11".U
    }.elsewhen(replayRequestBuffer.valid) {
      replayRequestBuffer.valid := false.B
      
      toCacheLookup.request := replayRequestBuffer
      requestTypeWire := "b10".U
      
      regReadUpdate(toCacheLookup.request.branch, branchOps, replayRequestBuffer.branch)
      
    }.elsewhen(inorderBuffer.valid && !toCacheLookup.holdInOrder && !(operationWires.isPeriRead || operationWires.isPeriWrite)) {
      inorderBuffer.valid := false.B

      toCacheLookup.request := inorderBuffer
      requestTypeWire := "b01".U
      regReadUpdate(toCacheLookup.request.branch, branchOps, inorderBuffer.branch)
      
      val isAtomicsWire = WireDefault((toCacheLookup.request.core.instruction(6,0) === "b0101111".U))
      // val isLRWire = WireDefault(toCacheLookup.request.core.instruction(31,27) === "b00010".U && isAtomicsWire)
      // val isSCWire = WireDefault(toCacheLookup.request.core.instruction(31,27) === "b00011".U && isAtomicsWire)
      val isAtmoicReadWire = WireDefault(isAtomicsWire && !toCacheLookup.request.writeData.valid)// && !(isSCWire || isLRWire))
      // val isLRReadWire = WireDefault(isLRWire && !toCacheLookup.request.writeData.valid)
      // val isSCReadWire = WireDefault(isSCWire && !toCacheLookup.request.writeData.valid)
      when(isAtmoicReadWire){
        rAtmoicsWritePending := true.B
      }
    }.elsewhen(speculativeBuffer.valid ) { //&& !(operationBuffer.valid && operationBuffer.address(addrWidth - 1, 3) === speculativeBuffer.address(addrWidth - 1, 3))) {
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
  when(toPeripheral.ready && (operationWires.isPeriRead || operationWires.isPeriWrite) && inorderBuffer.valid) {
    inorderBuffer.valid := false.B
    toPeripheral.request := inorderBuffer

    regReadUpdate(toCacheLookup.request.branch, branchOps, inorderBuffer.branch)

  }
  fenceReady := (!speculativeBuffer.valid && !inorderBuffer.valid && !operationBuffer.valid 
                  && !replayRequestBuffer.valid)
}