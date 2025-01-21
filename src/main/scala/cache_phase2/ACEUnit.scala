package DataCache
//Use namespaces if required

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import DataCache.constantsDCache._
import _root_.os.read.lines

class ACEUnit(
	dataWidth: Int,
  addrWidth: Int,
  id: Int,
  // busWidth: Int,
  length: Int,
  size: Int,
) extends Module {
  val request = IO(new requestACE(
    dataWidth = lineSize*8, 
    addrWidth = addrWidth,
  ))
  val loadData = IO(new loadData(
    dataWidth = lineSize*8
  ))
  val coherencyRequest = IO(new coherencyRequest)
  // val storeData = IO(new storeData)
  val bus = IO(new ACE(
    idWidth = 2,
    addressWidth = addrWidth,
    busWidth = dPort_WIDTH, 
  ))
  val setFence = IO(Input(Bool()))
  val coherencyReceived = IO(Output(Bool()))
  val isAtomicsInOperation =IO(Input(Bool()))
  val busWidth : Int = math.pow(2, dPort_SIZE).toInt * 8

  //IO initializing
  request.ready := false.B
  loadData.data := 0.U
  loadData.valid := false.B
  loadData.response := 0.U

  coherencyRequest.valid := false.B
  coherencyRequest.address := 0.U
  coherencyRequest.dataReq := false.B
  coherencyRequest.invalidateReq := false.B

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

  //-----------------------Intermidiate signals--------------------//
  val readComplete = WireDefault(false.B)
  val writeComplete = WireDefault(false.B)
  val coherentComplete = WireDefault(false.B)

  val coherencyReceivedWire = WireDefault(false.B)

  coherencyReceived := coherencyReceivedWire

  //-----------------------Input Buffer----------------------------//
  val inputBuffer = RegInit(new Bundle{
    val writeEn = Bool()
    val address = UInt(addrWidth.W)
    val isUnique = Bool()
    val isClean = Bool()
    val snoop = UInt(4.W)
  }.Lit(
    _.writeEn -> false.B,
    _.address -> 0.U,
    _.isUnique -> false.B,
    _.isClean -> false.B,
    _.snoop -> 0.U
  ))

  val fenceReadBarrierSend = RegInit(false.B)
  val fenceWriteBarrierSend = RegInit(false.B)

  val emptyState :: readState :: writeState :: coherentState :: fenceState :: Nil = Enum(5)
  val inputBufferState = RegInit(emptyState)
  switch(inputBufferState){
    is(emptyState){
      coherencyReceivedWire := bus.ACVALID && bus.ACPROT === dPort_PROT.U
      request.ready := true.B
      bus.ACREADY := coherencyRequest.ready && !setFence
      when(bus.ACVALID && bus.ACPROT === dPort_PROT.U){
        inputBuffer.address := bus.ACADDR
        inputBuffer.snoop := bus.ACSNOOP
        coherencyRequest.valid := bus.ACVALID
        coherencyRequest.address := bus.ACADDR
        coherencyRequest.invalidateReq := (bus.ACSNOOP === "b1001".U) || (bus.ACSNOOP === "b0111".U)
        coherencyRequest.dataReq := (bus.ACSNOOP === "b0001".U) || (bus.ACSNOOP === "b0111".U)
      } .elsewhen (request.valid){
        inputBuffer.writeEn := request.writeEn
        inputBuffer.address := request.address
        inputBuffer.isClean := request.isClean
        inputBuffer.isUnique := request.isUnique
      }
      when(setFence){
        inputBufferState := fenceState
      } .elsewhen(bus.ACVALID){
        when(isAtomicsInOperation){
          inputBufferState := Mux(request.valid, 
                              Mux(request.writeEn, writeState, readState),
                                emptyState)
        } .otherwise {
          inputBufferState := Mux(coherencyRequest.ready, coherentState, emptyState)
        }
      }.otherwise{
        inputBufferState := Mux(request.valid, 
                              Mux(request.writeEn, writeState, readState),
                                emptyState)
      }
    }
    is(readState){
      coherencyReceivedWire := bus.ACVALID && bus.ACPROT === dPort_PROT.U
      inputBufferState := Mux(readComplete, emptyState, readState)            
    }
    is(writeState){
      inputBufferState := Mux(writeComplete, emptyState, writeState)
    }
    is(coherentState){
      inputBufferState := Mux(coherentComplete, emptyState, coherentState)
    }
    is(fenceState){
      bus.AWID := id.U
      bus.AWADDR := 0.U
      bus.AWLEN := 0.U
      bus.AWSIZE := size.U
      bus.AWBURST := "b01".U
      bus.AWLOCK := "b0".U
      bus.AWCACHE := "b0011".U
      bus.AWPROT := dPort_PROT.U
      bus.AWQOS := "b0000".U
      bus.AWVALID := !fenceWriteBarrierSend
      bus.AWSNOOP := 0.U
      bus.AWDOMAIN := "b10".U
      bus.AWBAR := "b01".U

      bus.ARID := id.U
      bus.ARADDR := 0.U
      bus.ARLEN := 0.U
      bus.ARSIZE := size.U
      bus.ARBURST := "b01".U
      bus.ARLOCK := "b0".U
      bus.ARCACHE := "b0011".U
      bus.ARPROT := dPort_PROT.U
      bus.ARQOS := "b0000".U
      bus.ARVALID := !fenceReadBarrierSend
      bus.ARSNOOP := 0.U 
      bus.ARDOMAIN := "b10".U 
      bus.ARBAR := "b01".U 

      when(bus.ARREADY){
        fenceReadBarrierSend := true.B
      }
      when(bus.AWREADY){
        fenceWriteBarrierSend := true.B
      }

      inputBufferState := Mux(fenceReadBarrierSend && fenceWriteBarrierSend, emptyState, fenceState)
    }
  }

  //-----------------------Data Reg--------------------------------//
  val dataReg = RegInit(0.U((lineSize*8).W))
  dataReg := Mux(request.valid, request.data, dataReg)
  
  val responseReg = RegInit(0.U(2.W))


  //-----------------------AXI Controller--------------------------//
  val idleState :: requestState :: responseState :: dataOutState :: Nil = Enum(4)

  //-----------------------AXI Read--------------------------------//
  val readAXIState = RegInit(idleState)
  val readDataVec = RegInit(VecInit(Seq.fill(length+1)(0.U(busWidth.W))))
  val responseValid = RegInit(true.B)
  val readCounter = Module(new CounterModule(length))
  readCounter.incrm := false.B
  switch(readAXIState) {
    is(idleState){
      when(coherencyReceivedWire && !isAtomicsInOperation){
        readComplete := true.B
        readAXIState := idleState
      }
      readAXIState := Mux(inputBufferState === readState, requestState, idleState)
    }
    is(requestState){
      bus.ARVALID := true.B
      bus.ARID := id.U
      bus.ARADDR := inputBuffer.address
      bus.ARLEN := length.U
      bus.ARSIZE := size.U
      bus.ARBURST := "b01".U
      bus.ARLOCK := "b0".U
      bus.ARCACHE := "b1111".U
      bus.ARPROT := dPort_PROT.U
      bus.ARQOS := "b0000".U

      bus.ARDOMAIN := "b10".U
      bus.ARSNOOP := "b0001".U    //Default ReadShared
      switch(Cat(inputBuffer.isClean, inputBuffer.isUnique)){
        is("b00".U){ bus.ARSNOOP := "b0001".U}  //ReadShared
        is("b01".U){ bus.ARSNOOP := "b0111".U}  //ReadUnique
        is("b11".U){ bus.ARSNOOP := "b1011".U}  //CleanUnique
      }
      bus.ARBAR := "b00".U

      readCounter.reset := true.B
      when(coherencyReceivedWire){
        readAXIState := idleState
      } .otherwise{
        readAXIState := Mux(bus.ARREADY, responseState, requestState)
      }
    }
    is(responseState){
      bus.RREADY := true.B
      val isCleanUniqueWire = Cat(inputBuffer.isClean, inputBuffer.isUnique) === "b11".U
      when(isCleanUniqueWire){
        responseReg := bus.RRESP(3,2)
        responseValid := Mux(bus.RRESP(1,0) === "b00".U, responseValid, false.B)
      } .otherwise{
        when(bus.RVALID & bus.RID === id.U){
          readCounter.incrm := true.B
          readDataVec(readCounter.count) := bus.RDATA 
          responseValid := Mux(bus.RRESP(1,0) === "b00".U, responseValid, false.B)
          responseReg := bus.RRESP(3,2) //Not checking for response validity in isShared and passDirty 
        }
      }
      when(isCleanUniqueWire){
        when(coherencyReceivedWire){
          readAXIState := idleState
        } .otherwise {
          readAXIState := Mux(bus.RVALID & bus.RID === id.U, 
                          Mux(responseValid, dataOutState, requestState),
                            responseState)
        }
        
      } .otherwise{
        when(coherencyReceivedWire){
          readAXIState := idleState
        } .otherwise {
          readAXIState := Mux(bus.RLAST && bus.RVALID,
                          Mux(responseValid, dataOutState, requestState),
                            responseState)
        }
        
      }
      
    }
    is(dataOutState){
      when(coherencyReceivedWire && !isAtomicsInOperation){
        readComplete := true.B
        readAXIState := idleState
      }.otherwise{
        val isCleanUniqueWire = Cat(inputBuffer.isClean, inputBuffer.isUnique) === "b11".U
        readComplete := loadData.ready
        loadData.valid := true.B
        loadData.data := Mux(isCleanUniqueWire, 0.U, Cat(readDataVec.reverse))
        loadData.response := responseReg
        readAXIState := Mux(loadData.ready, idleState, dataOutState)
      }
    }
  }

  //-----------------------AXI Write-------------------------------//
  val writeAXIState = RegInit(idleState)
  val writeCounter = Module(new CounterModule(length))
  writeCounter.incrm := false.B
  switch(writeAXIState) {
    is(idleState){
        writeAXIState := Mux(inputBufferState === writeState, requestState, idleState)
    }
    is(requestState){
      bus.AWVALID := true.B
      bus.AWID := id.U
      bus.AWADDR := inputBuffer.address
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
        dataReg((i + 1) * busWidth - 1, i * busWidth)
      ))
      when(bus.WREADY){
        bus.WDATA := writeChunks(writeCounter.count)
        writeCounter.incrm := true.B 
      }

      writeAXIState := Mux(bus.WLAST, responseState, requestState)
    }
    is(responseState){
      bus.BREADY := true.B
      writeComplete := bus.BVALID && bus.BID === id.U && bus.BRESP === "b00".U

      writeAXIState := Mux(bus.BVALID && (bus.BID === id.U), 
                        Mux(bus.BRESP === "b00".U, idleState, requestState),
                          responseState)
    }
    is(dataOutState){
      writeAXIState := idleState
    }
  }

  //--------------------Coherent state----------------------------//
  val responseValidReg = RegInit(false.B)
  val coherentAXIState = RegInit(idleState)
  val coherentCounter = Module(new CounterModule(length))
  coherentCounter.incrm := false.B
  switch(coherentAXIState){
    is(idleState){
      coherencyRequest.valid := inputBufferState === coherentState
      coherencyRequest.address := inputBuffer.address
      coherencyRequest.invalidateReq := (inputBuffer.snoop === "b1001".U) || (inputBuffer.snoop === "b0111".U)
      coherencyRequest.dataReq := (inputBuffer.snoop === "b0001".U) || (inputBuffer.snoop === "b0111".U)

      coherentAXIState := Mux(coherencyRequest.accepted , requestState, idleState)
    }
    is(requestState){
      request.ready := true.B
      when(request.valid) {
        responseValidReg := request.writeEn
        dataReg := request.data
        responseReg := Cat(request.isUnique, request.isClean)
      }

      coherentAXIState := Mux(request.valid, responseState, requestState)
    }
    is(responseState){
      bus.CRVALID := true.B
      bus.CRRESP := Mux(responseValidReg, Cat(0.U(1.W), !responseReg(1), !responseReg(0), 0.U(1.W), responseValidReg.asUInt),
                        0.U)
      when(bus.CRREADY){
        coherentComplete := !responseValidReg
        coherentAXIState := Mux(responseValidReg, dataOutState, idleState)    
      } .otherwise{
        coherentAXIState := responseState
      }
    }
    is(dataOutState){
      bus.CDVALID := true.B
      
      val numSlices = length + 1
      val writeChunks = VecInit(Seq.tabulate(numSlices)(i => 
        dataReg((i + 1) * busWidth - 1, i * busWidth)
      ))

      when(bus.CDREADY){
        bus.CDDATA := writeChunks(coherentCounter.count)
        coherentCounter.incrm := true.B 
      }
      bus.CDLAST := coherentCounter.count === length.U

      coherentComplete := bus.CDLAST && bus.CDREADY

      coherentAXIState := Mux(bus.CDLAST && bus.CDREADY, idleState, dataOutState)
    }
  }

}
