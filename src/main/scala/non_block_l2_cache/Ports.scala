package l2_cache

import Chisel.log2Ceil
import chisel3._
import chisel3.experimental.BundleLiterals._
import chisel3.util._
import chisel3.experimental.IO

  
  class composableInterface extends Bundle {
    val ready = Output(Bool())
    val fired = Input(Bool())
  }

  class AXIlite1(
    idWidth : Int=2,
    addressWidth : Int=32,
    dataWidth : Int=256
  ) extends Bundle{

      val ARADDR = Input(UInt(addressWidth.W))
      val ARID = Input(UInt(idWidth.W))
      val ARVALID = Input(Bool()) 
      val ARREADY = Output(Bool())
      val ARLEN = Input(UInt(4.W))
      val ARSIZE = Input(UInt(3.W))
      val ARBURST =Input(UInt(2.W))
      val ARLOCK = Input(UInt(2.W))
      val ARCACHE = Input(UInt(4.W))
      val ARPROT = Input(UInt(3.W))
      val ARQOS = Input(UInt(4.W))

      val RDATA = Output(UInt(dataWidth.W))
      val RID = Output(UInt(idWidth.W))
      val RRESP = Output(UInt(2.W))
      val RREADY=Input(Bool())
      val RVALID=Output(Bool())
      val RLAST = Output(Bool())

  }

  class AXIlite2(
    idWidth : Int=2,
    addressWidth : Int=32,
    dataWidth : Int=256
  ) extends Bundle{

      val AWADDR = Input(UInt(addressWidth.W))
      val AWREADY = Output(Bool())
      val AWVALID = Input(Bool())
      val AWLEN = Input(UInt(4.W))
      val AWCACHE = Input(UInt(4.W))
      val AWSIZE = Input(UInt(3.W))
      val AWLOCK = Input(UInt(2.W))
      val AWPROT = Input(UInt(3.W))
      val AWQOS = Input(UInt(4.W))
      val AWBURST = Input(UInt(2.W))
      val AWID = Input(UInt(idWidth.W))

      val WDATA = Input(UInt(dataWidth.W))
      val WREADY = Output(Bool())
      val WVALID = Input(Bool())
      val WSTRB = Input(UInt((dataWidth/8).W))
      val WLAST = Input(Bool())

      val BREADY = Input(Bool())
      val BVALID = Output(Bool())
      val BRESP = Output(Bool())
      val BID = Output(Bool())
  }



  class AXIlite(
    idWidth : Int=2,
    addressWidth : Int=32,
    dataWidth : Int=64
  ) extends Bundle{

      val ARADDR = Input(UInt(addressWidth.W))
      val ARVALID = Input(Bool()) 
      val ARREADY = Output(Bool())
      val ARLEN = Input(UInt(4.W))
      val ARID = Input(UInt(idWidth.W))

      val RDATA = Output(UInt(dataWidth.W))
      val RREADY=Input(Bool())
      val RVALID=Output(Bool())
      val RID = Output(UInt(idWidth.W))
      val RRESP = Output(UInt(2.W))
      val RLAST = Output(Bool())

      val AWADDR = Input(UInt(addressWidth.W))
      val AWREADY = Output(Bool())
      val AWVALID = Input(Bool())
      val AWLEN = Input(UInt(4.W))
      val AWID = Input(UInt(idWidth.W))


      val WDATA = Input(UInt(dataWidth.W))
      val WREADY = Output(Bool())
      val WVALID = Input(Bool())
      val WLAST = Input(Bool())

      val BREADY = Input(Bool())
      val BVALID = Output(Bool())
      val BRESP = Output(Bool())
      val BID = Output(Bool())

  }


  class Rob_out(
    Rob_depth : Int=8,
    addressWidth : Int=32
  ) extends composableInterface {
    val Rob_address = Output(UInt(log2Ceil(Rob_depth).W))
    val Mem_address = Output(UInt(addressWidth.W))
    val data = Output(UInt(512.W))
    val valid = Output(Bool()) 
    val is_R = Output(Bool())
  }


  class Rob_in(
    Rob_depth : Int=8,
    addressWidth : Int=32
  )extends composableInterface{
    val Rob_address = Input(UInt(log2Ceil(Rob_depth).W))
    val Mem_address = Input(UInt(addressWidth.W))
    val data = Input(UInt(512.W))
    val valid = Input(Bool()) 
    val is_R = Input(Bool())
  }




  class cache_in(
    addressWidth : Int=32,
    Rob_depth : Int=8
  ) extends composableInterface{
    val addr = Input(UInt(addressWidth.W))
    val Rob_address = Input(UInt(log2Ceil(Rob_depth).W))
    val is_R =Input(Bool())
    val data = Input(UInt(512.W))
    val valid = Input(Bool())
    val from_MSHR = Input(Bool())
    val R_data = Input(Vec(4,UInt(512.W)))
    //val full_line = Input(Bool())

  }



  class cache_hit_out(
    addressWidth : Int=32,
    Rob_depth : Int=8
  )extends composableInterface{
    val Rob_address = Output(UInt(log2Ceil(Rob_depth).W))
    val readData = Output(UInt(512.W))
    val addr = Output(UInt(addressWidth.W))
    val is_R = Output(Bool())
    val valid = Output(Bool())
  }



  class cache_miss_out(
    addressWidth : Int=32,
    Rob_depth : Int=8
  )extends composableInterface{
    val Rob_addr = Output(UInt(log2Ceil(Rob_depth).W))
    val Mem_addr = Output(UInt(addressWidth.W))
    val w_data = Output(UInt(512.W))
    val is_R = Output(Bool())
    val valid = Output(Bool())
    val replace = Output(Bool())
    val rep_data = Output(Vec(4,UInt(512.W)))
    val replaWire = Output(Bool())
  }


  class cache_miss_in(
    addressWidth : Int=32,
    Rob_depth : Int=8
  )extends composableInterface{
    val Rob_addr = Input(UInt(log2Ceil(Rob_depth).W))
    val Mem_addr = Input(UInt(addressWidth.W))
    val R_data = Input(Vec(4,UInt(512.W)))
    val valid = Input(Bool())
  }



  class Mem_read_in(
    addressWidth : Int=32,
    Rob_depth : Int=8
  )extends composableInterface{
    val Rob_addr = Input(UInt(log2Ceil(Rob_depth).W))
    val Mem_addr = Input(UInt(addressWidth.W))
    val W_data = Input(UInt(512.W))
    val repl_hit = Input(Bool())
    val valid = Input(Bool())
    val repl_data = Input(Vec(4,UInt(512.W)))
    val is_R = Input(Bool())
  }


  class Mem_read_out(
    addressWidth : Int=32,
    Rob_depth : Int=8
  )extends composableInterface{
    val Rob_addr = Output(UInt(log2Ceil(Rob_depth).W))
    val Mem_addr = Output(UInt(addressWidth.W))
    val R_data = Output(Vec(4,UInt(512.W)))
    val valid = Output(Bool())
    val is_R = Output(Bool())
  }

  class replace_in(
    addressWidth : Int=32,
    Rob_depth : Int=8
  )extends composableInterface{
    val Mem_addr = Input(UInt(addressWidth.W))
    val data = Input((Vec(4,UInt(512.W))))
  }

  class miss_out(
  )extends Bundle{
    val repl_hit = Output(Bool())
    val data = Output((Vec(4,UInt(512.W))))
  }
