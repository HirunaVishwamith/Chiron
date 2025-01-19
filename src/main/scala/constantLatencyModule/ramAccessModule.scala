package constantLatencyModule

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._

import common.configuration._
import common._
import _root_.cache.AXI
import os.stat

/**
  * This module presents two slave AXI ports for the core (1x data 1x instruction)
  * . Both these interfaces have a contant latency
  * 
  * On FPGA, if the latency guarantee cannot be met, errors must be indicated
  *
  */
class ramAccessModule(
  maxByteSizeWrite: Int = 8,
  maxByteSizeRead: Int = 64,
  idWidth: Int = 1,
  addressWidth: Int = 32,
  dataWidth: Int = 32,
  waitReadReqTime: Int = 100,
  waitReadRespTime: Int = 100,
  waitWriteReqTime: Int = 100,
  waitWriteRespTime: Int = 100
) extends Module {

  // interface to connect core AXI interfaces
  val core = IO(Flipped(new Bundle {
    val instruction, data = new AXI(
      idWidth = idWidth, addressWidth = addressWidth, dataWidth = dataWidth
    )
    def all() = Seq(instruction, data)
  }))
  val dram = IO(new AXI)
  object coreClient {
    val instruction :: data :: Nil = Enum(2)
  }
  // we buffer read requests before sending them to dram
  val bufferedReadRequest = RegInit(new Bundle {
    val valid = Bool()
    val araddr = UInt(addressWidth.W)
    val arlen = UInt(constants.AXIFieldLengths.LEN.W)
    val arsize = UInt(constants.AXIFieldLengths.SIZE.W)
    val client = coreClient.instruction.cloneType
  } Lit(_.valid -> false.B))

  // we will buffer the write request before it can be sent to main memory
  // Important!!!
  // The current core, awvalid and wvalid are independently asserted from
  // wready and awready respectively

  // In the current version of the core, at most a write operation can
  // only have 8 bytes (through store double instruction)
  object bufferedWriteRequest {
    val address = RegInit(new Bundle {
      val valid = Bool()
      // we only care about the following at the current moment
      val awaddr = UInt(addressWidth.W)
      val awlen = UInt(constants.AXIFieldLengths.LEN.W)
      val awsize = UInt(constants.AXIFieldLengths.SIZE.W)
    } Lit(_.valid -> false.B))
    val data = RegInit(VecInit.fill(maxByteSizeWrite*constants.byteSize/dataWidth)(new Bundle {
        val valid = Bool()
        val wdata = UInt(dataWidth.W)
        val wstrb = UInt((dataWidth/constants.byteSize).W)
        val wlast = Bool()
    } Lit(_.valid -> false.B)))
  } 

  object states {
    val (idle :: 
      sendReadReq :: waitReadResp :: sendReadResp :: 
      waitWriteReq :: sendWriteReq :: waitWriteResp :: sendWriteResp :: Nil
    ) = Enum(8)
  }
  val state = RegInit(states.idle)

  // The ready signals with the core are by default
  // grounded until a valid signal is asserted by 
  // the core
  core.instruction.ARREADY := false.B
  // data signals are anyways always grounded
  core.instruction.AWREADY := false.B
  core.instruction.WREADY := false.B
  core.data.ARREADY := false.B
  core.data.AWREADY := false.B
  core.data.WREADY := false.B

  dram.ARVALID := false.B
  dram.ARADDR := bufferedReadRequest.araddr
  dram.ARBURST := constants.AXI.BURST.INCR.U
  dram.ARCACHE := constants.AXI.encodeARCACHE(Modifiable = true).U
  dram.ARID := constants.AXI.defaultIDForOneClient.U
  dram.ARLEN := bufferedReadRequest.arlen
  dram.ARLOCK := false.B.asUInt
  dram.ARPROT := constants.AXI.encodeAXPROT(Secure = true).U
  dram.ARQOS := constants.AXI.noQOSDefined.U
  dram.ARSIZE := bufferedReadRequest.arsize

  dram.AWVALID := false.B
  dram.AWADDR := bufferedWriteRequest.address.awaddr
  dram.AWBURST := constants.AXI.BURST.INCR.U
  dram.AWCACHE := constants.AXI.encodeAWCACHE(Modifiable = true).U
  dram.AWID := constants.AXI.defaultIDForOneClient.U
  dram.AWLEN := bufferedWriteRequest.address.awlen
  dram.AWLOCK := false.B.asUInt
  dram.AWPROT := constants.AXI.encodeAXPROT(Secure = true).B
  dram.AWQOS := constants.AXI.noQOSDefined.U
  dram.AWSIZE := bufferedWriteRequest.address.awsize

  dram.WVALID := false.B
  dram.WDATA := bufferedWriteRequest.data.reverse.head.wdata
  dram.WLAST := bufferedWriteRequest.data.reverse.head.wlast
  dram.WSTRB := bufferedWriteRequest.data.reverse.head.wstrb

  // controlling buffered read request
  switch (state) {
    is (states.idle) {
      when (core.all().map(i => i.ARVALID && i.ARREADY).reduce(_ || _)) {
        // data channel has a higher priority
        bufferedReadRequest.valid := true.B
        bufferedReadRequest.araddr := 
          Mux(core.data.ARVALID, core.data.ARADDR, core.instruction.ARADDR)
        bufferedReadRequest.arlen :=
          Mux(core.data.ARVALID, core.data.ARLEN, core.instruction.ARLEN)
        bufferedReadRequest.arsize :=
          Mux(core.data.ARVALID, core.data.ARSIZE, core.instruction.ARSIZE)
        bufferedReadRequest.client :=
          Mux(core.data.ARVALID, coreClient.data, coreClient.instruction)
      }
    }
    is (states.sendReadReq) {
      when (dram.ARVALID && dram.ARREADY) { bufferedReadRequest.valid := false.B }
    }
  }

  // controlling buffered write request
  when (core.data.AWVALID && core.data.AWREADY) {
    // core.data.AWREADY is high when (state === states.idle) 
    // or (state === states.waitWriteReq && !bufferedWriteRequest.address.valid)
    bufferedWriteRequest.address.valid := true.B

    bufferedWriteRequest.address.awaddr := core.data.AWADDR
    bufferedWriteRequest.address.awlen := core.data.AWLEN
    bufferedWriteRequest.address.awsize := core.data.AWSIZE
  }
  when (dram.AWVALID && dram.AWREADY) {
    // dram.AWVALID should only be high when state === states.sendWriteReq
    bufferedWriteRequest.address.valid := false.B
  }
  // moving data to the back of bufferedWriteRequest.data
  bufferedWriteRequest.data.zip(bufferedWriteRequest.data.drop(1))
  .foreach{ case(curr, next) => {
    when (curr.valid && !next.valid) {
      next := curr
      curr.valid := false.B
    }
  }}
  when (core.data.WVALID && core.data.WREADY) {
    bufferedWriteRequest.data.head.valid := true.B
    bufferedWriteRequest.data.head.wdata := core.data.WDATA
    bufferedWriteRequest.data.head.wlast := core.data.WLAST
    bufferedWriteRequest.data.head.wstrb := core.data.WSTRB
  }
  when (dram.WVALID && dram.WREADY) {
    bufferedWriteRequest.data.reverse.head.valid := false.B
  }

  val bufferedReadResponse = RegInit(VecInit.fill(maxByteSizeRead*constants.byteSize/dataWidth)(new Bundle {
    val valid = Bool()
    val rdata = UInt(dataWidth.W)
    val rlast = Bool()
  } Lit(_.valid -> false.B)))
  // read responses are added to head of bufferedReadResponse FIFO
  when (dram.RVALID && dram.RREADY) {
    bufferedReadResponse.head.valid := true.B
    bufferedReadResponse.head.rdata := dram.RDATA
    bufferedReadResponse.head.rlast := dram.RLAST
  }
  // read response are pushed to back of FIFO
  bufferedReadResponse.zip(bufferedReadResponse.drop(1))
  .foreach{ case (curr, next) => {
    when (curr.valid && !next.valid) {
      next := curr
      curr.valid := false.B
    }
  }}
  when (dram.RVALID && dram.RREADY) {
    bufferedReadResponse.head.valid := true.B
    bufferedReadResponse.head.rdata := dram.RDATA
    bufferedReadResponse.head.rlast := dram.RLAST
  }
  when (core.all().map(i => i.RVALID && i.RREADY).reduce(_ || _)) {
    // Only one channel can fire at a time anyway
    bufferedReadResponse.reverse.head.valid := false.B
  }

  dram.RREADY := false.B

  core.all().foreach(i => {
    i.RVALID := false.B
    i.RDATA := 0.U// bufferedReadResponse.reverse.head.rdata
    i.RID := constants.AXI.defaultIDForOneClient.U
    i.RLAST := false.B//bufferedReadResponse.reverse.head.rlast
    i.RRESP := constants.AXI.RRESP.OKAY.U
  })

  val writeResponseRecieved = RegInit(false.B)

  dram.BREADY := false.B

  core.all().foreach(i => {
    i.BVALID := false.B
    i.BID := constants.AXI.defaultIDForOneClient.U
    i.BRESP := constants.AXI.BRESP.OKAY.U
  })

  when (dram.BREADY && dram.BVALID) { writeResponseRecieved := true.B }
  when (core.data.BVALID && core.data.BREADY) { writeResponseRecieved := false.B }

  val latencyRecords = RegInit(new Bundle {
    val waitAR, waitSendWrite, waitReadResp, waitWriteResp = UInt(32.W)
  } Lit(_.waitAR -> 0.U, _.waitReadResp -> 0.U, _.waitSendWrite -> 0.U, _.waitWriteResp -> 0.U))

  val waitTimeCiel = Seq(waitReadReqTime, waitReadRespTime, waitWriteReqTime, waitWriteRespTime).max + 1
  val remainingWaitTime = Reg(UInt(log2Ceil(waitTimeCiel).W))
  val waitCounter = RegInit(0.U(32.W))
  switch (state) {
    is (states.idle) {
      // Only one channel can be ready at a time
      waitCounter := 0.U
      when (core.data.ARVALID) { 
        core.data.ARREADY := true.B
        remainingWaitTime := waitReadReqTime.U
        state := states.sendReadReq 
      }.elsewhen(core.data.AWVALID) {
        core.data.AWREADY := true.B
        state := states.waitWriteReq
      }.elsewhen(core.data.WVALID) {
        core.data.WREADY := true.B
        state := states.waitWriteReq
      }.elsewhen(core.instruction.ARVALID) {
        core.instruction.ARREADY := true.B
        remainingWaitTime := waitWriteReqTime.U
        state := states.sendReadReq
      }
    }
    is (states.waitWriteReq) {
      // waiting for the write address beat
      when (!bufferedWriteRequest.address.valid) {
        core.data.AWREADY := true.B
      }
      // waiting for all the write data beats
      when (!bufferedWriteRequest.data.map(i => i.valid && i.wlast).reduce(_ || _)) {
        core.data.WREADY := !bufferedWriteRequest.data.head.valid
      }
      // after write request is compeletely recieved, we
      // need to send it to dram
      when (
        bufferedWriteRequest.address.valid &&
        bufferedWriteRequest.data.map(i => i.valid && i.wlast).reduce(_ || _)
      ) {
        state := states.sendWriteReq
        remainingWaitTime := waitWriteReqTime.U
      }
    }
    is (states.sendReadReq) {  
      when (dram.ARVALID) { waitCounter := waitCounter + 1.U }

      dram.ARVALID := bufferedReadRequest.valid
      remainingWaitTime := remainingWaitTime - 1.U
      // We wait here for a constant time regardless of
      // dram.ARREADY. However, the request must be sent 
      // before the counter reaches zero
      when (remainingWaitTime === 0.U) { 
        state := states.waitReadResp
        remainingWaitTime := waitReadRespTime.U 

        latencyRecords.waitAR := Mux(waitCounter > latencyRecords.waitAR, waitCounter, latencyRecords.waitAR)
        waitCounter := 0.U
      }
    }
    is (states.sendWriteReq) {
      when( dram.AWVALID || bufferedWriteRequest.data.map(i => i.valid && i.wlast).reduce(_ || _) ) { waitCounter := waitCounter + 1.U }

      dram.AWVALID := bufferedWriteRequest.address.valid
      dram.WVALID := bufferedWriteRequest.data.reverse.head.valid
      remainingWaitTime := remainingWaitTime - 1.U
      // We wait here for a constant time regardless of
      // dram.AWREADY & dram.WREADY. However, the request must be sent 
      // before the counter reaches zero
      when (remainingWaitTime === 0.U) { 
        state := states.waitWriteResp
        remainingWaitTime := waitWriteRespTime.U 

        when (latencyRecords.waitSendWrite < waitCounter) { latencyRecords.waitSendWrite := waitCounter }
        waitCounter := 0.U
      }
    }
    is (states.waitReadResp) {
      when(!bufferedReadResponse.map(i => i.valid && i.rlast).reduce(_ || _)) { waitCounter := waitCounter + 1.U }

      dram.RREADY := !bufferedReadResponse.head.valid && !bufferedReadResponse.map(i => i.valid && i.rlast).reduce(_ || _)
      remainingWaitTime := remainingWaitTime - 1.U
      // We wait here for a constant time regardless of
      // dram.RVALID. However, all the requested data
      // must arrive during this time
      when (remainingWaitTime === 0.U) { 
        state := states.sendReadResp

        latencyRecords.waitReadResp := Mux(waitCounter > latencyRecords.waitReadResp, waitCounter, latencyRecords.waitReadResp)
        waitCounter := 0.U 
      }
    }
    is (states.sendReadResp) {
      switch (bufferedReadRequest.client) {
        is (coreClient.instruction) { core.instruction.RVALID := bufferedReadResponse.reverse.head.valid }
        is (coreClient.data) { core.data.RVALID := bufferedReadResponse.reverse.head.valid }
      }
      core.all().foreach(_.RDATA := bufferedReadResponse.reverse.head.rdata)
      core.all().foreach(_.RLAST := bufferedReadResponse.reverse.head.rlast)
      // finished with read transaction
      when (!bufferedReadResponse.map(_.valid).reduce(_ || _)) { state := states.idle }
    }
    is (states.waitWriteResp) {
      when (dram.BREADY) { waitCounter := waitCounter + 1.U }

      dram.BREADY := !writeResponseRecieved
      remainingWaitTime := remainingWaitTime - 1.U
      // We wait here for a constant time regardless of
      // dram.BVALID. However, the response must arrive
      // during this time
      when (remainingWaitTime === 0.U) { 
        state := states.sendWriteResp

        latencyRecords.waitWriteResp := Mux(waitCounter > latencyRecords.waitWriteResp, waitCounter, latencyRecords.waitWriteResp)
        waitCounter := 0.U
      }
    }
    is (states.sendWriteResp) {
      core.data.BVALID := writeResponseRecieved
      // finished with write transaction
      when (core.data.BVALID && core.data.BREADY) { state := states.idle }
    }
  }

  val records = IO(Output(Vec(4, UInt(32.W))))
  records.zip(Seq(latencyRecords.waitAR, latencyRecords.waitReadResp, latencyRecords.waitSendWrite, latencyRecords.waitWriteResp))
  .foreach { case (sink, source) => sink := source }

}

object ramAccessModule extends App {
  emitVerilog(new ramAccessModule)
}