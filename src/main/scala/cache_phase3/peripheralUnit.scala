package cache_phase3

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import cache_phase3.constants._
import cache_phase3._
import cache_phase3.ChiselUtils.zeroInit
import cache_phase3.ChiselUtils.cacheBranchWriteUpdate

class peripheralUnit(
	dataWidth: Int,
  addrWidth: Int,
  id: Int,
  length: Int,
  size: Int,
) extends Module {
  val request = IO(new Bundle{
    val ready = Output(Bool())
    val request = Input(new requestWithDataWire)})
  val responseOut = IO(new Bundle {
    val ready = Input(Bool())
    val request = Output(new responseOutWithAddrWire)})
  val bus = IO(new AXI(
    idWidth = 2,
    addressWidth = addrWidth,
    busWidth = peripheral_WIDTH, //32
  ))
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
  val requestBuffer = RegInit(0.U.asTypeOf(new requestWithDataWire))
  val readRequestBuffer = RegInit(0.U.asTypeOf(new requestWithDataWire))
  val writeRequestBuffer = RegInit(0.U.asTypeOf(new requestWithDataWire))

  val responseOutBuffer = RegInit(0.U.asTypeOf(new responseOut))
  responseOut.request.valid := responseOutBuffer.valid
  responseOut.request.prfDest := responseOutBuffer.prfDest
  responseOut.request.robAddr := responseOutBuffer.robAddr
  responseOut.request.result := responseOutBuffer.result
  responseOut.request.instruction := responseOutBuffer.instruction

  //-----------------------MSHR-------------------------------------------//
  val peripheralMSHR = Module(new fifoBaseModule(
    depth = schedulerDepth,
    traitType = new requestWithDataWire
  ))

  peripheralMSHR.read.ready := false.B
  zeroInit(peripheralMSHR.write.data)

  when(requestBuffer.valid){
    request.ready := false.B
    when(readRequestBuffer.valid && !requestBuffer.writeEn){
      readRequestBuffer <> requestBuffer

      when(branchOps.valid){
        cacheBranchWriteUpdate(readRequestBuffer,branchOps,requestBuffer)
      }
      requestBuffer.valid := false.B
    }.elsewhen(!writeRequestBuffer.valid && requestBuffer.writeEn){
      writeRequestBuffer <> requestBuffer

      when(branchOps.valid){
        cacheBranchWriteUpdate(writeRequestBuffer,branchOps,requestBuffer)
      }
      requestBuffer.valid := false.B
    }
  } .otherwise {
    request.ready := true.B
    when(request.request.valid){
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
        writeAXIState := Mux(writeRequestBuffer.valid, writeRequestState, writeIdleState)
    }
    is(writeRequestState){
      val sizeByIns = writeRequestBuffer.instruction(13,12)
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
        writeRequestBuffer.writeData((i + 1) * busWidth - 1, i * busWidth)
      ))
      when(bus.WREADY && bus.AWREADY){
        bus.WDATA := writeChunks(writeCounter.count)
        writeCounter.incrm := true.B 
      }
      writeAXIState := Mux(bus.WLAST && bus.WREADY && bus.AWREADY, writeResponseState, writeRequestState)
    }
    is(writeResponseState){
      bus.BREADY := true.B
      writeRequestBuffer.valid := !(bus.BVALID && bus.BID === id.U && bus.BRESP === "b00".U)

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
      readAXIRequestState := Mux(readRequestBuffer.valid && peripheralMSHR.write.ready, readRequestState, readIdleState)
    }
    is(readRequestState){
      val sizeByIns = readRequestBuffer.instruction(13,12)
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
        peripheralMSHR.write.data.valid := true.B
        peripheralMSHR.write.data.prfDest := readRequestBuffer.prfDest
        peripheralMSHR.write.data.robAddr := readRequestBuffer.robAddr
        peripheralMSHR.write.data.instruction := readRequestBuffer.instruction
        peripheralMSHR.write.data.address := readRequestBuffer.address
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
      
      peripheralMSHR.read.ready := true.B
      responseOutBuffer.valid := false.B
      responseOutBuffer.prfDest := peripheralMSHR.read.data.prfDest
      responseOutBuffer.robAddr := peripheralMSHR.read.data.robAddr
      responseOutBuffer.result := 0.U
      responseOutBuffer.instruction := peripheralMSHR.read.data.instruction
      
      readAXIResponseState := Mux(peripheralMSHR.read.data.valid, readResponseState, readDataInState)
    }
    is(readResponseState){
      bus.RREADY := !responseOutBuffer.valid
      when(bus.RVALID & bus.RID === id.U){
        readCounter.incrm := true.B
        readDataVec(readCounter.count) := bus.RDATA
        responseValid := Mux(bus.RRESP === "b00".U, responseValid, false.B)
      }
      readAXIResponseState := Mux(bus.RLAST && bus.RVALID && responseValid, readDataOutState, readResponseState)
    }
    is(readDataOutState){
      val doubleWordChoosen = Cat(readDataVec.reverse)
      val shiftAmount = (1.U << responseOutBuffer.instruction(13,12).asUInt)
      val section = (1.U << (8.U*shiftAmount)) - 1.U 
      val byteChunks = VecInit(Seq.tabulate(8) { i =>
        doubleWordChoosen((i + 1) * 8 - 1, i * 8) // 8-bit slices
      })
      val byteChoosed     = byteChunks(0.U)
      val halfwordChoosed = Cat(byteChunks(1.U),byteChunks(0.U))
      val wordChoosed     = Cat(byteChunks(3.U),byteChunks(2.U), byteChunks(1.U),byteChunks(0.U))
      switch(responseOutBuffer.instruction(13, 12)){
        is("b00".U){responseOutBuffer.result := Mux(responseOutBuffer.instruction(14),byteChoosed,
                                      Cat(Fill((dataWidth-1*8),byteChoosed(7)),byteChoosed))}
        is("b01".U){responseOutBuffer.result := Mux(responseOutBuffer.instruction(14),halfwordChoosed,
                                      Cat(Fill((dataWidth-2*8),halfwordChoosed(15)),halfwordChoosed))}
        is("b10".U){responseOutBuffer.result := Mux(responseOutBuffer.instruction(14),wordChoosed,
                                      Cat(Fill((dataWidth-4*8),wordChoosed(31)),wordChoosed))}
        is("b11".U){responseOutBuffer.result := Mux(responseOutBuffer.instruction(14),"x0".U,
                                      doubleWordChoosen)}
      }
      responseOutBuffer.valid := true.B
      readAXIResponseState := Mux(responseOut.ready, readDataInState, readDataOutState)
    }
  }
}
