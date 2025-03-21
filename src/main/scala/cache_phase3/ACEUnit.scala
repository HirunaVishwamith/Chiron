package cache_phase3

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import cache_phase3.constants._
import dataclass.data
import cache_phase3.ChiselUtils.zeroInit

//TODO : For now only readUnique is used, rest will be added later
//TODO : Add seperate field for data received for clairty in readresponse
//TODO : Add branchOps for the readBuffer, responseBuffer, read, Reg and Write all cases

class ACEUnit(
	dataWidth: Int,
  addrWidth: Int,
  id: Int,
  length: Int,
  size: Int,
) extends Module{
  val readRequest = IO(new Bundle {
    val ready = Output(Bool())
    val request = Input(new requestWithDataWire)
  })
  val readResponse = IO(new Bundle {
    val ready = Input(Bool())
    val request = Output(new replayWithCacheLineWire)
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
    idWidth = 2,
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

  val readBuffer =  RegInit(0.U.asTypeOf(new replayWithBranchInvalidWire))
  readRequest.ready := !readBuffer.valid
  when(!readBuffer.valid){
    readBuffer.valid := readRequest.request.valid
    readBuffer.address := readRequest.request.address
    readBuffer.instruction := readRequest.request.instruction
    readBuffer.branchMask := readRequest.request.branchMask
    readBuffer.robAddr := readRequest.request.robAddr
    readBuffer.prfDest := readRequest.request.prfDest
    readBuffer.writeEn := readRequest.request.writeEn
    readBuffer.writeData := readRequest.request.writeData
  }
  val responseBuffer = RegInit(0.U.asTypeOf(new replayWithBranchInvalidWire))
  readResponse.request.valid := responseBuffer.valid && !responseBuffer.branchInvalid
  readResponse.request.address := responseBuffer.address
  readResponse.request.instruction := responseBuffer.instruction
  readResponse.request.branchMask := responseBuffer.branchMask
  readResponse.request.robAddr := responseBuffer.robAddr
  readResponse.request.prfDest := responseBuffer.prfDest
  readResponse.request.cacheLine := responseBuffer.cacheLine
  readResponse.request.response := responseBuffer.response
  readResponse.request.writeEn := responseBuffer.writeEn
  readResponse.request.writeData := responseBuffer.writeData

  val ACEMSHR = Module(new fifoWithBranchOpsII(
    depth = schedulerDepth,
    traitType = new replayWithBranchInvalidWire
  ))
  zeroInit(ACEMSHR.write.data)
  ACEMSHR.read.ready := false.B
  ACEMSHR.branchOps := branchOps

  //WriteRequests
  val writeBuffer = RegInit(0.U.asTypeOf(new writeBackWire))
  writeRequest.ready := !writeBuffer.valid
  writeBuffer := writeRequest.request


  //CoherentRequests
  val coherencyRequestBuffer = RegInit(0.U.asTypeOf(new coherencyRequestWire))  
  coherencyRequest.request := coherencyRequestBuffer

  val coherencyResponseBuffer = RegInit(0.U.asTypeOf(new coherencyResponseWire))
  coherencyResponse.ready := !coherencyResponseBuffer.valid
  coherencyResponseBuffer := coherencyResponse.request

  //-----------------------AXI Write-------------------------------//
  val writeIdleState :: writeRequestState :: writeResponseState :: Nil = Enum(3)

  val writeACEState = RegInit(writeIdleState)
  val writeCounter = Module(new moduleCounter(length))
  writeCounter.incrm := false.B
  writeCounter.reset := false.B
  switch(writeACEState) {
    is(writeIdleState){
        writeCounter.reset := true.B

        writeACEState := Mux(writeBuffer.valid, writeRequestState, writeIdleState)
    }
    is(writeRequestState){
      bus.AWVALID := true.B
      bus.AWID := id.U
      bus.AWADDR := writeBuffer.address(addrWidth - 1, log2Ceil(lineSize))
      bus.AWLEN := length.U
      bus.AWSIZE := size.U
      bus.AWBURST := "b01".U
      bus.AWLOCK := "b0".U
      bus.AWCACHE := "b1111".U
      bus.AWPROT := dPort_PROT.U
      bus.AWQOS := "b0000".U

      bus.AWDOMAIN := "b10".U
      bus.AWSNOOP := "b011".U

      bus.WVALID := true.B
      bus.WSTRB := Fill(busWidth/8, 1.U)
      bus.WLAST := writeCounter.count === length.U

      val numSlices = length + 1
      val writeChunks = VecInit(Seq.tabulate(numSlices)(i => 
        writeBuffer.data((i + 1) * busWidth - 1, i * busWidth)
      ))
      when(bus.WREADY && bus.AWREADY){
        bus.WDATA := writeChunks(writeCounter.count)
        writeCounter.incrm := true.B 
      }
      writeACEState := Mux(bus.WLAST && bus.WREADY && bus.AWREADY, writeResponseState, writeRequestState)
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
      readACERequestState := Mux(readBuffer.valid, readRequestState, readIdleState)
    }
    is(readRequestState){
      bus.ARVALID := true.B
      bus.ARID := id.U
      bus.ARADDR := Cat(readBuffer.address(addrWidth - 1, log2Ceil(lineSize*8)), 0.U(log2Ceil(lineSize*8).W))
      bus.ARLEN := length.U
      bus.ARSIZE := size.U
      bus.ARBURST := "b01".U
      bus.ARLOCK := "b0".U
      bus.ARCACHE := "b1111".U
      bus.ARPROT := dPort_PROT.U
      bus.ARQOS := "b0000".U

      bus.ARDOMAIN := "b10".U
      bus.ARSNOOP := "b0001".U    
      switch(Cat(readBuffer.response)){
        is("b00".U){ bus.ARSNOOP := "b0001".U}  //ReadShared
        is("b01".U){ bus.ARSNOOP := "b0111".U}  //ReadUnique
        is("b11".U){ bus.ARSNOOP := "b1011".U}  //CleanUnique
      }
      bus.ARSNOOP := "b0111".U //ReadUnique
      bus.ARBAR := "b00".U

      when(bus.ARREADY){
        ACEMSHR.write.data := readBuffer
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
  val readCounter = Module(new moduleCounter(length))
  readCounter.incrm := false.B
  readCounter.reset := false.B
  switch(readACEResponseState) {
    is(readDataInState){
      readCounter.reset := true.B

      ACEMSHR.read.ready := true.B
      responseBuffer.valid := false.B
      responseBuffer.prfDest := ACEMSHR.read.data.prfDest
      responseBuffer.robAddr := ACEMSHR.read.data.robAddr
      responseBuffer.cacheLine := 0.U
      responseBuffer.instruction := ACEMSHR.read.data.instruction
      responseBuffer.address := ACEMSHR.read.data.address
      responseBuffer.branchMask := ACEMSHR.read.data.branchMask
      responseBuffer.branchInvalid := ACEMSHR.read.data.branchInvalid
      
      readACEResponseState := Mux(ACEMSHR.read.data.valid, readResponseState, readDataInState)
    }
    is(readResponseState){
      bus.RREADY := true.B
      when(bus.RVALID & bus.RID === id.U){
        readCounter.incrm := true.B
        readDataVec(readCounter.count) := bus.RDATA 
        readResponseValid := Mux(bus.RRESP(1,0) === "b00".U, readResponseValid, false.B)
        responseBuffer.response := bus.RRESP(3,2) //Not checking for response validity in isShared and passDirty 
      }
      readACEResponseState := Mux(bus.RLAST && bus.RVALID && readResponseValid, readDataOutState, readResponseState)
    }
    is(readDataOutState){
      responseBuffer.valid := true.B
      responseBuffer.cacheLine := Cat(readDataVec.reverse)
      
      readACEResponseState := Mux(readResponse.ready, readDataInState, readDataOutState)
    }
  } 
  //See this
  //--------------------Coherent state----------------------------//
  //* Since a new coherent request comes after the previous request's response is released-
  //* -the request and response are kept in the same state machine without pipelining
  val coherentIdleState :: coherentRequestState :: coherentResponseState :: coherentDataOutState :: Nil = Enum(4)
  val responseValidReg = RegInit(false.B)
  val coherentAXIState = RegInit(coherentIdleState)
  val coherentCounter = Module(new moduleCounter(length))
  coherentCounter.incrm := false.B
  coherentCounter.reset := false.B
  switch(coherentAXIState){
    is(coherentIdleState){
      bus.ACREADY := true.B
      val coherencyReceived = bus.ACVALID && bus.ACPROT === dPort_PROT.U
      coherencyRequestBuffer.valid := coherencyReceived
      coherencyRequestBuffer.address := bus.ACADDR
      coherencyRequestBuffer.response := Cat(((bus.ACSNOOP === "b1001".U) || (bus.ACSNOOP === "b0111".U)), 
                                              ((bus.ACSNOOP === "b0001".U) || (bus.ACSNOOP === "b0111".U)))
      coherentAXIState := Mux(coherencyReceived , coherentRequestState, coherentIdleState)
    }
    is(coherentRequestState){
      coherencyResponse.ready := true.B
      when(coherencyResponse.request.valid) {
        coherencyResponseBuffer := coherencyResponse.request

        coherentAXIState := Mux(coherencyResponse.request.valid, coherentResponseState, coherentRequestState)
      }
    }
    is(coherentResponseState){
      bus.CRVALID := true.B

      coherentCounter.reset := true.B

      bus.CRRESP := Mux(coherencyResponseBuffer.valid, Cat(0.U(1.W), !coherencyResponseBuffer.response(0), !coherencyResponseBuffer.response(1) , 0.U(1.W), responseValidReg.asUInt),
                        0.U)
      when(bus.CRREADY){
        coherentAXIState := Mux(coherencyResponseBuffer.valid, coherentDataOutState, coherentIdleState)    
      } .otherwise{
        coherentAXIState := coherentResponseState
      }
    }
    is(coherentDataOutState){
      bus.CDVALID := true.B
      
      val numSlices = length + 1
      val writeChunks = VecInit(Seq.tabulate(numSlices)(i => 
        coherencyResponseBuffer.data((i + 1) * busWidth - 1, i * busWidth)
      ))
      when(bus.CDREADY){
        bus.CDDATA := writeChunks(coherentCounter.count)
        coherentCounter.incrm := true.B 
      }
      bus.CDLAST := coherentCounter.count === length.U
      coherencyResponseBuffer.valid := !(bus.CDLAST && bus.CDREADY)

      coherentAXIState := Mux(bus.CDLAST && bus.CDREADY, coherentIdleState, coherentDataOutState)
    }
  }
  fenceReady := !readBuffer.valid && !responseBuffer.valid && ACEMSHR.isEmpty
}