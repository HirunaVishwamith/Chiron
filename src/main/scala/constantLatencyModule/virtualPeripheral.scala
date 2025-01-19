package constantLatencyModule

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._

import common.configuration._
import common._
import _root_.cache.AXI
import common.constants.AXI.connectDefaultMaster

class virtualPeripheral(
  maxByteSizeWrite: Int = 8,
  maxByteSizeRead: Int = 64,
  addressWidth: Int = 32,
  dataWidth: Int = 32
) extends Module {
  import configuration.virtualPeripheral._

  val core = IO(Flipped(new AXI))

  val bufferedReadRequest = RegInit(new Bundle {
    val valid = Bool()
    val araddr = UInt(addressWidth.W)
    val arlen = UInt(constants.AXIFieldLengths.LEN.W)
    val arsize = UInt(constants.AXIFieldLengths.SIZE.W)
  } Lit(_.valid -> false.B))

  val bufferedReadResponse = RegInit(VecInit.fill(maxByteSizeRead*constants.byteSize/dataWidth)(new Bundle {
    val valid = Bool()
    val rdata = UInt(dataWidth.W)
    val rlast = Bool()
  } Lit(_.valid -> false.B)))

  // moving data back to the queue of bufferedReadResponse
  bufferedReadResponse.zip(bufferedReadResponse.drop(1))
  .foreach{ case (curr, next) => {
    when (curr.valid && !next.valid) {
      next := curr
      curr.valid := false.B
    }
  }}

  when (core.ARVALID && core.ARREADY) {
    bufferedReadRequest.valid := true.B
    bufferedReadRequest.araddr := core.ARADDR
    bufferedReadRequest.arlen := core.ARLEN
    bufferedReadRequest.arsize := core.ARSIZE
  }

  core.RVALID := false.B
  core.RDATA := bufferedReadResponse.reverse.head.rdata
  core.RID := constants.AXI.defaultIDForOneClient.U
  core.RLAST := bufferedReadResponse.reverse.head.rlast
  core.RRESP := constants.AXI.RRESP.OKAY.U

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
  bufferedWriteRequest.data.zip(bufferedWriteRequest.data.drop(1))
  .foreach { case (curr, next) => {
    when (curr.valid && !next.valid) {
      next := curr
      curr.valid := false.B
    }
  }}

  when (core.WVALID && core.WREADY) {
    bufferedWriteRequest.data.head.valid := true.B
    bufferedWriteRequest.data.head.wdata := core.WDATA
    bufferedWriteRequest.data.head.wlast := core.WLAST
    bufferedWriteRequest.data.head.wstrb := core.WSTRB
  }

  when (core.AWREADY && core.AWVALID) {
    bufferedWriteRequest.address.valid := true.B
    bufferedWriteRequest.address.awaddr := core.AWADDR
    bufferedWriteRequest.address.awsize := core.AWSIZE
  }

  val mtime = RegInit(0.U(mtimeWidth.W))
  val mtimecmp = RegInit(0.U(mtimeWidth.W))

  val txFIFO = RegInit(VecInit.fill(configuration.virtualPeripheral.txFIFOLength)(new Bundle {
    val valid = Bool()
    val pushed = Bool()
    val byte = UInt(constants.byteSize.W)
  } Lit(_.valid -> false.B)))
  txFIFO.zip(txFIFO.drop(1))
  .foreach { case (curr, next) => {
    when (curr.valid && !next.valid) {
      next := curr
      curr.valid := false.B
    }
  }}


  object states {
    val (idle :: waitingForRequest :: performingRead :: sendReadResp ::
      waitWriteReq :: performingWrite :: sendWriteResp :: Nil) = Enum(7)
  }
  val state = RegInit(states.idle)

  core.ARREADY := false.B
  core.AWREADY := false.B
  core.WREADY := false.B

  core.RVALID := false.B
  core.RDATA := bufferedReadResponse.reverse.head.rdata
  core.RID := constants.AXI.defaultIDForOneClient.U
  core.RLAST := bufferedReadResponse.reverse.head.rlast
  core.RRESP := constants.AXI.RRESP.OKAY.U

  core.BVALID := false.B
  core.BID := constants.AXI.defaultIDForOneClient.U
  core.BRESP := constants.AXI.BRESP.OKAY.U

  switch (state) {
    is (states.idle) {
      when (core.ARVALID) {
        core.ARREADY := true.B
        state := states.performingRead
      }.elsewhen(core.AWVALID) {
        core.AWREADY := true.B
        state := states.waitWriteReq
      }.elsewhen(core.WVALID) {
        core.WREADY := true.B
        state := states.waitWriteReq
      }
    }
    is (states.performingRead) {
      switch (bufferedReadRequest.araddr) {
        is (configuration.virtualPeripheral.addressSpace.ZynqPSUart0.XUARTPS_SR_OFFSET.U) {
          bufferedReadResponse.head.valid := true.B
          bufferedReadResponse.head.rdata := 2.U // no keyboard hit for now
          // assume all target requests are 32 bits
          bufferedReadResponse.head.rlast := true.B 
          state := states.sendReadResp
          }
        is (configuration.virtualPeripheral.addressSpace.ZynqPSUart1.XUARTPS_SR_OFFSET.U) {
          bufferedReadResponse.head.valid := true.B
          bufferedReadResponse.head.rdata := 2.U // no keyboard hit for now
          // assume all target requests are 32 bits
          bufferedReadResponse.head.rlast := true.B 
          state := states.sendReadResp
        }
        is (configuration.virtualPeripheral.addressSpace.clint.mtime.U) {
          assert (maxByteSizeRead > 1, "Expect this request to be 2 words long")
          bufferedReadResponse(0).valid := true.B
          bufferedReadResponse(0).rlast := true.B
          assert(dataWidth==32, "Virtual peripheral for XLEN 64 bit should be rewritten")
          bufferedReadResponse(0).rdata := mtime(mtimeWidth-1, dataWidth)
          bufferedReadResponse(1).valid := true.B
          bufferedReadResponse(1).rlast := false.B
          bufferedReadResponse(1).rdata := mtime(dataWidth-1, 0)
          state := states.sendReadResp
        }
      }
    }
    is (states.sendReadResp) {
      core.RVALID := bufferedReadResponse.reverse.head.valid
      when (core.RVALID && core.RREADY) { bufferedReadResponse.reverse.head.valid := false.B }
      // all beats sent
      when (!bufferedReadResponse.map(i => i.valid && i.rlast).reduce(_ || _)) { state := states.idle }
    }
    is (states.waitWriteReq) {
      core.AWREADY := !bufferedWriteRequest.address.valid
      core.WREADY := !bufferedWriteRequest.data.head.valid
      when (bufferedWriteRequest.address.valid && bufferedWriteRequest.data.map(i => i.valid && i.wlast).reduce(_ || _)) {
        state := states.performingWrite
      }
    }
    is (states.performingWrite) {
      when (bufferedWriteRequest.data.reverse.head.valid) {
        switch (bufferedWriteRequest.address.awaddr) {
          is (addressSpace.clint.mtimecmp.U) {
            mtimecmp :=  Mux(
              bufferedWriteRequest.data.reverse.head.wlast,
              Cat(bufferedWriteRequest.data.reverse.head.wdata, mtimecmp(dataWidth-1, 0)), 
              Cat(mtimecmp(mtimeWidth-1, dataWidth), bufferedWriteRequest.data.reverse.head.wdata)
            )
            when (bufferedWriteRequest.data.reverse.head.wlast) {
              state := states.sendWriteResp
              bufferedWriteRequest.address.valid := false.B
            }
            bufferedWriteRequest.data.reverse.head.valid := false.B
          }
          is (addressSpace.ZynqPSUart0.XUARTPS_FIFO_OFFSET.U) {
            when (!txFIFO.head.valid) {
              // we only expect one data beat
              txFIFO.head.valid := true.B
              txFIFO.head.pushed := false.B
              txFIFO.head.byte := bufferedWriteRequest.data.reverse.head.wdata(constants.byteSize-1, 0)
              bufferedWriteRequest.data.reverse.head.valid := false.B
              state := states.sendWriteResp
              bufferedWriteRequest.address.valid := false.B
            }
          }
          is (addressSpace.ZynqPSUart1.XUARTPS_FIFO_OFFSET.U) {
            when (!txFIFO.head.valid) {
              // we only expect one data beat
              txFIFO.head.valid := true.B
              txFIFO.head.pushed := false.B
              txFIFO.head.byte := bufferedWriteRequest.data.reverse.head.wdata(constants.byteSize-1, 0)
              bufferedWriteRequest.data.reverse.head.valid := false.B
              state := states.sendWriteResp
              bufferedWriteRequest.address.valid := false.B
            }
          }
          is (addressSpace.clint.msip.U) {
            bufferedWriteRequest.data.reverse.head.valid := false.B
            when (bufferedWriteRequest.data.reverse.head.wlast) {
              state := states.sendWriteResp
            }
          }
        }
      }
    }
    is (states.sendWriteResp) {
      core.BVALID := true.B
      when (core.BREADY && core.BVALID) { state := states.idle }
    }
  }

  val txClearTimer = RegInit(0.U(log2Ceil(charClearTimeTx+1).W))
  when (txFIFO.reverse.head.valid && !txFIFO.reverse.head.pushed && !txClearTimer.orR) {
    txClearTimer := charClearTimeTx.U
  }
  when (txClearTimer > 0.U) { 
    txClearTimer := txClearTimer - 1.U 
    when (txClearTimer === 1.U) {
      txFIFO.reverse.head.valid := false.B
    }
  }

  val masterAXI = IO(new AXI)

  masterAXI.ARVALID := false.B
  masterAXI.ARADDR := addressSpace.ZynqPSUart1.XUARTPS_SR_OFFSET.U
  masterAXI.ARBURST := constants.AXI.BURST.INCR.U
  masterAXI.ARCACHE := constants.AXI.encodeARCACHE(Modifiable = true).U
  masterAXI.ARID := constants.AXI.defaultIDForOneClient.U
  // making a 32 bit read
  masterAXI.ARLEN := constants.AXI.encodeAXLEN(AXSIZE = 2, opSizeInBytes = 4).U
  masterAXI.ARLOCK := false.B.asUInt
  masterAXI.ARPROT := constants.AXI.encodeAXPROT(Secure = true).U
  masterAXI.ARQOS := constants.AXI.noQOSDefined.U
  masterAXI.ARSIZE := constants.AXI.encodeAXSIZE(bytesPerBeat = dataWidth/constants.byteSize).U

  masterAXI.AWVALID := false.B
  masterAXI.AWADDR := addressSpace.ZynqPSUart1.XUARTPS_FIFO_OFFSET.U
  masterAXI.AWBURST := constants.AXI.BURST.INCR.U
  masterAXI.AWCACHE := constants.AXI.encodeAWCACHE(Modifiable = true).U
  masterAXI.AWID := constants.AXI.defaultIDForOneClient.U
  masterAXI.AWLEN := constants.AXI.encodeAXLEN(AXSIZE = 2, opSizeInBytes = 4).U
  masterAXI.AWLOCK := false.B.asUInt
  masterAXI.AWPROT := constants.AXI.encodeAXPROT(Secure = true).B
  masterAXI.AWQOS := constants.AXI.noQOSDefined.U
  masterAXI.AWSIZE := constants.AXI.encodeAXSIZE(bytesPerBeat = dataWidth/constants.byteSize).U

  masterAXI.WVALID := false.B
  masterAXI.WDATA := Cat(0.U((dataWidth-constants.byteSize).W), txFIFO.reverse.head.byte)
  masterAXI.WLAST := true.B
  masterAXI.WSTRB := 1.U // TODO: Mention why 1
  
  masterAXI.RREADY := false.B

  masterAXI.BREADY := false.B

  object masterAXIStates {
    val (idle :: waitOnAR :: waitOnR :: waitOnWriteReq :: waitOnB :: 
      waitOnAW :: waitOnW :: Nil) = Enum(7)
  }
  val masterAXIState = RegInit(masterAXIStates.idle)
  switch (masterAXIState) {
    is (masterAXIStates.idle) {
      when (txFIFO.reverse.head.valid && !txFIFO.reverse.head.pushed) {
        masterAXIState := masterAXIStates.waitOnAR
      }
    }
    is (masterAXIStates.waitOnAR) {
      masterAXI.ARVALID := true.B
      when (masterAXI.ARVALID && masterAXI.ARREADY) { masterAXIState := masterAXIStates.waitOnR }
    }
    is (masterAXIStates.waitOnR) {
      masterAXI.RREADY := true.B
      when (masterAXI.RREADY && masterAXI.RVALID) {
        when (XUARTPS_SR_TXFULL(masterAXI.RDATA)) { masterAXIState := masterAXIStates.waitOnAR }
        .otherwise { masterAXIState := masterAXIStates.waitOnWriteReq }
      }
    }
    is (masterAXIStates.waitOnWriteReq) {
      masterAXI.AWVALID := true.B
      masterAXI.WVALID := true.B
      when (masterAXI.AWREADY && masterAXI.WREADY) { masterAXIState := masterAXIStates.waitOnB }
      .elsewhen(masterAXI.AWREADY) { masterAXIState := masterAXIStates.waitOnW }
      .elsewhen(masterAXI.WREADY) { masterAXIState := masterAXIStates.waitOnAW }
    }
    is (masterAXIStates.waitOnAW) {
      masterAXI.AWVALID := true.B
      when (masterAXI.AWREADY) { masterAXIState := masterAXIStates.waitOnB }
    }
    is (masterAXIStates.waitOnW) {
      masterAXI.WVALID := true.B
      when (masterAXI.WREADY) { masterAXIState := masterAXIStates.waitOnB }
    }
    is (masterAXIStates.waitOnB) {
      masterAXI.BREADY := true.B
      when (masterAXI.BVALID) { 
        masterAXIState := masterAXIStates.idle
        txFIFO.reverse.head.pushed := true.B 
      }
    }
  }

  val MTIP = IO(Output(Bool()))
  MTIP := (mtimecmp < mtime)

  val stepWidth = RegInit(0.U(3.W))
  stepWidth := stepWidth + 1.U

  mtime := mtime + stepWidth.andR.asUInt

  // connectDefaultMaster(masterAXI)
}

object virtualPeripheral extends App {
  emitVerilog(new virtualPeripheral)
}