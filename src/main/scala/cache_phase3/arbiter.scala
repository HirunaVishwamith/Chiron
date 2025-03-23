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
    val request = new request
    val isSpeculative = Input(Bool())
    val inorderReady = Output(Bool())
    val speculativeReady = Output(Bool())
  })
  val toPeripheral = IO(new Bundle {
    val ready = Input(Bool())
    val request = Output(new requestWithDataWire)
  })
  val toCacheLookup = IO(new Bundle {
    val ready = Input(Bool())
    val holdInOrder = Input(Bool())
    val request = Output(new cacheLookupWire)
  })
  val replayRequest = IO(new Bundle {
    val ready = Output(Bool())
    val request = Input(new replayWithCacheLineWire)
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
  
  request.speculativeReady := false.B
  request.inorderReady := false.B
  zeroInit(toPeripheral.request)
  zeroInit(toCacheLookup.request)
  replayRequest.ready := false.B
  coherencyRequest.ready := false.B
  writeCommit.ready := false.B
  fenceReady := false.B

  val speculativeBuffer = RegInit(0.U.asTypeOf(new requestWire))

  val inorderBuffer = RegInit(0.U.asTypeOf(new requestWithBranchInvalidWire))

  val operationBuffer = RegInit(0.U.asTypeOf(new requestWithBranchInvalidWire))

  val coherencyRequestBuffer = RegInit(0.U.asTypeOf(new coherencyRequestWire))

  val replayRequestBuffer = RegInit(0.U.asTypeOf(new replayWithCacheLineWire))

  request.speculativeReady := !speculativeBuffer.valid
  request.inorderReady := !(operationBuffer.valid || inorderBuffer.valid)

  //---------------------Request Enqueue---------------------//
  when(request.request.valid){
    when(request.isSpeculative){
      speculativeBuffer:= request.request
    } .otherwise {   
      operationBuffer.valid := request.request.valid
      operationBuffer.address := request.request.address
      operationBuffer.instruction := request.request.instruction
      operationBuffer.branchMask := request.request.branchMask
      operationBuffer.robAddr := request.request.robAddr
      operationBuffer.prfDest := request.request.prfDest
      operationBuffer.branchInvalid := false.B
      operationBuffer.writeEn := false.B
      operationBuffer.writeData := 0.U
    }
  } 
  when(!coherencyRequestBuffer.valid && coherencyRequest.request.valid){
    coherencyRequestBuffer := coherencyRequest.request
  }
  coherencyRequest.ready := !coherencyRequestBuffer.valid
  when(!replayRequestBuffer.valid && replayRequest.request.valid){
    replayRequestBuffer := replayRequest.request
  }
  replayRequest.ready := !replayRequestBuffer.valid

  //--------------------Operations State Machine-----------------//
  val idleState :: commitReadyState :: commitFiredState :: waitState :: hollowState :: Nil = Enum(5)
  val operationState = RegInit(idleState)

  val operationWires = Wire(new Bundle{
    val valid = Bool()
    val isRead = Bool()
    val isWrite = Bool()
    val isLR = Bool()
    val isSC = Bool()
    val rAtomics = Bool()
  })
  operationWires := 0.U.asTypeOf(operationWires) 
  operationWires.valid := operationBuffer.valid
  operationWires.isRead := operationBuffer.instruction(6,0) === "b0000011".U
  operationWires.isWrite := operationBuffer.instruction(6,0) === "b0100011".U
  operationWires.rAtomics := operationBuffer.instruction(6,0) === "b0101111".U
  operationWires.isLR := operationBuffer.instruction(31,27) === "b00010".U && operationWires.rAtomics
  operationWires.isSC := operationBuffer.instruction(31,27) === "b00011".U && operationWires.rAtomics

  switch(operationState){
    is(idleState){
      operationBuffer.writeEn:= false.B
      when(operationWires.valid){
        when(operationWires.isRead){
          inorderBuffer := operationBuffer
          operationBuffer.valid := false.B
        } .elsewhen(operationWires.isWrite){
          operationState := commitReadyState
        } .elsewhen(operationWires.isLR){
          inorderBuffer := operationBuffer
          operationState := waitState
        } .elsewhen(operationWires.isSC){
          inorderBuffer := operationBuffer
          inorderBuffer.writeEn := false.B
          operationState := hollowState
        } .elsewhen(operationWires.rAtomics){
          inorderBuffer := operationBuffer
          inorderBuffer.writeEn := false.B
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
        when(!operationWires.isLR){
          inorderBuffer := operationBuffer
        }
        inorderBuffer.writeData := writeDataIn.data
        inorderBuffer.writeEn := writeDataIn.valid
        operationBuffer.valid := false.B
        operationState := idleState
      }
    }
    is(hollowState){ 
      operationState := Mux(responseOut.valid && responseOut.instruction === operationBuffer.instruction, 
                                commitReadyState, hollowState)
    }
    is(waitState){
      operationState := Mux(responseOut.valid && responseOut.instruction === operationBuffer.instruction, 
                                commitReadyState, waitState)
    }

  }
  //TODO : When data not passed on
  branchUpdateWithinReg(speculativeBuffer, branchOps)
  branchUpdateWithinReg(replayRequestBuffer, branchOps)
  branchUpdateWithinReg(operationBuffer, branchOps)
  branchUpdateWithinReg(inorderBuffer, branchOps)

  //---------------------Request Dequeue---------------------//
  //* Priority Order
  //*    1.  Coherency
  //*    2.  Replay
  //*    3.  Inorder
  //*    4.  Speculative
  val rAtmoicsWritePending = RegInit(false.B)
  when(toCacheLookup.ready && !branchOps.valid) {
    when(rAtmoicsWritePending){
      when(!toCacheLookup.holdInOrder){
        toCacheLookup.request.valid := inorderBuffer.valid && inorderBuffer.branchInvalid
        toCacheLookup.request.address := inorderBuffer.address
        toCacheLookup.request.instruction := inorderBuffer.instruction
        toCacheLookup.request.branchMask := inorderBuffer.branchMask
        toCacheLookup.request.robAddr := inorderBuffer.robAddr
        toCacheLookup.request.prfDest := inorderBuffer.prfDest
        toCacheLookup.request.writeEn := inorderBuffer.writeEn
        toCacheLookup.request.writeData := inorderBuffer.writeData
        toCacheLookup.request.requestType := "b01".U

        when(branchOps.valid){
          outgoingBranchUpdateInvalidate(inorderBuffer, branchOps, toCacheLookup.request)
        }
        rAtmoicsWritePending := false.B
      } .otherwise{
        coherencyRequestBuffer.valid := false.B

        toCacheLookup.request.valid := coherencyRequestBuffer.valid
        toCacheLookup.request.address := coherencyRequestBuffer.address
        toCacheLookup.request.response := coherencyRequestBuffer.response
        toCacheLookup.request.requestType := "b11".U
      }
    }.elsewhen(coherencyRequestBuffer.valid) {
      coherencyRequestBuffer.valid := false.B

      toCacheLookup.request.valid := coherencyRequestBuffer.valid
      toCacheLookup.request.address := coherencyRequestBuffer.address
      toCacheLookup.request.response := coherencyRequestBuffer.response
      toCacheLookup.request.requestType := "b11".U
    }.elsewhen(replayRequestBuffer.valid) {
      replayRequestBuffer.valid := false.B
      
      toCacheLookup.request.valid := replayRequestBuffer.valid
      toCacheLookup.request.address := replayRequestBuffer.address
      toCacheLookup.request.instruction := replayRequestBuffer.instruction
      toCacheLookup.request.robAddr := replayRequestBuffer.robAddr
      toCacheLookup.request.prfDest := replayRequestBuffer.prfDest
      toCacheLookup.request.cacheLine := replayRequestBuffer.cacheLine
      toCacheLookup.request.response := replayRequestBuffer.response
      toCacheLookup.request.writeEn := replayRequestBuffer.writeEn
      toCacheLookup.request.writeData := replayRequestBuffer.writeData
      toCacheLookup.request.requestType := "b10".U
      
      when(branchOps.valid){
        outgoingBranchUpdateData(replayRequestBuffer, branchOps, toCacheLookup.request)
      }
    }.elsewhen(inorderBuffer.valid && !toCacheLookup.holdInOrder) {
      inorderBuffer.valid := false.B

      toCacheLookup.request.valid := inorderBuffer.valid && !inorderBuffer.branchInvalid
      toCacheLookup.request.address := inorderBuffer.address
      toCacheLookup.request.instruction := inorderBuffer.instruction
      toCacheLookup.request.branchMask := inorderBuffer.branchMask
      toCacheLookup.request.robAddr := inorderBuffer.robAddr
      toCacheLookup.request.prfDest := inorderBuffer.prfDest
      toCacheLookup.request.writeEn := inorderBuffer.writeEn
      toCacheLookup.request.writeData := inorderBuffer.writeData
      toCacheLookup.request.requestType := "b01".U

      when(branchOps.valid){
          outgoingBranchUpdateInvalidate(inorderBuffer, branchOps, toCacheLookup.request)
      }
      val isSCWire = WireDefault(toCacheLookup.request.instruction(31,27) === "b00011".U && (toCacheLookup.request.instruction(6,0) === "b0101111".U))
      val isSCReadWire = WireDefault(isSCWire && !toCacheLookup.request.writeEn)
      when(isSCReadWire){
        rAtmoicsWritePending := true.B
      }
    }.elsewhen(speculativeBuffer.valid) {
      speculativeBuffer.valid := false.B

      toCacheLookup.request.valid := speculativeBuffer.valid
      toCacheLookup.request.address := speculativeBuffer.address
      toCacheLookup.request.instruction := speculativeBuffer.instruction
      toCacheLookup.request.robAddr := speculativeBuffer.robAddr
      toCacheLookup.request.prfDest := speculativeBuffer.prfDest
      toCacheLookup.request.writeEn :=  false.B
      toCacheLookup.request.writeData :=  0.U
      toCacheLookup.request.requestType := "b01".U

      when(branchOps.valid){
        outgoingBranchUpdateData(speculativeBuffer, branchOps, toCacheLookup.request)
      }
    }.otherwise{
      toCacheLookup.request.valid := false.B
    }
  }
  when(toPeripheral.ready) {
    toPeripheral.request := inorderBuffer
    when(branchOps.valid){
        outgoingBranchUpdateInvalidate(inorderBuffer, branchOps, toCacheLookup.request)
    }
  }
  fenceReady := (!speculativeBuffer.valid && !inorderBuffer.valid && !operationBuffer.valid 
                  && !replayRequestBuffer.valid)
}