// No `package testbench` here even though this file lives in the testbench/
// directory: it needs unqualified access to `core`/`Interconnect`/`L2_cache`,
// which live in the default (unnamed) package — same reason system.scala
// (also physically in this directory) has no package line either.
//
// chironCore — the target-independent heart of the chip: 4 cores, the ACE
// Interconnect (CCU), and the L2 (LLC), wired identically for every build
// target. Extracted out of what was originally all inlined in `system`
// (testbench/system.scala) so the memory backend (mainMemory in sim, real
// DDR3 via MIG on FPGA — see testbench/fpgaTop.scala) and the console/CLINT
// peripheral attachment can differ per target without duplicating this ~900
// lines of core/interconnect/L2 wiring, or risking it drifting out of sync
// between two hand-copied versions.
//
// IO boundary: LLC's mem_read_axi/mem_write_axi (an AXI master pair — sim
// wires these into mainMemory.clients(1); FPGA exposes them at chip level for
// Vivado to route to MIG), one AXI master peripheral port per core (sim wires
// these into MultiUart's client0-3; FPGA does the same), MTIP/MSIP inputs per
// core (driven by whichever CLINT/UART peripheral is attached), and the
// per-core debug/profiling outputs (registersOut/robOut/perfCountersOut) that
// existed at system's top level before this extraction — unchanged shape, so
// system.scala's own top-level ports (and every C++ harness reading them)
// don't need to change at all.
import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import chisel3.experimental.IO

import pipeline.ports._
import common.coreConfiguration._
import Icache.AXI
import Decode.constants
import Interconnect._
import L2_cache._

class chironCore extends Module {

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
    val pc_feFetchNotReady  = RegInit(0.U(64.W))
    val pc_feDecodeNotReady = RegInit(0.U(64.W))
    val pc_feExpectedBlock  = RegInit(0.U(64.W))
    val pc_robHeadNotReady  = RegInit(0.U(64.W))
    val pc_robReadyBlocked  = RegInit(0.U(64.W))
    val pc_hnrLoad   = RegInit(0.U(64.W))
    val pc_hnrBranch = RegInit(0.U(64.W))
    val pc_hnrMext   = RegInit(0.U(64.W))
    val pc_hnrAmo    = RegInit(0.U(64.W))
    val pc_hnrOther  = RegInit(0.U(64.W))
    // D-cache hit-latency probe. hnrLoad counts the *cycles* a head-of-ROB load
    // stalls, which cannot say what a shorter hit pipeline would buy. Count the
    // stall *episodes* too: one per load that stalls at the head at all, and
    // those that last >= 2 cycles. Trimming one stage off the hit path removes
    // at most `episodes` cycles, two stages at most `episodes + ge2`.
    val pc_hnrLoadEpisodes = RegInit(0.U(64.W))
    val pc_hnrLoadGE2      = RegInit(0.U(64.W))
    val pc_rnrStoreGate = RegInit(0.U(64.W))
    val pc_rnrWbGate    = RegInit(0.U(64.W))
    val pc_rnrLoadGate  = RegInit(0.U(64.W))
    val pc_issueReadyGE2 = RegInit(0.U(64.W))
    val pc_commitTwoOpp  = RegInit(0.U(64.W))
    // Why decode refused the next instruction — see decode.stallReason.
    val pc_dsPrfExhausted   = RegInit(0.U(64.W))
    val pc_dsBranchMaskFull = RegInit(0.U(64.W))
    val pc_dsRenameCollide  = RegInit(0.U(64.W))
    // What actually flushed the pipeline. branchTotal/branchesPassed lump the
    // coherency load-squash in with branch resolution (core.scala forces
    // branchEvals.valid high and .passed low for it), so "branch accuracy" is
    // not a predictor metric. Split the flushes and count real retired branches.
    val pc_flushBranch    = RegInit(0.U(64.W))
    val pc_flushCoherent  = RegInit(0.U(64.W))
    val pc_retiredBranch  = RegInit(0.U(64.W))

    pc_cycles := pc_cycles + 1.U
    when(rob.commit.fired) { pc_instRetired := pc_instRetired + 1.U }
    when(branchOps.valid) {
      pc_branchTotal := pc_branchTotal + 1.U
      when(branchOps.passed) { pc_branchesPassed := pc_branchesPassed + 1.U }
    }
    when(decode.toExec.ready) {
      pc_decodeReady := pc_decodeReady + 1.U
      when(decode.toExec.fired)       { pc_decodeFired := pc_decodeFired + 1.U }
      when(!scheduler.allocate.ready) { pc_schedStalls := pc_schedStalls + 1.U }
      when(!rob.allocate.ready)       { pc_robStalls   := pc_robStalls   + 1.U }
    }
    when(icache.fromFetch.req.valid && !icache.fromFetch.resp.valid) {
      pc_icacheStalls := pc_icacheStalls + 1.U
    }
    when(memoryRequest.valid) { pc_dcacheReqs := pc_dcacheReqs + 1.U }

    val fe_expMismatch = decode.fromFetch.expected.valid &&
                         (decode.fromFetch.expected.pc =/= fetch.toDecode.pc)
    when(!fetch.toDecode.ready) {
      pc_feFetchNotReady := pc_feFetchNotReady + 1.U
    }.elsewhen(!decode.fromFetch.ready) {
      pc_feDecodeNotReady := pc_feDecodeNotReady + 1.U
    }.elsewhen(fe_expMismatch) {
      pc_feExpectedBlock := pc_feExpectedBlock + 1.U
    }

    when(rob.headValid && !rob.commit.ready)    { pc_robHeadNotReady := pc_robHeadNotReady + 1.U }
    when(rob.commit.ready && !rob.commit.fired) { pc_robReadyBlocked := pc_robReadyBlocked + 1.U }

    val headOp       = rob.commit.instruction(6, 0)
    val headIsLoad   = headOp === "b0000011".U
    val headIsBranch = headOp === "b1100011".U || headOp === "b1101111".U || headOp === "b1100111".U
    val headIsMext   = (headOp === "b0110011".U || headOp === "b0111011".U) && rob.commit.instruction(25)
    val headIsAmo    = headOp === "b0101111".U
    when(rob.headValid && !rob.commit.ready) {
      when(headIsLoad)        { pc_hnrLoad   := pc_hnrLoad   + 1.U }
      .elsewhen(headIsBranch) { pc_hnrBranch := pc_hnrBranch + 1.U }
      .elsewhen(headIsMext)   { pc_hnrMext   := pc_hnrMext   + 1.U }
      .elsewhen(headIsAmo)    { pc_hnrAmo    := pc_hnrAmo    + 1.U }
      .otherwise              { pc_hnrOther  := pc_hnrOther  + 1.U }
    }

    // One episode per load that stalls at the head. Commit is 1-wide, so two
    // consecutive stalling loads are always separated by the cycle in which the
    // first one commits (commit.ready high) -- the condition drops and the
    // episodes stay distinct.
    val hnrLoadCond  = rob.headValid && !rob.commit.ready && headIsLoad
    val hnrLoadPrev  = RegNext(hnrLoadCond, false.B)
    val hnrLoadPrev2 = RegNext(hnrLoadPrev, false.B)
    when(hnrLoadCond && !hnrLoadPrev) {
      pc_hnrLoadEpisodes := pc_hnrLoadEpisodes + 1.U
    }
    when(hnrLoadCond && hnrLoadPrev && !hnrLoadPrev2) {
      pc_hnrLoadGE2 := pc_hnrLoadGE2 + 1.U
    }
    when(rob.commit.ready && !rob.commit.fired) {
      when((rob.commit.instruction(6, 4) === "b010".U) && !memAccess.writeInstructionCommit.ready) {
        pc_rnrStoreGate := pc_rnrStoreGate + 1.U
      }
      when(!decode.writeBackResult.ready) { pc_rnrWbGate := pc_rnrWbGate + 1.U }
      when((rob.commit.instruction(6, 2).orR === 0.U) && (coherentLoadInvalid || !memAccess.loadCommit.valid)) {
        pc_rnrLoadGate := pc_rnrLoadGate + 1.U
      }
    }
    when(scheduler.readyCount >= 2.U)          { pc_issueReadyGE2 := pc_issueReadyGE2 + 1.U }
    when(rob.commit.fired && rob.secondReady)   { pc_commitTwoOpp  := pc_commitTwoOpp  + 1.U }

    when(branchOps.valid && !branchOps.passed) {
      when(coherentLoadInvalidReg) { pc_flushCoherent := pc_flushCoherent + 1.U }
      .otherwise                   { pc_flushBranch   := pc_flushBranch   + 1.U }
    }
    when(rob.commit.fired && rob.commit.instruction(6, 4) === "b110".U) {
      pc_retiredBranch := pc_retiredBranch + 1.U
    }

    when(decode.stallReason.prfExhausted)    { pc_dsPrfExhausted   := pc_dsPrfExhausted   + 1.U }
    when(decode.stallReason.branchMaskFull)  { pc_dsBranchMaskFull := pc_dsBranchMaskFull + 1.U }
    when(decode.stallReason.renameCollision) { pc_dsRenameCollide  := pc_dsRenameCollide  + 1.U }

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
      val robHeadNotReady = UInt(64.W)
      val robReadyBlocked = UInt(64.W)
      val hnrLoad         = UInt(64.W)
      val hnrBranch       = UInt(64.W)
      val hnrMext         = UInt(64.W)
      val hnrAmo          = UInt(64.W)
      val hnrOther        = UInt(64.W)
      val hnrLoadEpisodes = UInt(64.W)
      val hnrLoadGE2      = UInt(64.W)
      val rnrStoreGate    = UInt(64.W)
      val rnrWbGate       = UInt(64.W)
      val rnrLoadGate     = UInt(64.W)
      val issueReadyGE2   = UInt(64.W)
      val commitTwoOpp    = UInt(64.W)
      val dsPrfExhausted  = UInt(64.W)
      val dsBranchMaskFull= UInt(64.W)
      val dsRenameCollide = UInt(64.W)
      val flushBranch     = UInt(64.W)
      val flushCoherent   = UInt(64.W)
      val retiredBranch   = UInt(64.W)
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
    perfCnt.robHeadNotReady := pc_robHeadNotReady
    perfCnt.robReadyBlocked := pc_robReadyBlocked
    perfCnt.hnrLoad         := pc_hnrLoad
    perfCnt.hnrBranch       := pc_hnrBranch
    perfCnt.hnrMext         := pc_hnrMext
    perfCnt.hnrAmo          := pc_hnrAmo
    perfCnt.hnrOther        := pc_hnrOther
    perfCnt.hnrLoadEpisodes := pc_hnrLoadEpisodes
    perfCnt.hnrLoadGE2      := pc_hnrLoadGE2
    perfCnt.rnrStoreGate    := pc_rnrStoreGate
    perfCnt.rnrWbGate       := pc_rnrWbGate
    perfCnt.rnrLoadGate     := pc_rnrLoadGate
    perfCnt.issueReadyGE2   := pc_issueReadyGE2
    perfCnt.commitTwoOpp    := pc_commitTwoOpp
    perfCnt.dsPrfExhausted  := pc_dsPrfExhausted
    perfCnt.dsBranchMaskFull:= pc_dsBranchMaskFull
    perfCnt.dsRenameCollide := pc_dsRenameCollide
    perfCnt.flushBranch     := pc_flushBranch
    perfCnt.flushCoherent   := pc_flushCoherent
    perfCnt.retiredBranch   := pc_retiredBranch
  })

  val core1 = Module(new core(
    dPort_id = 2,
    peripheral_id = 1,
    iPort_id = 3,
    mhart_id = 1
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
    val pc_feFetchNotReady  = RegInit(0.U(64.W))
    val pc_feDecodeNotReady = RegInit(0.U(64.W))
    val pc_feExpectedBlock  = RegInit(0.U(64.W))
    val pc_robHeadNotReady  = RegInit(0.U(64.W))
    val pc_robReadyBlocked  = RegInit(0.U(64.W))
    val pc_hnrLoad   = RegInit(0.U(64.W))
    val pc_hnrBranch = RegInit(0.U(64.W))
    val pc_hnrMext   = RegInit(0.U(64.W))
    val pc_hnrAmo    = RegInit(0.U(64.W))
    val pc_hnrOther  = RegInit(0.U(64.W))
    // D-cache hit-latency probe. hnrLoad counts the *cycles* a head-of-ROB load
    // stalls, which cannot say what a shorter hit pipeline would buy. Count the
    // stall *episodes* too: one per load that stalls at the head at all, and
    // those that last >= 2 cycles. Trimming one stage off the hit path removes
    // at most `episodes` cycles, two stages at most `episodes + ge2`.
    val pc_hnrLoadEpisodes = RegInit(0.U(64.W))
    val pc_hnrLoadGE2      = RegInit(0.U(64.W))
    val pc_rnrStoreGate = RegInit(0.U(64.W))
    val pc_rnrWbGate    = RegInit(0.U(64.W))
    val pc_rnrLoadGate  = RegInit(0.U(64.W))
    val pc_issueReadyGE2 = RegInit(0.U(64.W))
    val pc_commitTwoOpp  = RegInit(0.U(64.W))
    // Why decode refused the next instruction — see decode.stallReason.
    val pc_dsPrfExhausted   = RegInit(0.U(64.W))
    val pc_dsBranchMaskFull = RegInit(0.U(64.W))
    val pc_dsRenameCollide  = RegInit(0.U(64.W))
    // What actually flushed the pipeline. branchTotal/branchesPassed lump the
    // coherency load-squash in with branch resolution (core.scala forces
    // branchEvals.valid high and .passed low for it), so "branch accuracy" is
    // not a predictor metric. Split the flushes and count real retired branches.
    val pc_flushBranch    = RegInit(0.U(64.W))
    val pc_flushCoherent  = RegInit(0.U(64.W))
    val pc_retiredBranch  = RegInit(0.U(64.W))

    pc_cycles := pc_cycles + 1.U
    when(rob.commit.fired) { pc_instRetired := pc_instRetired + 1.U }
    when(branchOps.valid) {
      pc_branchTotal := pc_branchTotal + 1.U
      when(branchOps.passed) { pc_branchesPassed := pc_branchesPassed + 1.U }
    }
    when(decode.toExec.ready) {
      pc_decodeReady := pc_decodeReady + 1.U
      when(decode.toExec.fired)       { pc_decodeFired := pc_decodeFired + 1.U }
      when(!scheduler.allocate.ready) { pc_schedStalls := pc_schedStalls + 1.U }
      when(!rob.allocate.ready)       { pc_robStalls   := pc_robStalls   + 1.U }
    }
    when(icache.fromFetch.req.valid && !icache.fromFetch.resp.valid) {
      pc_icacheStalls := pc_icacheStalls + 1.U
    }
    when(memoryRequest.valid) { pc_dcacheReqs := pc_dcacheReqs + 1.U }

    val fe_expMismatch = decode.fromFetch.expected.valid &&
                         (decode.fromFetch.expected.pc =/= fetch.toDecode.pc)
    when(!fetch.toDecode.ready) {
      pc_feFetchNotReady := pc_feFetchNotReady + 1.U
    }.elsewhen(!decode.fromFetch.ready) {
      pc_feDecodeNotReady := pc_feDecodeNotReady + 1.U
    }.elsewhen(fe_expMismatch) {
      pc_feExpectedBlock := pc_feExpectedBlock + 1.U
    }

    when(rob.headValid && !rob.commit.ready)    { pc_robHeadNotReady := pc_robHeadNotReady + 1.U }
    when(rob.commit.ready && !rob.commit.fired) { pc_robReadyBlocked := pc_robReadyBlocked + 1.U }

    val headOp       = rob.commit.instruction(6, 0)
    val headIsLoad   = headOp === "b0000011".U
    val headIsBranch = headOp === "b1100011".U || headOp === "b1101111".U || headOp === "b1100111".U
    val headIsMext   = (headOp === "b0110011".U || headOp === "b0111011".U) && rob.commit.instruction(25)
    val headIsAmo    = headOp === "b0101111".U
    when(rob.headValid && !rob.commit.ready) {
      when(headIsLoad)        { pc_hnrLoad   := pc_hnrLoad   + 1.U }
      .elsewhen(headIsBranch) { pc_hnrBranch := pc_hnrBranch + 1.U }
      .elsewhen(headIsMext)   { pc_hnrMext   := pc_hnrMext   + 1.U }
      .elsewhen(headIsAmo)    { pc_hnrAmo    := pc_hnrAmo    + 1.U }
      .otherwise              { pc_hnrOther  := pc_hnrOther  + 1.U }
    }

    // One episode per load that stalls at the head. Commit is 1-wide, so two
    // consecutive stalling loads are always separated by the cycle in which the
    // first one commits (commit.ready high) -- the condition drops and the
    // episodes stay distinct.
    val hnrLoadCond  = rob.headValid && !rob.commit.ready && headIsLoad
    val hnrLoadPrev  = RegNext(hnrLoadCond, false.B)
    val hnrLoadPrev2 = RegNext(hnrLoadPrev, false.B)
    when(hnrLoadCond && !hnrLoadPrev) {
      pc_hnrLoadEpisodes := pc_hnrLoadEpisodes + 1.U
    }
    when(hnrLoadCond && hnrLoadPrev && !hnrLoadPrev2) {
      pc_hnrLoadGE2 := pc_hnrLoadGE2 + 1.U
    }
    when(rob.commit.ready && !rob.commit.fired) {
      when((rob.commit.instruction(6, 4) === "b010".U) && !memAccess.writeInstructionCommit.ready) {
        pc_rnrStoreGate := pc_rnrStoreGate + 1.U
      }
      when(!decode.writeBackResult.ready) { pc_rnrWbGate := pc_rnrWbGate + 1.U }
      when((rob.commit.instruction(6, 2).orR === 0.U) && (coherentLoadInvalid || !memAccess.loadCommit.valid)) {
        pc_rnrLoadGate := pc_rnrLoadGate + 1.U
      }
    }
    when(scheduler.readyCount >= 2.U)          { pc_issueReadyGE2 := pc_issueReadyGE2 + 1.U }
    when(rob.commit.fired && rob.secondReady)   { pc_commitTwoOpp  := pc_commitTwoOpp  + 1.U }

    when(branchOps.valid && !branchOps.passed) {
      when(coherentLoadInvalidReg) { pc_flushCoherent := pc_flushCoherent + 1.U }
      .otherwise                   { pc_flushBranch   := pc_flushBranch   + 1.U }
    }
    when(rob.commit.fired && rob.commit.instruction(6, 4) === "b110".U) {
      pc_retiredBranch := pc_retiredBranch + 1.U
    }

    when(decode.stallReason.prfExhausted)    { pc_dsPrfExhausted   := pc_dsPrfExhausted   + 1.U }
    when(decode.stallReason.branchMaskFull)  { pc_dsBranchMaskFull := pc_dsBranchMaskFull + 1.U }
    when(decode.stallReason.renameCollision) { pc_dsRenameCollide  := pc_dsRenameCollide  + 1.U }

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
      val robHeadNotReady = UInt(64.W)
      val robReadyBlocked = UInt(64.W)
      val hnrLoad         = UInt(64.W)
      val hnrBranch       = UInt(64.W)
      val hnrMext         = UInt(64.W)
      val hnrAmo          = UInt(64.W)
      val hnrOther        = UInt(64.W)
      val hnrLoadEpisodes = UInt(64.W)
      val hnrLoadGE2      = UInt(64.W)
      val rnrStoreGate    = UInt(64.W)
      val rnrWbGate       = UInt(64.W)
      val rnrLoadGate     = UInt(64.W)
      val issueReadyGE2   = UInt(64.W)
      val commitTwoOpp    = UInt(64.W)
      val dsPrfExhausted  = UInt(64.W)
      val dsBranchMaskFull= UInt(64.W)
      val dsRenameCollide = UInt(64.W)
      val flushBranch     = UInt(64.W)
      val flushCoherent   = UInt(64.W)
      val retiredBranch   = UInt(64.W)
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
    perfCnt.robHeadNotReady := pc_robHeadNotReady
    perfCnt.robReadyBlocked := pc_robReadyBlocked
    perfCnt.hnrLoad         := pc_hnrLoad
    perfCnt.hnrBranch       := pc_hnrBranch
    perfCnt.hnrMext         := pc_hnrMext
    perfCnt.hnrAmo          := pc_hnrAmo
    perfCnt.hnrOther        := pc_hnrOther
    perfCnt.hnrLoadEpisodes := pc_hnrLoadEpisodes
    perfCnt.hnrLoadGE2      := pc_hnrLoadGE2
    perfCnt.rnrStoreGate    := pc_rnrStoreGate
    perfCnt.rnrWbGate       := pc_rnrWbGate
    perfCnt.rnrLoadGate     := pc_rnrLoadGate
    perfCnt.issueReadyGE2   := pc_issueReadyGE2
    perfCnt.commitTwoOpp    := pc_commitTwoOpp
    perfCnt.dsPrfExhausted  := pc_dsPrfExhausted
    perfCnt.dsBranchMaskFull:= pc_dsBranchMaskFull
    perfCnt.dsRenameCollide := pc_dsRenameCollide
    perfCnt.flushBranch     := pc_flushBranch
    perfCnt.flushCoherent   := pc_flushCoherent
    perfCnt.retiredBranch   := pc_retiredBranch
  })

  val core2 = Module(new core(
    dPort_id = 4,
    peripheral_id = 2,
    iPort_id = 5,
    mhart_id = 2
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
    val pc_feFetchNotReady  = RegInit(0.U(64.W))
    val pc_feDecodeNotReady = RegInit(0.U(64.W))
    val pc_feExpectedBlock  = RegInit(0.U(64.W))
    val pc_robHeadNotReady  = RegInit(0.U(64.W))
    val pc_robReadyBlocked  = RegInit(0.U(64.W))
    val pc_hnrLoad   = RegInit(0.U(64.W))
    val pc_hnrBranch = RegInit(0.U(64.W))
    val pc_hnrMext   = RegInit(0.U(64.W))
    val pc_hnrAmo    = RegInit(0.U(64.W))
    val pc_hnrOther  = RegInit(0.U(64.W))
    // D-cache hit-latency probe. hnrLoad counts the *cycles* a head-of-ROB load
    // stalls, which cannot say what a shorter hit pipeline would buy. Count the
    // stall *episodes* too: one per load that stalls at the head at all, and
    // those that last >= 2 cycles. Trimming one stage off the hit path removes
    // at most `episodes` cycles, two stages at most `episodes + ge2`.
    val pc_hnrLoadEpisodes = RegInit(0.U(64.W))
    val pc_hnrLoadGE2      = RegInit(0.U(64.W))
    val pc_rnrStoreGate = RegInit(0.U(64.W))
    val pc_rnrWbGate    = RegInit(0.U(64.W))
    val pc_rnrLoadGate  = RegInit(0.U(64.W))
    val pc_issueReadyGE2 = RegInit(0.U(64.W))
    val pc_commitTwoOpp  = RegInit(0.U(64.W))
    // Why decode refused the next instruction — see decode.stallReason.
    val pc_dsPrfExhausted   = RegInit(0.U(64.W))
    val pc_dsBranchMaskFull = RegInit(0.U(64.W))
    val pc_dsRenameCollide  = RegInit(0.U(64.W))
    // What actually flushed the pipeline. branchTotal/branchesPassed lump the
    // coherency load-squash in with branch resolution (core.scala forces
    // branchEvals.valid high and .passed low for it), so "branch accuracy" is
    // not a predictor metric. Split the flushes and count real retired branches.
    val pc_flushBranch    = RegInit(0.U(64.W))
    val pc_flushCoherent  = RegInit(0.U(64.W))
    val pc_retiredBranch  = RegInit(0.U(64.W))

    pc_cycles := pc_cycles + 1.U
    when(rob.commit.fired) { pc_instRetired := pc_instRetired + 1.U }
    when(branchOps.valid) {
      pc_branchTotal := pc_branchTotal + 1.U
      when(branchOps.passed) { pc_branchesPassed := pc_branchesPassed + 1.U }
    }
    when(decode.toExec.ready) {
      pc_decodeReady := pc_decodeReady + 1.U
      when(decode.toExec.fired)       { pc_decodeFired := pc_decodeFired + 1.U }
      when(!scheduler.allocate.ready) { pc_schedStalls := pc_schedStalls + 1.U }
      when(!rob.allocate.ready)       { pc_robStalls   := pc_robStalls   + 1.U }
    }
    when(icache.fromFetch.req.valid && !icache.fromFetch.resp.valid) {
      pc_icacheStalls := pc_icacheStalls + 1.U
    }
    when(memoryRequest.valid) { pc_dcacheReqs := pc_dcacheReqs + 1.U }

    val fe_expMismatch = decode.fromFetch.expected.valid &&
                         (decode.fromFetch.expected.pc =/= fetch.toDecode.pc)
    when(!fetch.toDecode.ready) {
      pc_feFetchNotReady := pc_feFetchNotReady + 1.U
    }.elsewhen(!decode.fromFetch.ready) {
      pc_feDecodeNotReady := pc_feDecodeNotReady + 1.U
    }.elsewhen(fe_expMismatch) {
      pc_feExpectedBlock := pc_feExpectedBlock + 1.U
    }

    when(rob.headValid && !rob.commit.ready)    { pc_robHeadNotReady := pc_robHeadNotReady + 1.U }
    when(rob.commit.ready && !rob.commit.fired) { pc_robReadyBlocked := pc_robReadyBlocked + 1.U }

    val headOp       = rob.commit.instruction(6, 0)
    val headIsLoad   = headOp === "b0000011".U
    val headIsBranch = headOp === "b1100011".U || headOp === "b1101111".U || headOp === "b1100111".U
    val headIsMext   = (headOp === "b0110011".U || headOp === "b0111011".U) && rob.commit.instruction(25)
    val headIsAmo    = headOp === "b0101111".U
    when(rob.headValid && !rob.commit.ready) {
      when(headIsLoad)        { pc_hnrLoad   := pc_hnrLoad   + 1.U }
      .elsewhen(headIsBranch) { pc_hnrBranch := pc_hnrBranch + 1.U }
      .elsewhen(headIsMext)   { pc_hnrMext   := pc_hnrMext   + 1.U }
      .elsewhen(headIsAmo)    { pc_hnrAmo    := pc_hnrAmo    + 1.U }
      .otherwise              { pc_hnrOther  := pc_hnrOther  + 1.U }
    }

    // One episode per load that stalls at the head. Commit is 1-wide, so two
    // consecutive stalling loads are always separated by the cycle in which the
    // first one commits (commit.ready high) -- the condition drops and the
    // episodes stay distinct.
    val hnrLoadCond  = rob.headValid && !rob.commit.ready && headIsLoad
    val hnrLoadPrev  = RegNext(hnrLoadCond, false.B)
    val hnrLoadPrev2 = RegNext(hnrLoadPrev, false.B)
    when(hnrLoadCond && !hnrLoadPrev) {
      pc_hnrLoadEpisodes := pc_hnrLoadEpisodes + 1.U
    }
    when(hnrLoadCond && hnrLoadPrev && !hnrLoadPrev2) {
      pc_hnrLoadGE2 := pc_hnrLoadGE2 + 1.U
    }
    when(rob.commit.ready && !rob.commit.fired) {
      when((rob.commit.instruction(6, 4) === "b010".U) && !memAccess.writeInstructionCommit.ready) {
        pc_rnrStoreGate := pc_rnrStoreGate + 1.U
      }
      when(!decode.writeBackResult.ready) { pc_rnrWbGate := pc_rnrWbGate + 1.U }
      when((rob.commit.instruction(6, 2).orR === 0.U) && (coherentLoadInvalid || !memAccess.loadCommit.valid)) {
        pc_rnrLoadGate := pc_rnrLoadGate + 1.U
      }
    }
    when(scheduler.readyCount >= 2.U)          { pc_issueReadyGE2 := pc_issueReadyGE2 + 1.U }
    when(rob.commit.fired && rob.secondReady)   { pc_commitTwoOpp  := pc_commitTwoOpp  + 1.U }

    when(branchOps.valid && !branchOps.passed) {
      when(coherentLoadInvalidReg) { pc_flushCoherent := pc_flushCoherent + 1.U }
      .otherwise                   { pc_flushBranch   := pc_flushBranch   + 1.U }
    }
    when(rob.commit.fired && rob.commit.instruction(6, 4) === "b110".U) {
      pc_retiredBranch := pc_retiredBranch + 1.U
    }

    when(decode.stallReason.prfExhausted)    { pc_dsPrfExhausted   := pc_dsPrfExhausted   + 1.U }
    when(decode.stallReason.branchMaskFull)  { pc_dsBranchMaskFull := pc_dsBranchMaskFull + 1.U }
    when(decode.stallReason.renameCollision) { pc_dsRenameCollide  := pc_dsRenameCollide  + 1.U }

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
      val robHeadNotReady = UInt(64.W)
      val robReadyBlocked = UInt(64.W)
      val hnrLoad         = UInt(64.W)
      val hnrBranch       = UInt(64.W)
      val hnrMext         = UInt(64.W)
      val hnrAmo          = UInt(64.W)
      val hnrOther        = UInt(64.W)
      val hnrLoadEpisodes = UInt(64.W)
      val hnrLoadGE2      = UInt(64.W)
      val rnrStoreGate    = UInt(64.W)
      val rnrWbGate       = UInt(64.W)
      val rnrLoadGate     = UInt(64.W)
      val issueReadyGE2   = UInt(64.W)
      val commitTwoOpp    = UInt(64.W)
      val dsPrfExhausted  = UInt(64.W)
      val dsBranchMaskFull= UInt(64.W)
      val dsRenameCollide = UInt(64.W)
      val flushBranch     = UInt(64.W)
      val flushCoherent   = UInt(64.W)
      val retiredBranch   = UInt(64.W)
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
    perfCnt.robHeadNotReady := pc_robHeadNotReady
    perfCnt.robReadyBlocked := pc_robReadyBlocked
    perfCnt.hnrLoad         := pc_hnrLoad
    perfCnt.hnrBranch       := pc_hnrBranch
    perfCnt.hnrMext         := pc_hnrMext
    perfCnt.hnrAmo          := pc_hnrAmo
    perfCnt.hnrOther        := pc_hnrOther
    perfCnt.hnrLoadEpisodes := pc_hnrLoadEpisodes
    perfCnt.hnrLoadGE2      := pc_hnrLoadGE2
    perfCnt.rnrStoreGate    := pc_rnrStoreGate
    perfCnt.rnrWbGate       := pc_rnrWbGate
    perfCnt.rnrLoadGate     := pc_rnrLoadGate
    perfCnt.issueReadyGE2   := pc_issueReadyGE2
    perfCnt.commitTwoOpp    := pc_commitTwoOpp
    perfCnt.dsPrfExhausted  := pc_dsPrfExhausted
    perfCnt.dsBranchMaskFull:= pc_dsBranchMaskFull
    perfCnt.dsRenameCollide := pc_dsRenameCollide
    perfCnt.flushBranch     := pc_flushBranch
    perfCnt.flushCoherent   := pc_flushCoherent
    perfCnt.retiredBranch   := pc_retiredBranch
  })

  val core3 = Module(new core(
    dPort_id = 6,
    peripheral_id = 3,
    iPort_id = 7,
    mhart_id = 3
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
    val pc_feFetchNotReady  = RegInit(0.U(64.W))
    val pc_feDecodeNotReady = RegInit(0.U(64.W))
    val pc_feExpectedBlock  = RegInit(0.U(64.W))
    val pc_robHeadNotReady  = RegInit(0.U(64.W))
    val pc_robReadyBlocked  = RegInit(0.U(64.W))
    val pc_hnrLoad   = RegInit(0.U(64.W))
    val pc_hnrBranch = RegInit(0.U(64.W))
    val pc_hnrMext   = RegInit(0.U(64.W))
    val pc_hnrAmo    = RegInit(0.U(64.W))
    val pc_hnrOther  = RegInit(0.U(64.W))
    // D-cache hit-latency probe. hnrLoad counts the *cycles* a head-of-ROB load
    // stalls, which cannot say what a shorter hit pipeline would buy. Count the
    // stall *episodes* too: one per load that stalls at the head at all, and
    // those that last >= 2 cycles. Trimming one stage off the hit path removes
    // at most `episodes` cycles, two stages at most `episodes + ge2`.
    val pc_hnrLoadEpisodes = RegInit(0.U(64.W))
    val pc_hnrLoadGE2      = RegInit(0.U(64.W))
    val pc_rnrStoreGate = RegInit(0.U(64.W))
    val pc_rnrWbGate    = RegInit(0.U(64.W))
    val pc_rnrLoadGate  = RegInit(0.U(64.W))
    val pc_issueReadyGE2 = RegInit(0.U(64.W))
    val pc_commitTwoOpp  = RegInit(0.U(64.W))
    // Why decode refused the next instruction — see decode.stallReason.
    val pc_dsPrfExhausted   = RegInit(0.U(64.W))
    val pc_dsBranchMaskFull = RegInit(0.U(64.W))
    val pc_dsRenameCollide  = RegInit(0.U(64.W))
    // What actually flushed the pipeline. branchTotal/branchesPassed lump the
    // coherency load-squash in with branch resolution (core.scala forces
    // branchEvals.valid high and .passed low for it), so "branch accuracy" is
    // not a predictor metric. Split the flushes and count real retired branches.
    val pc_flushBranch    = RegInit(0.U(64.W))
    val pc_flushCoherent  = RegInit(0.U(64.W))
    val pc_retiredBranch  = RegInit(0.U(64.W))

    pc_cycles := pc_cycles + 1.U
    when(rob.commit.fired) { pc_instRetired := pc_instRetired + 1.U }
    when(branchOps.valid) {
      pc_branchTotal := pc_branchTotal + 1.U
      when(branchOps.passed) { pc_branchesPassed := pc_branchesPassed + 1.U }
    }
    when(decode.toExec.ready) {
      pc_decodeReady := pc_decodeReady + 1.U
      when(decode.toExec.fired)       { pc_decodeFired := pc_decodeFired + 1.U }
      when(!scheduler.allocate.ready) { pc_schedStalls := pc_schedStalls + 1.U }
      when(!rob.allocate.ready)       { pc_robStalls   := pc_robStalls   + 1.U }
    }
    when(icache.fromFetch.req.valid && !icache.fromFetch.resp.valid) {
      pc_icacheStalls := pc_icacheStalls + 1.U
    }
    when(memoryRequest.valid) { pc_dcacheReqs := pc_dcacheReqs + 1.U }

    val fe_expMismatch = decode.fromFetch.expected.valid &&
                         (decode.fromFetch.expected.pc =/= fetch.toDecode.pc)
    when(!fetch.toDecode.ready) {
      pc_feFetchNotReady := pc_feFetchNotReady + 1.U
    }.elsewhen(!decode.fromFetch.ready) {
      pc_feDecodeNotReady := pc_feDecodeNotReady + 1.U
    }.elsewhen(fe_expMismatch) {
      pc_feExpectedBlock := pc_feExpectedBlock + 1.U
    }

    when(rob.headValid && !rob.commit.ready)    { pc_robHeadNotReady := pc_robHeadNotReady + 1.U }
    when(rob.commit.ready && !rob.commit.fired) { pc_robReadyBlocked := pc_robReadyBlocked + 1.U }

    val headOp       = rob.commit.instruction(6, 0)
    val headIsLoad   = headOp === "b0000011".U
    val headIsBranch = headOp === "b1100011".U || headOp === "b1101111".U || headOp === "b1100111".U
    val headIsMext   = (headOp === "b0110011".U || headOp === "b0111011".U) && rob.commit.instruction(25)
    val headIsAmo    = headOp === "b0101111".U
    when(rob.headValid && !rob.commit.ready) {
      when(headIsLoad)        { pc_hnrLoad   := pc_hnrLoad   + 1.U }
      .elsewhen(headIsBranch) { pc_hnrBranch := pc_hnrBranch + 1.U }
      .elsewhen(headIsMext)   { pc_hnrMext   := pc_hnrMext   + 1.U }
      .elsewhen(headIsAmo)    { pc_hnrAmo    := pc_hnrAmo    + 1.U }
      .otherwise              { pc_hnrOther  := pc_hnrOther  + 1.U }
    }

    // One episode per load that stalls at the head. Commit is 1-wide, so two
    // consecutive stalling loads are always separated by the cycle in which the
    // first one commits (commit.ready high) -- the condition drops and the
    // episodes stay distinct.
    val hnrLoadCond  = rob.headValid && !rob.commit.ready && headIsLoad
    val hnrLoadPrev  = RegNext(hnrLoadCond, false.B)
    val hnrLoadPrev2 = RegNext(hnrLoadPrev, false.B)
    when(hnrLoadCond && !hnrLoadPrev) {
      pc_hnrLoadEpisodes := pc_hnrLoadEpisodes + 1.U
    }
    when(hnrLoadCond && hnrLoadPrev && !hnrLoadPrev2) {
      pc_hnrLoadGE2 := pc_hnrLoadGE2 + 1.U
    }
    when(rob.commit.ready && !rob.commit.fired) {
      when((rob.commit.instruction(6, 4) === "b010".U) && !memAccess.writeInstructionCommit.ready) {
        pc_rnrStoreGate := pc_rnrStoreGate + 1.U
      }
      when(!decode.writeBackResult.ready) { pc_rnrWbGate := pc_rnrWbGate + 1.U }
      when((rob.commit.instruction(6, 2).orR === 0.U) && (coherentLoadInvalid || !memAccess.loadCommit.valid)) {
        pc_rnrLoadGate := pc_rnrLoadGate + 1.U
      }
    }
    when(scheduler.readyCount >= 2.U)          { pc_issueReadyGE2 := pc_issueReadyGE2 + 1.U }
    when(rob.commit.fired && rob.secondReady)   { pc_commitTwoOpp  := pc_commitTwoOpp  + 1.U }

    when(branchOps.valid && !branchOps.passed) {
      when(coherentLoadInvalidReg) { pc_flushCoherent := pc_flushCoherent + 1.U }
      .otherwise                   { pc_flushBranch   := pc_flushBranch   + 1.U }
    }
    when(rob.commit.fired && rob.commit.instruction(6, 4) === "b110".U) {
      pc_retiredBranch := pc_retiredBranch + 1.U
    }

    when(decode.stallReason.prfExhausted)    { pc_dsPrfExhausted   := pc_dsPrfExhausted   + 1.U }
    when(decode.stallReason.branchMaskFull)  { pc_dsBranchMaskFull := pc_dsBranchMaskFull + 1.U }
    when(decode.stallReason.renameCollision) { pc_dsRenameCollide  := pc_dsRenameCollide  + 1.U }

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
      val robHeadNotReady = UInt(64.W)
      val robReadyBlocked = UInt(64.W)
      val hnrLoad         = UInt(64.W)
      val hnrBranch       = UInt(64.W)
      val hnrMext         = UInt(64.W)
      val hnrAmo          = UInt(64.W)
      val hnrOther        = UInt(64.W)
      val hnrLoadEpisodes = UInt(64.W)
      val hnrLoadGE2      = UInt(64.W)
      val rnrStoreGate    = UInt(64.W)
      val rnrWbGate       = UInt(64.W)
      val rnrLoadGate     = UInt(64.W)
      val issueReadyGE2   = UInt(64.W)
      val commitTwoOpp    = UInt(64.W)
      val dsPrfExhausted  = UInt(64.W)
      val dsBranchMaskFull= UInt(64.W)
      val dsRenameCollide = UInt(64.W)
      val flushBranch     = UInt(64.W)
      val flushCoherent   = UInt(64.W)
      val retiredBranch   = UInt(64.W)
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
    perfCnt.robHeadNotReady := pc_robHeadNotReady
    perfCnt.robReadyBlocked := pc_robReadyBlocked
    perfCnt.hnrLoad         := pc_hnrLoad
    perfCnt.hnrBranch       := pc_hnrBranch
    perfCnt.hnrMext         := pc_hnrMext
    perfCnt.hnrAmo          := pc_hnrAmo
    perfCnt.hnrOther        := pc_hnrOther
    perfCnt.hnrLoadEpisodes := pc_hnrLoadEpisodes
    perfCnt.hnrLoadGE2      := pc_hnrLoadGE2
    perfCnt.rnrStoreGate    := pc_rnrStoreGate
    perfCnt.rnrWbGate       := pc_rnrWbGate
    perfCnt.rnrLoadGate     := pc_rnrLoadGate
    perfCnt.issueReadyGE2   := pc_issueReadyGE2
    perfCnt.commitTwoOpp    := pc_commitTwoOpp
    perfCnt.dsPrfExhausted  := pc_dsPrfExhausted
    perfCnt.dsBranchMaskFull:= pc_dsBranchMaskFull
    perfCnt.dsRenameCollide := pc_dsRenameCollide
    perfCnt.flushBranch     := pc_flushBranch
    perfCnt.flushCoherent   := pc_flushCoherent
    perfCnt.retiredBranch   := pc_retiredBranch
  })

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

  //core1.dPort to interconnect connection
  //AW
  interconnect.io.acePort2.AWVALID := core1.dPort.AWVALID
  core1.dPort.AWREADY := interconnect.io.acePort2.AWREADY
  interconnect.io.acePort2.AWID := core1.dPort.AWID
  interconnect.io.acePort2.AWADDR := core1.dPort.AWADDR
  interconnect.io.acePort2.AWSNOOP := core1.dPort.AWSNOOP
  interconnect.io.acePort2.AWBAR := core1.dPort.AWBAR

  //W
  interconnect.io.acePort2.WVALID := core1.dPort.WVALID
  interconnect.io.acePort2.WDATA := core1.dPort.WDATA
  interconnect.io.acePort2.WLAST := core1.dPort.WLAST
  core1.dPort.WREADY := interconnect.io.acePort2.WREADY

  //B
  core1.dPort.BVALID := interconnect.io.acePort2.BVALID
  core1.dPort.BID := interconnect.io.acePort2.BID
  core1.dPort.BRESP := interconnect.io.acePort2.BRESP
  interconnect.io.acePort2.BREADY := core1.dPort.BREADY

  //AR
  interconnect.io.acePort2.ARVALID := core1.dPort.ARVALID
  core1.dPort.ARREADY := interconnect.io.acePort2.ARREADY
  interconnect.io.acePort2.ARID := core1.dPort.ARID
  interconnect.io.acePort2.ARADDR := core1.dPort.ARADDR
  interconnect.io.acePort2.ARSNOOP := core1.dPort.ARSNOOP
  interconnect.io.acePort2.ARBAR := core1.dPort.ARBAR

  //R
  core1.dPort.RVALID := interconnect.io.acePort2.RVALID
  interconnect.io.acePort2.RREADY := core1.dPort.RREADY
  core1.dPort.RID := interconnect.io.acePort2.RID
  core1.dPort.RDATA := interconnect.io.acePort2.RDATA
  core1.dPort.RRESP := interconnect.io.acePort2.RRESP
  core1.dPort.RLAST := interconnect.io.acePort2.RLAST

  //AC
  core1.dPort.ACVALID := interconnect.io.acePort2.ACVALID
  core1.dPort.ACADDR := interconnect.io.acePort2.ACADDR
  core1.dPort.ACSNOOP := interconnect.io.acePort2.ACSNOOP
  core1.dPort.ACPROT := 2.U
  interconnect.io.acePort2.ACREADY := core1.dPort.ACREADY

  //CR
  interconnect.io.acePort2.CRVALID := core1.dPort.CRVALID
  interconnect.io.acePort2.CRRESP := core1.dPort.CRRESP
  core1.dPort.CRREADY := interconnect.io.acePort2.CRREADY

  //CD
  interconnect.io.acePort2.CDVALID := core1.dPort.CDVALID
  core1.dPort.CDREADY := interconnect.io.acePort2.CDREADY
  interconnect.io.acePort2.CDDATA := core1.dPort.CDDATA
  interconnect.io.acePort2.CDLAST := core1.dPort.CDLAST

  //core1.iPort to interconnect connection
  //AW
  interconnect.io.acePort3.AWVALID := core1.iPort.AWVALID
  core1.iPort.AWREADY := interconnect.io.acePort3.AWREADY
  interconnect.io.acePort3.AWID := core1.iPort.AWID
  interconnect.io.acePort3.AWADDR := core1.iPort.AWADDR
  interconnect.io.acePort3.AWSNOOP := core1.iPort.AWSNOOP
  interconnect.io.acePort3.AWBAR := core1.iPort.AWBAR

  //W
  interconnect.io.acePort3.WVALID := core1.iPort.WVALID
  interconnect.io.acePort3.WDATA := core1.iPort.WDATA
  interconnect.io.acePort3.WLAST := core1.iPort.WLAST
  core1.iPort.WREADY := interconnect.io.acePort3.WREADY

  //B
  core1.iPort.BVALID := interconnect.io.acePort3.BVALID
  core1.iPort.BID := interconnect.io.acePort3.BID
  core1.iPort.BRESP := interconnect.io.acePort3.BRESP
  interconnect.io.acePort3.BREADY := core1.iPort.BREADY

  //AR
  interconnect.io.acePort3.ARVALID := core1.iPort.ARVALID
  core1.iPort.ARREADY := interconnect.io.acePort3.ARREADY
  interconnect.io.acePort3.ARID := core1.iPort.ARID
  interconnect.io.acePort3.ARADDR := core1.iPort.ARADDR
  interconnect.io.acePort3.ARSNOOP := core1.iPort.ARSNOOP
  interconnect.io.acePort3.ARBAR := core1.iPort.ARBAR

  //R
  core1.iPort.RVALID := interconnect.io.acePort3.RVALID
  interconnect.io.acePort3.RREADY := core1.iPort.RREADY
  core1.iPort.RID := interconnect.io.acePort3.RID
  core1.iPort.RDATA := interconnect.io.acePort3.RDATA
  core1.iPort.RRESP := interconnect.io.acePort3.RRESP
  core1.iPort.RLAST := interconnect.io.acePort3.RLAST

  //AC
  core1.iPort.ACVALID := interconnect.io.acePort3.ACVALID
  core1.iPort.ACADDR := interconnect.io.acePort3.ACADDR
  core1.iPort.ACSNOOP := interconnect.io.acePort3.ACSNOOP
  core1.iPort.ACPROT := 2.U
  interconnect.io.acePort3.ACREADY := core1.iPort.ACREADY

  //CR
  interconnect.io.acePort3.CRVALID := core1.iPort.CRVALID
  interconnect.io.acePort3.CRRESP := core1.iPort.CRRESP
  core1.iPort.CRREADY := interconnect.io.acePort3.CRREADY

  //CD
  interconnect.io.acePort3.CDVALID := core1.iPort.CDVALID
  core1.iPort.CDREADY := interconnect.io.acePort3.CDREADY
  interconnect.io.acePort3.CDDATA := core1.iPort.CDDATA
  interconnect.io.acePort3.CDLAST := core1.iPort.CDLAST


  //core2.dPort to interconnect connection
  //AW
  interconnect.io.acePort4.AWVALID := core2.dPort.AWVALID
  core2.dPort.AWREADY := interconnect.io.acePort4.AWREADY
  interconnect.io.acePort4.AWID := core2.dPort.AWID
  interconnect.io.acePort4.AWADDR := core2.dPort.AWADDR
  interconnect.io.acePort4.AWSNOOP := core2.dPort.AWSNOOP
  interconnect.io.acePort4.AWBAR := core2.dPort.AWBAR

  //W
  interconnect.io.acePort4.WVALID := core2.dPort.WVALID
  interconnect.io.acePort4.WDATA := core2.dPort.WDATA
  interconnect.io.acePort4.WLAST := core2.dPort.WLAST
  core2.dPort.WREADY := interconnect.io.acePort4.WREADY

  //B
  core2.dPort.BVALID := interconnect.io.acePort4.BVALID
  core2.dPort.BID := interconnect.io.acePort4.BID
  core2.dPort.BRESP := interconnect.io.acePort4.BRESP
  interconnect.io.acePort4.BREADY := core2.dPort.BREADY

  //AR
  interconnect.io.acePort4.ARVALID := core2.dPort.ARVALID
  core2.dPort.ARREADY := interconnect.io.acePort4.ARREADY
  interconnect.io.acePort4.ARID := core2.dPort.ARID
  interconnect.io.acePort4.ARADDR := core2.dPort.ARADDR
  interconnect.io.acePort4.ARSNOOP := core2.dPort.ARSNOOP
  interconnect.io.acePort4.ARBAR := core2.dPort.ARBAR

  //R
  core2.dPort.RVALID := interconnect.io.acePort4.RVALID
  interconnect.io.acePort4.RREADY := core2.dPort.RREADY
  core2.dPort.RID := interconnect.io.acePort4.RID
  core2.dPort.RDATA := interconnect.io.acePort4.RDATA
  core2.dPort.RRESP := interconnect.io.acePort4.RRESP
  core2.dPort.RLAST := interconnect.io.acePort4.RLAST

  //AC
  core2.dPort.ACVALID := interconnect.io.acePort4.ACVALID
  core2.dPort.ACADDR := interconnect.io.acePort4.ACADDR
  core2.dPort.ACSNOOP := interconnect.io.acePort4.ACSNOOP
  core2.dPort.ACPROT := 2.U
  interconnect.io.acePort4.ACREADY := core2.dPort.ACREADY

  //CR
  interconnect.io.acePort4.CRVALID := core2.dPort.CRVALID
  interconnect.io.acePort4.CRRESP := core2.dPort.CRRESP
  core2.dPort.CRREADY := interconnect.io.acePort4.CRREADY

  //CD
  interconnect.io.acePort4.CDVALID := core2.dPort.CDVALID
  core2.dPort.CDREADY := interconnect.io.acePort4.CDREADY
  interconnect.io.acePort4.CDDATA := core2.dPort.CDDATA
  interconnect.io.acePort4.CDLAST := core2.dPort.CDLAST



  //core2.iPort to interconnect connection
  //AW
  interconnect.io.acePort5.AWVALID := core2.iPort.AWVALID
  core2.iPort.AWREADY := interconnect.io.acePort5.AWREADY
  interconnect.io.acePort5.AWID := core2.iPort.AWID
  interconnect.io.acePort5.AWADDR := core2.iPort.AWADDR
  interconnect.io.acePort5.AWSNOOP := core2.iPort.AWSNOOP
  interconnect.io.acePort5.AWBAR := core2.iPort.AWBAR

  //W
  interconnect.io.acePort5.WVALID := core2.iPort.WVALID
  interconnect.io.acePort5.WDATA := core2.iPort.WDATA
  interconnect.io.acePort5.WLAST := core2.iPort.WLAST
  core2.iPort.WREADY := interconnect.io.acePort5.WREADY

  //B
  core2.iPort.BVALID := interconnect.io.acePort5.BVALID
  core2.iPort.BID := interconnect.io.acePort5.BID
  core2.iPort.BRESP := interconnect.io.acePort5.BRESP
  interconnect.io.acePort5.BREADY := core2.iPort.BREADY

  //AR
  interconnect.io.acePort5.ARVALID := core2.iPort.ARVALID
  core2.iPort.ARREADY := interconnect.io.acePort5.ARREADY
  interconnect.io.acePort5.ARID := core2.iPort.ARID
  interconnect.io.acePort5.ARADDR := core2.iPort.ARADDR
  interconnect.io.acePort5.ARSNOOP := core2.iPort.ARSNOOP
  interconnect.io.acePort5.ARBAR := core2.iPort.ARBAR

  //R
  core2.iPort.RVALID := interconnect.io.acePort5.RVALID
  interconnect.io.acePort5.RREADY := core2.iPort.RREADY
  core2.iPort.RID := interconnect.io.acePort5.RID
  core2.iPort.RDATA := interconnect.io.acePort5.RDATA
  core2.iPort.RRESP := interconnect.io.acePort5.RRESP
  core2.iPort.RLAST := interconnect.io.acePort5.RLAST

  //AC
  core2.iPort.ACVALID := interconnect.io.acePort5.ACVALID
  core2.iPort.ACADDR := interconnect.io.acePort5.ACADDR
  core2.iPort.ACSNOOP := interconnect.io.acePort5.ACSNOOP
  core2.iPort.ACPROT := 2.U
  interconnect.io.acePort5.ACREADY := core2.iPort.ACREADY

  //CR
  interconnect.io.acePort5.CRVALID := core2.iPort.CRVALID
  interconnect.io.acePort5.CRRESP := core2.iPort.CRRESP
  core2.iPort.CRREADY := interconnect.io.acePort5.CRREADY

  //CD
  interconnect.io.acePort5.CDVALID := core2.iPort.CDVALID
  core2.iPort.CDREADY := interconnect.io.acePort5.CDREADY
  interconnect.io.acePort5.CDDATA := core2.iPort.CDDATA
  interconnect.io.acePort5.CDLAST := core2.iPort.CDLAST



  //core3.dPort to interconnect connection
  //AW
  interconnect.io.acePort6.AWVALID := core3.dPort.AWVALID
  core3.dPort.AWREADY := interconnect.io.acePort6.AWREADY
  interconnect.io.acePort6.AWID := core3.dPort.AWID
  interconnect.io.acePort6.AWADDR := core3.dPort.AWADDR
  interconnect.io.acePort6.AWSNOOP := core3.dPort.AWSNOOP
  interconnect.io.acePort6.AWBAR := core3.dPort.AWBAR

  //W
  interconnect.io.acePort6.WVALID := core3.dPort.WVALID
  interconnect.io.acePort6.WDATA := core3.dPort.WDATA
  interconnect.io.acePort6.WLAST := core3.dPort.WLAST
  core3.dPort.WREADY := interconnect.io.acePort6.WREADY

  //B
  core3.dPort.BVALID := interconnect.io.acePort6.BVALID
  core3.dPort.BID := interconnect.io.acePort6.BID
  core3.dPort.BRESP := interconnect.io.acePort6.BRESP
  interconnect.io.acePort6.BREADY := core3.dPort.BREADY

  //AR
  interconnect.io.acePort6.ARVALID := core3.dPort.ARVALID
  core3.dPort.ARREADY := interconnect.io.acePort6.ARREADY
  interconnect.io.acePort6.ARID := core3.dPort.ARID
  interconnect.io.acePort6.ARADDR := core3.dPort.ARADDR
  interconnect.io.acePort6.ARSNOOP := core3.dPort.ARSNOOP
  interconnect.io.acePort6.ARBAR := core3.dPort.ARBAR

  //R
  core3.dPort.RVALID := interconnect.io.acePort6.RVALID
  interconnect.io.acePort6.RREADY := core3.dPort.RREADY
  core3.dPort.RID := interconnect.io.acePort6.RID
  core3.dPort.RDATA := interconnect.io.acePort6.RDATA
  core3.dPort.RRESP := interconnect.io.acePort6.RRESP
  core3.dPort.RLAST := interconnect.io.acePort6.RLAST

  //AC
  core3.dPort.ACVALID := interconnect.io.acePort6.ACVALID
  core3.dPort.ACADDR := interconnect.io.acePort6.ACADDR
  core3.dPort.ACSNOOP := interconnect.io.acePort6.ACSNOOP
  core3.dPort.ACPROT := 2.U
  interconnect.io.acePort6.ACREADY := core3.dPort.ACREADY

  //CR
  interconnect.io.acePort6.CRVALID := core3.dPort.CRVALID
  interconnect.io.acePort6.CRRESP := core3.dPort.CRRESP
  core3.dPort.CRREADY := interconnect.io.acePort6.CRREADY

  //CD
  interconnect.io.acePort6.CDVALID := core3.dPort.CDVALID
  core3.dPort.CDREADY := interconnect.io.acePort6.CDREADY
  interconnect.io.acePort6.CDDATA := core3.dPort.CDDATA
  interconnect.io.acePort6.CDLAST := core3.dPort.CDLAST



  //core3.iPort to interconnect connection
  //AW
  interconnect.io.acePort7.AWVALID := core3.iPort.AWVALID
  core3.iPort.AWREADY := interconnect.io.acePort7.AWREADY
  interconnect.io.acePort7.AWID := core3.iPort.AWID
  interconnect.io.acePort7.AWADDR := core3.iPort.AWADDR
  interconnect.io.acePort7.AWSNOOP := core3.iPort.AWSNOOP
  interconnect.io.acePort7.AWBAR := core3.iPort.AWBAR

  //W
  interconnect.io.acePort7.WVALID := core3.iPort.WVALID
  interconnect.io.acePort7.WDATA := core3.iPort.WDATA
  interconnect.io.acePort7.WLAST := core3.iPort.WLAST
  core3.iPort.WREADY := interconnect.io.acePort7.WREADY

  //B
  core3.iPort.BVALID := interconnect.io.acePort7.BVALID
  core3.iPort.BID := interconnect.io.acePort7.BID
  core3.iPort.BRESP := interconnect.io.acePort7.BRESP
  interconnect.io.acePort7.BREADY := core3.iPort.BREADY

  //AR
  interconnect.io.acePort7.ARVALID := core3.iPort.ARVALID
  core3.iPort.ARREADY := interconnect.io.acePort7.ARREADY
  interconnect.io.acePort7.ARID := core3.iPort.ARID
  interconnect.io.acePort7.ARADDR := core3.iPort.ARADDR
  interconnect.io.acePort7.ARSNOOP := core3.iPort.ARSNOOP
  interconnect.io.acePort7.ARBAR := core3.iPort.ARBAR

  //R
  core3.iPort.RVALID := interconnect.io.acePort7.RVALID
  interconnect.io.acePort7.RREADY := core3.iPort.RREADY
  core3.iPort.RID := interconnect.io.acePort7.RID
  core3.iPort.RDATA := interconnect.io.acePort7.RDATA
  core3.iPort.RRESP := interconnect.io.acePort7.RRESP
  core3.iPort.RLAST := interconnect.io.acePort7.RLAST

  //AC
  core3.iPort.ACVALID := interconnect.io.acePort7.ACVALID
  core3.iPort.ACADDR := interconnect.io.acePort7.ACADDR
  core3.iPort.ACSNOOP := interconnect.io.acePort7.ACSNOOP
  core3.iPort.ACPROT := 2.U
  interconnect.io.acePort7.ACREADY := core3.iPort.ACREADY

  //CR
  interconnect.io.acePort7.CRVALID := core3.iPort.CRVALID
  interconnect.io.acePort7.CRRESP := core3.iPort.CRRESP
  core3.iPort.CRREADY := interconnect.io.acePort7.CRREADY

  //CD
  interconnect.io.acePort7.CDVALID := core3.iPort.CDVALID
  core3.iPort.CDREADY := interconnect.io.acePort7.CDREADY
  interconnect.io.acePort7.CDDATA := core3.iPort.CDDATA
  interconnect.io.acePort7.CDLAST := core3.iPort.CDLAST


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

  // ── Chip-level IO boundary ──────────────────────────────────────────────

  // LLC's memory-side AXI master pair. sim (system.scala) wires these into
  // mainMemory.clients(1); the FPGA top exposes them at the chip boundary
  // for Vivado to route to a real DDR3 controller (MIG). Explicit Flipped
  // type + field-by-field wiring (not `<>`) to match this codebase's existing
  // convention for these AXIlite1/AXIlite2 bundles (mem_read_axi/
  // mem_write_axi are themselves already Flipped inside l2_mem — bulk `<>`
  // through a second layer of the same flip confuses Chisel's direction
  // inference; explicit assignment sidesteps it entirely).
  val mem_read_axi  = IO(Flipped(new AXIlite1(idWidth = 3, addressWidth = 32, dataWidth = 256)))
  // LLC drives the AR* request + RREADY (its outputs) -> forward into our own
  // (identically-shaped, identically-directioned) chip-level output fields.
  mem_read_axi.ARADDR  := LLC.io.mem_read_axi.ARADDR
  mem_read_axi.ARID    := LLC.io.mem_read_axi.ARID
  mem_read_axi.ARVALID := LLC.io.mem_read_axi.ARVALID
  mem_read_axi.ARLEN   := LLC.io.mem_read_axi.ARLEN
  mem_read_axi.ARSIZE  := LLC.io.mem_read_axi.ARSIZE
  mem_read_axi.ARBURST := LLC.io.mem_read_axi.ARBURST
  mem_read_axi.ARLOCK  := LLC.io.mem_read_axi.ARLOCK
  mem_read_axi.ARCACHE := LLC.io.mem_read_axi.ARCACHE
  mem_read_axi.ARPROT  := LLC.io.mem_read_axi.ARPROT
  mem_read_axi.ARQOS   := LLC.io.mem_read_axi.ARQOS
  mem_read_axi.RREADY  := LLC.io.mem_read_axi.RREADY
  // Our own chip-level inputs (externally driven: ARREADY + R* data) -> down
  // into LLC's identically-directioned input fields.
  LLC.io.mem_read_axi.ARREADY := mem_read_axi.ARREADY
  LLC.io.mem_read_axi.RDATA   := mem_read_axi.RDATA
  LLC.io.mem_read_axi.RID     := mem_read_axi.RID
  LLC.io.mem_read_axi.RRESP   := mem_read_axi.RRESP
  LLC.io.mem_read_axi.RVALID  := mem_read_axi.RVALID
  LLC.io.mem_read_axi.RLAST   := mem_read_axi.RLAST

  val mem_write_axi = IO(Flipped(new AXIlite2(idWidth = 3, addressWidth = 32, dataWidth = 256)))
  // LLC drives AW*/W*/BREADY (its outputs) -> our own chip-level outputs.
  mem_write_axi.AWADDR  := LLC.io.mem_write_axi.AWADDR
  mem_write_axi.AWVALID := LLC.io.mem_write_axi.AWVALID
  mem_write_axi.AWLEN   := LLC.io.mem_write_axi.AWLEN
  mem_write_axi.AWCACHE := LLC.io.mem_write_axi.AWCACHE
  mem_write_axi.AWSIZE  := LLC.io.mem_write_axi.AWSIZE
  mem_write_axi.AWLOCK  := LLC.io.mem_write_axi.AWLOCK
  mem_write_axi.AWPROT  := LLC.io.mem_write_axi.AWPROT
  mem_write_axi.AWQOS   := LLC.io.mem_write_axi.AWQOS
  mem_write_axi.AWBURST := LLC.io.mem_write_axi.AWBURST
  mem_write_axi.AWID    := LLC.io.mem_write_axi.AWID
  mem_write_axi.WDATA   := LLC.io.mem_write_axi.WDATA
  mem_write_axi.WVALID  := LLC.io.mem_write_axi.WVALID
  mem_write_axi.WSTRB   := LLC.io.mem_write_axi.WSTRB
  mem_write_axi.WLAST   := LLC.io.mem_write_axi.WLAST
  mem_write_axi.BREADY  := LLC.io.mem_write_axi.BREADY
  // Our own chip-level inputs (externally driven: AWREADY/WREADY/B*) -> down
  // into LLC's identically-directioned input fields.
  LLC.io.mem_write_axi.AWREADY := mem_write_axi.AWREADY
  LLC.io.mem_write_axi.WREADY  := mem_write_axi.WREADY
  LLC.io.mem_write_axi.BVALID  := mem_write_axi.BVALID
  LLC.io.mem_write_axi.BRESP   := mem_write_axi.BRESP
  LLC.io.mem_write_axi.BID     := mem_write_axi.BID

  // One AXI master peripheral port per core (matches core.peripheral) — sim
  // wires these into MultiUart's client0-3; the FPGA top does the same.
  val uartClient0 = IO(new AXI)
  val uartClient1 = IO(new AXI)
  val uartClient2 = IO(new AXI)
  val uartClient3 = IO(new AXI)
  uartClient0 <> core0.peripheral
  uartClient1 <> core1.peripheral
  uartClient2 <> core2.peripheral
  uartClient3 <> core3.peripheral

  // Timer/software interrupts, driven by whichever CLINT/UART peripheral is
  // attached outside (MultiUart in both sim and FPGA today).
  val mtip0 = IO(Input(Bool()))
  val mtip1 = IO(Input(Bool()))
  val mtip2 = IO(Input(Bool()))
  val mtip3 = IO(Input(Bool()))
  core0.MTIP := mtip0
  core1.MTIP := mtip1
  core2.MTIP := mtip2
  core3.MTIP := mtip3

  val msip0 = IO(Input(Bool()))
  val msip1 = IO(Input(Bool()))
  val msip2 = IO(Input(Bool()))
  val msip3 = IO(Input(Bool()))
  core0.MSIP := msip0
  core1.MSIP := msip1
  core2.MSIP := msip2
  core3.MSIP := msip3

  // ── Per-core debug/profiling outputs (unchanged shape vs. old system.scala) ──
  val registersOut0 = IO(Output(core0.registersOut.cloneType))
  val registersOutBuffer0 = Reg(registersOut0.cloneType)
  registersOut0 := Mux(core0.robOut.commitFired && RegNext(core0.robOut.commitFired, false.B), core0.registersOut ,registersOutBuffer0)
  registersOut0(32) := core0.registersOut(32)

  val robOut0 = IO(Output(core0.robOut.cloneType))
  robOut0 := core0.robOut
  when(RegNext(core0.allRobFiresOut, false.B)) { registersOutBuffer0 := core0.registersOut }

  val registersOut1 = IO(Output(core1.registersOut.cloneType))
  val registersOutBuffer1 = Reg(registersOut1.cloneType)
  registersOut1 := Mux(core1.robOut.commitFired && RegNext(core1.robOut.commitFired, false.B), core1.registersOut ,registersOutBuffer1)
  registersOut1(32) := core1.registersOut(32)

  val robOut1 = IO(Output(core1.robOut.cloneType))
  robOut1 := core1.robOut
  when(RegNext(core1.allRobFiresOut, false.B)) { registersOutBuffer1 := core1.registersOut }

  val registersOut2 = IO(Output(core2.registersOut.cloneType))
  val registersOutBuffer2 = Reg(registersOut2.cloneType)
  registersOut2 := Mux(core2.robOut.commitFired && RegNext(core2.robOut.commitFired, false.B), core2.registersOut ,registersOutBuffer2)
  registersOut2(32) := core2.registersOut(32)

  val robOut2 = IO(Output(core2.robOut.cloneType))
  robOut2 := core2.robOut
  when(RegNext(core2.allRobFiresOut, false.B)) { registersOutBuffer2 := core2.registersOut }

  val registersOut3 = IO(Output(core3.registersOut.cloneType))
  val registersOutBuffer3 = Reg(registersOut3.cloneType)
  registersOut3 := Mux(core3.robOut.commitFired && RegNext(core3.robOut.commitFired, false.B), core3.registersOut ,registersOutBuffer3)
  registersOut3(32) := core3.registersOut(32)

  val robOut3 = IO(Output(core3.robOut.cloneType))
  robOut3 := core3.robOut
  when(RegNext(core3.allRobFiresOut, false.B)) { registersOutBuffer3 := core3.registersOut }

  // === Per-core system-level AXI counters ===
  // D-cache miss = read request accepted by interconnect (= cache miss going to L2)
  // Slots [10-17] in perfCountersOut mirror the single-core layout.
  // [15-17] L2->DRAM are shared across all cores (same hardware, same value).

  val pc0_dCacheMiss    = RegInit(0.U(64.W))
  val pc0_dCacheRdBeats = RegInit(0.U(64.W))
  val pc0_dCacheWrBeats = RegInit(0.U(64.W))
  val pc0_iCacheMiss    = RegInit(0.U(64.W))
  val pc0_iCacheRdBeats = RegInit(0.U(64.W))

  val pc1_dCacheMiss    = RegInit(0.U(64.W))
  val pc1_dCacheRdBeats = RegInit(0.U(64.W))
  val pc1_dCacheWrBeats = RegInit(0.U(64.W))
  val pc1_iCacheMiss    = RegInit(0.U(64.W))
  val pc1_iCacheRdBeats = RegInit(0.U(64.W))

  val pc2_dCacheMiss    = RegInit(0.U(64.W))
  val pc2_dCacheRdBeats = RegInit(0.U(64.W))
  val pc2_dCacheWrBeats = RegInit(0.U(64.W))
  val pc2_iCacheMiss    = RegInit(0.U(64.W))
  val pc2_iCacheRdBeats = RegInit(0.U(64.W))

  val pc3_dCacheMiss    = RegInit(0.U(64.W))
  val pc3_dCacheRdBeats = RegInit(0.U(64.W))
  val pc3_dCacheWrBeats = RegInit(0.U(64.W))
  val pc3_iCacheMiss    = RegInit(0.U(64.W))
  val pc3_iCacheRdBeats = RegInit(0.U(64.W))

  val pc_l2ToMemRdReqs  = RegInit(0.U(64.W))
  val pc_l2ToMemRdBeats = RegInit(0.U(64.W))
  val pc_l2ToMemWrBeats = RegInit(0.U(64.W))

  when(core0.dPort.ARVALID && interconnect.io.acePort0.ARREADY) { pc0_dCacheMiss    := pc0_dCacheMiss    + 1.U }
  when(interconnect.io.acePort0.RVALID && core0.dPort.RREADY)   { pc0_dCacheRdBeats := pc0_dCacheRdBeats + 1.U }
  when(core0.dPort.WVALID && interconnect.io.acePort0.WREADY)   { pc0_dCacheWrBeats := pc0_dCacheWrBeats + 1.U }
  when(core0.iPort.ARVALID && interconnect.io.acePort1.ARREADY) { pc0_iCacheMiss    := pc0_iCacheMiss    + 1.U }
  when(interconnect.io.acePort1.RVALID && core0.iPort.RREADY)   { pc0_iCacheRdBeats := pc0_iCacheRdBeats + 1.U }

  when(core1.dPort.ARVALID && interconnect.io.acePort2.ARREADY) { pc1_dCacheMiss    := pc1_dCacheMiss    + 1.U }
  when(interconnect.io.acePort2.RVALID && core1.dPort.RREADY)   { pc1_dCacheRdBeats := pc1_dCacheRdBeats + 1.U }
  when(core1.dPort.WVALID && interconnect.io.acePort2.WREADY)   { pc1_dCacheWrBeats := pc1_dCacheWrBeats + 1.U }
  when(core1.iPort.ARVALID && interconnect.io.acePort3.ARREADY) { pc1_iCacheMiss    := pc1_iCacheMiss    + 1.U }
  when(interconnect.io.acePort3.RVALID && core1.iPort.RREADY)   { pc1_iCacheRdBeats := pc1_iCacheRdBeats + 1.U }

  when(core2.dPort.ARVALID && interconnect.io.acePort4.ARREADY) { pc2_dCacheMiss    := pc2_dCacheMiss    + 1.U }
  when(interconnect.io.acePort4.RVALID && core2.dPort.RREADY)   { pc2_dCacheRdBeats := pc2_dCacheRdBeats + 1.U }
  when(core2.dPort.WVALID && interconnect.io.acePort4.WREADY)   { pc2_dCacheWrBeats := pc2_dCacheWrBeats + 1.U }
  when(core2.iPort.ARVALID && interconnect.io.acePort5.ARREADY) { pc2_iCacheMiss    := pc2_iCacheMiss    + 1.U }
  when(interconnect.io.acePort5.RVALID && core2.iPort.RREADY)   { pc2_iCacheRdBeats := pc2_iCacheRdBeats + 1.U }

  when(core3.dPort.ARVALID && interconnect.io.acePort6.ARREADY) { pc3_dCacheMiss    := pc3_dCacheMiss    + 1.U }
  when(interconnect.io.acePort6.RVALID && core3.dPort.RREADY)   { pc3_dCacheRdBeats := pc3_dCacheRdBeats + 1.U }
  when(core3.dPort.WVALID && interconnect.io.acePort6.WREADY)   { pc3_dCacheWrBeats := pc3_dCacheWrBeats + 1.U }
  when(core3.iPort.ARVALID && interconnect.io.acePort7.ARREADY) { pc3_iCacheMiss    := pc3_iCacheMiss    + 1.U }
  when(interconnect.io.acePort7.RVALID && core3.iPort.RREADY)   { pc3_iCacheRdBeats := pc3_iCacheRdBeats + 1.U }

  // NOTE: uses this module's own mem_read_axi/mem_write_axi boundary ports
  // (not a concrete memory module) so this counting logic works identically
  // whether the downstream is sim's mainMemory or a real MIG on FPGA.
  when(mem_read_axi.ARVALID && mem_read_axi.ARREADY)  { pc_l2ToMemRdReqs  := pc_l2ToMemRdReqs  + 1.U }
  when(mem_read_axi.RVALID && mem_read_axi.RREADY)    { pc_l2ToMemRdBeats := pc_l2ToMemRdBeats + 1.U }
  when(mem_write_axi.WVALID && mem_write_axi.WREADY)  { pc_l2ToMemWrBeats := pc_l2ToMemWrBeats + 1.U }

  // Flat Vec(41) per core for C++ / Verilator access. Index layout mirrors single-core profiler.h:
  //  [0-9]   core   [10-17] AXI   [18-20] fe-bubbles
  //  [21-28] 0      [29-30] rob-head  [31-35] head-class
  //  [36-38] rnr    [39-40] 2-wide

  val perfCountersOut0 = IO(Output(Vec(41, UInt(64.W))))
  perfCountersOut0(0)  := core0.perfCnt.cycles
  perfCountersOut0(1)  := core0.perfCnt.instRetired
  perfCountersOut0(2)  := core0.perfCnt.branchTotal
  perfCountersOut0(3)  := core0.perfCnt.branchesPassed
  perfCountersOut0(4)  := core0.perfCnt.schedulerStalls
  perfCountersOut0(5)  := core0.perfCnt.robStalls
  perfCountersOut0(6)  := core0.perfCnt.decodeReady
  perfCountersOut0(7)  := core0.perfCnt.decodeFired
  perfCountersOut0(8)  := core0.perfCnt.icacheStalls
  perfCountersOut0(9)  := core0.perfCnt.dcacheReqs
  perfCountersOut0(10) := pc0_dCacheMiss
  perfCountersOut0(11) := pc0_dCacheRdBeats
  perfCountersOut0(12) := pc0_dCacheWrBeats
  perfCountersOut0(13) := pc0_iCacheMiss
  perfCountersOut0(14) := pc0_iCacheRdBeats
  perfCountersOut0(15) := pc_l2ToMemRdReqs
  perfCountersOut0(16) := pc_l2ToMemRdBeats
  perfCountersOut0(17) := pc_l2ToMemWrBeats
  perfCountersOut0(18) := core0.perfCnt.feFetchNotReady
  perfCountersOut0(19) := core0.perfCnt.feDecodeNotReady
  perfCountersOut0(20) := core0.perfCnt.feExpectedBlock
  perfCountersOut0(21) := core0.perfCnt.dsPrfExhausted
  perfCountersOut0(22) := core0.perfCnt.dsBranchMaskFull
  perfCountersOut0(23) := core0.perfCnt.dsRenameCollide
  perfCountersOut0(24) := core0.perfCnt.flushBranch
  perfCountersOut0(25) := core0.perfCnt.flushCoherent
  perfCountersOut0(26) := core0.perfCnt.retiredBranch
  perfCountersOut0(27) := core0.perfCnt.hnrLoadEpisodes
  perfCountersOut0(28) := core0.perfCnt.hnrLoadGE2
  perfCountersOut0(29) := core0.perfCnt.robHeadNotReady
  perfCountersOut0(30) := core0.perfCnt.robReadyBlocked
  perfCountersOut0(31) := core0.perfCnt.hnrLoad
  perfCountersOut0(32) := core0.perfCnt.hnrBranch
  perfCountersOut0(33) := core0.perfCnt.hnrMext
  perfCountersOut0(34) := core0.perfCnt.hnrAmo
  perfCountersOut0(35) := core0.perfCnt.hnrOther
  perfCountersOut0(36) := core0.perfCnt.rnrStoreGate
  perfCountersOut0(37) := core0.perfCnt.rnrWbGate
  perfCountersOut0(38) := core0.perfCnt.rnrLoadGate
  perfCountersOut0(39) := core0.perfCnt.issueReadyGE2
  perfCountersOut0(40) := core0.perfCnt.commitTwoOpp

  val perfCountersOut1 = IO(Output(Vec(41, UInt(64.W))))
  perfCountersOut1(0)  := core1.perfCnt.cycles
  perfCountersOut1(1)  := core1.perfCnt.instRetired
  perfCountersOut1(2)  := core1.perfCnt.branchTotal
  perfCountersOut1(3)  := core1.perfCnt.branchesPassed
  perfCountersOut1(4)  := core1.perfCnt.schedulerStalls
  perfCountersOut1(5)  := core1.perfCnt.robStalls
  perfCountersOut1(6)  := core1.perfCnt.decodeReady
  perfCountersOut1(7)  := core1.perfCnt.decodeFired
  perfCountersOut1(8)  := core1.perfCnt.icacheStalls
  perfCountersOut1(9)  := core1.perfCnt.dcacheReqs
  perfCountersOut1(10) := pc1_dCacheMiss
  perfCountersOut1(11) := pc1_dCacheRdBeats
  perfCountersOut1(12) := pc1_dCacheWrBeats
  perfCountersOut1(13) := pc1_iCacheMiss
  perfCountersOut1(14) := pc1_iCacheRdBeats
  perfCountersOut1(15) := pc_l2ToMemRdReqs
  perfCountersOut1(16) := pc_l2ToMemRdBeats
  perfCountersOut1(17) := pc_l2ToMemWrBeats
  perfCountersOut1(18) := core1.perfCnt.feFetchNotReady
  perfCountersOut1(19) := core1.perfCnt.feDecodeNotReady
  perfCountersOut1(20) := core1.perfCnt.feExpectedBlock
  perfCountersOut1(21) := core1.perfCnt.dsPrfExhausted
  perfCountersOut1(22) := core1.perfCnt.dsBranchMaskFull
  perfCountersOut1(23) := core1.perfCnt.dsRenameCollide
  perfCountersOut1(24) := core1.perfCnt.flushBranch
  perfCountersOut1(25) := core1.perfCnt.flushCoherent
  perfCountersOut1(26) := core1.perfCnt.retiredBranch
  perfCountersOut1(27) := core1.perfCnt.hnrLoadEpisodes
  perfCountersOut1(28) := core1.perfCnt.hnrLoadGE2
  perfCountersOut1(29) := core1.perfCnt.robHeadNotReady
  perfCountersOut1(30) := core1.perfCnt.robReadyBlocked
  perfCountersOut1(31) := core1.perfCnt.hnrLoad
  perfCountersOut1(32) := core1.perfCnt.hnrBranch
  perfCountersOut1(33) := core1.perfCnt.hnrMext
  perfCountersOut1(34) := core1.perfCnt.hnrAmo
  perfCountersOut1(35) := core1.perfCnt.hnrOther
  perfCountersOut1(36) := core1.perfCnt.rnrStoreGate
  perfCountersOut1(37) := core1.perfCnt.rnrWbGate
  perfCountersOut1(38) := core1.perfCnt.rnrLoadGate
  perfCountersOut1(39) := core1.perfCnt.issueReadyGE2
  perfCountersOut1(40) := core1.perfCnt.commitTwoOpp

  val perfCountersOut2 = IO(Output(Vec(41, UInt(64.W))))
  perfCountersOut2(0)  := core2.perfCnt.cycles
  perfCountersOut2(1)  := core2.perfCnt.instRetired
  perfCountersOut2(2)  := core2.perfCnt.branchTotal
  perfCountersOut2(3)  := core2.perfCnt.branchesPassed
  perfCountersOut2(4)  := core2.perfCnt.schedulerStalls
  perfCountersOut2(5)  := core2.perfCnt.robStalls
  perfCountersOut2(6)  := core2.perfCnt.decodeReady
  perfCountersOut2(7)  := core2.perfCnt.decodeFired
  perfCountersOut2(8)  := core2.perfCnt.icacheStalls
  perfCountersOut2(9)  := core2.perfCnt.dcacheReqs
  perfCountersOut2(10) := pc2_dCacheMiss
  perfCountersOut2(11) := pc2_dCacheRdBeats
  perfCountersOut2(12) := pc2_dCacheWrBeats
  perfCountersOut2(13) := pc2_iCacheMiss
  perfCountersOut2(14) := pc2_iCacheRdBeats
  perfCountersOut2(15) := pc_l2ToMemRdReqs
  perfCountersOut2(16) := pc_l2ToMemRdBeats
  perfCountersOut2(17) := pc_l2ToMemWrBeats
  perfCountersOut2(18) := core2.perfCnt.feFetchNotReady
  perfCountersOut2(19) := core2.perfCnt.feDecodeNotReady
  perfCountersOut2(20) := core2.perfCnt.feExpectedBlock
  perfCountersOut2(21) := core2.perfCnt.dsPrfExhausted
  perfCountersOut2(22) := core2.perfCnt.dsBranchMaskFull
  perfCountersOut2(23) := core2.perfCnt.dsRenameCollide
  perfCountersOut2(24) := core2.perfCnt.flushBranch
  perfCountersOut2(25) := core2.perfCnt.flushCoherent
  perfCountersOut2(26) := core2.perfCnt.retiredBranch
  perfCountersOut2(27) := core2.perfCnt.hnrLoadEpisodes
  perfCountersOut2(28) := core2.perfCnt.hnrLoadGE2
  perfCountersOut2(29) := core2.perfCnt.robHeadNotReady
  perfCountersOut2(30) := core2.perfCnt.robReadyBlocked
  perfCountersOut2(31) := core2.perfCnt.hnrLoad
  perfCountersOut2(32) := core2.perfCnt.hnrBranch
  perfCountersOut2(33) := core2.perfCnt.hnrMext
  perfCountersOut2(34) := core2.perfCnt.hnrAmo
  perfCountersOut2(35) := core2.perfCnt.hnrOther
  perfCountersOut2(36) := core2.perfCnt.rnrStoreGate
  perfCountersOut2(37) := core2.perfCnt.rnrWbGate
  perfCountersOut2(38) := core2.perfCnt.rnrLoadGate
  perfCountersOut2(39) := core2.perfCnt.issueReadyGE2
  perfCountersOut2(40) := core2.perfCnt.commitTwoOpp

  val perfCountersOut3 = IO(Output(Vec(41, UInt(64.W))))
  perfCountersOut3(0)  := core3.perfCnt.cycles
  perfCountersOut3(1)  := core3.perfCnt.instRetired
  perfCountersOut3(2)  := core3.perfCnt.branchTotal
  perfCountersOut3(3)  := core3.perfCnt.branchesPassed
  perfCountersOut3(4)  := core3.perfCnt.schedulerStalls
  perfCountersOut3(5)  := core3.perfCnt.robStalls
  perfCountersOut3(6)  := core3.perfCnt.decodeReady
  perfCountersOut3(7)  := core3.perfCnt.decodeFired
  perfCountersOut3(8)  := core3.perfCnt.icacheStalls
  perfCountersOut3(9)  := core3.perfCnt.dcacheReqs
  perfCountersOut3(10) := pc3_dCacheMiss
  perfCountersOut3(11) := pc3_dCacheRdBeats
  perfCountersOut3(12) := pc3_dCacheWrBeats
  perfCountersOut3(13) := pc3_iCacheMiss
  perfCountersOut3(14) := pc3_iCacheRdBeats
  perfCountersOut3(15) := pc_l2ToMemRdReqs
  perfCountersOut3(16) := pc_l2ToMemRdBeats
  perfCountersOut3(17) := pc_l2ToMemWrBeats
  perfCountersOut3(18) := core3.perfCnt.feFetchNotReady
  perfCountersOut3(19) := core3.perfCnt.feDecodeNotReady
  perfCountersOut3(20) := core3.perfCnt.feExpectedBlock
  perfCountersOut3(21) := core3.perfCnt.dsPrfExhausted
  perfCountersOut3(22) := core3.perfCnt.dsBranchMaskFull
  perfCountersOut3(23) := core3.perfCnt.dsRenameCollide
  perfCountersOut3(24) := core3.perfCnt.flushBranch
  perfCountersOut3(25) := core3.perfCnt.flushCoherent
  perfCountersOut3(26) := core3.perfCnt.retiredBranch
  perfCountersOut3(27) := core3.perfCnt.hnrLoadEpisodes
  perfCountersOut3(28) := core3.perfCnt.hnrLoadGE2
  perfCountersOut3(29) := core3.perfCnt.robHeadNotReady
  perfCountersOut3(30) := core3.perfCnt.robReadyBlocked
  perfCountersOut3(31) := core3.perfCnt.hnrLoad
  perfCountersOut3(32) := core3.perfCnt.hnrBranch
  perfCountersOut3(33) := core3.perfCnt.hnrMext
  perfCountersOut3(34) := core3.perfCnt.hnrAmo
  perfCountersOut3(35) := core3.perfCnt.hnrOther
  perfCountersOut3(36) := core3.perfCnt.rnrStoreGate
  perfCountersOut3(37) := core3.perfCnt.rnrWbGate
  perfCountersOut3(38) := core3.perfCnt.rnrLoadGate
  perfCountersOut3(39) := core3.perfCnt.issueReadyGE2
  perfCountersOut3(40) := core3.perfCnt.commitTwoOpp
}
