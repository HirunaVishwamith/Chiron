package cache

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import common.configuration
import common.composableInterface

class peripheralHandler extends Module {
  val request = IO(Input(new pipelineMemoryRequest))

  val servicing = RegInit(new pipelineMemoryRequest Lit(_.valid -> false.B))

  val finishedRequest = IO(Output(new pipelineMemoryRequest))
  finishedRequest := servicing

  val arvalid, rready, awvalid, wvalid, wlast, bready, stall = RegInit(false.B)

  val ready = IO(Output(Bool()))
  ready := !Seq(arvalid, rready, awvalid, wvalid, bready, stall).reduce(_ || _)

  val rdata = Reg(UInt(64.W)) // register aligned

  val branchOps = IO(Input(new Bundle {
    val valid = Bool()
    val branchMask = UInt(configuration.newBranchMaskWidth.W) //leon coherency
    val passed = Bool()
  }))

  val writeCleared = RegInit(false.B)

  when(request.valid && ready) {
    servicing := request
    when(request.instruction(5).asBool) { awvalid := true.B }
    .otherwise { arvalid := true.B }
    when(branchOps.valid) {
      when((branchOps.branchMask & request.branchMask).orR) {
        servicing.branchMask := branchOps.branchMask ^ request.branchMask
      }
      when(!branchOps.passed && (branchOps.branchMask & request.branchMask).orR) {
        awvalid := false.B
        arvalid := false.B
        servicing.valid := false.B
      }
    }
  }

  val axi = IO(new AXI)
  
  when(axi.ARVALID && axi.ARREADY) {
    arvalid := false.B
    rready := true.B
  }

  when(axi.RVALID && axi.RREADY && axi.RLAST) {
    rready := false.B
    when(servicing.valid) {
      stall := true.B
    }
  }

  val readFinished = IO(new composableInterface)
  readFinished.ready := stall

  when(readFinished.fired) {
    stall := false.B
    servicing.valid := false.B
  }

  val readDataOut = IO(Output(UInt(64.W)))
  readDataOut := rdata

  when(axi.RVALID && axi.RREADY && axi.RLAST) {
    when(servicing.instruction(13, 12) === 3.U) {
      rdata := Cat(axi.RDATA, rdata(31, 0))
    }.otherwise {
      rdata := {
        val msb = VecInit(
          VecInit(0 until 4 map(_*8 + 7) map(i => axi.RDATA(i)))(servicing.address(1, 0)),
          VecInit(0 until 2 map(_*16 + 15) map(i => axi.RDATA(i)))(servicing.address(2))
        )(servicing.instruction(12))

        VecInit(
          Cat(Fill(56, Mux(!servicing.instruction(14).asBool, msb, 0.U(1.W))), VecInit.tabulate(4)(i => axi.RDATA(8*i + 7, 8*i))(servicing.address(1,0))),
          Cat(Fill(48, Mux(!servicing.instruction(14).asBool, msb, 0.U(1.W))), VecInit.tabulate(2)(i => axi.RDATA(16*i + 15, 16*i))(servicing.address(2))),
          Cat(Fill(32, Mux(!servicing.instruction(14).asBool, axi.RDATA(31), 0.U(1.W))), axi.RDATA),
          axi.RDATA
        )(servicing.instruction(13, 12))
      }
    }
  }.elsewhen(axi.RVALID && axi.RREADY) {
    rdata := Cat(0.U(32.W), axi.RDATA)
  }

  val wdata = Reg(UInt(64.W))
  val wstrb = Reg(UInt(4.W))
  val writeIn = IO(Input(new Bundle {
    val valid = Bool()
    val data = UInt(64.W)
  }))

  when(writeIn.valid) {
    wdata := writeIn.data << (VecInit(0.U, 8.U, 16.U, 24.U)(servicing.address(1, 0)))
    wvalid := true.B
    wstrb := VecInit(1.U, 3.U, 15.U, 15.U)(servicing.instruction(13, 12)) << servicing.address(1, 0)
    writeCleared := true.B
    when(servicing.instruction(13, 12) =/= 3.U) { wlast := true.B }
  }

  when(axi.AWVALID && axi.AWREADY) {
    awvalid := false.B
    writeCleared := false.B
    bready := true.B
  }

  when(axi.WVALID && axi.WREADY && axi.WLAST) {
    wvalid := false.B
    wlast := false.B
  }.elsewhen(axi.WVALID && axi.WREADY) {
    wlast := true.B
    wdata := (wdata >> 32)
  }

  when(axi.BREADY && axi.BVALID) {
    bready := false.B
    servicing.valid := false.B
  }

  when(branchOps.valid && servicing.valid) {
    when((servicing.branchMask & branchOps.branchMask).orR) {
      servicing.branchMask := servicing.branchMask ^ branchOps.branchMask
    }
    when(!branchOps.passed && (servicing.branchMask & branchOps.branchMask).orR) {
      servicing.valid := false.B
      awvalid := false.B
      stall := false.B
      arvalid := false.B
    }
  }

  axi.ARADDR  := servicing.address
  axi.ARBURST := 1.U
  axi.ARCACHE := 2.U
  axi.ARID    := 0.U
  axi.ARLEN   := Mux(servicing.instruction(13, 12) === 3.U, 1.U, 0.U)
  axi.ARLOCK  := 0.U
  axi.ARPROT  := 0.U
  axi.ARQOS   := 0.U
  axi.ARSIZE  := Mux(servicing.instruction(13, 12) === 3.U, 2.U, servicing.instruction(13, 12))
  axi.ARVALID := arvalid && !servicing.branchMask.orR // reading address might not be idompotent
  axi.AWADDR  := servicing.address
  axi.AWBURST := 1.U
  axi.AWCACHE := 2.U
  axi.AWID    := 0.U
  axi.AWLEN   := Mux(servicing.instruction(13, 12) === 3.U, 1.U, 0.U)
  axi.AWLOCK  := 0.U
  axi.AWPROT  := 0.U
  axi.AWQOS   := 0.U
  axi.AWSIZE  := Mux(servicing.instruction(13, 12) === 3.U, 2.U, servicing.instruction(13, 12))
  axi.AWVALID := awvalid && writeCleared
  axi.BREADY  := bready
  axi.RREADY  := rready
  axi.WDATA   := wdata(31, 0)
  axi.WLAST   := wlast
  axi.WSTRB   := wstrb
  axi.WVALID  := wvalid
}
