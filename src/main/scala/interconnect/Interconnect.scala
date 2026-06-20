package Interconnect

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import chisel3.experimental.IO


class Interconnect extends Module {
  val io = IO(new Bundle {
    val acePort0 = new ace()  //core_0
    val acePort1 = new ace()  //core_1
    val acePort2 = new ace()  //core_2
    val acePort3 = new ace()  //core_3
    val acePort4 = new ace()  //core_4
    val acePort5 = new ace()  //core_5
    val acePort6 = new ace()  //core_6
    val acePort7 = new ace()  //core_7
    val L2 = new Bundle{
		//AW
		val AWVALID = Output(Bool())
		val AWREADY = Input(Bool())
		//metadata
		val AWID = Output(UInt(3.W))
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
		val ARID = Output(UInt(3.W))
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
		val RID = Input(UInt(3.W))
		val RDATA = Input(UInt(64.W))
		val RRESP = Input(UInt(2.W))          //0:1 is AXI
		val RLAST = Input(Bool())
		//val RUSER = Input(UInt())

		//B
		val BVALID = Input(Bool())
		val BREADY = Output(Bool())
		//metadata
		val BID = Input(UInt(3.W))
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
        val stateReg_9 = Output(UInt(3.W))
        val stateReg_10 = Output(UInt(3.W))
        val stateReg_11 = Output(UInt(3.W))
        val stateReg_12 = Output(UInt(3.W))
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

  Arbiter.io.AWVALID_2 := io.acePort2.AWVALID
  Arbiter.io.AWBAR_2 := io.acePort2.AWBAR(0)
  io.acePort2.AWREADY := Arbiter.io.AWREADY_2
  Arbiter.io.WVALID_2 := io.acePort2.WVALID
  Arbiter.io.WLAST_2 := io.acePort2.WLAST
  io.acePort2.WREADY := Arbiter.io.WREADY_2
  Arbiter.io.ARVALID_2 := io.acePort2.ARVALID
  io.acePort2.ARREADY := Arbiter.io.ARREADY_2

  Arbiter.io.AWVALID_3 := io.acePort3.AWVALID
  io.acePort3.AWREADY := Arbiter.io.AWREADY_3
  Arbiter.io.WVALID_3 := io.acePort3.WVALID
  Arbiter.io.WLAST_3 := io.acePort3.WLAST
  io.acePort3.WREADY := Arbiter.io.WREADY_3
  Arbiter.io.ARVALID_3 := io.acePort3.ARVALID
  io.acePort3.ARREADY := Arbiter.io.ARREADY_3

  Arbiter.io.AWVALID_4 := io.acePort4.AWVALID
  Arbiter.io.AWBAR_4 := io.acePort4.AWBAR(0)
  io.acePort4.AWREADY := Arbiter.io.AWREADY_4
  Arbiter.io.WVALID_4 := io.acePort4.WVALID
  Arbiter.io.WLAST_4 := io.acePort4.WLAST
  io.acePort4.WREADY := Arbiter.io.WREADY_4
  Arbiter.io.ARVALID_4 := io.acePort4.ARVALID
  io.acePort4.ARREADY := Arbiter.io.ARREADY_4

  Arbiter.io.AWVALID_5 := io.acePort5.AWVALID
  io.acePort5.AWREADY := Arbiter.io.AWREADY_5
  Arbiter.io.WVALID_5 := io.acePort5.WVALID
  Arbiter.io.WLAST_5 := io.acePort5.WLAST
  io.acePort5.WREADY := Arbiter.io.WREADY_5
  Arbiter.io.ARVALID_5 := io.acePort5.ARVALID
  io.acePort5.ARREADY := Arbiter.io.ARREADY_5

  Arbiter.io.AWVALID_6 := io.acePort6.AWVALID
  Arbiter.io.AWBAR_6 := io.acePort6.AWBAR(0)
  io.acePort6.AWREADY := Arbiter.io.AWREADY_6
  Arbiter.io.WVALID_6 := io.acePort6.WVALID
  Arbiter.io.WLAST_6 := io.acePort6.WLAST
  io.acePort6.WREADY := Arbiter.io.WREADY_6
  Arbiter.io.ARVALID_6 := io.acePort6.ARVALID
  io.acePort6.ARREADY := Arbiter.io.ARREADY_6

  Arbiter.io.AWVALID_7 := io.acePort7.AWVALID
  io.acePort7.AWREADY := Arbiter.io.AWREADY_7
  Arbiter.io.WVALID_7 := io.acePort7.WVALID
  Arbiter.io.WLAST_7 := io.acePort7.WLAST
  io.acePort7.WREADY := Arbiter.io.WREADY_7
  Arbiter.io.ARVALID_7 := io.acePort7.ARVALID
  io.acePort7.ARREADY := Arbiter.io.ARREADY_7


  // Instantiate RingBuffer with a given depth
  val FIFO = Module(new ringbuffer(depth = 32)) //width is 71 bits

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


  when(Arbiter.io.select === "b00000".U){        //0.U AR_0
    FIFO.io.enq.bits := Cat(io.acePort0.ARID, io.acePort0.ARADDR,Mux(io.acePort0.ARBAR(0),"b0100".U(4.W),io.acePort0.ARSNOOP))
  }.elsewhen(Arbiter.io.select === "b00001".U){  //1.U AW_0
    FIFO.io.enq.bits := Cat(io.acePort0.AWID, io.acePort0.AWADDR, io.acePort0.AWBAR(0),io.acePort0.AWSNOOP)
  }.elsewhen(Arbiter.io.select === "b00010".U){  //2.U W_0
    FIFO.io.enq.bits := Cat("b000".U(3.W), io.acePort0.WDATA, "b000".U(3.W), io.acePort0.WLAST)
  }.elsewhen(Arbiter.io.select === "b00100".U){  //4.U AR_1
    FIFO.io.enq.bits := Cat(io.acePort1.ARID, io.acePort1.ARADDR,Mux(io.acePort1.ARBAR(0),"b0100".U(4.W),io.acePort1.ARSNOOP))
  }.elsewhen(Arbiter.io.select === "b00101".U){  //5.U AW_1
    FIFO.io.enq.bits := Cat(io.acePort1.AWID, io.acePort1.AWADDR, io.acePort1.AWBAR(0),io.acePort1.AWSNOOP)
  }.elsewhen(Arbiter.io.select === "b00110".U){  //6.U W_1
    FIFO.io.enq.bits := Cat("b000".U(3.W), io.acePort1.WDATA, "b000".U(3.W), io.acePort1.WLAST)
  }.elsewhen(Arbiter.io.select === "b01000".U){  //8.U AR_2
    FIFO.io.enq.bits := Cat(io.acePort2.ARID, io.acePort2.ARADDR,Mux(io.acePort2.ARBAR(0),"b0100".U(4.W),io.acePort2.ARSNOOP))
  }.elsewhen(Arbiter.io.select === "b01001".U){  //9.U AW_2
    FIFO.io.enq.bits := Cat(io.acePort2.AWID, io.acePort2.AWADDR, io.acePort2.AWBAR(0),io.acePort2.AWSNOOP)
  }.elsewhen(Arbiter.io.select === "b01010".U){  //10.U W_2
    FIFO.io.enq.bits := Cat("b000".U(3.W), io.acePort2.WDATA, "b000".U(3.W), io.acePort2.WLAST)
  }.elsewhen(Arbiter.io.select === "b01100".U){  //12.U AR_3
    FIFO.io.enq.bits := Cat(io.acePort3.ARID, io.acePort3.ARADDR,Mux(io.acePort3.ARBAR(0),"b0100".U(4.W),io.acePort3.ARSNOOP))
  }.elsewhen(Arbiter.io.select === "b01101".U){  //13.U AW_3
    FIFO.io.enq.bits := Cat(io.acePort3.AWID, io.acePort3.AWADDR, io.acePort3.AWBAR(0),io.acePort3.AWSNOOP)
  }.elsewhen(Arbiter.io.select === "b01110".U){  //14.U W_3
    FIFO.io.enq.bits := Cat("b000".U(3.W), io.acePort3.WDATA, "b000".U(3.W), io.acePort3.WLAST)
  }.elsewhen(Arbiter.io.select === "b10000".U){  //16.U AR_4
    FIFO.io.enq.bits := Cat(io.acePort4.ARID, io.acePort4.ARADDR,Mux(io.acePort4.ARBAR(0),"b0100".U(4.W),io.acePort4.ARSNOOP))
  }.elsewhen(Arbiter.io.select === "b10001".U){  //17.U AW_4
    FIFO.io.enq.bits := Cat(io.acePort4.AWID, io.acePort4.AWADDR, io.acePort4.AWBAR(0),io.acePort4.AWSNOOP)
  }.elsewhen(Arbiter.io.select === "b10010".U){  //18.U W_4
    FIFO.io.enq.bits := Cat("b000".U(3.W), io.acePort4.WDATA, "b000".U(3.W), io.acePort4.WLAST)
  }.elsewhen(Arbiter.io.select === "b10100".U){  //20.U AR_5
    FIFO.io.enq.bits := Cat(io.acePort5.ARID, io.acePort5.ARADDR,Mux(io.acePort5.ARBAR(0),"b0100".U(4.W),io.acePort5.ARSNOOP))
  }.elsewhen(Arbiter.io.select === "b10101".U){  //21.U AW_5
    FIFO.io.enq.bits := Cat(io.acePort5.AWID, io.acePort5.AWADDR, io.acePort5.AWBAR(0),io.acePort5.AWSNOOP)
  }.elsewhen(Arbiter.io.select === "b10110".U){  //22.U W_5
    FIFO.io.enq.bits := Cat("b000".U(3.W), io.acePort5.WDATA, "b000".U(3.W), io.acePort5.WLAST)
  }.elsewhen(Arbiter.io.select === "b11000".U){  //24.U AR_6
    FIFO.io.enq.bits := Cat(io.acePort6.ARID, io.acePort6.ARADDR,Mux(io.acePort6.ARBAR(0),"b0100".U(4.W),io.acePort6.ARSNOOP))
  }.elsewhen(Arbiter.io.select === "b11001".U){  //25.U AW_6
    FIFO.io.enq.bits := Cat(io.acePort6.AWID, io.acePort6.AWADDR, io.acePort6.AWBAR(0),io.acePort6.AWSNOOP)
  }.elsewhen(Arbiter.io.select === "b11010".U){  //26.U W_6
    FIFO.io.enq.bits := Cat("b000".U(3.W), io.acePort6.WDATA, "b000".U(3.W), io.acePort6.WLAST)
  }.elsewhen(Arbiter.io.select === "b11100".U){  //28.U AR_7
    FIFO.io.enq.bits := Cat(io.acePort7.ARID, io.acePort7.ARADDR,Mux(io.acePort7.ARBAR(0),"b0100".U(4.W),io.acePort7.ARSNOOP))
  }.elsewhen(Arbiter.io.select === "b11101".U){  //29.U AW_7
    FIFO.io.enq.bits := Cat(io.acePort7.AWID, io.acePort7.AWADDR, io.acePort7.AWBAR(0),io.acePort7.AWSNOOP)
  }.elsewhen(Arbiter.io.select === "b11110".U){  //30.U W_7
    FIFO.io.enq.bits := Cat("b000".U(3.W), io.acePort7.WDATA, "b000".U(3.W), io.acePort7.WLAST)
  }.otherwise{
    FIFO.io.enq.bits := 0.U(71.W)
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

  //core_0
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

  //core_1
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

  //core_2
  //CA channel
  io.acePort2.ACVALID := CCU.core2.ACVALID
  CCU.core2.ACREADY := io.acePort2.ACREADY
  io.acePort2.ACADDR := CCU.core2.ACADDR
  io.acePort2.ACSNOOP := CCU.core2.ACSNOOP

  //CR channel
  CCU.core2.CRVALID := io.acePort2.CRVALID
  io.acePort2.CRREADY := CCU.core2.CRREADY
  CCU.core2.CRRESP := io.acePort2.CRRESP

  //CD channel
  CCU.core2.CDVALID := io.acePort2.CDVALID
  io.acePort2.CDREADY := CCU.core2.CDREADY
  CCU.core2.CDDATA := io.acePort2.CDDATA
  CCU.core2.CDLAST := io.acePort2.CDLAST

  //R channel
  io.acePort2.RVALID := CCU.core2.RVALID
  CCU.core2.RREADY := io.acePort2.RREADY
  io.acePort2.RID := CCU.core2.RID
  io.acePort2.RRESP := CCU.core2.RRESP
  io.acePort2.RDATA := CCU.core2.RDATA
  io.acePort2.RLAST := CCU.core2.RLAST

  //B channel
  io.acePort2.BVALID := CCU.core2.BVALID
  CCU.core2.BREADY := io.acePort2.BREADY
  io.acePort2.BID := CCU.core2.BID
  io.acePort2.BRESP := CCU.core2.BRESP

  //core_3
  //CA channel
  io.acePort3.ACVALID := CCU.core3.ACVALID
  CCU.core3.ACREADY := io.acePort3.ACREADY
  io.acePort3.ACADDR := CCU.core3.ACADDR
  io.acePort3.ACSNOOP := CCU.core3.ACSNOOP

  //CR channel
  CCU.core3.CRVALID := io.acePort3.CRVALID
  io.acePort3.CRREADY := CCU.core3.CRREADY
  CCU.core3.CRRESP := io.acePort3.CRRESP

  //CD channel
  CCU.core3.CDVALID := io.acePort3.CDVALID
  io.acePort3.CDREADY := CCU.core3.CDREADY
  CCU.core3.CDDATA := io.acePort3.CDDATA
  CCU.core3.CDLAST := io.acePort3.CDLAST

  //R channel
  io.acePort3.RVALID := CCU.core3.RVALID
  CCU.core3.RREADY := io.acePort3.RREADY
  io.acePort3.RID := CCU.core3.RID
  io.acePort3.RRESP := CCU.core3.RRESP
  io.acePort3.RDATA := CCU.core3.RDATA
  io.acePort3.RLAST := CCU.core3.RLAST

  //B channel
  io.acePort3.BVALID := CCU.core3.BVALID
  CCU.core3.BREADY := io.acePort3.BREADY
  io.acePort3.BID := CCU.core3.BID
  io.acePort3.BRESP := CCU.core3.BRESP

  //core_4
  //CA channel
  io.acePort4.ACVALID := CCU.core4.ACVALID
  CCU.core4.ACREADY := io.acePort4.ACREADY
  io.acePort4.ACADDR := CCU.core4.ACADDR
  io.acePort4.ACSNOOP := CCU.core4.ACSNOOP

  //CR channel
  CCU.core4.CRVALID := io.acePort4.CRVALID
  io.acePort4.CRREADY := CCU.core4.CRREADY
  CCU.core4.CRRESP := io.acePort4.CRRESP

  //CD channel
  CCU.core4.CDVALID := io.acePort4.CDVALID
  io.acePort4.CDREADY := CCU.core4.CDREADY
  CCU.core4.CDDATA := io.acePort4.CDDATA
  CCU.core4.CDLAST := io.acePort4.CDLAST

  //R channel
  io.acePort4.RVALID := CCU.core4.RVALID
  CCU.core4.RREADY := io.acePort4.RREADY
  io.acePort4.RID := CCU.core4.RID
  io.acePort4.RRESP := CCU.core4.RRESP
  io.acePort4.RDATA := CCU.core4.RDATA
  io.acePort4.RLAST := CCU.core4.RLAST

  //B channel
  io.acePort4.BVALID := CCU.core4.BVALID
  CCU.core4.BREADY := io.acePort4.BREADY
  io.acePort4.BID := CCU.core4.BID
  io.acePort4.BRESP := CCU.core4.BRESP

  //core_5
  //CA channel
  io.acePort5.ACVALID := CCU.core5.ACVALID
  CCU.core5.ACREADY := io.acePort5.ACREADY
  io.acePort5.ACADDR := CCU.core5.ACADDR
  io.acePort5.ACSNOOP := CCU.core5.ACSNOOP

  //CR channel
  CCU.core5.CRVALID := io.acePort5.CRVALID
  io.acePort5.CRREADY := CCU.core5.CRREADY
  CCU.core5.CRRESP := io.acePort5.CRRESP

  //CD channel
  CCU.core5.CDVALID := io.acePort5.CDVALID
  io.acePort5.CDREADY := CCU.core5.CDREADY
  CCU.core5.CDDATA := io.acePort5.CDDATA
  CCU.core5.CDLAST := io.acePort5.CDLAST

  //R channel
  io.acePort5.RVALID := CCU.core5.RVALID
  CCU.core5.RREADY := io.acePort5.RREADY
  io.acePort5.RID := CCU.core5.RID
  io.acePort5.RRESP := CCU.core5.RRESP
  io.acePort5.RDATA := CCU.core5.RDATA
  io.acePort5.RLAST := CCU.core5.RLAST

  //B channel
  io.acePort5.BVALID := CCU.core5.BVALID
  CCU.core5.BREADY := io.acePort5.BREADY
  io.acePort5.BID := CCU.core5.BID
  io.acePort5.BRESP := CCU.core5.BRESP

  //core_6
  //CA channel
  io.acePort6.ACVALID := CCU.core6.ACVALID
  CCU.core6.ACREADY := io.acePort6.ACREADY
  io.acePort6.ACADDR := CCU.core6.ACADDR
  io.acePort6.ACSNOOP := CCU.core6.ACSNOOP

  //CR channel
  CCU.core6.CRVALID := io.acePort6.CRVALID
  io.acePort6.CRREADY := CCU.core6.CRREADY
  CCU.core6.CRRESP := io.acePort6.CRRESP

  //CD channel
  CCU.core6.CDVALID := io.acePort6.CDVALID
  io.acePort6.CDREADY := CCU.core6.CDREADY
  CCU.core6.CDDATA := io.acePort6.CDDATA
  CCU.core6.CDLAST := io.acePort6.CDLAST

  //R channel
  io.acePort6.RVALID := CCU.core6.RVALID
  CCU.core6.RREADY := io.acePort6.RREADY
  io.acePort6.RID := CCU.core6.RID
  io.acePort6.RRESP := CCU.core6.RRESP
  io.acePort6.RDATA := CCU.core6.RDATA
  io.acePort6.RLAST := CCU.core6.RLAST

  //B channel
  io.acePort6.BVALID := CCU.core6.BVALID
  CCU.core6.BREADY := io.acePort6.BREADY
  io.acePort6.BID := CCU.core6.BID
  io.acePort6.BRESP := CCU.core6.BRESP

  //core_7
  //CA channel
  io.acePort7.ACVALID := CCU.core7.ACVALID
  CCU.core7.ACREADY := io.acePort7.ACREADY
  io.acePort7.ACADDR := CCU.core7.ACADDR
  io.acePort7.ACSNOOP := CCU.core7.ACSNOOP

  //CR channel
  CCU.core7.CRVALID := io.acePort7.CRVALID
  io.acePort7.CRREADY := CCU.core7.CRREADY
  CCU.core7.CRRESP := io.acePort7.CRRESP

  //CD channel
  CCU.core7.CDVALID := io.acePort7.CDVALID
  io.acePort7.CDREADY := CCU.core7.CDREADY
  CCU.core7.CDDATA := io.acePort7.CDDATA
  CCU.core7.CDLAST := io.acePort7.CDLAST

  //R channel
  io.acePort7.RVALID := CCU.core7.RVALID
  CCU.core7.RREADY := io.acePort7.RREADY
  io.acePort7.RID := CCU.core7.RID
  io.acePort7.RRESP := CCU.core7.RRESP
  io.acePort7.RDATA := CCU.core7.RDATA
  io.acePort7.RLAST := CCU.core7.RLAST

  //B channel
  io.acePort7.BVALID := CCU.core7.BVALID
  CCU.core7.BREADY := io.acePort7.BREADY
  io.acePort7.BID := CCU.core7.BID
  io.acePort7.BRESP := CCU.core7.BRESP

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
  io.debug.stateReg_9 := CCU.debug.stateReg_9
  io.debug.stateReg_10 := CCU.debug.stateReg_10
  io.debug.stateReg_11 := CCU.debug.stateReg_11
  io.debug.stateReg_12 := CCU.debug.stateReg_12
  */


}

object Interconnect extends App {
  emitVerilog(new Interconnect)
}

