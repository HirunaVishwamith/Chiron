package Dcache

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import Dcache.constants._
import Dcache._
import Dcache.ChiselUtils._

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
    idWidth = idWidth,
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
  // A squashed MMIO load must not write back. Nothing else in this unit used
  // to enforce that: the branch state captured into readRequestBuffer /
  // peripheralMSHR / responseOutBuffer was never aged by later resolutions, and
  // cacheModule's peripheral response arm (unlike the cache arm) carries no
  // branch.valid gate -- FIRRTL even pruned responseOut.request.branch out of
  // the generated Verilog as unread. So a load speculated past a mispredicted
  // branch still landed its result in the PRF, and since core.scala:837 puts no
  // squash check on the load write port, it corrupted whatever the destination
  // had been reallocated to. Identical in shape to the ACE responseBuffer
  // clobber (ACEUnit.scala), which wedged mt-ipitmr; here the speculated loads
  // are UART/PLIC/CLINT polls in Linux's console and IRQ paths.
  // The AXI transaction itself is never aborted -- see the drain comment at
  // readDataInState -- only the writeback is suppressed.
  responseOut.request.valid := responseOutBuffer.valid && responseOutBuffer.branch.valid
  // True on a cycle a buffer is (re)loaded; the ageing blocks below must skip
  // such cycles, because regRecordUpdate's branch-PASS arm ends in an
  // unconditional `buffer.valid := buffer.valid` (utils.scala:137) that would
  // write back the PREVIOUS occupant's valid bit and resurrect it.
  val reqLoading = WireDefault(false.B)
  val readReqLoading = WireDefault(false.B)
  val responseOutLoading = WireDefault(false.B)

  val writeCommitInstructionBuffer = RegInit(false.B)
  writeInstructionCommit.ready := writeCommitInstructionBuffer
  when(writeInstructionCommit.fired){
    writeCommitInstructionBuffer := false.B
  }
  //-----------------------MSHR-------------------------------------------//
  // fifoWithBranchOps, not fifoBaseModule: the base class has no branch state
  // at all, so an MMIO read already on the bus could never be squashed. Same
  // class the ACE MSHR uses.
  val peripheralMSHR = Module(new fifoWithBranchOps(
    depth = schedulerDepth,
    traitType = new requestPipelineWire
  ))

  peripheralMSHR.read.ready := false.B
  zeroInit(peripheralMSHR.write.data)
  // Was never connected, so fifoBaseModule's per-entry squash loop (fifo.scala
  // 105-127) was dead code here and FIRRTL pruned the port off the instance
  // entirely: an in-flight MMIO read could not be squashed once its address
  // was on the bus.
  peripheralMSHR.branchOps := branchOps

  when(requestBuffer.valid && requestBuffer.branch.valid){
    request.ready := false.B
    when(!readRequestBuffer.valid && !requestBuffer.writeData.valid){
      
      readRequestBuffer := requestBuffer
      regWriteUpdate(readRequestBuffer.branch,branchOps,requestBuffer.branch)
      readReqLoading := true.B
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
      reqLoading := true.B
    }
  }

  // Age the staging request too, so a squash is caught before the unit ever
  // asks the bus for it. Retire it outright: the block above only clears
  // requestBuffer.valid on a hand-off to the read/write buffer, which a dead
  // request never gets, and while it sits valid it holds request.ready low.
  val reqSquashed = branchOps.valid && !branchOps.passed &&
                    (requestBuffer.branch.mask & branchOps.branchMask).orR
  when(requestBuffer.valid && !reqLoading) {
    regRecordUpdate(requestBuffer.branch, branchOps)
    when(reqSquashed || !requestBuffer.branch.valid) {
      requestBuffer.valid := false.B
    }
  }

  //-----------------------AXI Write-------------------------------//
  // NOTE: writeRequestBuffer is deliberately NOT aged. Peripheral stores only
  // reach this unit after the arbiter's commitReadyState/writeCommit handshake,
  // i.e. after the ROB has committed them, so no later branch can squash one.
  // Ageing it would be actively harmful: writeIdleState's only exit is
  // `writeRequestBuffer.branch.valid`, and the buffer is only released by the
  // AXI B response, so a cleared branch.valid would strand the buffer and block
  // every subsequent MMIO write -- the UART included.
  val writeIdleState :: writeRequestState :: writeResponseState :: Nil = Enum(3)

  val writeAXIState = RegInit(writeIdleState)
  val writeCounter = Module(new moduleCounter(length))
  writeCounter.incrm := false.B
  writeCounter.reset := false.B
  val sizeByIns = WireDefault(writeRequestBuffer.core.instruction(13,12))
  val sizePerBurst = WireDefault((1.U << sizeByIns) * 8.U)
  val numOfBeats = WireDefault(((sizePerBurst + busWidth.U - 1.U) / busWidth.U) - 1.U)
  switch(writeAXIState) {
    is(writeIdleState){
        writeCounter.reset := true.B
        writeAXIState := Mux(writeRequestBuffer.valid && writeRequestBuffer.branch.valid, writeRequestState, writeIdleState)
    }
    is(writeRequestState){
      bus.AWVALID := writeCounter.count === 0.U
      bus.AWID := id.U
      bus.AWADDR := writeRequestBuffer.address
      bus.AWLEN := numOfBeats
      bus.AWSIZE := Mux(sizePerBurst <= busWidth.U, sizeByIns, Log2(busWidth.U / 8.U) )
      bus.AWBURST := "b01".U
      bus.AWLOCK := "b0".U
      bus.AWCACHE := "b0000".U
      bus.AWPROT := "b010".U
      bus.AWQOS := "b0000".U
  
      bus.WVALID := true.B
      bus.WSTRB := Fill(busWidth/8, 1.U)
      bus.WLAST := writeCounter.count === numOfBeats

      val numSlices = length + 1
      val writeChunks = VecInit(Seq.tabulate(numSlices)(i => 
        writeRequestBuffer.writeData.data((i + 1) * busWidth - 1, i * busWidth)
      ))
      when(bus.WREADY && bus.AWREADY && writeCounter.count === 0.U){
        writeCounter.incrm := true.B 
      }.elsewhen(bus.WREADY){
        writeCounter.incrm := true.B 
      }
      bus.WDATA := writeChunks(writeCounter.count)
      writeAXIState := Mux(bus.WLAST && bus.WREADY, writeResponseState, writeRequestState)
    }
    is(writeResponseState){
      bus.BREADY := true.B
      writeRequestBuffer.valid := !(bus.BVALID && bus.BID === id.U && bus.BRESP === "b00".U)
      writeCommitInstructionBuffer := bus.BVALID && (bus.BID === id.U) && bus.BRESP === "b00".U
      writeAXIState := Mux(bus.BVALID && (bus.BID === id.U), 
                        Mux(bus.BRESP === "b00".U, writeIdleState, writeRequestState),
                          writeResponseState)
    }
  }

  //-----------------------AXI ReadRequest--------------------------------//
  val readIdleState :: readRequestState :: Nil = Enum(2)
  val readAXIRequestState = RegInit(readIdleState)
  // Age the pending read request. Once it reaches this buffer it is committed
  // to nothing yet -- ARVALID has not been asserted -- so a squash here is the
  // cheap case: drop it outright below and no bus traffic happens at all.
  when(readRequestBuffer.valid && !readReqLoading) {
    regRecordUpdate(readRequestBuffer.branch, branchOps)
  }
  switch(readAXIRequestState) {
    is(readIdleState){
      // A squashed request must be RETIRED here, not merely held: the state
      // machine's only other exit from this buffer is ARREADY, so leaving a
      // dead request valid would block every later MMIO read forever.
      when(readRequestBuffer.valid && !readRequestBuffer.branch.valid) {
        readRequestBuffer.valid := false.B
      }
      readAXIRequestState := Mux(readRequestBuffer.valid && readRequestBuffer.branch.valid && peripheralMSHR.write.ready, readRequestState, readIdleState)
    }
    is(readRequestState){
      val sizeByIns = readRequestBuffer.core.instruction(13,12)
      val sizePerBurst = (1.U << sizeByIns) * 8.U

      bus.ARVALID := true.B
      bus.ARID := id.U
      bus.ARADDR := readRequestBuffer.address
      bus.ARLEN := ((sizePerBurst + busWidth.U - 1.U) / busWidth.U) - 1.U
      bus.ARSIZE := Mux(sizePerBurst <= busWidth.U, sizeByIns, Log2(busWidth.U / 8.U) )
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
        responseOutLoading := true.B
      }
      responseOutBuffer.valid := false.B
      // Drain the R channel for EVERY issued transaction, squashed or not.
      // This used to be gated on the entry's branch.valid, which was safe only
      // because nothing ever cleared it; now that squashes actually reach the
      // MSHR, skipping readResponseState would leave the slave's read data
      // beats stranded with RREADY low and hang every later MMIO access. The
      // squash is honoured at the writeback instead (responseOut.request.valid).
      readAXIResponseState := Mux(peripheralMSHR.read.data.valid && !peripheralMSHR.isEmpty, readResponseState, readDataInState)
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
  // Age the response buffer, skipping the cycle it is loaded from the MSHR (see
  // readReqLoading/responseOutLoading above). Elaborated after the switch so it
  // wins last-connect on the branch fields only -- responseOutBuffer.valid is
  // still owned by the state machine, so a squashed response keeps driving the
  // FSM to completion and only its writeback is suppressed.
  when(responseOutBuffer.valid && !responseOutLoading) {
    regRecordUpdate(responseOutBuffer.branch, branchOps)
  }

  //Resource Utilization
  peripheralMSHR.write.data.cacheLine.cacheLine := 0.U
  peripheralMSHR.write.data.cacheLine.required := false.B
  peripheralMSHR.write.data.cacheLine.response := 0.U

  readRequestBuffer.cacheLine.cacheLine := 0.U
  readRequestBuffer.cacheLine.required := false.B
  readRequestBuffer.cacheLine.response := 0.U

  requestBuffer.cacheLine.cacheLine := 0.U
  requestBuffer.cacheLine.required := false.B
  requestBuffer.cacheLine.response := 0.U

  writeRequestBuffer.cacheLine.cacheLine := 0.U
  writeRequestBuffer.cacheLine.required := false.B
  writeRequestBuffer.cacheLine.response := 0.U
}
