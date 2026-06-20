package LLC_cache
import Chisel.log2Ceil
import chisel3._
import chisel3.experimental.BundleLiterals._
import chisel3.util._
import chisel3.experimental.IO
import os.write
import javax.print.DocFlavor.INPUT_STREAM



  class AXIlite(
    idWidth : Int=2,
    addressWidth : Int=32,
    dataWidth : Int=64
  ) extends Bundle{

      val AWID = Input(UInt(idWidth.W))
      val AWADDR = Input(UInt(addressWidth.W))
      val AWVALID = Input(Bool())
      val AWREADY = Output(Bool())


      val WVALID = Input(Bool())
      val WDATA = Input(UInt(dataWidth.W))
      val WREADY = Output(Bool())
      val WLAST = Input(Bool())
      

      val BRESP=Output(UInt(2.W))
      val BREADY=Input(Bool())
      val BVALID=Output(Bool())
      val BID = Output(Bool())

      val ARID = Input(UInt(idWidth.W))
      val ARADDR = Input(UInt(addressWidth.W))
      val ARVALID = Input(Bool()) 
      val ARREADY = Output(Bool())

  

      val RDATA = Output(UInt(dataWidth.W))
      val RID = Output(UInt(idWidth.W))
      val RREADY=Input(Bool())
      val RVALID=Output(Bool())
      val RRESP = Output(UInt(2.W))
      val RLAST = Output(Bool())

  }


  class AXIlite2(
    idWidth : Int=2,
    addressWidth : Int=32,
    dataWidth : Int=256
  ) extends Bundle{

      val AWADDR = Input(UInt(addressWidth.W))
      val AWVALID = Input(Bool())
      val AWREADY = Output(Bool())
      val AWCACHE = Input(UInt(4.W))
      val AWLEN = Input(UInt(8.W))
      val AWSIZE = Input(UInt(3.W))
      val AWLOCK = Input(UInt(2.W))
      val AWPROT = Input(UInt(3.W))
      val AWQOS = Input(UInt(4.W))
      val AWBURST = Input(UInt(2.W))
      val AWID = Input(UInt(idWidth.W))
      

      val WVALID = Input(Bool())
      val WDATA = Input(UInt(dataWidth.W))
      val WREADY = Output(Bool())
      val WSTRB = Input(UInt((dataWidth/8).W))
      val WLAST = Input(Bool())

      val BRESP = Output(UInt(2.W))
      val BVALID = Output(Bool())
      val BREADY = Input(Bool())
      val BID = Output(Bool())

      val ARID = Input(UInt(idWidth.W))
      val ARADDR = Input(UInt(addressWidth.W))
      val ARVALID = Input(Bool()) 
      val ARREADY = Output(Bool())
      val ARLEN = Input(UInt(8.W))
      val ARSIZE = Input(UInt(3.W))
      val ARBURST =Input(UInt(2.W))
      val ARLOCK = Input(UInt(2.W))
      val ARCACHE = Input(UInt(4.W))
      val ARPROT = Input(UInt(3.W))
      val ARQOS = Input(UInt(4.W))

      val RID = Output(UInt(idWidth.W))
	    val RDATA = Output(UInt(dataWidth.W))
	    val RRESP = Output(UInt(2.W))
	    val RLAST = Output(Bool())
	    val RVALID = Output(Bool())
	    val RREADY = Input(Bool())
  }