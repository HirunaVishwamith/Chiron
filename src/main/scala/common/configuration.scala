package common

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._

object configuration {
  val clock = 75000000 // Hz

  val waitTimeAfterReset =  20 // 1 // seconds

  def timeInCyclesFromSec(time: Int) =  (time*clock/*  / 32 */)

  val newBranchMaskWidth = 5; //leon for coherency 
  val coherent_BranchMask=0x10.U(newBranchMaskWidth.W) // leon for coherency
  val branchPC_depth = 4


  val instrIssueDepth = 8
  val robAddrWidth = 4
  val prfAddrWidth = 6
  val branchMaskWidth = 4
  val ramBaseAddress = 0x0000000010000000L
  val ramHighAddress = 0x000000009fffffffL
  val instructionBase = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L
  val bootBase = 0
  val bootHigh = 0x00000000000fffffL
  object cache {
    val newRequestBufferDepth = 4
    val dependentReadsDepth = 4
    val storeInstructionDepth = 4
    val associativity = 2
    val missStoreDepth = 4 // > 2
    val lineIndexWidth = 6 // offsetLineWidth = 3
    val wordOffsetWidth = 3 // word size is 64-bits
    // should handle all instructions in cache pipeline after handler saturates (>4)
    val saturatedMissDepth = 5 
    val hitUnderMissDepth = 4 // iff for the same cache block
    val writeDepth = 4
  }
  object addressSpace {
    val addressWidth = 32
    val dataWidth = 32
  }

  def inRangeRAM(address: UInt) = (address >= ramBaseAddress.U) && (address <= ramHighAddress.U)
  def inBootAccess(address: UInt) = (address >= bootBase.U) && (address <= bootHigh.U)

  object virtualPeripheral {
    val txFIFOLength = 64
    val baudRate = 115200
    val charClearTimeTx = (clock/baudRate)*20
    val mtimeWidth = 64

    object addressSpace {
      object ZynqPSUart0 {
        val XUARTPS_SR_OFFSET = 0x0E000002CL
        val XUARTPS_FIFO_OFFSET = 0x0E0000030L
      }

      object ZynqPSUart1 {
        val XUARTPS_SR_OFFSET = 0x0E000102CL
        val XUARTPS_FIFO_OFFSET = 0x0E0001030L
      }

      object clint {
        val mtime = 0x0200bff8
        val mtimecmp = 0x02004000
        val msip = 0x02000000
      }
    } 

    def XUARTPS_SR_TXFULL(XUARTPS_SR_OFFSET: UInt) =
      (XUARTPS_SR_OFFSET >> 4.U)(0).asBool
  }
}

object coreConfiguration {
    val robAddrWidth = 3
    val ramBaseAddress = 0x0000000010000000L
    val ramHighAddress = 0x000000009fffffffL
    val iCacheOffsetWidth = 4
    val iCacheLineWidth = 6
    val iCacheTagWidth = 32 - iCacheLineWidth - iCacheOffsetWidth - 2
    val iCacheBlockSize = (1 << iCacheOffsetWidth) // number of instructions
    val dCacheDoubleWordOffsetWidth = 3
    val dCacheLineWidth = 6
    val dCacheTagWidth = 32 - dCacheLineWidth - dCacheDoubleWordOffsetWidth - 3
    val dCacheBlockSize = (1 << dCacheDoubleWordOffsetWidth)
    val instructionBase = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L
}

object constants {
  // These will be constants for all configurations
  val byteSize = 8 // in bits
  object AXIFieldLengths {
    val LEN = 8
    val SIZE = 3
    val BURST = 2
    val LOCK = 1
    val CACHE = 4
    val PROT = 3
    val QOS = 4
  }
  object AXI {
    object BURST {
      val FIXED = 0
      val INCR = 1
      val WRAP = 2
    }
    
    def encodeARCACHE (
      Bufferable: Boolean = false,
      Modifiable: Boolean = false,
      Allocate: Boolean = false,
      Other_Allocate: Boolean = false
    ): Int = {
      Seq(Bufferable, Modifiable, Allocate, Other_Allocate)
      .zipWithIndex.map { case (bit, index) => if (bit) (1 << index) else 0}
      .reduce(_ + _)
    }

    def encodeAWCACHE (
      Bufferable: Boolean = false,
      Modifiable: Boolean = false,
      Other_Allocate: Boolean = false,
      Allocate: Boolean = false
    ): Int = {
      Seq(Bufferable, Modifiable, Other_Allocate, Allocate)
      .zipWithIndex.map { case (bit, index) => if (bit) (1 << index) else 0}
      .reduce(_ + _)
    }

    def encodeAXPROT (
      Priviledged: Boolean = false,
      Secure: Boolean = false,
      Instruction_access: Boolean = false
    ): Int = {
      Seq(Priviledged, !Secure, Instruction_access)
      .zipWithIndex.map { case (bit, index) => if (bit) (1 << index) else 0}
      .reduce(_ + _)
    }

    def encodeAXLEN (
      AXSIZE: Int = 2, 
      opSizeInBytes: Int = 4
    ): Int = (opSizeInBytes/(1 << AXSIZE)) - 1

    def encodeAXSIZE (bytesPerBeat: Int = 4) = 
      log2Ceil(bytesPerBeat)

    val noQOSDefined = 0

    val defaultIDForOneClient = 0

    object RRESP {
      val OKAY = 0x0 
      val EXOKAY = 0x1 
      val SLVERR = 0x2 
      val DECERR = 0x3 
      val PREFETCHED = 0x4 
      val TRANSFAULT = 0x5 
      val OKAYDIRTY = 0x6 
      val RESERVED = 0x7  
    }

    object BRESP {
      val OKAY = 0x0 
      val EXOKAY = 0x1 
      val SLVERR = 0x2 
      val DECERR = 0x3 
      val DEFER = 0x4 
      val TRANSFAULT = 0x5 
      val RESERVED = 0x6 
      val UNSUPPORTED = 0x7 
    }

    def connectDefaultMaster(port: cache.AXI): Unit = {
      port.ARVALID := false.B
      port.ARADDR := 0.U
      port.ARBURST := AXI.BURST.INCR.U
      port.ARCACHE := AXI.encodeARCACHE().U
      port.ARID := AXI.defaultIDForOneClient.U
      // making a 32 bit read
      port.ARLEN := AXI.encodeAXLEN().U
      port.ARLOCK := false.B.asUInt
      port.ARPROT := AXI.encodeAXPROT(Secure = true).U
      port.ARQOS := AXI.noQOSDefined.U
      port.ARSIZE := AXI.encodeAXSIZE().U

      port.AWVALID := false.B
      port.AWADDR := 0.U
      port.AWBURST := AXI.BURST.INCR.U
      port.AWCACHE := AXI.encodeAWCACHE(Modifiable = true).U
      port.AWID := AXI.defaultIDForOneClient.U
      port.AWLEN := AXI.encodeAXLEN().U
      port.AWLOCK := false.B.asUInt
      port.AWPROT := AXI.encodeAXPROT(Secure = true).B
      port.AWQOS := AXI.noQOSDefined.U
      port.AWSIZE := AXI.encodeAXSIZE().U

      port.WVALID := false.B
      port.WDATA := 0.U
      port.WLAST := true.B
      port.WSTRB := 1.U // TODO: Mention why 1
      
      port.RREADY := false.B

      port.BREADY := false.B

    }
  }
}
