package Interconnect

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._


class ace(
  idWidth: Int = 2,
  addressWidth: Int = 64,
  dataWidth: Int = 64
)extends Bundle {
	val AWVALID = Input(Bool())
	val AWREADY = Output(Bool())
	//metadata
	val AWID = Input(UInt(idWidth.W))
	val AWADDR = Input(UInt(addressWidth.W))
	//val AWLEN = Input(UInt(8.W))
	//val AWSIZE = Input(UInt(3.W))
	//val AWBURST = Input(UInt(2.W))
	//val AWLOCK = Input(UInt(1.W))
	//val AWCACHE = Input(UInt(4.W))
	//val AWPROT = Input(UInt(3.W))
	//val AWQOS = Input(UInt(4.W))
	//val AWREGION = Input(UInt())
	//val AWUSER = Input(UInt())
	//ace signals
	val AWSNOOP = Input(UInt(3.W))
	//val AWDOMAIN = Input(UInt(2.W))
	val AWBAR = Input(UInt(2.W))
	//val AWUNIQUE = Input(UInt(1.W))


	val WVALID = Input(Bool())
	val WREADY = Output(Bool())
	//metadata
	val WDATA = Input(UInt(dataWidth.W))
	//val WSTRB = Input(UInt((dataWidth/8).W))
	val WLAST = Input(Bool())
	//val WUSER = Input(UInt())


	val BVALID = Output(Bool())
	val BREADY = Input(Bool())
	//metadata
	val BID = Output(UInt(idWidth.W))
	val BRESP = Output(UInt(2.W))
	//val BUSER = Output(UInt())


	val ARVALID = Input(Bool())
	val ARREADY = Output(Bool())
	//metadata
	val ARID = Input(UInt(idWidth.W))
	val ARADDR = Input(UInt(addressWidth.W))
	//val ARLEN = Input(UInt(8.W))
	//val ARSIZE = Input(UInt(3.W))
	//val ARBURST = Input(UInt(2.W))
	//val ARLOCK = Input(UInt(1.W))
	//val ARCACHE = Input(UInt(4.W))
	//val ARPROT = Input(UInt(3.W))
	//val ARQOS = Input(UInt(4.W))
	//val ARREGION = Input(UInt())
	//val ARUSER = Input(UInt())
	//ace signals
	val ARSNOOP = Input(UInt(4.W))
	//val ARDOMAIN = Input(UInt(2.W))
	val ARBAR = Input(UInt(2.W))


	val RVALID = Output(Bool())
	val RREADY = Input(Bool())
	//metadata
	val RID = Output(UInt(idWidth.W))
	val RDATA = Output(UInt(dataWidth.W))
	val RRESP = Output(UInt(4.W))          //0:1 is AXI, 2:3 is ACE
	val RLAST = Output(Bool())
	//val RUSER = Output(UInt())

	val ACVALID = Output(Bool())
	val ACREADY = Input(Bool())
	//metadata
	val ACADDR = Output(UInt(addressWidth.W))
	val ACSNOOP = Output(UInt(4.W))
	//val ACPROT = Output(UInt(3.W))

	val CRVALID = Input(Bool())
	val CRREADY = Output(Bool())
	//metadata
	val CRRESP = Input(UInt(5.W))

	val CDVALID = Input(Bool())
	val CDREADY = Output(Bool())
	//metadata
	val CDDATA = Input(UInt(dataWidth.W))
	val CDLAST = Input(Bool())
}

