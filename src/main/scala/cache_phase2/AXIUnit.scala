package DataCache
//Use namespaces if required

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import DataCache.constantsDCache._
import _root_.os.read.lines

class AXIUnit(
	dataWidth: Int,
  addrWidth: Int,
  id: Int,
  // busWidth: Int,
  length: Int,
  size: Int,
) extends Module {
  val request = IO(new requestAXI(
    dataWidth = dataWidth, 
    addrWidth = addrWidth,
  ))
  val loadData = IO(new loadData(
    dataWidth = dataWidth
  ))
  // val storeData = IO(new storeData)
  val bus = IO(new AXI(
    idWidth = 2,
    addressWidth = addrWidth,
    busWidth = peripheral_WIDTH, //32
  ))
  val busWidth : Int = math.pow(2, peripheral_SIZE).toInt * 8

  //IO initializing
  request.ready := false.B
  loadData.data := 0.U
  loadData.valid := false.B
  loadData.response := 0.U    //Not required for peripherals

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
  //-----------------------Intermidiate signals--------------------//
  val readComplete = WireDefault(false.B)
  val writeComplete = WireDefault(false.B)

  //-----------------------Input Buffer----------------------------//
  val inputBuffer = RegInit(new Bundle{
    val writeEn = Bool()
    val address = UInt(addrWidth.W)
    val instruction = UInt(insWidth.W)
  }.Lit(
    _.writeEn -> false.B,
    _.address -> 0.U,
    _.instruction -> 0.U
  ))

  val emptyState :: readState :: writeState :: Nil = Enum(3)
  val inputBufferState = RegInit(emptyState)
  switch(inputBufferState){
    is(emptyState){
      request.ready := true.B
      when(request.valid){
        inputBuffer.writeEn := request.writeEn
        inputBuffer.address := request.address
        inputBuffer.instruction := request.instruction
      }

      inputBufferState := Mux(request.valid, 
                            Mux(request.writeEn, writeState, readState),
                              emptyState)
    }
    is(readState){
      inputBufferState := Mux(readComplete, emptyState, readState)            
    }
    is(writeState){
      inputBufferState := Mux(writeComplete, emptyState, writeState)
    }
  }

    //-----------------------Data Reg--------------------------------//
    val dataReg = RegInit(0.U(dataWidth.W))
    dataReg := Mux(request.valid, request.data, dataReg)

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
        readAXIState := Mux(inputBufferState === readState, requestState, idleState)
      }
      is(requestState){
        val sizeByIns = inputBuffer.instruction(13,12)
        val sizePerBeat = (1.U << sizeByIns) * 8.U

        bus.ARVALID := true.B
        bus.ARID := id.U
        bus.ARADDR := inputBuffer.address
        bus.ARLEN := ((sizePerBeat + busWidth.U - 1.U) / busWidth.U) - 1.U
        bus.ARSIZE := Mux(sizePerBeat <= busWidth.U, sizeByIns, Log2(busWidth.U / 8.U) )
        bus.ARBURST := "b01".U
        bus.ARLOCK := "b0".U
        bus.ARCACHE := "b0000".U
        bus.ARPROT := "b010".U
        bus.ARQOS := "b0000".U

        readCounter.reset := true.B

        readAXIState := Mux(bus.ARREADY, responseState, requestState)
      }
      is(responseState){
        bus.RREADY := true.B
        when(bus.RVALID & bus.RID === id.U){
          readCounter.incrm := true.B
          readDataVec(readCounter.count) := bus.RDATA
          responseValid := Mux(bus.RRESP === "b00".U, responseValid, false.B)
        }

        readAXIState := Mux(bus.RLAST && bus.RVALID,
                      Mux(responseValid, dataOutState, requestState),
                        responseState)
      }
      is(dataOutState){
        readComplete := loadData.ready

        val doubleWordChoosen = Cat(readDataVec.reverse)
        val shiftAmount = (1.U << inputBuffer.instruction(13,12).asUInt)
        val section = (1.U << (8.U*shiftAmount)) - 1.U 
        val byteChunks = VecInit(Seq.tabulate(8) { i =>
          doubleWordChoosen((i + 1) * 8 - 1, i * 8) // 8-bit slices
        })
        val byteChoosed     = byteChunks(inputBuffer.address(2,0))
        val halfwordChoosed = Cat(byteChunks(2.U * inputBuffer.address(2,1) + 1.U),byteChunks(2.U * inputBuffer.address(2,1)))
        val wordChoosed     = Cat(byteChunks(2.U * inputBuffer.address(2) + 3.U),byteChunks(2.U * inputBuffer.address(2) + 2.U), 
                                  byteChunks(2.U * inputBuffer.address(2) + 1.U),byteChunks(2.U * inputBuffer.address(2)))
        switch(inputBuffer.instruction(13, 12)){
          is("b00".U){loadData.data := Mux(inputBuffer.instruction(14),byteChoosed,
                                        Cat(Fill((dataWidth-1*8),byteChoosed(7)),byteChoosed))}
          is("b01".U){loadData.data := Mux(inputBuffer.instruction(14),halfwordChoosed,
                                        Cat(Fill((dataWidth-2*8),halfwordChoosed(15)),halfwordChoosed))}
          is("b10".U){loadData.data := Mux(inputBuffer.instruction(14),wordChoosed,
                                        Cat(Fill((dataWidth-4*8),wordChoosed(31)),wordChoosed))}
          is("b11".U){loadData.data := Mux(inputBuffer.instruction(14),"x0".U,
                                        doubleWordChoosen)}
        }

        loadData.valid := true.B
        readAXIState := Mux(loadData.ready, idleState, dataOutState)
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
        val sizeByIns = inputBuffer.instruction(13,12)
        val sizePerBeat = (1.U << sizeByIns) * 8.U

        bus.AWVALID := true.B
        bus.AWID := id.U
        bus.AWADDR := inputBuffer.address
        bus.ARLEN := ((sizePerBeat + busWidth.U - 1.U) / busWidth.U) - 1.U
        bus.ARSIZE := Mux(sizePerBeat <= busWidth.U, sizeByIns, Log2(busWidth.U / 8.U) )
        bus.AWBURST := "b01".U
        bus.AWLOCK := "b0".U
        bus.AWCACHE := "b0000".U
        bus.AWPROT := "b010".U
        bus.AWQOS := "b0000".U

        bus.WVALID := true.B
        bus.WSTRB := ((1.U << (1.U << bus.ARSIZE)) - 1.U).asUInt
        bus.WLAST := writeCounter.count === bus.ARLEN

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
}
