
package DataCache
//Use namespaces if required

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import DataCache.constantsDCache._


// Change the module name
class CacheModule (
  peripheral_id : Int,
  dPort_id : Int
)extends Module {
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

  //IO initalizing
  canAllocate := false.B  
  
  responseOut.valid := false.B
  responseOut.prfDest := 0.U
  responseOut.robAddr := 0.U
  responseOut.result := 0.U
  responseOut.instruction := 0.U

  fenceInstructions.ready := false.B
  writeCommit.ready := false.B

  val requestScheduler = Module(new Scheduler)
  val cacheLookup = Module(new cacheLookup)
  val peripheralAXIUnit = Module(new AXIUnit(
    dataWidth = dataWidth,
    addrWidth = addrWidth,
    id = peripheral_id,
    // busWidth = peripheral_WIDTH,
    length = peripheral_LEN,
    size = peripheral_SIZE
  ))
  val writeDataInFifo = Module(new writeDataInFifo)
  val memoryAXIUnit = Module(new ACEUnit(
    dataWidth = dataWidth,
    addrWidth = addrWidth,
    id = dPort_id,
    // busWidth = dPort_WIDTH,
    length = dPort_LEN,
    size = dPort_SIZE
  ))

  requestScheduler.canAccept := false.B
  writeDataInFifo.canAccept := false.B

  cacheLookup.storeDataIn.data := 0.U
  cacheLookup.storeDataIn.valid := false.B

  cacheLookup.responseOut.ready := false.B
  cacheLookup.cacheRequest.writeEn := false.B
  cacheLookup.cacheRequest.instruction := 0.U
  cacheLookup.cacheRequest.valid := false.B
  cacheLookup.cacheRequest.address := 0.U
  cacheLookup.branchFail := false.B


  peripheralAXIUnit.request.valid := false.B
  peripheralAXIUnit.request.writeEn := false.B
  peripheralAXIUnit.request.address := 0.U
  peripheralAXIUnit.request.data := 0.U
  peripheralAXIUnit.request.instruction := 0.U
  peripheralAXIUnit.loadData.ready := false.B

  requestScheduler.requestIn <> request
  requestScheduler.branchOps <> branchOps
  canAllocate := requestScheduler.canAllocate
  //-----------------Input buffer---------------//
  val empty :: awaitRead :: awaitWrite :: full :: Nil = Enum(4)
  
  val inputBufferState = RegInit(empty)
  val inputBuffer = RegInit(new Bundle{
    val address = UInt(addrWidth.W)
    val prfDest = UInt(prfAddrWidth.W)
    val robAddr = UInt(robAddrWidth.W)
    val branchMask = UInt(branchMaskWidth.W)
    val result = UInt(dataWidth.W)
    val instruction = UInt(insWidth.W)    
  }.Lit(
    _.address -> 0.U,
    _.prfDest -> 0.U,
    _.robAddr -> 0.U,
    _.branchMask -> 0.U,
    _.result -> 0.U,
    _.instruction -> 0.U
  ))

  //------------BranchOps------------------//
  val branchMaskWire = WireInit(0.U(branchMaskWidth.W))
  val branchPassWire = WireInit(false.B)
  val branchValidWire = WireInit(false.B)

  val isFlushed = RegInit(false.B)
  val isFlushedWire = WireInit(false.B)

  isFlushedWire := isFlushed

  when(branchOps.valid){
    branchValidWire := true.B
    branchMaskWire := branchOps.branchMask
    branchPassWire := branchOps.passed
  }

  when(isFlushed){
    isFlushed := Mux(inputBufferState === empty, false.B, isFlushed)
  } .otherwise{
    when(branchValidWire && (inputBufferState =/= empty)){
      when(branchPassWire && (inputBuffer.branchMask & branchMaskWire).orR){
        inputBuffer.branchMask := inputBuffer.branchMask ^ branchMaskWire
        isFlushed := false.B
        isFlushedWire := false.B
      } .otherwise{
        isFlushed := (inputBuffer.branchMask & branchMaskWire).orR
        isFlushedWire := (inputBuffer.branchMask & branchMaskWire).orR
      }
    }
  }

  //R type atomics for now
  val isLoadWire = WireDefault(false.B)
  val isAtomicsWire = WireDefault(false.B)
  val isLoadReservedWire = WireDefault(false.B)
  val isStoreConditionalWire = WireDefault(false.B)
  val isInsValid = WireDefault(false.B)

  val requestAccepted = RegInit(false.B)
  when(inputBufferState =/= awaitWrite){
    requestAccepted := false.B
  }
  val coherencyInterrupted = RegInit(false.B)
  when(inputBufferState =/= awaitWrite){
    coherencyInterrupted := false.B
  }
  val gotFired = RegInit(false.B)
  when(inputBufferState =/= awaitWrite){
  gotFired := false.B
  }
  val storeDataReg = RegInit(0.U(dataWidth.W))

  when(inputBufferState =/= empty){
    isLoadWire := inputBuffer.instruction(6,3) === "b0000".U
    isAtomicsWire := inputBuffer.instruction(6,2) === "b01011".U     
    isLoadReservedWire := (isAtomicsWire && inputBuffer.instruction(31,27) === "b00010".U)
    isStoreConditionalWire := (isAtomicsWire && inputBuffer.instruction(31,27) === "b00011".U)
    isInsValid := inputBuffer.instruction(1,0) === "b11".U
  } .elsewhen(requestScheduler.requestOut.valid){
    isLoadWire := requestScheduler.requestOut.instruction(6,3) === "b0000".U
    isAtomicsWire := requestScheduler.requestOut.instruction(6,2) === "b01011".U     
    isLoadReservedWire := (isAtomicsWire && requestScheduler.requestOut.instruction(31,27) === "b00010".U)
    isStoreConditionalWire := (isAtomicsWire && requestScheduler.requestOut.instruction(31,27) === "b00011".U)
    isInsValid := requestScheduler.requestOut.instruction(1,0) === "b11".U
  } 

  switch(inputBufferState){
    is(empty){
      requestScheduler.canAccept := true.B
      when(requestScheduler.requestOut.valid){
        inputBuffer.address := requestScheduler.requestOut.address
        inputBuffer.instruction := requestScheduler.requestOut.instruction
        inputBuffer.robAddr := requestScheduler.requestOut.robAddr
        inputBuffer.prfDest := requestScheduler.requestOut.prfDest

        when(branchValidWire){
          when(branchPassWire){
            inputBuffer.branchMask := requestScheduler.requestOut.branchMask ^ branchMaskWire
          }.otherwise{
            inputBuffer.branchMask := requestScheduler.requestOut.branchMask
            isFlushed := (requestScheduler.requestOut.branchMask & branchMaskWire).orR
          }
        }.otherwise{
          inputBuffer.branchMask := requestScheduler.requestOut.branchMask
        }
      }
      val toAwaitRead = Mux(requestScheduler.requestOut.valid, 
                            ((isLoadWire || isLoadReservedWire || (isAtomicsWire && !isStoreConditionalWire)) && isInsValid ), 
                              false.B)

      when(requestScheduler.requestOut.valid){
        inputBufferState := Mux(toAwaitRead, awaitRead, awaitWrite)
      } .otherwise {
        inputBufferState := empty
      }
                             
    }
    is(awaitRead){
      val peripheralRead = WireInit(inputBuffer.address === FIFO_ADDR_RX.U)

      when(peripheralRead){
        peripheralAXIUnit.loadData.ready := true.B
        when(peripheralAXIUnit.request.ready && !inputBuffer.branchMask(3,0).orR){
          peripheralAXIUnit.request.valid := true.B
          peripheralAXIUnit.request.writeEn := false.B
          peripheralAXIUnit.request.address := inputBuffer.address
          peripheralAXIUnit.request.data := 0.U
          peripheralAXIUnit.request.instruction := inputBuffer.instruction
        }
      } otherwise{
        //Cache will take data when ready, can wait as next state is when data is ready 
        when((!isAtomicsWire || (isAtomicsWire && !inputBuffer.branchMask(3,0).orR))){
          cacheLookup.responseOut.ready := true.B

          cacheLookup.cacheRequest.valid := true.B
          cacheLookup.cacheRequest.writeEn := false.B
          cacheLookup.cacheRequest.address := inputBuffer.address
          cacheLookup.cacheRequest.instruction := inputBuffer.instruction
          cacheLookup.storeDataIn.data := 0.U
          cacheLookup.storeDataIn.valid := false.B
        }
        //Pass the branch fail results in
        when(!isAtomicsWire){
          cacheLookup.branchFail := isFlushedWire
        }
      }

      when(peripheralRead){
        inputBuffer.result := Mux(peripheralAXIUnit.loadData.valid, peripheralAXIUnit.loadData.data, inputBuffer.result)
      } .otherwise{
        inputBuffer.result := Mux(cacheLookup.responseOut.valid, cacheLookup.responseOut.data, inputBuffer.result)
      }

      when(peripheralRead){
        when(isFlushedWire){
          inputBufferState := empty
        }.otherwise{
          inputBufferState := Mux(peripheralAXIUnit.loadData.valid, full, awaitRead)
        }
      }.otherwise{
        when(isFlushedWire){
          inputBufferState := empty
        }.otherwise{
          inputBufferState := Mux(cacheLookup.responseOut.valid, full, awaitRead)
        }
      }
    }
    is(awaitWrite){
      val peripheralWrite = WireInit(inputBuffer.address === FIFO_ADDR_TX.U)
      when(peripheralWrite){
        writeDataInFifo.canAccept := !requestAccepted
        //Wait till branch is resolved for peripheral write
        when(peripheralAXIUnit.request.ready  && !inputBuffer.branchMask(3,0).orR && writeDataInFifo.dataOut.valid && !requestAccepted){
          
          peripheralAXIUnit.request.valid := true.B
          peripheralAXIUnit.request.writeEn := true.B
          peripheralAXIUnit.request.address := inputBuffer.address
          peripheralAXIUnit.request.data :=  writeDataInFifo.dataOut.data
          peripheralAXIUnit.request.instruction := inputBuffer.instruction

          requestAccepted := Mux(peripheralAXIUnit.request.ready && !requestAccepted, true.B, requestAccepted)
        } 
      } .otherwise{
        //Wait till branch is resolved for memory store
        when(!coherencyInterrupted){
          coherencyInterrupted := memoryAXIUnit.coherencyReceived
        }
        when(memoryAXIUnit.coherencyReceived){
          requestAccepted := false.B
        }
        when(!inputBuffer.branchMask(3,0).orR  && memoryAXIUnit.request.ready && writeDataInFifo.dataOut.valid && !requestAccepted){
          cacheLookup.responseOut.ready := isStoreConditionalWire

          writeDataInFifo.canAccept := Mux(coherencyInterrupted, false.B, !cacheLookup.cacheRequest.accepted)

          cacheLookup.cacheRequest.valid := !cacheLookup.cacheRequest.accepted
          cacheLookup.cacheRequest.writeEn := true.B
          cacheLookup.cacheRequest.address := inputBuffer.address
          cacheLookup.cacheRequest.instruction := inputBuffer.instruction

          cacheLookup.storeDataIn.data := Mux(coherencyInterrupted, storeDataReg, writeDataInFifo.dataOut.data)
          cacheLookup.storeDataIn.valid := !cacheLookup.cacheRequest.accepted

          when(!coherencyInterrupted){
            storeDataReg := writeDataInFifo.dataOut.data
          }

          when(memoryAXIUnit.coherencyReceived){
            requestAccepted := false.B
          } .otherwise{
            requestAccepted := Mux(cacheLookup.cacheRequest.valid && !requestAccepted, true.B, requestAccepted)
          }
        }
      }
      when(peripheralWrite){
        writeCommit.ready := !gotFired//requestAccepted & peripheralAXIUnit.request.ready
        gotFired := Mux(writeCommit.fired && !gotFired, true.B, gotFired)
      } .otherwise{
        writeCommit.ready := !gotFired//requestAccepted & cacheLookup.cacheRequest.ready
        gotFired := Mux(writeCommit.fired && !gotFired, true.B, gotFired)
      }

      when(isStoreConditionalWire){
        cacheLookup.responseOut.ready := true.B
        when(peripheralWrite){
          //Although it is facilitated, peripheral should not be handling SCs.
          inputBuffer.result := Mux(peripheralAXIUnit.loadData.valid, peripheralAXIUnit.loadData.data, inputBuffer.result)
        }.otherwise{
          inputBuffer.result := Mux(cacheLookup.responseOut.valid, cacheLookup.responseOut.data, inputBuffer.result)
        }
      } otherwise{
        inputBuffer.result := 0.U
      }
      
      when(isFlushedWire){
        inputBufferState := empty
      } .otherwise{
        when(gotFired){//writeCommit.fired){
          when(requestAccepted){ //&& (peripheralAXIUnit.request.ready || cacheLookup.cacheRequest.ready)){
            when(peripheralWrite){
              when(peripheralAXIUnit.request.ready){
                inputBufferState:= Mux(isStoreConditionalWire, full, empty)
              }
            } .otherwise{
              when(cacheLookup.cacheRequest.ready){
                inputBufferState:= Mux(isStoreConditionalWire, full, empty)
              }
            }
            // inputBufferState:= Mux(isStoreConditionalWire, full, empty)
          }
        } .otherwise{
          inputBufferState := awaitWrite
        }
      }
    }
    is(full){
      when(isFlushedWire){
        responseOut.valid := false.B
      } otherwise{
        responseOut.valid := ((isLoadWire || isAtomicsWire) && isInsValid )
      }
      responseOut.prfDest := inputBuffer.prfDest 
      responseOut.robAddr := inputBuffer.robAddr 
      responseOut.result := inputBuffer.result 
      responseOut.instruction := inputBuffer.instruction 

      inputBufferState := Mux(isAtomicsWire && !(isLoadReservedWire || isStoreConditionalWire), awaitWrite, empty)
    }
  }

  //-----------------WriteDataIn fifo--------------------//
  writeDataInFifo.dataIn <> writeDataIn

  memoryAXIUnit.isAtomicsInOperation := cacheLookup.isAtomicsInOperation
  cacheLookup.coherencyReceived := memoryAXIUnit.coherencyReceived
  memoryAXIUnit.request <> cacheLookup.memoryRequest
  memoryAXIUnit.loadData <>cacheLookup.loadData
  memoryAXIUnit.coherencyRequest <> cacheLookup.coherencyRequest
  memoryAXIUnit.setFence := false.B
  dPort <> memoryAXIUnit.bus
  peripheral <> peripheralAXIUnit.bus
  cacheLookup.loadCommit <> loadCommit

  //-----------------Initiate Fence----------------------//
  val fenceInititatedReg = RegInit(false.B)
  when(initiateFence && requestScheduler.isEmpty && writeDataInFifo.isEmpty && inputBufferState === empty){
    fenceInititatedReg := true.B
  }
  when(fenceInititatedReg){
    fenceInstructions.ready := true.B
    canAllocate := false.B
    fenceInititatedReg := Mux(fenceInstructions.fired, false.B, true.B)
    memoryAXIUnit.setFence := fenceInstructions.fired
  }
}


// To generate the verilog hardware
// Change the module name as required
object CacheModuleMain extends App {
  println("Generating the CacheModule hardware")
  //Hardware files will be out into generated
  emitVerilog(new CacheModule(peripheral_id = 0, dPort_id = 0), Array("--target-dir", "generated"))
}
