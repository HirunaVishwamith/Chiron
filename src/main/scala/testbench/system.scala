//soc simulation implementation


import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import chisel3.experimental.IO

import pipeline.ports._
import common.coreConfiguration._
import Icache.AXI
import _root_.testbench.mainMemory
import _root_.testbench.MultiUart
import Decode.constants
import _root_.testbench.simulatedMemory
import Interconnect._
import L2_cache._

// Shared perfCnt counter logic injected into each core's anonymous subclass.
// Slots [21-28] are always 0 (no lineStreamer in quad-core fetch).
// Vec(41) index map mirrors the single-core profiler.h layout:
//  [0-9]   core counters   [10-17] system AXI   [18-20] frontend bubbles
//  [21-28] streamer (0)    [29-30] ROB-head split
//  [31-35] head-class      [36-38] rnr-gate      [39-40] 2-wide opp

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
    val pc_rnrStoreGate = RegInit(0.U(64.W))
    val pc_rnrWbGate    = RegInit(0.U(64.W))
    val pc_rnrLoadGate  = RegInit(0.U(64.W))
    val pc_issueReadyGE2 = RegInit(0.U(64.W))
    val pc_commitTwoOpp  = RegInit(0.U(64.W))

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
      val rnrStoreGate    = UInt(64.W)
      val rnrWbGate       = UInt(64.W)
      val rnrLoadGate     = UInt(64.W)
      val issueReadyGE2   = UInt(64.W)
      val commitTwoOpp    = UInt(64.W)
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
    perfCnt.rnrStoreGate    := pc_rnrStoreGate
    perfCnt.rnrWbGate       := pc_rnrWbGate
    perfCnt.rnrLoadGate     := pc_rnrLoadGate
    perfCnt.issueReadyGE2   := pc_issueReadyGE2
    perfCnt.commitTwoOpp    := pc_commitTwoOpp
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
    val pc_rnrStoreGate = RegInit(0.U(64.W))
    val pc_rnrWbGate    = RegInit(0.U(64.W))
    val pc_rnrLoadGate  = RegInit(0.U(64.W))
    val pc_issueReadyGE2 = RegInit(0.U(64.W))
    val pc_commitTwoOpp  = RegInit(0.U(64.W))

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
      val rnrStoreGate    = UInt(64.W)
      val rnrWbGate       = UInt(64.W)
      val rnrLoadGate     = UInt(64.W)
      val issueReadyGE2   = UInt(64.W)
      val commitTwoOpp    = UInt(64.W)
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
    perfCnt.rnrStoreGate    := pc_rnrStoreGate
    perfCnt.rnrWbGate       := pc_rnrWbGate
    perfCnt.rnrLoadGate     := pc_rnrLoadGate
    perfCnt.issueReadyGE2   := pc_issueReadyGE2
    perfCnt.commitTwoOpp    := pc_commitTwoOpp
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
    val pc_rnrStoreGate = RegInit(0.U(64.W))
    val pc_rnrWbGate    = RegInit(0.U(64.W))
    val pc_rnrLoadGate  = RegInit(0.U(64.W))
    val pc_issueReadyGE2 = RegInit(0.U(64.W))
    val pc_commitTwoOpp  = RegInit(0.U(64.W))

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
      val rnrStoreGate    = UInt(64.W)
      val rnrWbGate       = UInt(64.W)
      val rnrLoadGate     = UInt(64.W)
      val issueReadyGE2   = UInt(64.W)
      val commitTwoOpp    = UInt(64.W)
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
    perfCnt.rnrStoreGate    := pc_rnrStoreGate
    perfCnt.rnrWbGate       := pc_rnrWbGate
    perfCnt.rnrLoadGate     := pc_rnrLoadGate
    perfCnt.issueReadyGE2   := pc_issueReadyGE2
    perfCnt.commitTwoOpp    := pc_commitTwoOpp
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
    val pc_rnrStoreGate = RegInit(0.U(64.W))
    val pc_rnrWbGate    = RegInit(0.U(64.W))
    val pc_rnrLoadGate  = RegInit(0.U(64.W))
    val pc_issueReadyGE2 = RegInit(0.U(64.W))
    val pc_commitTwoOpp  = RegInit(0.U(64.W))

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
      val rnrStoreGate    = UInt(64.W)
      val rnrWbGate       = UInt(64.W)
      val rnrLoadGate     = UInt(64.W)
      val issueReadyGE2   = UInt(64.W)
      val commitTwoOpp    = UInt(64.W)
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
    perfCnt.rnrStoreGate    := pc_rnrStoreGate
    perfCnt.rnrWbGate       := pc_rnrWbGate
    perfCnt.rnrLoadGate     := pc_rnrLoadGate
    perfCnt.issueReadyGE2   := pc_issueReadyGE2
    perfCnt.commitTwoOpp    := pc_commitTwoOpp
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

  //mainMemory Prober
  val prober = IO(memory.externalProbe.cloneType)
  prober <> memory.externalProbe

  //Peripherals & MTIPs

  val peripherals = Module(new MultiUart())

  val core0OutChar = IO(Output(peripherals.putChar0.cloneType))
  val core1OutChar = IO(Output(peripherals.putChar1.cloneType))
  val core2OutChar = IO(Output(peripherals.putChar2.cloneType))
  val core3OutChar = IO(Output(peripherals.putChar3.cloneType))

  core0OutChar := peripherals.putChar0
  core1OutChar := peripherals.putChar1
  core2OutChar := peripherals.putChar2
  core3OutChar := peripherals.putChar3


  core0.peripheral <> peripherals.client0
  core1.peripheral <> peripherals.client1
  core2.peripheral <> peripherals.client2
  core3.peripheral <> peripherals.client3

  core0.MTIP := peripherals.MTIP0
  core1.MTIP := peripherals.MTIP1
  core2.MTIP := peripherals.MTIP2
  core3.MTIP := peripherals.MTIP3



  //core0
  val registersOut0 = IO(Output(core0.registersOut.cloneType))
  val registersOutBuffer0 = Reg(registersOut0.cloneType)
  registersOut0 := Mux(core0.robOut.commitFired && RegNext(core0.robOut.commitFired, false.B), core0.registersOut ,registersOutBuffer0)
  registersOut0(32) := core0.registersOut(32)

  val robOut0 = IO(Output(core0.robOut.cloneType))
  robOut0 := core0.robOut
  when(RegNext(core0.allRobFiresOut, false.B)) { registersOutBuffer0 := core0.registersOut }



  //core1
  val registersOut1 = IO(Output(core1.registersOut.cloneType))
  val registersOutBuffer1 = Reg(registersOut1.cloneType)
  registersOut1 := Mux(core1.robOut.commitFired && RegNext(core1.robOut.commitFired, false.B), core1.registersOut ,registersOutBuffer1)
  registersOut1(32) := core1.registersOut(32)

  val robOut1 = IO(Output(core1.robOut.cloneType))
  robOut1 := core1.robOut
  when(RegNext(core1.allRobFiresOut, false.B)) { registersOutBuffer1 := core1.registersOut }

  //core2
  val registersOut2 = IO(Output(core2.registersOut.cloneType))
  val registersOutBuffer2 = Reg(registersOut2.cloneType)
  registersOut2 := Mux(core2.robOut.commitFired && RegNext(core2.robOut.commitFired, false.B), core2.registersOut ,registersOutBuffer2)
  registersOut2(32) := core2.registersOut(32)

  val robOut2 = IO(Output(core2.robOut.cloneType))
  robOut2 := core2.robOut
  when(RegNext(core2.allRobFiresOut, false.B)) { registersOutBuffer2 := core2.registersOut }

  //core3
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

  when(LLC.io.mem_read_axi.ARVALID && memory.clients(1).ARREADY) { pc_l2ToMemRdReqs  := pc_l2ToMemRdReqs  + 1.U }
  when(memory.clients(1).RVALID && LLC.io.mem_read_axi.RREADY)   { pc_l2ToMemRdBeats := pc_l2ToMemRdBeats + 1.U }
  when(LLC.io.mem_write_axi.WVALID && memory.clients(1).WREADY)  { pc_l2ToMemWrBeats := pc_l2ToMemWrBeats + 1.U }

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
  perfCountersOut0(21) := 0.U
  perfCountersOut0(22) := 0.U
  perfCountersOut0(23) := 0.U
  perfCountersOut0(24) := 0.U
  perfCountersOut0(25) := 0.U
  perfCountersOut0(26) := 0.U
  perfCountersOut0(27) := 0.U
  perfCountersOut0(28) := 0.U
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
  perfCountersOut1(21) := 0.U
  perfCountersOut1(22) := 0.U
  perfCountersOut1(23) := 0.U
  perfCountersOut1(24) := 0.U
  perfCountersOut1(25) := 0.U
  perfCountersOut1(26) := 0.U
  perfCountersOut1(27) := 0.U
  perfCountersOut1(28) := 0.U
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
  perfCountersOut2(21) := 0.U
  perfCountersOut2(22) := 0.U
  perfCountersOut2(23) := 0.U
  perfCountersOut2(24) := 0.U
  perfCountersOut2(25) := 0.U
  perfCountersOut2(26) := 0.U
  perfCountersOut2(27) := 0.U
  perfCountersOut2(28) := 0.U
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
  perfCountersOut3(21) := 0.U
  perfCountersOut3(22) := 0.U
  perfCountersOut3(23) := 0.U
  perfCountersOut3(24) := 0.U
  perfCountersOut3(25) := 0.U
  perfCountersOut3(26) := 0.U
  perfCountersOut3(27) := 0.U
  perfCountersOut3(28) := 0.U
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

object system extends App {
  emitVerilog(new system)
}
