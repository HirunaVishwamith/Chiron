package cache_phase3

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import cache_phase3.constants._
import cache_phase3._
import cache_phase3.ChiselUtils._

class peripheralUnit(
	dataWidth: Int,
  addrWidth: Int,
  id: Int,
  length: Int,
  size: Int,
) extends Module {
  val request = IO(new Bundle{
    val ready = Output(Bool())
    val request = Input(new requestPipelineWire)})
  val responseOut = IO(new Bundle {
    val ready = Input(Bool())
    val request = Output(new requestPipelineWire)})
  val bus = IO(new AXI(
    idWidth = 2,
    addressWidth = addrWidth,
    busWidth = peripheral_WIDTH, //32
  ))
  val writeInstructionCommit = IO(new composableInterface)
  val branchOps = IO(new branchOps)
  val busWidth : Int = math.pow(2, peripheral_SIZE).toInt * 8

  //IO initializing
  request.ready := false.B
  zeroInit(responseOut.request)

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

  //-----------------------Buffers----------------------------//
  val requestBuffer = RegInit(0.U.asTypeOf(new requestPipelineWire))
  val readRequestBuffer = RegInit(0.U.asTypeOf(new requestPipelineWire))
  val writeRequestBuffer = RegInit(0.U.asTypeOf(new requestPipelineWire))
  
  val responseOutBuffer = RegInit(0.U.asTypeOf(new requestPipelineWire))
  responseOut.request := responseOutBuffer
  
  val writeCommitInstructionBuffer = RegInit(false.B)
  writeInstructionCommit.ready := writeCommitInstructionBuffer
  when(writeInstructionCommit.fired){
    writeCommitInstructionBuffer := false.B
  }
  //-----------------------MSHR-------------------------------------------//
  val peripheralMSHR = Module(new fifoBaseModule(
    depth = schedulerDepth,
    traitType = new requestPipelineWire
  ))

  peripheralMSHR.read.ready := false.B
  zeroInit(peripheralMSHR.write.data)

  when(requestBuffer.valid && requestBuffer.branch.valid){
    request.ready := false.B
    when(!readRequestBuffer.valid && !requestBuffer.writeData.valid){
      
      readRequestBuffer := requestBuffer
      regWriteUpdate(readRequestBuffer.branch,branchOps,requestBuffer.branch)
      requestBuffer.valid := false.B
    }.elsewhen(!writeRequestBuffer.valid && requestBuffer.writeData.valid && requestBuffer.branch.valid){

      writeRequestBuffer := requestBuffer
      regWriteUpdate(writeRequestBuffer.branch,branchOps,requestBuffer.branch)
      requestBuffer.valid := false.B
    }
  } .otherwise {
    request.ready := !writeCommitInstructionBuffer
    when(request.request.valid && request.request.branch.valid){
      requestBuffer := request.request
    }
  }

  //-----------------------AXI Write-------------------------------//
  val writeIdleState :: writeRequestState :: writeResponseState :: Nil = Enum(3)

  val writeAXIState = RegInit(writeIdleState)
  val writeCounter = Module(new moduleCounter(length))
  writeCounter.incrm := false.B
  writeCounter.reset := false.B
  switch(writeAXIState) {
    is(writeIdleState){
        writeCounter.reset := true.B
        writeAXIState := Mux(writeRequestBuffer.valid && writeRequestBuffer.branch.valid, writeRequestState, writeIdleState)
    }
    is(writeRequestState){
      val sizeByIns = writeRequestBuffer.core.instruction(13,12)
      val sizePerBeat = (1.U << sizeByIns) * 8.U

      bus.AWVALID := true.B
      bus.AWID := id.U
      bus.AWADDR := writeRequestBuffer.address
      bus.AWLEN := ((sizePerBeat + busWidth.U - 1.U) / busWidth.U) - 1.U
      bus.AWSIZE := Mux(sizePerBeat <= busWidth.U, sizeByIns, Log2(busWidth.U / 8.U) )
      bus.AWBURST := "b01".U
      bus.AWLOCK := "b0".U
      bus.AWCACHE := "b0000".U
      bus.AWPROT := "b010".U
      bus.AWQOS := "b0000".U

      bus.WVALID := true.B
      bus.WSTRB := Fill(busWidth/8, 1.U)
      bus.WLAST := writeCounter.count === bus.ARLEN

      val numSlices = length + 1
      val writeChunks = VecInit(Seq.tabulate(numSlices)(i => 
        writeRequestBuffer.writeData.data((i + 1) * busWidth - 1, i * busWidth)
      ))
      when(bus.WREADY && bus.AWREADY){
        writeCounter.incrm := true.B 
      }
      bus.WDATA := writeChunks(writeCounter.count)
      writeAXIState := Mux(bus.WLAST && bus.WREADY && bus.AWREADY, writeResponseState, writeRequestState)
    }
    is(writeResponseState){
      bus.BREADY := true.B
      writeRequestBuffer.valid := !(bus.BVALID && bus.BID === id.U && bus.BRESP === "b00".U)
      writeCommitInstructionBuffer := true.B
      writeAXIState := Mux(bus.BVALID && (bus.BID === id.U), 
                        Mux(bus.BRESP === "b00".U, writeIdleState, writeRequestState),
                          writeResponseState)
    }
  }

  //-----------------------AXI ReadRequest--------------------------------//
  val readIdleState :: readRequestState :: Nil = Enum(2)
  val readAXIRequestState = RegInit(readIdleState)
  switch(readAXIRequestState) {
    is(readIdleState){
      readAXIRequestState := Mux(readRequestBuffer.valid && readRequestBuffer.branch.valid && peripheralMSHR.write.ready, readRequestState, readIdleState)
    }
    is(readRequestState){
      val sizeByIns = readRequestBuffer.core.instruction(13,12)
      val sizePerBeat = (1.U << sizeByIns) * 8.U

      bus.ARVALID := true.B
      bus.ARID := id.U
      bus.ARADDR := readRequestBuffer.address
      bus.ARLEN := ((sizePerBeat + busWidth.U - 1.U) / busWidth.U) - 1.U
      bus.ARSIZE := Mux(sizePerBeat <= busWidth.U, sizeByIns, Log2(busWidth.U / 8.U) )
      bus.ARBURST := "b01".U
      bus.ARLOCK := "b0".U
      bus.ARCACHE := "b0000".U
      bus.ARPROT := "b010".U
      bus.ARQOS := "b0000".U

      readRequestBuffer.valid := !bus.ARREADY

      when(bus.ARREADY){
        peripheralMSHR.write.data := readRequestBuffer
      }
      readAXIRequestState := Mux(bus.ARREADY, readIdleState, readRequestState)
    }
  }
    
  //-----------------------AXI ReadResponse--------------------------------//
  val readDataInState:: readResponseState :: readDataOutState :: Nil = Enum(3)
  val readAXIResponseState = RegInit(readDataInState)
  val readDataVec = RegInit(VecInit(Seq.fill(length+1)(0.U(busWidth.W))))
  val responseValid = RegInit(true.B)
  val readCounter = Module(new moduleCounter(length))
  readCounter.incrm := false.B
  readCounter.reset := false.B
  switch(readAXIResponseState){
    is(readDataInState){
      readCounter.reset := true.B
      
      when(!peripheralMSHR.isEmpty){
        peripheralMSHR.read.ready := true.B
        responseOutBuffer := peripheralMSHR.read.data
      }
      responseOutBuffer.valid := false.B
      readAXIResponseState := Mux(peripheralMSHR.read.data.valid && peripheralMSHR.read.data.branch.valid && !peripheralMSHR.isEmpty, readResponseState, readDataInState)
    }
    is(readResponseState){
      bus.RREADY := true.B
      when(bus.RVALID & bus.RID === id.U){
        readCounter.incrm := true.B
        readDataVec(readCounter.count) := bus.RDATA
        responseValid := Mux(bus.RRESP === "b00".U, responseValid, false.B)
      }
      // responseOutBuffer.valid := bus.RLAST && bus.RVALID && responseValid
      readAXIResponseState := Mux(bus.RLAST && bus.RVALID && responseValid, readDataOutState, readResponseState)
    }
    is(readDataOutState){
      val doubleWordChoosen = Cat(readDataVec.reverse)
      val shiftAmount = (1.U << responseOutBuffer.core.instruction(13,12).asUInt)
      val section = (1.U << (8.U*shiftAmount)) - 1.U 
      val byteChunks = VecInit(Seq.tabulate(8) { i =>
        doubleWordChoosen((i + 1) * 8 - 1, i * 8) // 8-bit slices
      })
      val byteChoosed     = byteChunks(0.U)
      val halfwordChoosed = Cat(byteChunks(1.U),byteChunks(0.U))
      val wordChoosed     = Cat(byteChunks(3.U),byteChunks(2.U), byteChunks(1.U),byteChunks(0.U))
      switch(responseOutBuffer.core.instruction(13, 12)){
        is("b00".U){responseOutBuffer.writeData.data := Mux(responseOutBuffer.core.instruction(14),byteChoosed,
                                      Cat(Fill((dataWidth-1*8),byteChoosed(7)),byteChoosed))}
        is("b01".U){responseOutBuffer.writeData.data := Mux(responseOutBuffer.core.instruction(14),halfwordChoosed,
                                      Cat(Fill((dataWidth-2*8),halfwordChoosed(15)),halfwordChoosed))}
        is("b10".U){responseOutBuffer.writeData.data := Mux(responseOutBuffer.core.instruction(14),wordChoosed,
                                      Cat(Fill((dataWidth-4*8),wordChoosed(31)),wordChoosed))}
        is("b11".U){responseOutBuffer.writeData.data := Mux(responseOutBuffer.core.instruction(14),"x0".U,
                                      doubleWordChoosen)}
      }
      responseOutBuffer.valid := true.B // !responseOut.ready
      when(responseOutBuffer.valid && responseOut.ready){
        responseOutBuffer.valid := false.B
      }
      readAXIResponseState := Mux(responseOut.ready && responseOutBuffer.valid, readDataInState, readDataOutState)
    }
  }
}
