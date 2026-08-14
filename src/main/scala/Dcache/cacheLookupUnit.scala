package Dcache

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import Dcache._
import Dcache.constants._
import Dcache.ChiselUtils._
import os.size

//? After compiling
//TODO : In replay requests data not required field assertted then should not update cacheline
//TODO : Add cacheLine required track fifo
//TODO : -Enque if no match
//TODO : -Deque if match found

class cacheLookupUnit extends Module{
  val request = IO(new Bundle {
    val ready = Output(Bool())
    val holdInOrder = Output(Bool())
    val inorderPending = Output(Bool())
    val requestType = Input(UInt(2.W))
    val request = Input(new requestPipelineWire)
  })
  val toReplay = IO(new Bundle {
    val ready = Input(Bool())
    val request = Output(new requestPipelineWire)
  })
  val toWriteBack = IO(new Bundle {
    val ready = Input(Bool())
    val request = Output(new writeBackWire)
  })
  val toCoherency = IO(new Bundle {
    val ready = Input(Bool())
    val request = Output(new coherencyResponseWire)
  })
  val toResponse = IO(new Bundle {
    val request = Output(new requestPipelineWire)
  })
  val writeInstructionCommit = IO(new composableInterface)
  // Coherence snoop into the *staging* writeback slots (before the FIFO).
  // On replacement the victim tags are rewritten the same cycle as these
  // registers fill, so a peer snoop already misses L1 while the only dirty
  // copy sits here — answering "no data" poisons shared kernel data
  // (jiffies / tick_broadcast_lock under SMP).
  val writeBackStageSnoop = IO(new Bundle {
    val addr = Input(UInt(addrWidth.W))
    val hit = Output(Bool())
    val data = Output(new writeBackWire)
  })
  // Same-line snoop-vs-fill-install hazard. A replayed fill installs into the
  // BRAMs one cycle after dispatch; a snoop arbitrated just before/at that
  // write reads pre-install tags (or the same-cycle RAW returns old data),
  // answers "not present", and the stale fill then installs as VALID — a
  // permanently stale line no later invalidation can reach (proven on the
  // Linux SMP boot: CPU1 kept a zero task pointer from a line CPU0 owned
  // dirty). The ACE unit holds a same-line snoop while this is high; it
  // covers the dispatch stage and the BRAM-write cycle after it.
  val installSnoop = IO(new Bundle {
    val addr = Input(UInt(addrWidth.W))
    val hazard = Output(Bool())
  })
  // LR/SC forward-progress guard: for a bounded window after an LR sets the
  // reservation, the arbiter defers snoops that target the reserved LINE so
  // the subsequent SC can complete before a peer steals the line. Without
  // this, four harts in the RISC-V constrained lr/sc retry loop steal the
  // line from each other's LR-to-SC window forever — proven total livelock
  // (mt-lrsc, all four harts parked 130M+ cycles, counter never advanced).
  // The window is a pure countdown, so snoop service is delayed but never
  // blocked — deadlock-freedom is preserved.
  val reservationGuard = IO(new Bundle {
    val active = Output(Bool())
    val address = Output(UInt(addrWidth.W))
  })
  //Clean-on-fence.i walker : sweeps all sets/ways, writing back dirty lines to L2
  val flush = IO(new Bundle {
    val start = Input(Bool())
    val busy = Output(Bool())
  })
  val branchOps = IO(new branchOps)
  //! Debug only
  val debug = IO(new debug)

  request.ready := false.B
  request.holdInOrder := false.B
  zeroInit(toResponse.request)
  zeroInit(toCoherency.request)
  zeroInit(toWriteBack.request)
  zeroInit(toReplay.request)

  //-------------------Operation Valid register-------------------//
  val operationValid = RegNext(request.ready && request.request.valid && request.request.branch.valid)

  //-----------------------Data BRAM------------------------------//
  val blockCount = nway                                   //No.of blocks
  val dataDepth = (cacheSize * 1024) / (lineSize * nway)
  val dataAddrWidth = log2Ceil(dataDepth)
  val dataDataWidth = lineSize * 8

  val dataBRAM = Seq.fill(nway)(Module(new moduleForwardingMemory(
    addrWidth = dataAddrWidth, 
    dataWidth = dataDataWidth, 
    depth = dataDepth
  )))

  // Initialize all BRAM instances
  dataBRAM.foreach { bram =>
    bram.rdAddr := 0.U
    bram.wrAddr := 0.U
    bram.wrData := 0.U
    bram.wrEna := false.B
  }

  // Create vector of forwarding memory interfaces
  val dataBRAMVec = VecInit(dataBRAM.map { bram =>
    val bundle = Wire(new Bundle {
      val rdData = UInt(dataDataWidth.W)
      val wrEna = Bool()
      val wrData = UInt(dataDataWidth.W)
      val rdAddr = UInt(dataAddrWidth.W)
      val wrAddr = UInt(dataAddrWidth.W)
    })
    bundle.rdData := 0.U
    bundle.wrEna := false.B
    bundle.wrData := 0.U
    bundle.rdAddr := 0.U
    bundle.wrAddr := 0.U
    bundle
  })

  // Connect forwarding memory interfaces to BRAM instances
  dataBRAM.zip(dataBRAMVec).foreach { case (bram, vec) =>
    vec.rdData := bram.rdData
    bram.rdAddr := vec.rdAddr
    bram.wrAddr := vec.wrAddr
    bram.wrData := vec.wrData
    bram.wrEna := vec.wrEna
  }

  //-----------------------Tag BRAM--------------------------------//
  //------Tag structure-------//
  // PLRU bit | Shared bit | Modified bit | Validity bit | Tag bits

  val tagSize = addrWidth - dataAddrWidth - log2Ceil(lineSize)    //Per cache line
  val tagDepth = dataDepth
  val tagAddrWidth = log2Ceil(tagDepth)
  val tagSection = 4 + tagSize    // nway no.of tag sections will be kept in one BRAM
  val tagDataWidth = (nway * tagSection)  //4 bits for flags

  val tagBRAM = Module(new moduleForwardingMemory(
    addrWidth = tagAddrWidth, 
    dataWidth = tagDataWidth, 
    depth = tagDepth
  ))
  tagBRAM.rdAddr := 0.U
  tagBRAM.wrData := 0.U
  tagBRAM.wrEna  := false.B
  tagBRAM.wrAddr := false.B
  
  //---------------------Reservation register---------------------//
  val reservationRegister = RegInit(0.U.asTypeOf(new Bundle {
    val address = UInt(addrWidth.W)
    val reserved = Bool()
    //To say if a word - 0.U, or a double word - 1.U
    val size = UInt(1.W)  
  }))
  val toReservationRegisterWire = WireDefault(false.B)
  // Guard countdown (see reservationGuard IO). 128 cycles comfortably covers
  // LR writeback/commit plus SC dispatch through the inorder queue; kept
  // small so a deferred snoop's extra latency stays negligible.
  //
  // ONE-TENURE RULE: a spinning cmpxchg (`while (cmpxchg(&lock,0,id))`)
  // executes a fresh LR every iteration; re-arming the guard on each of them
  // holds the line hostage indefinitely and defers the lock HOLDER's release
  // store forever — proven deadlock (mt-lrsc phase 3, victim hart parked at
  // lr.d.aq with no commit). So when a guard window expires WITHOUT an SC,
  // guardBlocked suppresses re-arming until the deferred snoop actually
  // lands on the reserved line (or an SC completes). Every window therefore
  // drains at least one pending snoop before a new one can open.
  val reservationGuardCnt = RegInit(0.U(8.W))
  val reservationGuardBlocked = RegInit(false.B)
  val scWritePassFire = WireDefault(false.B)
  val guardSnoopLanded = WireDefault(false.B)   // set at the kill point below
  // The countdown is MONOTONIC within a tenure: a spinner's retry LRs come
  // every ~15 cycles, so reloading on each would keep the guard alive
  // forever (probe-proven: guard cnt cycling 82..121 while the lock
  // holder's release-store snoop sat parked and two other harts starved at
  // their LRs with no commit). Arming only from cnt==0 caps every tenure at
  // one window; an SC ends the tenure cleanly and allows an immediate
  // re-arm, expiry without an SC requires the pending snoop to land first.
  when(toReservationRegisterWire && !reservationGuardBlocked && !reservationGuardCnt.orR) {
    reservationGuardCnt := 128.U
  }.elsewhen(scWritePassFire || !reservationRegister.reserved) {
    reservationGuardCnt := 0.U
  }.elsewhen(reservationGuardCnt.orR) {
    reservationGuardCnt := reservationGuardCnt - 1.U
    when(reservationGuardCnt === 1.U) { reservationGuardBlocked := true.B }
  }
  when(scWritePassFire || guardSnoopLanded) { reservationGuardBlocked := false.B }
  reservationGuard.active := reservationGuardCnt.orR && reservationRegister.reserved
  reservationGuard.address := reservationRegister.address

  //-----------------------Last Miss record-----------------------//
  val lastInorderMissRecordRegister = RegInit(0.U.asTypeOf(new requestPipelineWire))

  val toLastInorderMissRecordRegisterWire = WireDefault(false.B)  
  when(request.ready && request.request.valid && request.request.branch.valid && lastInorderMissRecordRegister.valid && lastInorderMissRecordRegister.branch.valid){
    val replayMatch = WireDefault(
      (request.request.address === lastInorderMissRecordRegister.address) &&
      (request.request.core.instruction === lastInorderMissRecordRegister.core.instruction) &&
      (request.request.core.robAddr === lastInorderMissRecordRegister.core.robAddr) &&
      (request.request.core.prfDest === lastInorderMissRecordRegister.core.prfDest) 
    )
    when(replayMatch){
      lastInorderMissRecordRegister.valid := false.B
    }
  }

  val lastSpeculativeMissRecordRegister = RegInit(0.U.asTypeOf(new requestPipelineWire))

  val toLastSpeculativetMissRecordRegisterWire = WireDefault(false.B)  
  when(request.ready && request.request.valid && request.request.branch.valid && lastSpeculativeMissRecordRegister.valid && lastSpeculativeMissRecordRegister.branch.valid){
    val replayMatch = WireDefault(
      (request.request.address === lastSpeculativeMissRecordRegister.address) &&
      (request.request.core.instruction === lastSpeculativeMissRecordRegister.core.instruction) &&
      (request.request.core.robAddr === lastSpeculativeMissRecordRegister.core.robAddr) &&
      (request.request.core.prfDest === lastSpeculativeMissRecordRegister.core.prfDest) 
    )
    when(replayMatch){
      lastSpeculativeMissRecordRegister.valid := false.B
    }
  }

  //Read Buffer
  val readBuffer = RegInit(0.U.asTypeOf(new requestPipelineWire))
  val requestType = RegInit(0.U(2.W))

  //* For output buffers need to only update the memory response buffer
  //* Other outputs with instructions (replay Buffer) goes to an FIFO

  //To replay buffer
  val replayBuffer = RegInit(0.U.asTypeOf(new requestPipelineWire))
  val toReplayValidWire = WireDefault(false.B)
  when(toReplay.ready){
    toReplay.request := replayBuffer
    replayBuffer.valid := false.B
  }

  val toMemoryResponseValidWire = WireDefault(false.B)

  val coherencyResponseBuffer = RegInit(0.U.asTypeOf(new coherencyResponseWire))
  val toCoherencyResponseValidWire = WireDefault(false.B)
  when(toCoherency.ready){
    toCoherency.request := coherencyResponseBuffer
    when(coherencyResponseBuffer.valid){
      coherencyResponseBuffer.valid := false.B
    }
  }

  // Two writeback staging registers:
  //   writeBackBuffer       — load/store/miss replacement path (gates request.ready)
  //   walkerWriteBackBuffer — fence.i clean walker only
  // They share the single toWriteBack / writeBackFIFO port via a priority mux
  // (normal first, then walker). Separating them is load-bearing for Linux SMP:
  // bbl's fence.i walks a dirty D-cache full of the just-copied kernel image and
  // would otherwise pin the only writeBackBuffer while the CCU waits on a snoop
  // response that itself needs request.ready — a circular wait (confirmed: 20M
  // cycles, zero commits). With a dedicated walker buffer, request.ready only
  // cares about the normal-path staging slot, so snoops keep flowing while the
  // walker drains through the shared FIFO.
  val writeBackBuffer = RegInit(0.U.asTypeOf(new writeBackWire))
  val walkerWriteBackBuffer = RegInit(0.U.asTypeOf(new writeBackWire))
  val toWriteBackValidWire = WireDefault(false.B)
  when(toWriteBack.ready) {
    when(writeBackBuffer.valid) {
      toWriteBack.request := writeBackBuffer
      writeBackBuffer.valid := false.B
    }.elsewhen(walkerWriteBackBuffer.valid) {
      toWriteBack.request := walkerWriteBackBuffer
      walkerWriteBackBuffer.valid := false.B
    }.otherwise {
      // Keep valid low so the writeBackFIFO never samples a stale beat.
      toWriteBack.request.valid := false.B
    }
  }

  // Install hazard: replay request in the lookup stage now, or its BRAM write
  // cycle (one after). Line-granular compare against the pending snoop.
  {
    val lineHi = log2Ceil(lineSize)
    val installingNow = readBuffer.valid && requestType === "b10".U
    val installedLast = RegNext(installingNow, false.B)
    val installAddrLast = RegNext(readBuffer.address)
    val snoopLine = installSnoop.addr(addrWidth - 1, lineHi)
    installSnoop.hazard :=
      (installingNow && readBuffer.address(addrWidth - 1, lineHi) === snoopLine) ||
      (installedLast && installAddrLast(addrWidth - 1, lineHi) === snoopLine)
  }

  // Snoop CAM for the staging slots. ONLY the eviction slot may answer: the
  // walker slot carries retain=true because the tag was never reassigned, so
  // the L1 still holds that line and may have been stored into after the
  // walker captured it. Answering from the walker copy returns pre-store data
  // (see writeBackTrait.retain).
  {
    val lineHi = log2Ceil(lineSize)
    val snoopLine = writeBackStageSnoop.addr(addrWidth - 1, lineHi)
    val wbHit = writeBackBuffer.valid && !writeBackBuffer.retain &&
      writeBackBuffer.address(addrWidth - 1, lineHi) === snoopLine
    writeBackStageSnoop.hit := wbHit
    writeBackStageSnoop.data := writeBackBuffer
  }

  val writeCommitInstructionBuffer = RegInit(false.B)
  writeInstructionCommit.ready := writeCommitInstructionBuffer
  when(writeInstructionCommit.fired){
    writeCommitInstructionBuffer := false.B
  }

  //-----------------------Clean-on-fence.i walker state------------------//
  // Yield-to-snoop design: the walker must NOT block the request path, because
  // under SMP that path also carries coherency snoop requests from other cores
  // (arbiter routes them into cacheLookup only when request.ready is high). If
  // the walker held request.ready low for the whole sweep, this core would stop
  // answering snoops and the CCU/L2 would deadlock (its writeback needs the CCU,
  // which may be waiting on a snoop response from this very core). So instead the
  // walker (a) never gates request.ready, (b) reads each set's tags/data on a
  // cycle when no request is being serviced and latches them into registers, then
  // iterates the ways from those registers (immune to snoops stealing the BRAM),
  // and (c) posts only to walkerWriteBackBuffer (never the normal-path buffer).
  val sFlushIdle :: sFlushRead :: sFlushCapture :: sFlushEmit :: sFlushDrain :: Nil = Enum(5)
  val flushState = RegInit(sFlushIdle)
  val flushSet = RegInit(0.U(dataAddrWidth.W))
  val flushWay = RegInit(0.U(log2Ceil(nway).W))
  val flushActive = flushState =/= sFlushIdle
  val flushTagReg  = Reg(Vec(nway, UInt(tagSection.W)))
  val flushDataReg = Reg(Vec(nway, UInt(dataDataWidth.W)))
  // Set-poison: if a snoop touches the set the walker just captured, the
  // captured tags/data may be stale (snoop may have taken the dirty line via
  // CD). Drop the capture and re-read the set so we never write back a line we
  // no longer own.
  val flushSetPoison = RegInit(false.B)
  flush.busy := flushActive

  //____________________Functional description_________________________//
  //Response out is always release in one clock cycle, so no ready signal
  //NOTE: request.ready is intentionally NOT gated by flushActive -- see the
  //walker comment above (snoops must keep flowing during a fence.i sweep).
  // Only the normal-path writeBackBuffer backpressures request admission; the
  // walker has its own staging slot so a full writeBackFIFO no longer freezes
  // coherency snoops mid-fence.i.
  request.ready := toReplay.ready && toCoherency.ready &&
    (toWriteBack.ready || !writeBackBuffer.valid)
  // A request (snoop/load/store/replay) is entering the pipeline this cycle and
  // owns the BRAM read port; the walker must yield the port on these cycles.
  val servingRequest = request.ready && request.request.valid
  val islastInorderMissRecordRegisterValid = toLastInorderMissRecordRegisterWire || lastInorderMissRecordRegister.valid && lastInorderMissRecordRegister.branch.valid
  val islastSpeculativeMissRecordRegisterValid = toLastSpeculativetMissRecordRegisterWire || lastSpeculativeMissRecordRegister.valid && lastSpeculativeMissRecordRegister.branch.valid
  request.holdInOrder := (islastInorderMissRecordRegisterValid || islastSpeculativeMissRecordRegisterValid) && !writeCommitInstructionBuffer
  request.inorderPending := islastInorderMissRecordRegisterValid

  //Assigning addresses to the BRAMs
  val addrBeg = log2Ceil(lineSize)
  val addrEnd = dataAddrWidth - 1 + addrBeg
  dataBRAMVec.foreach { bram => bram.rdAddr := 0.U}
  tagBRAM.rdAddr := 0.U

  //Connecting correct addresses to the BRAMS as per request type
  when(request.request.valid && request.request.branch.valid){
    dataBRAMVec.foreach {bram => bram.rdAddr := request.request.address(addrEnd, addrBeg)}
    tagBRAM.rdAddr := request.request.address(addrEnd, addrBeg)

    readBuffer := request.request
    regWriteUpdate(readBuffer.branch, branchOps, request.request.branch)
    requestType := request.requestType
  } .otherwise {
    readBuffer.valid := false.B
  }

  //BranchOps of valid registers
    //BranchOps for valid registers
  when(replayBuffer.valid && replayBuffer.branch.valid){
    regRecordUpdate(replayBuffer.branch, branchOps)
  }
  //Input buffers
  when(readBuffer.valid && readBuffer.branch.valid && !operationValid){
    regRecordUpdate(readBuffer.branch, branchOps)
  }
  when(lastInorderMissRecordRegister.valid && lastInorderMissRecordRegister.branch.valid){
    regRecordUpdate(lastInorderMissRecordRegister.branch, branchOps)
  }
  when(lastSpeculativeMissRecordRegister.valid && lastSpeculativeMissRecordRegister.branch.valid){
    regRecordUpdate(lastSpeculativeMissRecordRegister.branch, branchOps)
  }

  //Main functions
  when(operationValid){
    //Setting control wires for request types
    val isReadWire = WireDefault(readBuffer.core.instruction(6,0) === "b0000011".U)
    val isWriteWire = WireDefault(readBuffer.core.instruction(6,0) === "b0100011".U)
    val isCoherentWire = WireDefault(requestType === "b11".U)
    val isAtomicsWire = WireDefault((readBuffer.core.instruction(6,0) === "b0101111".U))
    val isLRWire = WireDefault(readBuffer.core.instruction(31,27) === "b00010".U && isAtomicsWire)
    val isSCWire = WireDefault(readBuffer.core.instruction(31,27) === "b00011".U && isAtomicsWire)
    val isAtmoicReadWire = WireDefault(isAtomicsWire && !readBuffer.writeData.valid && !(isSCWire || isLRWire))
    val isAtmoicWriteWire = WireDefault(isAtomicsWire && readBuffer.writeData.valid && !(isSCWire || isLRWire))
    val isLRReadWire = WireDefault(isLRWire && !readBuffer.writeData.valid)
    val isLRWriteWire = WireDefault(isLRWire && readBuffer.writeData.valid)
    val isSCReadWire = WireDefault(isSCWire && !readBuffer.writeData.valid)
    val isSCWriteWire = WireDefault(isSCWire && readBuffer.writeData.valid)
    val requiredResponseWire = WireInit(0.U(2.W))

    //Getting tagBRAM results
    val tagChunks = VecInit(Seq.tabulate(nway) { i =>
      tagBRAM.rdData(((i + 1) * (tagSection)) - 1, i * (tagSection))
    })
    // Compare each chunk with the size of tagSection for the request address
    val matchFoundVec = WireDefault(VecInit(Seq.fill(nway)(false.B)))
    for (i <- 0 until nway) {
      matchFoundVec(i) := (tagChunks(i)(tagSize - 1, 0) 
                        === readBuffer.address(addrWidth - 1, dataAddrWidth + log2Ceil(lineSize)))
    }
    val hitTagWire = WireDefault(PriorityEncoder(matchFoundVec))
    val validBitWire = WireDefault(tagChunks(hitTagWire)(tagSize))
    val shareBitWire = WireDefault(tagChunks(hitTagWire)(tagSize + 2))
    val dirtyBitWire = WireDefault(tagChunks(hitTagWire)(tagSize + 1))
    val PLRUBitWire = WireDefault(tagChunks(hitTagWire)(tagSize + 3))
    
    val isDirtyWire = WireDefault(dirtyBitWire && validBitWire)
    val isSharedWire = WireDefault(shareBitWire && validBitWire)
    val isDataMissWire = WireDefault(!(matchFoundVec.reduce(_ | _) && validBitWire))
    val isPermissionMiss = WireDefault(!isDataMissWire && isSharedWire)
    val isReplayValidWire = WireDefault(requestType === "b10".U) //&& !readBuffer.cacheLine.invalidated)
    // A CleanUnique upgrade whose line was invalidated by a peer's snoop while
    // the upgrade was in flight: the response carries no data (ACEUnit sets
    // cacheLine.valid=0 for CleanUnique) and the tag no longer matches.
    // Treating this replay as a hit re-validates the dead way from the stale
    // data BRAM with NO data transfer — resurrecting a line another hart just
    // rewrote. That silently undoes the peer's committed store system-wide
    // (mt-lrscirq: c3's mutex-release store erased by c1's stale upgrade ->
    // owner never returns to 0 -> every hart spins forever; Linux: dropped
    // spinlock unlock -> post-/init freeze). Such a replay must NOT pass; it
    // is sent back around as a data-carrying ReadUnique instead.
    val isStaleUpgradeReplay = WireDefault(
      isReplayValidWire && isDataMissWire && !readBuffer.cacheLine.valid)

    //Updating wires
    val newtagChunks = VecInit(Seq.tabulate(nway) { i =>
      tagBRAM.rdData((i + 1) * (tagSection) - 1, i * (tagSection))
    })
    val newAddrWire =  WireDefault(tagChunks(hitTagWire)(tagSize - 1,0))
    val newValidBitWire =  WireDefault(tagChunks(hitTagWire)(tagSize))
    val newDirtyBitWire =  WireDefault(tagChunks(hitTagWire)(tagSize + 1))
    val newShareBitWire =  WireDefault(tagChunks(hitTagWire)(tagSize + 2))
    val newPLRUBitWire =  WireDefault(tagChunks(hitTagWire)(tagSize + 3))

    //CacheLineUpdate wire
    val readCacheLineUpdate = WireDefault(isReplayValidWire && isDataMissWire && readBuffer.cacheLine.valid)
    val writeCacheLineUpdate = WireDefault(isReplayValidWire && readBuffer.cacheLine.valid)
    //DataBRAMs
    val cacheLineChoosen = WireDefault(dataBRAMVec(PriorityEncoder(matchFoundVec)).rdData)
    when(isReadWire){
      cacheLineChoosen := Mux(readCacheLineUpdate, readBuffer.cacheLine.cacheLine, dataBRAMVec(PriorityEncoder(matchFoundVec)).rdData)
    }
    when(isLRReadWire || isAtmoicReadWire || isWriteWire || isAtmoicWriteWire){
      cacheLineChoosen := Mux(writeCacheLineUpdate, readBuffer.cacheLine.cacheLine, dataBRAMVec(PriorityEncoder(matchFoundVec)).rdData)
    }
    val writeChunks = VecInit(Seq.tabulate(lineSize * 8 * 2 / dataWidth) { i =>
      cacheLineChoosen((i + 1) * (32) - 1, i * (32))
    })
    val newWriteChunks = VecInit(Seq.tabulate(lineSize * 8 * 2 / dataWidth) { i =>
      cacheLineChoosen((i + 1) * (32) - 1, i * (32))
    })
    val wordWrite = writeChunks(readBuffer.address(5,2))
    val doubleWordWrite = Cat(writeChunks((readBuffer.address(5,3) ## 1.U)),(writeChunks(readBuffer.address(5,3) ## 0.U)))
    val writeByteChunks = VecInit.tabulate(8)(i => doubleWordWrite(8 * (i + 1) - 1, 8 * i))

    //PLRU logic
    val PLRUSetWire = WireDefault(VecInit(tagChunks.map(chunk => chunk(tagSize + 3))))
    val flippedPLRUSetWire = WireDefault(VecInit(PLRUSetWire.map(bit => ~bit)))
    val replacingset = PriorityEncoder(flippedPLRUSetWire)

    val isUpdateValidWire = WireDefault(tagChunks(replacingset)(tagSize))
    val isUpdateDirtyWire = WireDefault(tagChunks(replacingset)(tagSize + 1))

    //read 
    when(isReadWire){
      when(!isDataMissWire){//Hit
        newPLRUBitWire := Mux(PLRUSetWire.reduce(_ & _), 0.U, 1.U)
      } .elsewhen(isReplayValidWire){
        newPLRUBitWire := Mux(PLRUSetWire.reduce(_ & _), 0.U, 1.U)
        newValidBitWire := 1.U
        // Demand loads always install Shared. The CCU/L2 path historically
        // returned IsShared=0 for ReadShared satisfied from L2 (cold miss), so
        // two cores racing on the same line could both install Exclusive and
        // then write without CleanUnique/snoops — classic dual-exclusive
        // coherence hole. That breaks seqlocks (Linux ktime_get, mt-seqlock):
        // the writer's stores never invalidate the reader's stale Exclusive
        // copy. Forcing Shared on every plain load means any subsequent store
        // takes the CleanUnique upgrade and invalidates peers. Atomics still
        // use ReadUnique and install Exclusive below.
        newShareBitWire := 1.U
        newDirtyBitWire := readBuffer.cacheLine.response(0)
        newAddrWire := readBuffer.address(addrWidth - 1, dataAddrWidth + log2Ceil(lineSize))
        for (i <- 0 until writeChunks.length) {
          writeChunks(i) := readBuffer.cacheLine.cacheLine((i + 1) * 32 - 1, i * 32)
        }
      }
    }  
    when(isLRReadWire || isAtmoicReadWire){
      when(!isPermissionMiss && !isDataMissWire){//Hit
        newPLRUBitWire := Mux(PLRUSetWire.reduce(_ & _), 0.U, 1.U)
      } .elsewhen(isReplayValidWire && !isStaleUpgradeReplay){
        newPLRUBitWire := Mux(PLRUSetWire.reduce(_ & _), 0.U, 1.U)
        newValidBitWire := 1.U
        newShareBitWire := 0.U //readBuffer.cacheLine.response(1)
        newDirtyBitWire := readBuffer.cacheLine.response(0)
        newAddrWire := readBuffer.address(addrWidth - 1, dataAddrWidth + log2Ceil(lineSize))
        for (i <- 0 until writeChunks.length) {
          writeChunks(i) := readBuffer.cacheLine.cacheLine((i + 1) * 32 - 1, i * 32)
        }
      }
    }

    //Write related
    val result32 = WireDefault(0.U(32.W))
    val result64 = WireDefault(0.U(64.W)) 
    when(isWriteWire ||  isAtmoicWriteWire || isSCWriteWire){
      when(!isPermissionMiss && !isDataMissWire){
        newDirtyBitWire := 1.U
        newPLRUBitWire := Mux(PLRUSetWire.reduce(_ & _), 0.U, 1.U)
      } .elsewhen(isReplayValidWire && isDataMissWire && !isStaleUpgradeReplay){
        newValidBitWire := 1.U
        newDirtyBitWire := 1.U
        newShareBitWire := 0.U //readBuffer.cacheLine.response(1)//Can put to 0.U       
        newPLRUBitWire := Mux(PLRUSetWire.reduce(_ & _), 0.U, 1.U)
        newAddrWire := readBuffer.address(addrWidth - 1, dataAddrWidth + log2Ceil(lineSize))
        for (i <- 0 until writeChunks.length) {
          writeChunks(i) := readBuffer.cacheLine.cacheLine((i + 1) * 32 - 1, i * 32)
        }
        //The data available but permission miss situation
      } .elsewhen(isReplayValidWire && isPermissionMiss){
        newValidBitWire := 1.U
        newShareBitWire := 0.U //readBuffer.cacheLine.response(1)       
        newDirtyBitWire := 1.U
        newAddrWire := readBuffer.address(addrWidth - 1, dataAddrWidth + log2Ceil(lineSize))
      }
      when(isAtmoicWriteWire){
        when(readBuffer.core.instruction(14,12) === "b010".U){
          switch(readBuffer.core.instruction(31,27)){
            is("b00001".U){result32 := readBuffer.writeData.data(31,0)}  //SWAP
            is("b00000".U){result32 := wordWrite + readBuffer.writeData.data(31,0)}  //ADD
            is("b00100".U){result32 := wordWrite ^ readBuffer.writeData.data(31,0)}  //XOR
            is("b01100".U){result32 := wordWrite & readBuffer.writeData.data(31,0)}  //AND
            is("b01000".U){result32 := wordWrite | readBuffer.writeData.data(31,0)}  //OR
            is("b10000".U){result32 := Mux(wordWrite.asSInt < readBuffer.writeData.data(31,0).asSInt, wordWrite, readBuffer.writeData.data(31,0))}  //MIN
            is("b10100".U){result32 := Mux(wordWrite.asSInt > readBuffer.writeData.data(31,0).asSInt, wordWrite, readBuffer.writeData.data(31,0))}  //MAX
            is("b11000".U){result32 := Mux(wordWrite.asUInt < readBuffer.writeData.data(31,0).asUInt, wordWrite, readBuffer.writeData.data(31,0))}  //MINU
            is("b11100".U){result32 := Mux(wordWrite.asUInt > readBuffer.writeData.data(31,0).asUInt, wordWrite, readBuffer.writeData.data(31,0))}  //MAXU
          }
          newWriteChunks(readBuffer.address(5,2)) := result32
        }
        when(readBuffer.core.instruction(14,12) === "b011".U){
          switch(readBuffer.core.instruction(31,27)){
            is("b00001".U){result64 := readBuffer.writeData.data}  //SWAP
            is("b00000".U){result64 := doubleWordWrite + readBuffer.writeData.data}  //ADD
            is("b00100".U){result64 := doubleWordWrite ^ readBuffer.writeData.data}  //XOR
            is("b01100".U){result64 := doubleWordWrite & readBuffer.writeData.data}  //AND
            is("b01000".U){result64 := doubleWordWrite | readBuffer.writeData.data}  //OR
            is("b10000".U){result64 := Mux(doubleWordWrite.asSInt < readBuffer.writeData.data.asSInt, doubleWordWrite, readBuffer.writeData.data)}  //MIN
            is("b10100".U){result64 := Mux(doubleWordWrite.asSInt > readBuffer.writeData.data.asSInt, doubleWordWrite, readBuffer.writeData.data)}  //MAX
            is("b11000".U){result64 := Mux(doubleWordWrite.asUInt < readBuffer.writeData.data.asUInt, doubleWordWrite, readBuffer.writeData.data)}  //MINU
            is("b11100".U){result64 := Mux(doubleWordWrite.asUInt > readBuffer.writeData.data.asUInt, doubleWordWrite, readBuffer.writeData.data)}  //MAXU
          }
          newWriteChunks(readBuffer.address(5,2)) := result64(31,0)
          newWriteChunks(readBuffer.address(5,2) + 1.U) := result64(63,32)
        }
      } .otherwise {
          switch(readBuffer.core.instruction(13,12)){
            is("b00".U){for (i <- 0 until 1) {writeByteChunks(readBuffer.address(2, 0) + i.U) := readBuffer.writeData.data(8 * (i + 1) - 1, 8 * i)}}
            is("b01".U){for (i <- 0 until 2) {writeByteChunks(readBuffer.address(2, 1)*2.U + i.U) := readBuffer.writeData.data(8 * (i + 1) - 1, 8 * i)}}
            is("b10".U){for (i <- 0 until 4) {writeByteChunks(readBuffer.address(2)*4.U + i.U) := readBuffer.writeData.data(8 * (i + 1) - 1, 8 * i)}}
            is("b11".U){for (i <- 0 until 8) {writeByteChunks(i.U) := readBuffer.writeData.data(8 * (i + 1) - 1, 8 * i)}}
          }
          newWriteChunks(readBuffer.address(5, 3)*2.U) := Cat(writeByteChunks.slice(0, 4).reverse)
          newWriteChunks(readBuffer.address(5, 3)*2.U + 1.U) := Cat(writeByteChunks.slice(4, 8).reverse)
      }  
    }
    when(isCoherentWire){
      when(readBuffer.cacheLine.response(1)){
        newValidBitWire := 0.U
        newPLRUBitWire := 0.U
        newShareBitWire := 0.U
        newDirtyBitWire := 0.U
      } .otherwise{
        newShareBitWire := Mux(readBuffer.cacheLine.response(0) && !isDataMissWire, 1.U, isSharedWire) //Changed anew
      }
    }
    val isReservationMatch32 = WireDefault((reservationRegister.address((addrWidth-1),2)) === (readBuffer.address((addrWidth-1),2)))
    val isReservationMatch64 = WireDefault((reservationRegister.address((addrWidth-1),3)) === (readBuffer.address((addrWidth-1),3)))
    val isReservationMatch = Mux(reservationRegister.size.asBool, isReservationMatch64, isReservationMatch32)
    // Snoops carry LINE-aligned addresses (low bits zero), so a word/dword
    // compare only ever matches a reservation on dword 0 of the line: a peer
    // could steal the whole line while a reservation on any other offset
    // survived, and the SC then reported success without owning the line —
    // a silent cross-hart lost update (Linux: corrupted mutex owner /
    // csd llist / tty state; MUTEX_WARN_ON(owner & MUTEX_FLAG_PICKUP)).
    // Coherent ops therefore kill on LINE match; the core's own stores keep
    // the word/dword granularity below (a spurious SC failure is legal, a
    // false success is not).
    val isReservationLineMatch = WireDefault(
      reservationRegister.address(addrWidth - 1, log2Ceil(lineSize)) ===
        readBuffer.address(addrWidth - 1, log2Ceil(lineSize)))
    when(reservationRegister.reserved && isCoherentWire && isReservationLineMatch){
      reservationRegister.reserved := false.B
      guardSnoopLanded := true.B   // deferred snoop drained -> guard may re-arm
    }
    when(reservationRegister.reserved && (isWriteWire ||  isAtmoicWriteWire)){
      switch(reservationRegister.size){
        is(0.U){reservationRegister.reserved := !isReservationMatch32}
        is(1.U){reservationRegister.reserved := !isReservationMatch64}
      }
    }

    //BRAM update
    val updatingSet = Mux(isDataMissWire, replacingset, hitTagWire)
    when(PLRUSetWire.reduce(_ & _)){
      for (i <- 0 until nway) {
        newtagChunks(i) := tagChunks(i) & ~(1.U << (tagSize + 3))
      }
    }
    newtagChunks(updatingSet) := Cat(newPLRUBitWire, newShareBitWire, newDirtyBitWire, newValidBitWire, newAddrWire)
    val dataBRAMUpdateWire = WireDefault(false.B)
    val tagBRAMUpdateWire = WireDefault(false.B)
    
    tagBRAM.wrEna := tagBRAMUpdateWire
    tagBRAM.wrData := newtagChunks.reverse.reduce(Cat(_, _))
    tagBRAM.wrAddr := readBuffer.address(addrEnd, addrBeg)

    dataBRAMVec(updatingSet).wrEna := dataBRAMUpdateWire
    dataBRAMVec(updatingSet).wrData := newWriteChunks.reverse.reduce(Cat(_, _))
    dataBRAMVec(updatingSet).wrAddr := readBuffer.address(addrEnd, addrBeg)

    //Setting control signals on deciding which buffer should data flow
    when(isReadWire){
      when(isReplayValidWire && isDataMissWire || !isDataMissWire){ //Hit
        toMemoryResponseValidWire := true.B
        tagBRAMUpdateWire:= true.B
        toWriteBackValidWire := (isUpdateDirtyWire && isUpdateValidWire) && isReplayValidWire && isDataMissWire
        dataBRAMUpdateWire := readCacheLineUpdate
      } .otherwise {
        toReplayValidWire := true.B
        requiredResponseWire := "b00".U
        toLastSpeculativetMissRecordRegisterWire := true.B
      }
    }
    when(isLRReadWire || isAtmoicReadWire){
      when((isReplayValidWire && !isStaleUpgradeReplay) || (!isPermissionMiss && !isDataMissWire)){ //Hit
        toMemoryResponseValidWire := true.B
        tagBRAMUpdateWire:= true.B
        toReservationRegisterWire := true.B
        toWriteBackValidWire := (isUpdateDirtyWire && isUpdateValidWire) && isReplayValidWire && !isPermissionMiss
        dataBRAMUpdateWire := writeCacheLineUpdate
      } .otherwise {
        // isStaleUpgradeReplay lands here with isDataMissWire set, so the
        // required response is the data-carrying "b01" ReadUnique.
        toReplayValidWire := true.B
        requiredResponseWire := Mux(isPermissionMiss && !isDataMissWire, "b11".U, "b01".U)
        toLastInorderMissRecordRegisterWire := true.B
      }
    }
    when(isLRWriteWire){writeCommitInstructionBuffer := true.B}
    when(isCoherentWire){
      toCoherencyResponseValidWire := true.B
      tagBRAMUpdateWire:= !isDataMissWire
    }
    when(isWriteWire || isAtmoicWriteWire){
      when((isReplayValidWire && !isStaleUpgradeReplay) || (!isPermissionMiss && !isDataMissWire)){
        toWriteBackValidWire := (isUpdateDirtyWire && isUpdateValidWire) && isReplayValidWire && !isPermissionMiss
        tagBRAMUpdateWire:= true.B
        dataBRAMUpdateWire := true.B
        writeCommitInstructionBuffer := true.B
      } .otherwise {
        toReplayValidWire := true.B
        requiredResponseWire := Mux(isPermissionMiss && !isDataMissWire, "b11".U, "b01".U)
        toLastInorderMissRecordRegisterWire := true.B
      }
    }
    when(isSCReadWire){toMemoryResponseValidWire := true.B}
    when(isSCWriteWire){
      scWritePassFire := true.B
      reservationRegister.reserved := false.B
      // SC may only write if the reservation held AND the line is still
      // present and owned (not shared, not evicted). Without the presence
      // check, an eviction (or, before the line-granular kill above, a peer
      // steal) between LR and SC left the reservation set and this pass
      // wrote tag+data into whatever way the replacement pointer chose —
      // corrupting an unrelated line. The result reported at the SC read
      // pass uses the same condition, and the arbiter's atomic window keeps
      // same-line snoops out between the two passes, so read-pass success
      // implies write-pass success.
      when(reservationRegister.reserved && isReservationMatch &&
           !isDataMissWire && !isSharedWire){
        toWriteBackValidWire := isDirtyWire  &&  isReplayValidWire && isDataMissWire && !isPermissionMiss
        tagBRAMUpdateWire:= true.B
        dataBRAMUpdateWire := true.B
      }
      writeCommitInstructionBuffer := true.B
    }

    //Setting the dataOut
    val responseResultWire = WireDefault(0.U(dataWidth.W))
    val doubleWordSize = 64
    val numChunks = lineSize * 8 / doubleWordSize
    val doubleWordChunks = VecInit(Seq.tabulate(numChunks) { i =>
      cacheLineChoosen((i + 1) * doubleWordSize - 1, i * doubleWordSize)
    })
    val doubleWordChoosen = doubleWordChunks(readBuffer.address(log2Ceil(lineSize) - 1, 3))
    val shiftAmount = (1.U << readBuffer.core.instruction(13,12).asUInt)
    val section = (1.U << (8.U*shiftAmount)) - 1.U 
    val byteChunks = VecInit(Seq.tabulate(8) { i =>
      doubleWordChoosen((i + 1) * 8 - 1, i * 8) // 8-bit slices
    })
    val byteChoosed     = byteChunks(readBuffer.address(2,0))
    val halfwordChoosed = Cat(byteChunks(2.U * readBuffer.address(2,1) + 1.U),byteChunks(2.U * readBuffer.address(2,1)))
    val wordChoosed     = Cat(byteChunks(4.U * readBuffer.address(2) + 3.U),byteChunks(4.U * readBuffer.address(2) + 2.U), 
                              byteChunks(4.U * readBuffer.address(2) + 1.U),byteChunks(4.U * readBuffer.address(2)))
    
    switch(readBuffer.core.instruction(13, 12)){
      is("b00".U){responseResultWire := Mux(readBuffer.core.instruction(14),byteChoosed,
                                    Cat(Fill((dataWidth-1*8),byteChoosed(7)),byteChoosed))}
      is("b01".U){responseResultWire := Mux(readBuffer.core.instruction(14),halfwordChoosed,
                                    Cat(Fill((dataWidth-2*8),halfwordChoosed(15)),halfwordChoosed))}
      is("b10".U){responseResultWire := Mux(readBuffer.core.instruction(14),wordChoosed,
                                    Cat(Fill((dataWidth-4*8),wordChoosed(31)),wordChoosed))}
      is("b11".U){responseResultWire := Mux(readBuffer.core.instruction(14),"x0".U,
                                    doubleWordChoosen)}
    }
    when(isSCReadWire){
      // Success (0) requires the line be PRESENT and OWNED in addition to a
      // live matching reservation — see the isSCWriteWire comment above.
      responseResultWire := Mux(reservationRegister.reserved && isReservationMatch &&
                                !isDataMissWire && !isSharedWire, 0.U, 1.U)
    }

    //____________________Output Buffer update___________________//
    //Replay
    when(toReplayValidWire && readBuffer.branch.valid){
      replayBuffer := readBuffer
      replayBuffer.cacheLine.response := requiredResponseWire
      regWriteUpdate(replayBuffer.branch, branchOps, readBuffer.branch)
    }

    //Response
    when(toMemoryResponseValidWire && readBuffer.branch.valid){
      toResponse.request := readBuffer
      toResponse.request.writeData.data := responseResultWire
      regWriteUpdate(toResponse.request.branch, branchOps, readBuffer.branch)
    }

    //Coherency
    when(toCoherencyResponseValidWire){
      coherencyResponseBuffer.valid := toCoherencyResponseValidWire
      when(readBuffer.cacheLine.response(0)){
        coherencyResponseBuffer.cacheLine := Mux(!isDataMissWire, dataBRAMVec(hitTagWire).rdData, 0.U)
        coherencyResponseBuffer.dataValid := !isDataMissWire
        coherencyResponseBuffer.response := newShareBitWire ## Mux(readBuffer.cacheLine.response(1), isDirtyWire, 0.U)
      } .elsewhen(readBuffer.cacheLine.response(1) && !isDataMissWire){
        // Invalidating snoop (CleanUnique) on ANY valid copy must offer the
        // line data, not only when it is dirty+exclusive. A CleanUnique whose
        // requester lost its own copy mid-flight (peer ReadUnique) otherwise
        // destroys the system's last valid copy with no data transfer and no
        // writeback: the next fill then comes from stale L2 (mt-lrscirq:
        // barrier count/sense reverted to a pre-arrival snapshot, all four
        // harts spin forever; same shape wedges Linux spinlocks). The
        // requester side already accepts CleanUnique data — wasCleanUniqueReg
        // clears when a full line streams in — so offering it here completes
        // that path. PassDirty still reflects this copy's dirty bit.
        coherencyResponseBuffer.cacheLine := Mux(!isDataMissWire, dataBRAMVec(hitTagWire).rdData, 0.U)
        coherencyResponseBuffer.dataValid := !isDataMissWire
        coherencyResponseBuffer.response := Mux(!isDataMissWire, newShareBitWire ## Mux(readBuffer.cacheLine.response(1), isDirtyWire, 0.U), 0.U)
      }.otherwise{
        coherencyResponseBuffer.cacheLine := 0.U
        coherencyResponseBuffer.dataValid := false.B
      }
    }

    //WriteBack
    when(toWriteBackValidWire){
      writeBackBuffer.valid := toWriteBackValidWire
      // Explicitly slice the pure tag (drop PLRU/Share/Dirty/Valid flags) —
      // matches walkerWriteBackBuffer. Relying on 36→32 truncation of
      // Cat(fullTagChunk, set, off) is fragile if tagSection ever changes.
      writeBackBuffer.address := Cat(tagChunks(updatingSet)(tagSize - 1, 0),
        readBuffer.address(addrEnd, addrBeg), 0.U(log2Ceil(lineSize).W))
      writeBackBuffer.data := dataBRAMVec(updatingSet).rdData
      // A real eviction: the tag has been reassigned, so this buffered copy is
      // the only one left and it MUST keep answering snoops.
      writeBackBuffer.retain := false.B
    }
    
    //Last Miss Memory Record
    when(toLastInorderMissRecordRegisterWire && readBuffer.branch.valid){  
      lastInorderMissRecordRegister := readBuffer
      regWriteUpdate(lastInorderMissRecordRegister.branch, branchOps, readBuffer.branch)
    }
    when(toLastSpeculativetMissRecordRegisterWire && readBuffer.branch.valid){  
      lastSpeculativeMissRecordRegister := readBuffer
      regWriteUpdate(lastSpeculativeMissRecordRegister.branch, branchOps, readBuffer.branch)
    }

    //Reservation register
    when(toReservationRegisterWire){
      reservationRegister.reserved := toReservationRegisterWire
      reservationRegister.address := readBuffer.address
      reservationRegister.size := readBuffer.core.instruction(12)
    }
  }

  //____________________Clean-on-fence.i walker logic____________________//
  // The D-cache is write-back, so on fence.i dirty lines live only in L1 and a
  // subsequent non-coherent I-fetch would read stale data from L2. This walker
  // sweeps every set/way and writes back each valid+dirty line via the dedicated
  // walkerWriteBackBuffer (see staging comment above). It only runs on fence.i
  // (see cacheModule's fence FSM), never on plain `fence` barriers.
  //
  // Crucially it YIELDS the BRAM read port to the normal request path (which
  // carries coherency snoops from other cores under SMP): it reads/drives BRAM
  // only on a cycle when no request is using it (`bramFree`), captures the whole
  // set into registers, and then drains the ways from those registers -- so a
  // snoop stealing the BRAM mid-sweep cannot corrupt the walk or deadlock the
  // coherence fabric. If a snoop *does* hit the captured set, flushSetPoison
  // forces a re-read so we never write back a line the snoop already took.
  val bramFree = !(request.request.valid && request.request.branch.valid)

  // Snoop to the set currently being emitted: poison the capture so we re-read.
  // requestType/readBuffer are registered one cycle after accept (operationValid).
  when(flushActive && (flushState === sFlushCapture || flushState === sFlushEmit) &&
       operationValid && (requestType === "b11".U)) {
    val snoopSet = readBuffer.address(addrEnd, addrBeg)
    when(snoopSet === flushSet) {
      flushSetPoison := true.B
    }
  }

  when(flush.start && flushState === sFlushIdle) {
    flushState := sFlushRead
    flushSet := 0.U
    flushWay := 0.U
    flushSetPoison := false.B
  }

  switch(flushState) {
    is(sFlushRead) {
      // Advance only once we own the BRAM this cycle (rdAddr=flushSet is driven
      // in the free-cycle block below), so next cycle rdData reflects flushSet.
      when(bramFree) {
        flushSetPoison := false.B
        flushState := sFlushCapture
      }
    }
    is(sFlushCapture) {
      // rdData now reflects flushSet (driven last cycle, unaffected by whatever
      // uses the port this cycle). Latch every way; the emit phase is then
      // independent of the BRAM.
      flushTagReg  := VecInit(Seq.tabulate(nway) { i =>
        tagBRAM.rdData(((i + 1) * tagSection) - 1, i * tagSection)
      })
      flushDataReg := VecInit(dataBRAMVec.map(_.rdData))
      when(flushSetPoison) {
        // Snoop landed between the read and this capture; try again.
        flushState := sFlushRead
      }.otherwise {
        flushState := sFlushEmit
      }
    }
    is(sFlushEmit) {
      when(flushSetPoison) {
        // Ownership may have changed mid-emit; re-capture the set from BRAM.
        flushWay := 0.U
        flushState := sFlushRead
      }.otherwise {
        val tagChunk = flushTagReg(flushWay)
        val wayValid = tagChunk(tagSize)
        val wayDirty = tagChunk(tagSize + 1)
        val needWriteBack = wayValid && wayDirty
        val lastWay = flushWay === (nway - 1).U
        val lastSet = flushSet === (tagDepth - 1).U

        // Post only into the walker staging slot (never the normal-path buffer),
        // and only when that slot is free. Non-dirty ways advance immediately.
        val canPost = !needWriteBack || !walkerWriteBackBuffer.valid
        when(canPost) {
          when(needWriteBack) {
            walkerWriteBackBuffer.valid := true.B
            // {tag, setIndex, blockOffset}. The (tagSize-1,0) slice drops the 4
            // flag bits so only the true tag is placed in the address.
            walkerWriteBackBuffer.address := Cat(tagChunk(tagSize - 1, 0), flushSet, 0.U(log2Ceil(lineSize).W))
            walkerWriteBackBuffer.data := flushDataReg(flushWay)
            // Clean-on-fence, NOT an eviction: the tag is deliberately left
            // alone, so the L1 keeps this line valid and writable. Mark it so
            // no snoop is ever answered from this copy — a store landing after
            // the capture would otherwise be handed to a peer as stale data
            // with PassDirty and lost for good.
            walkerWriteBackBuffer.retain := true.B
          }
          when(lastWay) {
            flushWay := 0.U
            when(lastSet) {
              flushState := sFlushDrain
            }.otherwise {
              flushSet := flushSet + 1.U
              flushState := sFlushRead
            }
          }.otherwise {
            flushWay := flushWay + 1.U
          }
        }
      }
    }
    is(sFlushDrain) {
      // Hold busy until the final walker writeback has left the staging slot.
      // (writeBackFIFO drain is still enforced by fenceDrain's subModulesReady.)
      when(!walkerWriteBackBuffer.valid) {
        flushState := sFlushIdle
      }
    }
  }

  // Drive the BRAM read address for the walker ONLY on a free cycle and only
  // while it is reading a set (sFlushRead). Last-connect, so on a busy cycle the
  // normal request path (which drove rdAddr above) wins and the walker waits.
  when(flushState === sFlushRead && bramFree) {
    tagBRAM.rdAddr := flushSet
    dataBRAMVec.foreach { bram => bram.rdAddr := flushSet }
  }

  //! Debug only
  debug.request := readBuffer
  debug.isServicing := operationValid
  debug.rdAddr := tagBRAM.rdAddr
  debug.tagData := tagBRAM.rdData
  debug.dataBRAM0 := dataBRAMVec(0).rdData
  debug.dataBRAM1 := dataBRAMVec(1).rdData
  debug.dataBRAM2 := dataBRAMVec(2).rdData
  debug.dataBRAM3 := dataBRAMVec(3).rdData
}
