package Interconnect

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import chisel3.experimental.IO


class Interconnect extends Module {
  val io = IO(new Bundle {
    val acePort0 = new ace()  //D$
    val acePort1 = new ace()  //I$
    val L2 = new Bundle{
		//AW
		val AWVALID = Output(Bool())
		val AWREADY = Input(Bool())
		//metadata
		val AWID = Output(UInt(2.W))
		val AWADDR = Output(UInt(64.W))
		//val AWLEN = Output(UInt(8.W))
		//val AWSIZE = Output(UInt(3.W))
		//val AWBURST = Output(UInt(2.W))
		//val AWLOCK = Output(UInt(1.W))
		//val AWCACHE = Output(UInt(4.W))
		//val AWPROT = Output(UInt(3.W))
		//val AWQOS = Output(UInt(4.W))
		//val AWREGION = Output(UInt())
		//val AWUSER = Output(UInt())

		//AR
		val ARVALID = Output(Bool())
		val ARREADY = Input(Bool())
		//metadata
		val ARID = Output(UInt(2.W))
		val ARADDR = Output(UInt(64.W))
		//val ARLEN = Output(UInt(8.W))
		//val ARSIZE = Output(UInt(3.W))
		//val ARBURST = Output(UInt(2.W))
		//val ARLOCK = Output(UInt(1.W))
		//val ARCACHE = Output(UInt(4.W))
		//val ARPROT = Output(UInt(3.W))
		//val ARQOS = Output(UInt(4.W))
		//val ARREGION = Output(UInt())
		//val ARUSER = Output(UInt())

		//W
		val WVALID = Output(Bool())
		val WREADY = Input(Bool())
		//metadata
		val WDATA = Output(UInt(64.W))
		//val WSTRB = Output(UInt((dataWidth/8).W))
		val WLAST = Output(Bool())
		//val WUSER = Output(UInt())

		//R
		val RVALID = Input(Bool())
		val RREADY = Output(Bool())
		//metadata
		val RID = Input(UInt(2.W))
		val RDATA = Input(UInt(64.W))
		val RRESP = Input(UInt(2.W))          //0:1 is AXI
		val RLAST = Input(Bool())
		//val RUSER = Input(UInt())

		//B
		val BVALID = Input(Bool())
		val BREADY = Output(Bool())
		//metadata
		val BID = Input(UInt(2.W))
		val BRESP = Input(UInt(2.W))
		//val BUSER = Input(UInt())
	}
	/**
	val debug =new Bundle{
        val stateReg_1 = Output(UInt(3.W))
        val stateReg_2 = Output(UInt(3.W))
        val stateReg_3 = Output(UInt(3.W))
        val stateReg_4 = Output(UInt(3.W))
        val stateReg_5 = Output(UInt(3.W))
        val stateReg_6 = Output(UInt(3.W))
        val stateReg_7 = Output(UInt(3.W))
        val stateReg_8 = Output(UInt(3.W))
	}
	*/
  })

  // Instantiate Arbiter
  val Arbiter = Module(new arbiter)

  //Arbiter connecting
  Arbiter.io.AWVALID_0 := io.acePort0.AWVALID
  Arbiter.io.AWBAR_0 := io.acePort0.AWBAR(0)
  io.acePort0.AWREADY := Arbiter.io.AWREADY_0
  Arbiter.io.WVALID_0 := io.acePort0.WVALID
  Arbiter.io.WLAST_0 := io.acePort0.WLAST
  io.acePort0.WREADY := Arbiter.io.WREADY_0
  Arbiter.io.ARVALID_0 := io.acePort0.ARVALID
  io.acePort0.ARREADY := Arbiter.io.ARREADY_0

  Arbiter.io.AWVALID_1 := io.acePort1.AWVALID
  io.acePort1.AWREADY := Arbiter.io.AWREADY_1
  Arbiter.io.WVALID_1 := io.acePort1.WVALID
  Arbiter.io.WLAST_1 := io.acePort1.WLAST
  io.acePort1.WREADY := Arbiter.io.WREADY_1
  Arbiter.io.ARVALID_1 := io.acePort1.ARVALID
  io.acePort1.ARREADY := Arbiter.io.ARREADY_1




  // Instantiate RingBuffer with a given depth
  val FIFO = Module(new ringbuffer(depth = 32)) //width is 70 bits

  //FIFO connecting
  FIFO.io.enq.valid := Arbiter.io.enq_valid
  Arbiter.io.enq_ready := FIFO.io.enq.ready

  //encoding
  //0100 ReadMemoryBarrier  (This encoding is not according to the ACE spec)
  //0000 ReadNoSnoop        (ACE spec encoding)
  //0001 ReadShared         (ACE spec encoding)
  //0111 ReadUnique         (ACE spec encoding)
  //1011 CleanUnique        (ACE spec encoding)
  //0011 WriteBack          (ACE spec encoding)
  //0010 WriteClean         (ACE spec encoding)
  //1000 WriteMemoryBarrier (ACE spec encoding)


  when(Arbiter.io.select === "b000".U){        //0.U AR_0
    FIFO.io.enq.bits := Cat(io.acePort0.ARID, io.acePort0.ARADDR,Mux(io.acePort0.ARBAR(0),"b0100".U(4.W),io.acePort0.ARSNOOP))
  }.elsewhen(Arbiter.io.select === "b001".U){  //1.U AW_0
    FIFO.io.enq.bits := Cat(io.acePort0.AWID, io.acePort0.AWADDR, io.acePort0.AWBAR(0),io.acePort0.AWSNOOP)
  }.elsewhen(Arbiter.io.select === "b010".U){  //2.U W_0
    FIFO.io.enq.bits := Cat("b00".U(2.W), io.acePort0.WDATA, "b000".U(3.W), io.acePort0.WLAST)
  }.elsewhen(Arbiter.io.select === "b100".U){  //4.U AR_1
    FIFO.io.enq.bits := Cat(io.acePort1.ARID, io.acePort1.ARADDR,Mux(io.acePort1.ARBAR(0),"b0100".U(4.W),io.acePort1.ARSNOOP))
  }.elsewhen(Arbiter.io.select === "b101".U){  //5.U AW_1
    FIFO.io.enq.bits := Cat(io.acePort1.AWID, io.acePort1.AWADDR, io.acePort1.AWBAR(0),io.acePort1.AWSNOOP)
  }.elsewhen(Arbiter.io.select === "b110".U){  //6.U W_1
    FIFO.io.enq.bits := Cat("b00".U(2.W), io.acePort1.WDATA, "b000".U(3.W), io.acePort1.WLAST)
  }.otherwise{
    FIFO.io.enq.bits := 0.U(70.W)
  }

  // Instantiate CCU
  val CCU = Module(new ccu)

  //FIFO deq port connection
  CCU.deq.valid := FIFO.io.deq.valid
  CCU.deq.data := FIFO.io.deq.bits
  FIFO.io.deq.ready := CCU.deq.ready

  //L2 connection
  //AW channel
  io.L2.AWVALID := CCU.L2.AWVALID
  CCU.L2.AWREADY := io.L2.AWREADY
  io.L2.AWID := CCU.L2.AWID
  io.L2.AWADDR := CCU.L2.AWADDR

  //W channel
  io.L2.WVALID := CCU.L2.WVALID
  CCU.L2.WREADY := io.L2.WREADY
  io.L2.WDATA := CCU.L2.WDATA
  io.L2.WLAST := CCU.L2.WLAST

  //B channel
  CCU.L2.BVALID := io.L2.BVALID
  io.L2.BREADY := CCU.L2.BREADY
  CCU.L2.BID := io.L2.BID
  CCU.L2.BRESP := io.L2.BRESP

  //AR channel
  io.L2.ARVALID := CCU.L2.ARVALID
  CCU.L2.ARREADY := io.L2.ARREADY
  io.L2.ARID := CCU.L2.ARID
  io.L2.ARADDR := CCU.L2.ARADDR

  //R channel
  CCU.L2.RVALID := io.L2.RVALID
  io.L2.RREADY := CCU.L2.RREADY
  CCU.L2.RID := io.L2.RID
  CCU.L2.RRESP := io.L2.RRESP
  CCU.L2.RDATA := io.L2.RDATA
  CCU.L2.RLAST := io.L2.RLAST

  //D$
  //CA channel
  io.acePort0.ACVALID := CCU.core0.ACVALID
  CCU.core0.ACREADY := io.acePort0.ACREADY
  io.acePort0.ACADDR := CCU.core0.ACADDR
  io.acePort0.ACSNOOP := CCU.core0.ACSNOOP

  //CR channel
  CCU.core0.CRVALID := io.acePort0.CRVALID
  io.acePort0.CRREADY := CCU.core0.CRREADY
  CCU.core0.CRRESP := io.acePort0.CRRESP

  //CD channel
  CCU.core0.CDVALID := io.acePort0.CDVALID
  io.acePort0.CDREADY := CCU.core0.CDREADY
  CCU.core0.CDDATA := io.acePort0.CDDATA
  CCU.core0.CDLAST := io.acePort0.CDLAST

  //R channel
  io.acePort0.RVALID := CCU.core0.RVALID
  CCU.core0.RREADY := io.acePort0.RREADY
  io.acePort0.RID := CCU.core0.RID
  io.acePort0.RRESP := CCU.core0.RRESP
  io.acePort0.RDATA := CCU.core0.RDATA
  io.acePort0.RLAST := CCU.core0.RLAST

  //B channel
  io.acePort0.BVALID := CCU.core0.BVALID
  CCU.core0.BREADY := io.acePort0.BREADY
  io.acePort0.BID := CCU.core0.BID
  io.acePort0.BRESP := CCU.core0.BRESP

  //I$
  //CA channel
  io.acePort1.ACVALID := CCU.core1.ACVALID
  CCU.core1.ACREADY := io.acePort1.ACREADY
  io.acePort1.ACADDR := CCU.core1.ACADDR
  io.acePort1.ACSNOOP := CCU.core1.ACSNOOP

  //CR channel
  CCU.core1.CRVALID := io.acePort1.CRVALID
  io.acePort1.CRREADY := CCU.core1.CRREADY
  CCU.core1.CRRESP := io.acePort1.CRRESP

  //CD channel
  CCU.core1.CDVALID := io.acePort1.CDVALID
  io.acePort1.CDREADY := CCU.core1.CDREADY
  CCU.core1.CDDATA := io.acePort1.CDDATA
  CCU.core1.CDLAST := io.acePort1.CDLAST

  //R channel
  io.acePort1.RVALID := CCU.core1.RVALID
  CCU.core1.RREADY := io.acePort1.RREADY
  io.acePort1.RID := CCU.core1.RID
  io.acePort1.RRESP := CCU.core1.RRESP
  io.acePort1.RDATA := CCU.core1.RDATA
  io.acePort1.RLAST := CCU.core1.RLAST

  //B channel
  io.acePort1.BVALID := CCU.core1.BVALID
  CCU.core1.BREADY := io.acePort1.BREADY
  io.acePort1.BID := CCU.core1.BID
  io.acePort1.BRESP := CCU.core1.BRESP

  Arbiter.io.nstall := (CCU.core0.BVALID && CCU.core0.BREADY) || (CCU.core1.BVALID && CCU.core1.BREADY) || (CCU.core0.RVALID && CCU.core0.RREADY && CCU.core0.RLAST) || (CCU.core1.RVALID && CCU.core1.RREADY && CCU.core1.RLAST)

  /**
  //debug signals
  io.debug.stateReg_1 := CCU.debug.stateReg_1
  io.debug.stateReg_2 := CCU.debug.stateReg_2
  io.debug.stateReg_3 := CCU.debug.stateReg_3
  io.debug.stateReg_4 := CCU.debug.stateReg_4
  io.debug.stateReg_5 := CCU.debug.stateReg_5
  io.debug.stateReg_6 := CCU.debug.stateReg_6
  io.debug.stateReg_7 := CCU.debug.stateReg_7
  io.debug.stateReg_8 := CCU.debug.stateReg_8
  */


}

object Interconnect extends App {
  emitVerilog(new Interconnect)
}

