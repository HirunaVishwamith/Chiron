//soc simulation implementation


import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import chisel3.experimental.IO

import pipeline.ports._
import common.coreConfiguration._
import cache.AXI
import _root_.testbench.mainMemory
import _root_.testbench.uart
import decode.constants
import _root_.testbench.simulatedMemory
import Interconnect._
import l2_cache._


class system extends Module {

  val core0 = Module(new core(
    dPort_id = 0,
    peripheral_id = 0,
    iPort_id = 1,
    mhart_id = 0
  ){
    val registersOut = IO(Output(decode.registersOut.cloneType))
    val architecturalRegisterFile = VecInit(decode.retiredRenamedTable.table.map(i => prf.registerFileOutput(i)))
    registersOut zip architecturalRegisterFile foreach { case(x, y) => x := y }
    registersOut.reverse.head := decode.registersOut.head

    val robOut = IO(Output(new Bundle() {
      val commitFired = Bool()
      val pc         = UInt(64.W)
      val interrupt = Bool()
    }))
    robOut.commitFired := rob.commit.fired
    robOut.pc          := rob.commit.pc
    robOut.interrupt   := decode.writeBackResult.instruction === "h80000073".U(64.W)
    when((rob.commit.instruction(6, 0) === "b1110011".U) && (rob.commit.instruction(14, 12).orR)) { robOut.commitFired := false.B }

    val allRobFiresOut = IO(Output(Bool()))
    allRobFiresOut := rob.commit.fired

    // Ungated profiling counters — count the full simulation run without the
    // programRunning gate (which closes after the first commit for Linux workloads).
    val pc_cycles         = RegInit(0.U(64.W))
    val pc_instRetired    = RegInit(0.U(64.W))
    val pc_branchTotal    = RegInit(0.U(64.W))
    val pc_branchesPassed = RegInit(0.U(64.W))
    val pc_schedStalls    = RegInit(0.U(64.W))
    val pc_robStalls      = RegInit(0.U(64.W))
    val pc_decodeReady    = RegInit(0.U(64.W))
    val pc_decodeFired    = RegInit(0.U(64.W))
    val pc_icacheStalls   = RegInit(0.U(64.W))
    val pc_dcacheReqs     = RegInit(0.U(64.W))

    // ── Frontend bubble decomposition (Stage 0 instrumentation) ───────────
    // The fetch->decode transfer fires when:
    //   fetch.toDecode.ready && decode.fromFetch.ready &&
    //   (!expected.valid || expected.pc === toDecode.pc)
    // These counters split every NON-firing cycle into its single cause so we
    // can prove where the per-instruction bubble lives before reworking fetch.
    val pc_feFetchNotReady = RegInit(0.U(64.W)) // fetch not presenting a valid instruction
    val pc_feDecodeNotReady= RegInit(0.U(64.W)) // fetch ready but decode backpressures
    val pc_feExpectedBlock = RegInit(0.U(64.W)) // both ready but expected-PC mismatch (redirect bubble)
    val pc_feRespValidIdle = RegInit(0.U(64.W)) // I$ has a response but it is not being consumed
    // Split of "fetch not ready" at the I$<->fetch boundary (production vs request side)
    val pc_feCacheNotProd  = RegInit(0.U(64.W)) // I$ is not presenting a valid response
    val pc_feReqFire       = RegInit(0.U(64.W)) // a fetch request was accepted by the I$
    val pc_feReqRefused    = RegInit(0.U(64.W)) // fetch wants to request but I$ refuses (req.ready low)

    pc_cycles := pc_cycles + 1.U
    when(rob.commit.fired) { pc_instRetired := pc_instRetired + 1.U }
    when(branchOps.valid) {
      pc_branchTotal := pc_branchTotal + 1.U
      when(branchOps.passed) { pc_branchesPassed := pc_branchesPassed + 1.U }
    }
    when(decode.toExec.ready) {
      pc_decodeReady := pc_decodeReady + 1.U
      when(decode.toExec.fired)    { pc_decodeFired  := pc_decodeFired  + 1.U }
      when(!scheduler.allocate.ready) { pc_schedStalls := pc_schedStalls + 1.U }
      when(!rob.allocate.ready)       { pc_robStalls   := pc_robStalls   + 1.U }
    }
    when(icache.fromFetch.req.valid && !icache.fromFetch.resp.valid) {
      pc_icacheStalls := pc_icacheStalls + 1.U
    }
    when(memoryRequest.valid) { pc_dcacheReqs := pc_dcacheReqs + 1.U }

    // Frontend bubble decomposition (mutually exclusive, counted every cycle).
    val fe_expMismatch = decode.fromFetch.expected.valid &&
                         (decode.fromFetch.expected.pc =/= fetch.toDecode.pc)
    when(!fetch.toDecode.ready) {
      pc_feFetchNotReady := pc_feFetchNotReady + 1.U
    }.elsewhen(!decode.fromFetch.ready) {
      pc_feDecodeNotReady := pc_feDecodeNotReady + 1.U
    }.elsewhen(fe_expMismatch) {
      pc_feExpectedBlock := pc_feExpectedBlock + 1.U
    }
    // F2 diagnostic: repointed to the streamer<->fetch boundary (the I$ side is
    // now only active on line fills, so these measure the live path).
    // feRespValidIdle = streamer produced a word but fetch isn't taking it
    when(streamer.fromFetch.resp.valid && !streamer.fromFetch.resp.ready) {
      pc_feRespValidIdle := pc_feRespValidIdle + 1.U
    }
    // feCacheNotProd = streamer is NOT presenting a word to fetch
    when(!streamer.fromFetch.resp.valid) { pc_feCacheNotProd := pc_feCacheNotProd + 1.U }
    // feReqFire = streamer accepted a fetch request (word served / fill started)
    when(streamer.fromFetch.req.valid && streamer.fromFetch.req.ready)  { pc_feReqFire    := pc_feReqFire    + 1.U }
    // feReqRefused = fetch wants a word but streamer is busy (filling)
    when(streamer.fromFetch.req.valid && !streamer.fromFetch.req.ready) { pc_feReqRefused := pc_feReqRefused + 1.U }

    // F2 streamer-internal diagnosis
    val pc_strmCurValid = RegInit(0.U(64.W))
    val pc_strmValidMiss = RegInit(0.U(64.W)) // line buffered but request misses it (the bug)
    val pc_strmLastReqBase = RegInit(0.U(64.W))
    val pc_strmLastCurBase = RegInit(0.U(64.W))
    when(streamer.dbg.curValid) { pc_strmCurValid := pc_strmCurValid + 1.U }
    when(streamer.dbg.reqValid && streamer.dbg.curValid && !streamer.dbg.baseMatch) {
      pc_strmValidMiss := pc_strmValidMiss + 1.U
    }
    // Fill latency: active cycles / number of fill starts.
    val fillingPrev = RegNext(streamer.dbg.filling)
    when(streamer.dbg.filling) { pc_strmLastReqBase := pc_strmLastReqBase + 1.U }            // fill-active cycles
    when(streamer.dbg.filling && !fillingPrev) { pc_strmLastCurBase := pc_strmLastCurBase + 1.U } // fill starts

    // B1: split commit stalls at the ROB head into the two possible causes.
    //   robHeadNotReady — oldest instruction is in the window but its result
    //                     hasn't arrived yet → latency-bound (issue→wakeup→
    //                     writeback loop is the wall).
    //   robReadyBlocked — head result is ready but the commit port doesn't
    //                     fire → commit-width / retire-port bound.
    val pc_robHeadNotReady = RegInit(0.U(64.W))
    val pc_robReadyBlocked = RegInit(0.U(64.W))
    when(rob.headValid && !rob.commit.ready)  { pc_robHeadNotReady := pc_robHeadNotReady + 1.U }
    when(rob.commit.ready && !rob.commit.fired) { pc_robReadyBlocked := pc_robReadyBlocked + 1.U }

    // B1b: classify WHAT is holding the ROB head when its result isn't ready,
    // by the head instruction's opcode. Stores can't appear here (they are
    // commit-ready at allocation), so the buckets are load / branch-jump /
    // M-extension / AMO / everything else (ALU, system, fence).
    val headOp = rob.commit.instruction(6, 0)
    val headIsLoad   = headOp === "b0000011".U
    val headIsBranch = headOp === "b1100011".U || headOp === "b1101111".U || headOp === "b1100111".U
    val headIsMext   = (headOp === "b0110011".U || headOp === "b0111011".U) && rob.commit.instruction(25)
    val headIsAmo    = headOp === "b0101111".U
    val pc_hnrLoad   = RegInit(0.U(64.W))
    val pc_hnrBranch = RegInit(0.U(64.W))
    val pc_hnrMext   = RegInit(0.U(64.W))
    val pc_hnrAmo    = RegInit(0.U(64.W))
    val pc_hnrOther  = RegInit(0.U(64.W))
    when(rob.headValid && !rob.commit.ready) {
      when(headIsLoad)        { pc_hnrLoad   := pc_hnrLoad   + 1.U }
      .elsewhen(headIsBranch) { pc_hnrBranch := pc_hnrBranch + 1.U }
      .elsewhen(headIsMext)   { pc_hnrMext   := pc_hnrMext   + 1.U }
      .elsewhen(headIsAmo)    { pc_hnrAmo    := pc_hnrAmo    + 1.U }
      .otherwise              { pc_hnrOther  := pc_hnrOther  + 1.U }
    }

    // B1c: when the head IS ready but commit doesn't fire, name the gate
    // (the three conditions ANDed into rob.commit.fired in core.scala).
    val pc_rnrStoreGate = RegInit(0.U(64.W)) // store head, cache write port not ready
    val pc_rnrWbGate    = RegInit(0.U(64.W)) // decode.writeBackResult not ready
    val pc_rnrLoadGate  = RegInit(0.U(64.W)) // load head, loadCommit/coherency gate
    when(rob.commit.ready && !rob.commit.fired) {
      when((rob.commit.instruction(6, 4) === "b010".U) && !memAccess.writeInstructionCommit.ready) {
        pc_rnrStoreGate := pc_rnrStoreGate + 1.U
      }
      when(!decode.writeBackResult.ready) { pc_rnrWbGate := pc_rnrWbGate + 1.U }
      when((rob.commit.instruction(6, 2).orR === 0.U) && (coherentLoadInvalid || !memAccess.loadCommit.valid)) {
        pc_rnrLoadGate := pc_rnrLoadGate + 1.U
      }
    }

    val perfCnt = IO(Output(new Bundle {
      val cycles          = UInt(64.W)
      val instRetired     = UInt(64.W)
      val branchTotal     = UInt(64.W)
      val branchesPassed  = UInt(64.W)
      val schedulerStalls = UInt(64.W)
      val robStalls       = UInt(64.W)
      val decodeReady     = UInt(64.W)
      val decodeFired     = UInt(64.W)
      val icacheStalls    = UInt(64.W)
      val dcacheReqs      = UInt(64.W)
      val feFetchNotReady = UInt(64.W)
      val feDecodeNotReady= UInt(64.W)
      val feExpectedBlock = UInt(64.W)
      val feRespValidIdle = UInt(64.W)
      val feCacheNotProd  = UInt(64.W)
      val feReqFire       = UInt(64.W)
      val feReqRefused    = UInt(64.W)
      val strmCurValid    = UInt(64.W)
      val strmValidMiss   = UInt(64.W)
      val strmLastReqBase = UInt(64.W)
      val strmLastCurBase = UInt(64.W)
      val robHeadNotReady = UInt(64.W)
      val robReadyBlocked = UInt(64.W)
      val hnrLoad         = UInt(64.W)
      val hnrBranch       = UInt(64.W)
      val hnrMext         = UInt(64.W)
      val hnrAmo          = UInt(64.W)
      val hnrOther        = UInt(64.W)
      val rnrStoreGate    = UInt(64.W)
      val rnrWbGate       = UInt(64.W)
      val rnrLoadGate     = UInt(64.W)
    }))
    perfCnt.cycles          := pc_cycles
    perfCnt.instRetired     := pc_instRetired
    perfCnt.branchTotal     := pc_branchTotal
    perfCnt.branchesPassed  := pc_branchesPassed
    perfCnt.schedulerStalls := pc_schedStalls
    perfCnt.robStalls       := pc_robStalls
    perfCnt.decodeReady     := pc_decodeReady
    perfCnt.decodeFired     := pc_decodeFired
    perfCnt.icacheStalls    := pc_icacheStalls
    perfCnt.dcacheReqs      := pc_dcacheReqs
    perfCnt.feFetchNotReady := pc_feFetchNotReady
    perfCnt.feDecodeNotReady:= pc_feDecodeNotReady
    perfCnt.feExpectedBlock := pc_feExpectedBlock
    perfCnt.feRespValidIdle := pc_feRespValidIdle
    perfCnt.feCacheNotProd  := pc_feCacheNotProd
    perfCnt.feReqFire       := pc_feReqFire
    perfCnt.feReqRefused    := pc_feReqRefused
    perfCnt.strmCurValid    := pc_strmCurValid
    perfCnt.strmValidMiss   := pc_strmValidMiss
    perfCnt.strmLastReqBase := pc_strmLastReqBase
    perfCnt.strmLastCurBase := pc_strmLastCurBase
    perfCnt.robHeadNotReady := pc_robHeadNotReady
    perfCnt.robReadyBlocked := pc_robReadyBlocked
    perfCnt.hnrLoad         := pc_hnrLoad
    perfCnt.hnrBranch       := pc_hnrBranch
    perfCnt.hnrMext         := pc_hnrMext
    perfCnt.hnrAmo          := pc_hnrAmo
    perfCnt.hnrOther        := pc_hnrOther
    perfCnt.rnrStoreGate    := pc_rnrStoreGate
    perfCnt.rnrWbGate       := pc_rnrWbGate
    perfCnt.rnrLoadGate     := pc_rnrLoadGate
  })

  val memory = Module(new mainMemory)
  val interconnect = Module(new Interconnect)
  val LLC = Module(new l2_mem)  

  //core's IOS
  //iPort ACE, dPort ACE, peripheral port AXI, MTIP

  //core0.dPort to interconnect connection
  //AW
  interconnect.io.acePort0.AWVALID := core0.dPort.AWVALID
  core0.dPort.AWREADY := interconnect.io.acePort0.AWREADY
  interconnect.io.acePort0.AWID := core0.dPort.AWID
  interconnect.io.acePort0.AWADDR := core0.dPort.AWADDR
  interconnect.io.acePort0.AWSNOOP := core0.dPort.AWSNOOP
  interconnect.io.acePort0.AWBAR := core0.dPort.AWBAR

  //W
  interconnect.io.acePort0.WVALID := core0.dPort.WVALID
  interconnect.io.acePort0.WDATA := core0.dPort.WDATA
  interconnect.io.acePort0.WLAST := core0.dPort.WLAST
  core0.dPort.WREADY := interconnect.io.acePort0.WREADY

  //B
  core0.dPort.BVALID := interconnect.io.acePort0.BVALID
  core0.dPort.BID := interconnect.io.acePort0.BID
  core0.dPort.BRESP := interconnect.io.acePort0.BRESP
  interconnect.io.acePort0.BREADY := core0.dPort.BREADY

  //AR
  interconnect.io.acePort0.ARVALID := core0.dPort.ARVALID
  core0.dPort.ARREADY := interconnect.io.acePort0.ARREADY
  interconnect.io.acePort0.ARID := core0.dPort.ARID
  interconnect.io.acePort0.ARADDR := core0.dPort.ARADDR
  interconnect.io.acePort0.ARSNOOP := core0.dPort.ARSNOOP
  interconnect.io.acePort0.ARBAR := core0.dPort.ARBAR

  //R
  core0.dPort.RVALID := interconnect.io.acePort0.RVALID
  interconnect.io.acePort0.RREADY := core0.dPort.RREADY
  core0.dPort.RID := interconnect.io.acePort0.RID
  core0.dPort.RDATA := interconnect.io.acePort0.RDATA
  core0.dPort.RRESP := interconnect.io.acePort0.RRESP
  core0.dPort.RLAST := interconnect.io.acePort0.RLAST

  //AC
  core0.dPort.ACVALID := interconnect.io.acePort0.ACVALID
  core0.dPort.ACADDR := interconnect.io.acePort0.ACADDR
  core0.dPort.ACSNOOP := interconnect.io.acePort0.ACSNOOP
  core0.dPort.ACPROT := 2.U
  interconnect.io.acePort0.ACREADY := core0.dPort.ACREADY

  //CR
  interconnect.io.acePort0.CRVALID := core0.dPort.CRVALID
  interconnect.io.acePort0.CRRESP := core0.dPort.CRRESP
  core0.dPort.CRREADY := interconnect.io.acePort0.CRREADY

  //CD
  interconnect.io.acePort0.CDVALID := core0.dPort.CDVALID
  core0.dPort.CDREADY := interconnect.io.acePort0.CDREADY
  interconnect.io.acePort0.CDDATA := core0.dPort.CDDATA
  interconnect.io.acePort0.CDLAST := core0.dPort.CDLAST

  //core0.iPort to interconnect connection
  //AW
  interconnect.io.acePort1.AWVALID := core0.iPort.AWVALID
  core0.iPort.AWREADY := interconnect.io.acePort1.AWREADY
  interconnect.io.acePort1.AWID := core0.iPort.AWID
  interconnect.io.acePort1.AWADDR := core0.iPort.AWADDR
  interconnect.io.acePort1.AWSNOOP := core0.iPort.AWSNOOP
  interconnect.io.acePort1.AWBAR := core0.iPort.AWBAR

  //W
  interconnect.io.acePort1.WVALID := core0.iPort.WVALID
  interconnect.io.acePort1.WDATA := core0.iPort.WDATA
  interconnect.io.acePort1.WLAST := core0.iPort.WLAST
  core0.iPort.WREADY := interconnect.io.acePort1.WREADY

  //B
  core0.iPort.BVALID := interconnect.io.acePort1.BVALID
  core0.iPort.BID := interconnect.io.acePort1.BID
  core0.iPort.BRESP := interconnect.io.acePort1.BRESP
  interconnect.io.acePort1.BREADY := core0.iPort.BREADY

  //AR
  interconnect.io.acePort1.ARVALID := core0.iPort.ARVALID
  core0.iPort.ARREADY := interconnect.io.acePort1.ARREADY
  interconnect.io.acePort1.ARID := core0.iPort.ARID
  interconnect.io.acePort1.ARADDR := core0.iPort.ARADDR
  interconnect.io.acePort1.ARSNOOP := core0.iPort.ARSNOOP
  interconnect.io.acePort1.ARBAR := core0.iPort.ARBAR

  //R
  core0.iPort.RVALID := interconnect.io.acePort1.RVALID
  interconnect.io.acePort1.RREADY := core0.iPort.RREADY
  core0.iPort.RID := interconnect.io.acePort1.RID
  core0.iPort.RDATA := interconnect.io.acePort1.RDATA
  core0.iPort.RRESP := interconnect.io.acePort1.RRESP
  core0.iPort.RLAST := interconnect.io.acePort1.RLAST

  //AC
  core0.iPort.ACVALID := interconnect.io.acePort1.ACVALID
  core0.iPort.ACADDR := interconnect.io.acePort1.ACADDR
  core0.iPort.ACSNOOP := interconnect.io.acePort1.ACSNOOP
  core0.iPort.ACPROT := 2.U
  interconnect.io.acePort1.ACREADY := core0.iPort.ACREADY

  //CR
  interconnect.io.acePort1.CRVALID := core0.iPort.CRVALID
  interconnect.io.acePort1.CRRESP := core0.iPort.CRRESP
  core0.iPort.CRREADY := interconnect.io.acePort1.CRREADY

  //CD
  interconnect.io.acePort1.CDVALID := core0.iPort.CDVALID
  core0.iPort.CDREADY := interconnect.io.acePort1.CDREADY
  interconnect.io.acePort1.CDDATA := core0.iPort.CDDATA
  interconnect.io.acePort1.CDLAST := core0.iPort.CDLAST

  
  //Interconnect L2 connection to Memory
  //AW
  LLC.io.cache_axi.AWVALID := interconnect.io.L2.AWVALID
  interconnect.io.L2.AWREADY := LLC.io.cache_axi.AWREADY
  LLC.io.cache_axi.AWID := interconnect.io.L2.AWID
  LLC.io.cache_axi.AWADDR := interconnect.io.L2.AWADDR
  LLC.io.cache_axi.AWLEN := 7.U


  //AR
  LLC.io.cache_axi.ARVALID := interconnect.io.L2.ARVALID
  interconnect.io.L2.ARREADY := LLC.io.cache_axi.ARREADY
  LLC.io.cache_axi.ARID := interconnect.io.L2.ARID
  LLC.io.cache_axi.ARADDR := interconnect.io.L2.ARADDR
  LLC.io.cache_axi.ARLEN := 7.U


  //W
  LLC.io.cache_axi.WVALID := interconnect.io.L2.WVALID
  interconnect.io.L2.WREADY := LLC.io.cache_axi.WREADY
  LLC.io.cache_axi.WDATA := interconnect.io.L2.WDATA
  LLC.io.cache_axi.WLAST := interconnect.io.L2.WLAST
  //memory.clients(1).WSTRB := "b11111111".U

  //R
  interconnect.io.L2.RVALID := LLC.io.cache_axi.RVALID
  LLC.io.cache_axi.RREADY := interconnect.io.L2.RREADY
  interconnect.io.L2.RID := LLC.io.cache_axi.RID
  interconnect.io.L2.RDATA := LLC.io.cache_axi.RDATA
  interconnect.io.L2.RLAST := LLC.io.cache_axi.RLAST
  interconnect.io.L2.RRESP := LLC.io.cache_axi.RRESP

  //B
  interconnect.io.L2.BVALID := LLC.io.cache_axi.BVALID
  LLC.io.cache_axi.BREADY := interconnect.io.L2.BREADY
  interconnect.io.L2.BID := LLC.io.cache_axi.BID
  interconnect.io.L2.BRESP := LLC.io.cache_axi.BRESP
  

//LLC connection with memory
  //AW
  memory.clients(1).AWVALID := LLC.io.mem_write_axi.AWVALID
  memory.clients(1).AWID := LLC.io.mem_write_axi.AWID
  memory.clients(1).AWADDR := LLC.io.mem_write_axi.AWADDR
  memory.clients(1).AWLEN := LLC.io.mem_write_axi.AWLEN
  memory.clients(1).AWSIZE := LLC.io.mem_write_axi.AWSIZE
  memory.clients(1).AWBURST := LLC.io.mem_write_axi.AWBURST
  memory.clients(1).AWLOCK := LLC.io.mem_write_axi.AWLOCK
  memory.clients(1).AWCACHE := LLC.io.mem_write_axi.AWCACHE
  memory.clients(1).AWPROT := LLC.io.mem_write_axi.AWPROT
  memory.clients(1).AWQOS := LLC.io.mem_write_axi.AWQOS
  LLC.io.mem_write_axi.AWREADY := memory.clients(1).AWREADY

  //AR
  memory.clients(1).ARVALID := LLC.io.mem_read_axi.ARVALID
  memory.clients(1).ARID := LLC.io.mem_read_axi.ARID
  memory.clients(1).ARADDR := LLC.io.mem_read_axi.ARADDR
  memory.clients(1).ARLEN := LLC.io.mem_read_axi.ARLEN
  memory.clients(1).ARSIZE := LLC.io.mem_read_axi.ARSIZE
  memory.clients(1).ARBURST := LLC.io.mem_read_axi.ARBURST
  memory.clients(1).ARLOCK := LLC.io.mem_read_axi.ARLOCK
  memory.clients(1).ARCACHE := LLC.io.mem_read_axi.ARCACHE
  memory.clients(1).ARPROT := LLC.io.mem_read_axi.ARPROT
  memory.clients(1).ARQOS := LLC.io.mem_read_axi.ARQOS
  LLC.io.mem_read_axi.ARREADY := memory.clients(1).ARREADY

  //W
  memory.clients(1).WVALID := LLC.io.mem_write_axi.WVALID
  memory.clients(1).WDATA := LLC.io.mem_write_axi.WDATA
  memory.clients(1).WLAST := LLC.io.mem_write_axi.WLAST
  memory.clients(1).WSTRB := LLC.io.mem_write_axi.WSTRB
  LLC.io.mem_write_axi.WREADY := memory.clients(1).WREADY

  //R
  memory.clients(1).RREADY := LLC.io.mem_read_axi.RREADY
  LLC.io.mem_read_axi.RID := memory.clients(1).RID
  LLC.io.mem_read_axi.RDATA := memory.clients(1).RDATA
  LLC.io.mem_read_axi.RRESP := memory.clients(1).RRESP
  LLC.io.mem_read_axi.RLAST := memory.clients(1).RLAST
  LLC.io.mem_read_axi.RVALID := memory.clients(1).RVALID


  //B
  memory.clients(1).BREADY := LLC.io.mem_write_axi.BREADY
  LLC.io.mem_write_axi.BVALID := memory.clients(1).BVALID
  LLC.io.mem_write_axi.BRESP := memory.clients(1).BRESP
  LLC.io.mem_write_axi.BID := 0.U


  //memory.clients(0) should be unconnedted and pulled down
  memory.clients(0).AWVALID := false.B
  memory.clients(0).AWID := 0.U
  memory.clients(0).AWADDR := 0.U
  memory.clients(0).AWLEN := 7.U
  memory.clients(0).AWSIZE := 5.U
  memory.clients(0).AWBURST := 1.U
  memory.clients(0).AWLOCK := 0.U
  memory.clients(0).AWCACHE := 2.U
  memory.clients(0).AWPROT := 0.U
  memory.clients(0).AWQOS := 0.U

  //AR
  memory.clients(0).ARVALID := false.B
  memory.clients(0).ARID := 0.U
  memory.clients(0).ARADDR := 0.U
  memory.clients(0).ARLEN := 7.U
  memory.clients(0).ARSIZE := 5.U
  memory.clients(0).ARBURST := 1.U
  memory.clients(0).ARLOCK := 0.U
  memory.clients(0).ARCACHE := 2.U
  memory.clients(0).ARPROT := 0.U
  memory.clients(0).ARQOS := 0.U

  //W
  memory.clients(0).WVALID := false.B
  memory.clients(0).WDATA := 0.U
  memory.clients(0).WLAST := 0.U
  memory.clients(0).WSTRB := "b11111111".U

  //R
  memory.clients(0).RREADY := false.B

  //B
  memory.clients(0).BREADY := false.B


  //Programming mainMemory
  val programmer = IO(Input(memory.programmer.cloneType))
  memory.programmer := programmer

  val finishedProgramming = IO(Input(memory.finishedProgramming.cloneType))
  memory.finishedProgramming := finishedProgramming

  val peripheralUart = Module(new uart{
    val putCharOut = IO(Output(putChar.cloneType))
    putCharOut := putChar
  })

  val putChar = IO(Output(peripheralUart.putCharOut.cloneType))
  putChar := peripheralUart.putCharOut

  core0.peripheral <> peripheralUart.client

  val prober = IO(memory.externalProbe.cloneType)
  prober <> memory.externalProbe

  val registersOut = IO(Output(core0.registersOut.cloneType))
  val registersOutBuffer = Reg(registersOut.cloneType)
  registersOut := Mux(core0.robOut.commitFired && RegNext(core0.robOut.commitFired, false.B), core0.registersOut ,registersOutBuffer)
  registersOut(32) := core0.registersOut(32)

  val robOut = IO(Output(core0.robOut.cloneType))
  robOut := core0.robOut
  when(RegNext(core0.allRobFiresOut, false.B)) { registersOutBuffer := core0.registersOut }

  /* val sampleOut = IO(Output(core0.sampleOut.cloneType))
  sampleOut := core0.sampleOut

  core0.MTIP := peripheralUart.MTIP
 */
  core0.MTIP := peripheralUart.MTIP
  // core0.PWR_ON := true.B
  //val sample = IO(Output(core0.status.cloneType))
  //sample := core0.status

  //val storesPendingOut = IO(Output(core0.storesPendingOut.cloneType))
  //storesPendingOut := core0.storesPendingOut

  //val robAddrRelease = IO(Output(core0.robAddrRelease.cloneType))
  //robAddrRelease := core0.robAddrRelease

  //val robOfDataQueue = IO(Output(core0.dataQueueRobRelease.cloneType))
  //robOfDataQueue := core0.dataQueueRobRelease

  // === System-level performance counters ===
  // Counter IDs match profiler.h
  val pc_dCacheMiss    = RegInit(0.U(64.W)) // 0: D-Cache→L2 read requests (= D-Cache read misses)
  val pc_dCacheRdBeats = RegInit(0.U(64.W)) // 1: D-Cache read data beats received
  val pc_dCacheWrBeats = RegInit(0.U(64.W)) // 2: D-Cache write data beats sent
  val pc_iCacheMiss    = RegInit(0.U(64.W)) // 3: I-Cache→L2 read requests (= I-Cache misses)
  val pc_iCacheRdBeats = RegInit(0.U(64.W)) // 4: I-Cache read data beats received
  val pc_l2ToMemRdReqs = RegInit(0.U(64.W)) // 5: L2→DRAM read requests (= L2 read misses)
  val pc_l2ToMemRdBeats= RegInit(0.U(64.W)) // 6: L2→DRAM read data beats
  val pc_l2ToMemWrBeats= RegInit(0.U(64.W)) // 7: L2→DRAM write data beats

  when(core0.dPort.ARVALID && interconnect.io.acePort0.ARREADY)  { pc_dCacheMiss    := pc_dCacheMiss    + 1.U }
  when(interconnect.io.acePort0.RVALID && core0.dPort.RREADY)    { pc_dCacheRdBeats := pc_dCacheRdBeats + 1.U }
  when(core0.dPort.WVALID && interconnect.io.acePort0.WREADY)    { pc_dCacheWrBeats := pc_dCacheWrBeats + 1.U }
  when(core0.iPort.ARVALID && interconnect.io.acePort1.ARREADY)  { pc_iCacheMiss    := pc_iCacheMiss    + 1.U }
  when(interconnect.io.acePort1.RVALID && core0.iPort.RREADY)    { pc_iCacheRdBeats := pc_iCacheRdBeats + 1.U }
  when(LLC.io.mem_read_axi.ARVALID && memory.clients(1).ARREADY) { pc_l2ToMemRdReqs := pc_l2ToMemRdReqs + 1.U }
  when(memory.clients(1).RVALID && LLC.io.mem_read_axi.RREADY)   { pc_l2ToMemRdBeats:= pc_l2ToMemRdBeats + 1.U }
  when(LLC.io.mem_write_axi.WVALID && memory.clients(1).WREADY)  { pc_l2ToMemWrBeats:= pc_l2ToMemWrBeats + 1.U }

  // Expose as a flat Vec for easy C++ access
  // 10 core + 8 system + 4 frontend-bubble + 3 boundary + 4 streamer-diag + 2 rob-split + 5 head-class + 3 rnr-gate = 39
  val perfCountersOut = IO(Output(Vec(39, UInt(64.W))))
  perfCountersOut(0)  := core0.perfCnt.cycles
  perfCountersOut(1)  := core0.perfCnt.instRetired
  perfCountersOut(2)  := core0.perfCnt.branchTotal
  perfCountersOut(3)  := core0.perfCnt.branchesPassed
  perfCountersOut(4)  := core0.perfCnt.schedulerStalls
  perfCountersOut(5)  := core0.perfCnt.robStalls
  perfCountersOut(6)  := core0.perfCnt.decodeReady
  perfCountersOut(7)  := core0.perfCnt.decodeFired
  perfCountersOut(8)  := core0.perfCnt.icacheStalls
  perfCountersOut(9)  := core0.perfCnt.dcacheReqs
  perfCountersOut(10) := pc_dCacheMiss
  perfCountersOut(11) := pc_dCacheRdBeats
  perfCountersOut(12) := pc_dCacheWrBeats
  perfCountersOut(13) := pc_iCacheMiss
  perfCountersOut(14) := pc_iCacheRdBeats
  perfCountersOut(15) := pc_l2ToMemRdReqs
  perfCountersOut(16) := pc_l2ToMemRdBeats
  perfCountersOut(17) := pc_l2ToMemWrBeats
  perfCountersOut(18) := core0.perfCnt.feFetchNotReady
  perfCountersOut(19) := core0.perfCnt.feDecodeNotReady
  perfCountersOut(20) := core0.perfCnt.feExpectedBlock
  perfCountersOut(21) := core0.perfCnt.feRespValidIdle
  perfCountersOut(22) := core0.perfCnt.feCacheNotProd
  perfCountersOut(23) := core0.perfCnt.feReqFire
  perfCountersOut(24) := core0.perfCnt.feReqRefused
  perfCountersOut(25) := core0.perfCnt.strmCurValid
  perfCountersOut(26) := core0.perfCnt.strmValidMiss
  perfCountersOut(27) := core0.perfCnt.strmLastReqBase
  perfCountersOut(28) := core0.perfCnt.strmLastCurBase
  perfCountersOut(29) := core0.perfCnt.robHeadNotReady
  perfCountersOut(30) := core0.perfCnt.robReadyBlocked
  perfCountersOut(31) := core0.perfCnt.hnrLoad
  perfCountersOut(32) := core0.perfCnt.hnrBranch
  perfCountersOut(33) := core0.perfCnt.hnrMext
  perfCountersOut(34) := core0.perfCnt.hnrAmo
  perfCountersOut(35) := core0.perfCnt.hnrOther
  perfCountersOut(36) := core0.perfCnt.rnrStoreGate
  perfCountersOut(37) := core0.perfCnt.rnrWbGate
  perfCountersOut(38) := core0.perfCnt.rnrLoadGate
}

object system extends App {
  emitVerilog(new system)
}
