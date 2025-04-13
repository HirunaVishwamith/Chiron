package cache_phase3

import chisel3._ 
import chisel3.util._ 
import chisel3.experimental.BundleLiterals._
import cache_phase3.constants._
import cache_phase3.ChiselUtils._

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
  when(request.request.valid && request.request.branch.valid){
    when(request.isSpeculative){
      speculativeBuffer:= request.request
    } .otherwise {   
      operationBuffer := request.request
    }
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

  //---------------------Request Dequeue---------------------//
  //* Priority Order
  //*    1.  Coherency
  //*    2.  Replay
  //*    3.  Inorder
  //*    4.  Speculative
  val atomicBusyState = RegInit(false.B)
  when(toCacheLookup.ready) {
    when(atomicBusyState && !toCacheLookup.holdInOrder){
      when(inorderBuffer.valid && !(operationWires.isPeriRead || operationWires.isPeriWrite)){
        inorderBuffer.valid := false.B
        
        toCacheLookup.request := inorderBuffer
        requestTypeWire := "b01".U
        regReadUpdate(toCacheLookup.request.branch, branchOps, inorderBuffer.branch)
        
        atomicBusyState := false.B
      } 
    }.elsewhen(coherencyRequest.request.valid){
      coherencyRequest.ready := true.B

      toCacheLookup.request.valid := coherencyRequest.request.valid
      toCacheLookup.request.address := coherencyRequest.request.address
      toCacheLookup.request.cacheLine.response := coherencyRequest.request.response
      toCacheLookup.request.branch.valid := true.B
      requestTypeWire := "b11".U
    }.elsewhen(replayRequest.request.valid){
      replayRequest.ready := true.B
      toCacheLookup.request := replayRequest.request
      requestTypeWire := "b10".U

    } .elsewhen(inorderBuffer.valid && !toCacheLookup.holdInOrder && !(operationWires.isPeriRead || operationWires.isPeriWrite)) {
      inorderBuffer.valid := false.B

      toCacheLookup.request := inorderBuffer
      requestTypeWire := "b01".U
      regReadUpdate(toCacheLookup.request.branch, branchOps, inorderBuffer.branch)
      
      atomicBusyState := Mux(operationWires.rAtomics && !inorderBuffer.writeData.valid, true.B, false.B)

    }.elsewhen(speculativeBuffer.valid ) {
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
  fenceReady := (!speculativeBuffer.valid && !inorderBuffer.valid && !operationBuffer.valid )

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