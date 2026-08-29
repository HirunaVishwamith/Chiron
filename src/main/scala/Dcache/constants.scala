package Dcache

//AXI port ID is hard-coded

object constants{
  val lineSize : Int = 64 //8   //in bytes
  // 64 KB = 256 sets x 4 ways x 64 B. Everything downstream derives from this
  // (cacheLookupUnit.dataDepth / dataAddrWidth / tagSize), including the
  // fence.i walker's set counter, so this is the only knob.
  // Do NOT go to 256 KB: the clean-on-fence walker sweeps every set x way, and
  // 8x-ing that is the opposite of what Linux wants (see
  // docs/linux-boot-ipc-experiments.md).
  val cacheSize : Int = 64    //in KB
  val delay : Int = 2         //BRAM delay
  val cacheAddrWidth : Int = 32
  val cacheDataWidth : Int = 64*8  //8*8 
  // val depth : Int = 1
  val nway : Int = 4 
  val idWidth: Int = 3
  val fifoDepth : Int = 8

  val addrWidth : Int = 32
  val dataWidth : Int = 64
  val insWidth : Int = 32

  val branchMaskWidth : Int = common.configuration.newBranchMaskWidth
  val robAddrWidth : Int = common.configuration.robAddrWidth
  val prfAddrWidth : Int = common.configuration.prfAddrWidth

  val FIFO_ADDR_TX = "hE000_1030"
  val FIFO_ADDR_RX = "hE000_102C"

  // val dPort_ID : Int = 1
  val dPort_PROT : Int = 2
  // 128-bit ACE data path: a 64 B line is 4 beats, not 8. The data phase was
  // 7.3 beats/txn -- about a quarter of the whole interconnect service time --
  // and every one of those beats sits on the critical path of a miss the core
  // is stalled on. LEN/SIZE and busWidth below are the only knobs; ACEUnit,
  // its counters and its line slicing are all derived from them.
  // Must stay consistent with: ace.dataWidth, the CCU data ports, the
  // Interconnect FIFO word, ICache.busWidth/ARLEN, l2_Rob(arlen,beat_size)
  // and chironCore's LLC AWLEN/ARLEN.
  val dPort_LEN : Int = 3     // beats-1 per line (4 x 128b = 512b = 64 B)
  val dPort_SIZE : Int = 4    // 2^4 = 16 bytes = 128 bits per beat
  val dPort_WIDTH: Int = math.pow(2, dPort_SIZE).toInt * 8  // 64 //32

  // val peripheral_ID : Int = 1
  val peripheral_LEN : Int = 1        //= "b0000_0001"
  val peripheral_SIZE : Int = 2       //= "b010"
  val peripheral_WIDTH : Int = math.pow(2, peripheral_SIZE).toInt * 8//32      //64

  val schedulerDepth : Int = 16

  val cacheResponseWidth : Int = 2
  val arbiterReqTypesWidth : Int = 2

  val DRAM_BASE = "h80000000"
  // Must match the real DRAM backing (mainMemory is 2^28 bytes = 256 MB).
  // Addresses the D-cache classifies as main memory alias modulo this size in
  // mainMemory, so an oversized range silently maps MMIO onto the image: with
  // the old h7fffffff (2 GB), bbl's Zynq PS-UART console at 0xE000_002C/30
  // (machine/mtrap.c mcall_console_putchar) read image bytes instead of the
  // uartPort — its TXFULL poll saw bit 4 stuck high and wedged Linux boot
  // before the first kernel byte. Everything outside [DRAM_BASE,
  // DRAM_BASE+DRAM_RANGE] routes to the peripheral port, which already decodes
  // the PS-UART registers.
  val DRAM_RANGE = "h0fffffff"
}