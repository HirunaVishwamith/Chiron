package common

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._

object configuration {
  val clock = 75000000 // Hz

  val waitTimeAfterReset =  20 // 1 // seconds

  def timeInCyclesFromSec(time: Int) =  (time*clock/*  / 32 */)

  // In-flight branch slots (bits [branchMaskWidth-1:0]) plus one coherency bit
  // at the top. Decode snapshots a rename map per slot. Width stays 4.
  // Decode stalls a new CFI only when every slot is taken. (A 2-in-flight
  // cap was a bandage for ~5% BPred; 1-in-flight hung Linux after
  // bootconsole. Widening to 8 on a polluted predictor made hart 0
  // almost entirely wrong-path.)
  val branchMaskWidth = 4
  val newBranchMaskWidth = branchMaskWidth + 1
  val coherent_BranchMask = (1 << branchMaskWidth).U(newBranchMaskWidth.W)
  val branchPC_depth = branchMaskWidth

  // Build knob retained for A/B debugging only. Default false: the fence.i
  // clean-on-fence walker is ON (required for rv64ui-p-fence_i and for correct
  // I-fetch after self-modifying / bbl kernel copy). The former Linux-only
  // dual-model path (walker off) is no longer needed: walkerWriteBackBuffer
  // decouples the walker's writebacks from the request/snoop path so SMP boot
  // no longer circular-waits on the single writeBackBuffer.
  val disableFenceIWalker = false
  val instrIssueDepth = 8
  // ROB depth = 2^robAddrWidth. 5 → 32 entries (was 4 → 16). Wider window
  // hides L1/L2 miss latency on Linux-like code; decode/issue/commit stay
  // 1-wide so peak IPC is still 1.0. PRF is 64 (32 architectural + 32
  // speculative) which matches a 32-entry ROB of dest-writing insns.
  val robAddrWidth = 4
  val prfAddrWidth = 6
  val ramBaseAddress = 0x0000000080000000L
  val ramHighAddress = 0x00000000ffffffffL
  val instructionBase = 0x0000000080000000L// = 0x0000000080000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L
  val bootBase = 0x0000000010000000L
  val bootHigh = 0x0000000010001f40L
  /**
    * Frontend branch prediction. Everything here only steers *which address
    * fetch requests next* — resolution and squash are untouched — so a bad
    * setting costs IPC, never correctness. `enableAdvancedPredictor = false`
    * restores the original BTB + gshare frontend for A/B from one build.
    */
  object frontend {
    val enableAdvancedPredictor = true
    val enableRAS               = true
    val enableTAGE              = true
    // Fetch never stalls for TAGE. Bimodal+BTB+RAS produce next_pc the same
    // cycle; TAGE is computed the same cycle but only registered, and wins
    // the next cycle if it disagrees (one bubble). JUMP/CALL/RET stay same-cycle.
    // Off: histo-q4 lost 7% cycles / 6.6pt C0 accuracy (TAGE != bimodal
    // often; the 1-cycle override still poisons later history). vvadd/filter
    // were small wins. Flip to true to A/B.
    val enableTageOverride      = false

    // Legacy gshare geometry, used when enableAdvancedPredictor is false.
    val gshareCounterDepth = 2048
    val gshareBtbSize      = 256

    // BTB hit is followed (taken). 8192 did not move Linux C0 (cold unique
    // CFIs, not capacity); memcpy hart already ~70%. Stay at 1024.
    val btbEntries     = 1024  // taken-target cache, written at resolution
    val cfiEntries     = 1024  // pre-decode branch classifier (cond/call/ret/…)
    val bimodalEntries = 2048  // TAGE base predictor

    val tageTableEntries    = 512
    val tageTagWidth        = 9
    val tageHistoryLengths  = Seq(4, 9, 19, 40)
    val tageAgeCounterWidth = 18 // usefulness reset every 2^18 trained branches
    val ghrLength           = tageHistoryLengths.max

    val rasDepth   = 16
    val rasSpWidth  = log2Ceil(rasDepth)
    val rasCntWidth = log2Ceil(rasDepth + 1)
    // Checkpointed alongside every in-flight fetch so a redirect can roll back.
    val rasCheckpointWidth = rasSpWidth + rasCntWidth
  }

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
    val ramBaseAddress = 0x0000000080000000L
    val ramHighAddress = 0x00000000ffffffffL
    val iCacheOffsetWidth = 4
    // Set-associative I$ in iCacheRegisters.v:
    //   2^lineWidth sets × 2^wayWidth ways × 2^offsetWidth instructions.
    //   4 / 2 / 4 → 16 sets × 4 ways × 16 × 4 B = 4 KB.
    //
    // Same 64 lines as the original direct-mapped 4 KB, now 4-way. Capacity
    // was measured and is NOT the lever: 4 KB → 32 KB (8x) moved the Linux
    // miss rate by ~7% and bought no IPC, because the I$ already ran a 99.35%
    // hit rate and the residual misses are compulsory -- cold code spread over
    // 3.4 MB of kernel text, which no size fixes. What actually cost ~130
    // cycles per miss was queueing in the CCU (see Interconnect/ccu.scala).
    //
    // So this stays at the original 4 KB: iCacheRegisters.v is a REGISTER
    // array, and 32 KB is 8x the flip-flops per core for nothing. The ways are
    // free relative to that -- same storage, fewer conflict misses. Way
    // selection lives inside the Verilog, so ICache.scala's hit test is
    // unchanged.
    //
    // Unlike the D$ this costs nothing at fence.i: the I$ invalidate is a
    // flash clear of the packed validBits vector, not a set×way walk. That is
    // why the I$ may grow and the D$ may not.
    val iCacheLineWidth = 4
    val iCacheWayWidth = 2
    val iCacheTagWidth = 32 - iCacheLineWidth - iCacheOffsetWidth - 2
    val iCacheBlockSize = (1 << iCacheOffsetWidth) // number of instructions

    // Next-line instruction prefetch (Icache/ICache.scala). Kernel text runs
    // mostly straight-line, so when the fill for line L lands, L+1 is very
    // likely the next miss -- fetch it while the core still has L's 16
    // instructions to chew through, and keep running ahead up to
    // iCachePrefetchDepth lines until a demand miss interrupts.
    //
    // Off by default: there is one fill engine and no MSHRs, so a demand miss
    // raised while a prefetch is in flight has to wait for it, and a wrong
    // prefetch is pure added load on a CCU that is the machine's throughput
    // limit. Only worth enabling once that has headroom -- measure, do not
    // assume.
    val iCachePrefetch = false
    val iCachePrefetchDepth = 4
    val dCacheDoubleWordOffsetWidth = 3
    val dCacheLineWidth = 6
    val dCacheTagWidth = 32 - dCacheLineWidth - dCacheDoubleWordOffsetWidth - 3
    val dCacheBlockSize = (1 << dCacheDoubleWordOffsetWidth)
    val instructionBase = 0x0000000080000000L// = 0x0000000080000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L// = 0x0000000010000000L
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

    def connectDefaultMaster(port: Icache.AXI): Unit = {
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
