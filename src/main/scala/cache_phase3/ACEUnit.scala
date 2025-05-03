package cache_phase3

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import cache_phase3.constants._
import dataclass.data
import os.truncate
import cache_phase3.ChiselUtils._

class ACEUnit(
	dataWidth: Int,
  addrWidth: Int,
  id: Int,
  length: Int,
  size: Int,
) extends Module{
  val readRequest = IO(new Bundle {
    val ready = Output(Bool())
    val request = Input(new requestPipelineWire)
  })
  val readResponse = IO(new Bundle {
    val ready = Input(Bool())
    val request = Output(new requestPipelineWire)
  })
  val writeRequest = IO(new Bundle {
    val ready = Output(Bool())
    val request = Input(new writeBackWire)
  })
  val coherencyRequest = IO(new Bundle {
    val ready = Input(Bool())
    val request = Output(new coherencyRequestWire)
  })
  val coherencyResponse = IO(new Bundle {
    val ready = Output(Bool())
    val request = Input(new coherencyResponseWire)
  })
  val fenceReady = IO(Output(Bool()))
  val branchOps = IO(Input(new branchOps))

  val bus = IO(new ACE(
    idWidth = idWidth,
    addressWidth = addrWidth,
    busWidth = dPort_WIDTH, 
  ))
  val busWidth : Int = math.pow(2, dPort_SIZE).toInt * 8

  readRequest.ready := false.B
  zeroInit(readResponse.request)
  writeRequest.ready := false.B
  zeroInit(coherencyRequest.request)
  coherencyResponse.ready := false.B

  //AXI initializing
  bus.AWID := id.U
  bus.AWADDR := 0.U
  bus.AWLEN := 0.U
  bus.AWSIZE := 0.U
  bus.AWBURST := 0.U
  bus.AWLOCK := 0.U
  bus.AWCACHE := 0.U
  bus.AWPROT := 0.U
  bus.AWQOS := 0.U
  bus.AWVALID := false.B

  bus.WDATA := 0.U
  bus.WSTRB := 0.U
  bus.WLAST := false.B
  bus.WVALID := false.B

  bus.BREADY := false.B

  bus.ARID := id.U
  bus.ARADDR := 0.U
  bus.ARLEN := 0.U
  bus.ARSIZE := 0.U
  bus.ARBURST := 0.U
  bus.ARLOCK := 0.U
  bus.ARCACHE := 0.U
  bus.ARPROT := 0.U
  bus.ARQOS := 0.U
  bus.ARVALID := false.B

  bus.RREADY := false.B

  
  bus.AWDOMAIN := 0.U
  bus.AWSNOOP := 0.U
  bus.AWBAR := 0.U

  bus.ARDOMAIN := 0.U
  bus.ARSNOOP := 0.U
  bus.ARBAR := 0.U

  bus.ACREADY := false.B

  bus.CRVALID := false.B
  bus.CRRESP := 0.U

  bus.CDVALID := false.B
  bus.CDDATA := 0.U
  bus.CDLAST := false.B

  val readBuffer =  RegInit(0.U.asTypeOf(new requestPipelineWire))
  val responseBuffer = RegInit(0.U.asTypeOf(new requestPipelineWire))
    val ACEMSHR = Module(new fifoWithBranchOps(
    depth = schedulerDepth,
    traitType = new requestPipelineWire
  ))
  zeroInit(ACEMSHR.write.data)
  val writeBuffer = RegInit(0.U.asTypeOf(new writeBackWire))
  val coherencyRequestBuffer = RegInit(0.U.asTypeOf(new coherencyRequestWire))  
  val coherencyResponseBuffer = RegInit(0.U.asTypeOf(new coherencyResponseWire))

  //-----------------------Ordering--------------------------------//
  val isCoherencyIdle = WireDefault(false.B)
  val isReadRespBusy = WireDefault(false.B)
  val isWriteACEBusyWire = WireDefault(false.B)
  val isCoherencyAddressMatchWire = WireDefault(responseBuffer.address(addrWidth - 1, log2Ceil(lineSize)) === coherencyRequestBuffer.address(addrWidth - 1, log2Ceil(lineSize)))
  val isWriteAddressMatchWire = WireDefault(false.B)
  when(writeRequest.request.valid && !writeBuffer.valid){
    isWriteAddressMatchWire := writeRequest.request.address(addrWidth - 1, log2Ceil(lineSize)) === coherencyRequestBuffer.address(addrWidth - 1, log2Ceil(lineSize))
  } .elsewhen(writeBuffer.valid){
    isWriteAddressMatchWire  := writeBuffer.address(addrWidth - 1, log2Ceil(lineSize)) === coherencyRequestBuffer.address(addrWidth - 1, log2Ceil(lineSize))
  }

  //ReadBuffer operations
  readRequest.ready := !readBuffer.valid
  when(readRequest.request.valid && readRequest.request.branch.valid && readRequest.ready){
    readBuffer := readRequest.request
    regWriteUpdate(readBuffer.branch, branchOps, readRequest.request.branch)
  }
  when(readBuffer.valid){
    regRecordUpdate(readBuffer.branch, branchOps)
  }

  //ResponseBuffer operations
  readResponse.request := responseBuffer
  regReadUpdate(readResponse.request.branch, branchOps, responseBuffer.branch)

  //ACEMSHR operations
  ACEMSHR.read.ready := false.B
  ACEMSHR.branchOps := branchOps

  //WriteRequests operations
  writeRequest.ready := !writeBuffer.valid
  when(!writeBuffer.valid && writeRequest.request.valid){
    writeBuffer := writeRequest.request
  }

  //CoherentRequests operations
  coherencyRequest.request := coherencyRequestBuffer

  //COherentResponse operations
  coherencyResponse.ready := !coherencyResponseBuffer.valid

  //-----------------------AXI Write-------------------------------//
  val writeIdleState :: writeRequestState :: writeDataState :: writeResponseState :: Nil = Enum(4)

  val writeACEState = RegInit(writeIdleState)
  val writeCounter = Module(new moduleCounter(length))
  writeCounter.incrm := false.B
  writeCounter.reset := false.B
  isWriteACEBusyWire := writeRequest.request.valid && !writeBuffer.valid || writeBuffer.valid
  switch(writeACEState) {
    is(writeIdleState){
        writeCounter.reset := true.B
        writeACEState := Mux(writeBuffer.valid, writeRequestState, writeIdleState)
    }
    is(writeRequestState){
      bus.AWVALID := true.B
      bus.AWID := id.U
      bus.AWADDR := Cat(writeBuffer.address(addrWidth - 1, log2Ceil(lineSize)), 0.U(log2Ceil(lineSize).W))
      bus.AWLEN := length.U
      bus.AWSIZE := size.U
      bus.AWBURST := "b01".U
      bus.AWLOCK := "b0".U
      bus.AWCACHE := "b1111".U
      bus.AWPROT := dPort_PROT.U
      bus.AWQOS := "b0000".U

      bus.AWDOMAIN := "b10".U
      bus.AWSNOOP := "b011".U

      writeACEState := Mux(bus.AWREADY, writeDataState, writeRequestState)
    }
    is(writeDataState){

      bus.WVALID := true.B
      bus.WSTRB := Fill(busWidth/8, 1.U)
      bus.WLAST := writeCounter.count === length.U

      val numSlices = length + 1
      val writeChunks = VecInit(Seq.tabulate(numSlices)(i => 
        writeBuffer.data((i + 1) * busWidth - 1, i * busWidth)
      ))
      when(bus.WREADY){
        writeCounter.incrm := true.B 
      }
      bus.WDATA := writeChunks(writeCounter.count)
      writeACEState := Mux(bus.WLAST && bus.WREADY, writeResponseState, writeDataState)
    }
    is(writeResponseState){
      bus.BREADY := true.B
      writeBuffer.valid := !(bus.BVALID && bus.BID === id.U && bus.BRESP === "b00".U)

      writeACEState := Mux(bus.BVALID && (bus.BID === id.U), 
                        Mux(bus.BRESP === "b00".U, writeIdleState, writeRequestState),
                          writeResponseState)
    }
  }
  //-----------------------AXI ReadRequest--------------------------------//
  val readIdleState :: readRequestState :: Nil = Enum(2)
  val readACERequestState = RegInit(readIdleState)
  switch(readACERequestState) {
    is(readIdleState){
      readACERequestState := Mux(readBuffer.valid && isCoherencyIdle&& !isWriteACEBusyWire, readRequestState, readIdleState)
    }
    is(readRequestState){
      bus.ARVALID := true.B
      bus.ARID := id.U
      bus.ARADDR := Cat(readBuffer.address(addrWidth - 1, log2Ceil(lineSize)), 0.U(log2Ceil(lineSize).W))
      bus.ARLEN := length.U
      bus.ARSIZE := size.U
      bus.ARBURST := "b01".U
      bus.ARLOCK := "b0".U
      bus.ARCACHE := "b1111".U
      bus.ARPROT := dPort_PROT.U
      bus.ARQOS := "b0000".U

      bus.ARDOMAIN := "b10".U
      bus.ARSNOOP := "b0001".U
      switch(Cat(readBuffer.cacheLine.response)){
        is("b00".U){ bus.ARSNOOP := "b0001".U}  //ReadShared
        is("b01".U){ bus.ARSNOOP := "b0111".U}  //ReadUnique
        is("b11".U){ bus.ARSNOOP := "b1011".U}  //CleanUnique
      }
      bus.ARBAR := "b00".U

      when(bus.ARREADY){
        ACEMSHR.write.data := readBuffer
        regReadUpdate(ACEMSHR.write.data.branch, branchOps, readBuffer.branch)
        readBuffer.valid := false.B
      }
      readACERequestState := Mux(bus.ARREADY, readIdleState, readRequestState)
    }
  }
  //-----------------------AXI ReadResponse--------------------------------//
  val readDataInState:: readResponseState :: readDataOutState :: Nil = Enum(3)
  val readACEResponseState = RegInit(readDataInState)
  val readDataVec = RegInit(VecInit(Seq.fill(length+1)(0.U(busWidth.W))))
  val readResponseValid = RegInit(true.B)
  val readResponseReg = RegInit(0.U(2.W))
  val readCounter = Module(new moduleCounter(length))
  val wasCleanUniqueReg = RegInit(false.B)
  readCounter.incrm := false.B
  readCounter.reset := false.B
  switch(readACEResponseState) {
    is(readDataInState){
      readCounter.reset := true.B

      when(!ACEMSHR.isEmpty){
        ACEMSHR.read.ready := true.B
        responseBuffer := ACEMSHR.read.data
      }
      responseBuffer.valid := false.B
      
      readACEResponseState := Mux(ACEMSHR.read.data.valid && !ACEMSHR.isEmpty, readResponseState, readDataInState)
    }
    is(readResponseState){
      val isCleanUniqueWire = WireDefault(responseBuffer.cacheLine.response === "b11".U)
      wasCleanUniqueReg := isCleanUniqueWire
      bus.RREADY := true.B
      
      when(bus.RVALID && bus.RID === id.U){
        readCounter.incrm := true.B
        readDataVec(readCounter.count) := bus.RDATA 
        readResponseValid := Mux(bus.RRESP(1,0) === "b00".U, readResponseValid, false.B)
        readResponseReg :=  bus.RRESP(3,2) //Not checking for response validity in isShared and passDirty 
      }
      isReadRespBusy := (readCounter.count =/= 0.U) || bus.RVALID && bus.RID === id.U && readCounter.count === 0.U
      when(isCleanUniqueWire){
        wasCleanUniqueReg := Mux(readCounter.count === length.U, false.B, isCleanUniqueWire)
      }

      readACEResponseState := Mux(bus.RLAST && bus.RVALID && readResponseValid, readDataOutState, readResponseState)
    }
    is(readDataOutState){
      responseBuffer.valid := true.B
      responseBuffer.cacheLine.cacheLine := Cat(readDataVec.reverse)
      responseBuffer.cacheLine.response := readResponseReg
      responseBuffer.cacheLine.valid := !wasCleanUniqueReg
      when(responseBuffer.valid && readResponse.ready){
        responseBuffer.valid := false.B
      }

      isReadRespBusy := true.B

      readACEResponseState := Mux(readResponse.ready, readDataInState, readDataOutState)
    }
  } 
  regRecordUpdate(responseBuffer.branch, branchOps)

  //--------------------Coherent state----------------------------//
  //* Since a new coherent request comes after the previous request's response is released-
  //* -the request and response are kept in the same state machine without pipelining
  val coherentIdleState :: coherencyRequestWaitState :: coherentRequestInState :: coherentResponseState :: coherentDataOutState :: Nil = Enum(5)
  val responseValidReg = RegInit(false.B)
  val coherentAXIState = RegInit(coherentIdleState)
  val coherentCounter = Module(new moduleCounter(length))
  val toCoherentRequestInStateWire = WireDefault(isReadRespBusy && isCoherencyAddressMatchWire)
  val chooseFromWriteBufferWire = WireDefault(isWriteAddressMatchWire && isWriteACEBusyWire)

  isCoherencyIdle := (coherentAXIState === coherentIdleState)
  coherentCounter.incrm := false.B
  coherentCounter.reset := false.B
  switch(coherentAXIState){
    is(coherentIdleState){

      bus.ACREADY := true.B
      coherencyResponseBuffer.valid := false.B
      val coherencyReceived = bus.ACVALID && bus.ACPROT === dPort_PROT.U
      coherencyRequestBuffer.valid := false.B
      coherencyRequestBuffer.address := bus.ACADDR
      coherencyRequestBuffer.response := Cat(((bus.ACSNOOP === "b1001".U) || (bus.ACSNOOP === "b0111".U)), 
                                              ((bus.ACSNOOP === "b0001".U) || (bus.ACSNOOP === "b0111".U)))
      coherentAXIState := Mux(coherencyReceived , coherencyRequestWaitState, coherentIdleState)
    }
    is(coherencyRequestWaitState){
      when(chooseFromWriteBufferWire){
        coherencyResponseBuffer.valid := writeBuffer.valid
        coherencyResponseBuffer.address := writeBuffer.address
        coherencyResponseBuffer.cacheLine := writeBuffer.data
        coherencyResponseBuffer.response := "b01".U
      }.otherwise{
        coherencyRequestBuffer.valid := Mux(toCoherentRequestInStateWire, false.B, true.B)
      }

      when(chooseFromWriteBufferWire){
        coherentAXIState := Mux(writeBuffer.valid, coherentResponseState, coherencyRequestWaitState)
      }.otherwise{
        coherentAXIState := Mux(toCoherentRequestInStateWire, coherencyRequestWaitState, coherentRequestInState)
      }
    }
    is(coherentRequestInState){
      when(coherencyRequestBuffer.valid){
        coherencyRequestBuffer.valid := Mux(coherencyRequest.ready, false.B, coherencyRequestBuffer.valid)
      }
      coherencyResponseBuffer := coherencyResponse.request
      
      coherentAXIState := Mux(coherencyResponse.request.valid, coherentResponseState, coherentRequestInState)
    }
    is(coherentResponseState){
      bus.CRVALID := true.B

      coherentCounter.reset := true.B

      bus.CRRESP := Mux(coherencyResponseBuffer.valid, Cat(0.U(1.W), coherencyResponseBuffer.response(1), coherencyResponseBuffer.response(0) , 0.U(1.W), coherencyResponseBuffer.dataValid.asUInt),
                        0.U)
      when(bus.CRREADY){
        coherentAXIState := Mux(coherencyResponseBuffer.dataValid, coherentDataOutState, coherentIdleState)    
      } .otherwise{
        coherentAXIState := coherentResponseState
      }
    }
    is(coherentDataOutState){
      bus.CDVALID := true.B
      
      val numSlices = length + 1
      val writeChunks = VecInit(Seq.tabulate(numSlices)(i => 
        coherencyResponseBuffer.cacheLine((i + 1) * busWidth - 1, i * busWidth)
      ))
      when(bus.CDREADY){
        coherentCounter.incrm := true.B 
      }
      bus.CDDATA := writeChunks(coherentCounter.count)
      bus.CDLAST := coherentCounter.count === length.U

      coherentAXIState := Mux(bus.CDLAST && bus.CDREADY, coherentIdleState, coherentDataOutState)
    }
  }
  fenceReady := !readBuffer.valid && !responseBuffer.valid && ACEMSHR.isEmpty

  //Resource Utilization
  readBuffer.cacheLine.cacheLine := 0.U
  readBuffer.cacheLine.required := false.B
  // readBuffer.cacheLine.response := 0.U

  ACEMSHR.write.data.cacheLine.cacheLine := 0.U
  ACEMSHR.write.data.cacheLine.required := false.B
  // ACEMSHR.write.data.cacheLine.response := 0.U
}