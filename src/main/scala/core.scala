import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._

import cache.iCache
import decode._
import pipeline.rob._
import scheduler._
import common._
import cache.AXI
import storeDataIssue._
import cache.pipelineMemoryRequest
import cache_phase3.constants._
import cache_phase3._


class core (
  peripheral_id: Int,
  dPort_id: Int,
  iPort_id: Int,
  mhart_id: Int
)extends Module {
  val icache = Module(new cache.iCache(iPort_id = iPort_id))

  val iPort = IO(new ACE(busWidth = 64))
  iPort <> icache.lowLevelMem

  // Fence functionality is ignored for now
  icache.updateAllCachelines.fired := false.B
  icache.cachelinesUpdatesResp.fired := false.B

  val fetch = Module(new fetch(8))
  // F2: block fetch. The line streamer turns the iCache's per-line responses
  // into the per-word stream the fetch unit expects, amortising one I$ access
  // over a whole cache line. Fetch is otherwise unchanged.
  val streamer = Module(new lineStreamer(common.coreConfiguration.iCacheOffsetWidth))

  // fetch <-> streamer (per-word interface)
  streamer.fromFetch.req <> fetch.cache.req
  fetch.cache.resp <> streamer.fromFetch.resp

  // streamer <-> icache (per-line interface)
  icache.fromFetch.req <> streamer.toCache.req
  icache.fromFetch.req.bits := Cat(0.U(32.W), streamer.toCache.req.bits(31, 0))
  streamer.toCache.resp.valid     := icache.fromFetch.resp.valid
  streamer.toCache.resp.bits.line := icache.fromFetch.resp.bits.line
  streamer.toCache.resp.bits.base := icache.fromFetch.resp.bits.base
  icache.fromFetch.resp.ready     := streamer.toCache.resp.ready

  // Fence functionality is ignored for now
  fetch.updateAllCachelines.fired := false.B
  fetch.carryOutFence.fired := false.B
  
  val decode = Module(new decode (
    mhart_id = mhart_id
  ) {
    val registersOut = IO(Output(Vec(33, UInt(64.W))))
    registersOut.foreach(_ := 0.U)
    registersOut.head := mstatus
  })
  Seq(fetch.toDecode.fired, decode.fromFetch.fired).foreach(
    _ := decode.fromFetch.ready && fetch.toDecode.ready && (
      !decode.fromFetch.expected.valid || (decode.fromFetch.expected.pc === fetch.toDecode.pc)
    )
  )
  fetch.toDecode.expected := decode.fromFetch.expected
  decode.fromFetch.instruction := fetch.toDecode.instruction
  decode.fromFetch.pc := fetch.toDecode.pc

  val dataQueue = Module(new storeDataIssue)
  val rob = Module(new rob(common.configuration.robAddrWidth, 4))
  val scheduler = Module(new scheduler)
  val memAccess = Module(new CacheModule(
    dPort_id = dPort_id,
    peripheral_id = peripheral_id
  ){
    val decrCounter = IO(Output(Bool()))
    decrCounter := false.B//dequeueData

    val writeToMemoryPending = RegInit(0.U(32.W))
    writeToMemoryPending := 0.U//writeToMemoryPending + writeHandler.writeCommit.fired.asUInt - writeHandler.itWasPeripheral.asUInt - writeHandler.request.valid.asUInt
    val writeToMemoryPendingOut = IO(Output(writeToMemoryPending.cloneType))
    writeToMemoryPendingOut := 0.U//writeToMemoryPending
  })

  val branchOps = Wire(new Bundle {
    val valid = Bool()
    val branchMask = UInt(configuration.newBranchMaskWidth.W) //leon coherency
    val passed = Bool()
  })

  val wakeUps = Wire(Vec(3 ,new Bundle {
    val valid = Bool()
    val prfAddr = UInt(configuration.prfAddrWidth.W)
  }))

  // lui & auipc are excuted at decode
  val instructionDecodedReady = Seq("b00011", "b00101", "b01101").map(s => s.U === decode.toExec.instruction(6,2)).reduce(_ || _) // decode.toExec.instruction(4,2) === "b101".U
  Seq(decode.toExec.fired, rob.allocate.fired, scheduler.allocate.fired).foreach {
    _ := (decode.toExec.ready && rob.allocate.ready && scheduler.allocate.ready && ((decode.toExec.instruction(6, 4) =/= "b010".U) || dataQueue.fromDecode.ready)) 
  }
  decode.toExec.robAddr := rob.allocate.robAddr
  rob.allocate.pc := decode.toExec.pc
  rob.allocate.instruction := decode.toExec.instruction
  rob.allocate.prfDest := decode.toExec.PRFDest
  rob.allocate.isReady := instructionDecodedReady
  scheduler.allocate.instruction := decode.toExec.instruction
  scheduler.allocate.branchMask := decode.toExec.branchMask
  scheduler.allocate.rs1.ready := decode.toExec.rs1Ready
  scheduler.allocate.rs1.prfAddr := decode.toExec.rs1Addr
  scheduler.allocate.rs2.ready := Mux((decode.toExec.instruction(6, 0) === BitPat("b001?011")), true.B, decode.toExec.rs2Ready)
  scheduler.allocate.rs2.prfAddr := decode.toExec.rs2Addr
  scheduler.allocate.prfDest := decode.toExec.PRFDest
  scheduler.allocate.robAddr := rob.allocate.robAddr
  dataQueue.fromDecode.branchMask := decode.toExec.branchMask
  dataQueue.fromDecode.instruction := decode.toExec.instruction
  dataQueue.fromDecode.rs2Addr := decode.toExec.rs2Addr
  dataQueue.fromDecode.rs2Ready := decode.toExec.rs2Ready
  dataQueue.fromDecode.valid := scheduler.allocate.fired && (decode.toExec.instruction(6, 4) === "b010".U)
  when(instructionDecodedReady) { scheduler.allocate.fired := false.B }
  when(branchOps.valid) {
   scheduler.allocate.branchMask := decode.toExec.branchMask ^ branchOps.branchMask
   dataQueue.fromDecode.branchMask := decode.toExec.branchMask ^ branchOps.branchMask
   when(!branchOps.passed) {
    scheduler.allocate.fired := false.B
    rob.allocate.fired := false.B
    dataQueue.fromDecode.valid := false.B
   } 
  }
  // waking up instructions before inserting to queue
  wakeUps.foreach { wakeup => {
    when(wakeup.valid) {
      when (decode.toExec.rs1Addr === wakeup.prfAddr) { scheduler.allocate.rs1.ready := true.B }
      when (decode.toExec.rs2Addr === wakeup.prfAddr) { scheduler.allocate.rs2.ready := true.B }
    }
  } }

  val mExtensionReady = RegInit(true.B)

  val prf = Module(new PRF)
  scheduler.release.fired := (
    scheduler.release.ready &&
    (!(Cat(scheduler.release.instruction(25), scheduler.release.instruction(6, 4)) === "b1011".U) || mExtensionReady)
  )
  prf.execRead.instruction := scheduler.release.instruction
  prf.execRead.branchMask := scheduler.release.branchMask
  prf.execRead.rs1Addr := scheduler.release.rs1prfAddr
  prf.execRead.rs2Addr := scheduler.release.rs2prfAddr
  prf.execRead.robAddr := scheduler.release.robAddr
  prf.execRead.valid := scheduler.release.fired
  prf.execRead.prfDest := scheduler.release.prfDest
  when(scheduler.release.instruction(1, 0) === "b00".U) { prf.execRead.valid := false.B }

  // B1: fused address generation. The old two-stage path (prf.toExec ->
  // addressGenerationInput reg -> adder -> memoryRequest reg) spent a whole
  // cycle just registering the adder inputs. The rs1 forwarding mux plus the
  // 64-bit add fit in one cycle at the target clock, so memory ops now reach
  // the cache one cycle earlier. Field assignments live further down, after
  // the fwdFrom network is declared.
  val memoryRequest = RegInit(new pipelineMemoryRequest Lit(_.valid -> false.B))

  memAccess.request := memoryRequest
  memAccess.branchOps := branchOps

  val singleCycleArithmeticRequest = RegInit(new Bundle {
    val valid = Bool()
    val rs1 = UInt(64.W)
    val rs2 = UInt(64.W)
    val instruction = UInt(32.W)
    val prfDest = UInt(configuration.prfAddrWidth.W)
    val robAddr = UInt(configuration.robAddrWidth.W)
    val branchMask = UInt(configuration.newBranchMaskWidth.W) //leon coherency
  }.Lit(_.valid -> false.B))

  val singleCycleArithmeticResponse = RegInit(new Bundle {
    val valid = Bool()
    val result = UInt(64.W)
    val prfDest = UInt(configuration.prfAddrWidth.W)
    val robAddr = UInt(configuration.robAddrWidth.W)
  }.Lit(_.valid -> false.B))

  val extnMRequest = RegInit(singleCycleArithmeticRequest.cloneType Lit(_.valid -> false.B))

  val extnMServicing = RegInit(singleCycleArithmeticRequest.cloneType Lit(_.valid -> false.B))
  val extnMPartialServicing = RegInit(singleCycleArithmeticRequest.cloneType Lit(_.valid -> false.B))
  val muls = Reg(Vec (6, UInt (96.W)))
  extnMPartialServicing := extnMRequest
  extnMServicing := extnMPartialServicing
  when(extnMRequest.instruction(14).asBool) { extnMPartialServicing.valid := false.B }

  val extnMResponse = RegInit(singleCycleArithmeticResponse.cloneType Lit(_.valid -> false.B))

  val division = RegInit(new Bundle {
    val request = singleCycleArithmeticRequest.cloneType
    val quotient = UInt(65.W) // initally divident
    val remainder = UInt(65.W) // initially zero
    val divisor = UInt(65.W)
    val counter = UInt(7.W) // when zero operation finished
    val resultNegative = Bool()
  } Lit(_.request.valid -> false.B))

  val fwdBuffers = RegInit(VecInit(Seq.fill(2)(new Bundle {
    val valid = Bool()
    val prfDest = UInt(configuration.prfAddrWidth.W)
    val result = UInt(64.W)
    val branchMask = UInt(configuration.newBranchMaskWidth.W) //leon coherency
  }.Lit(_.valid -> false.B))))
  val fwdFrom = Seq.fill(3)(Wire(fwdBuffers(0).cloneType))
  fwdFrom(1) := fwdBuffers(0)
  fwdFrom(2) := fwdBuffers(1)
  fwdFrom(0).valid := singleCycleArithmeticRequest.valid && singleCycleArithmeticRequest.instruction(11, 7).orR
  fwdFrom(0).prfDest := singleCycleArithmeticRequest.prfDest
  fwdFrom(0).branchMask := singleCycleArithmeticRequest.branchMask

  // Fused address generation (see memoryRequest declaration above).
  val addressGenRs1 = Mux(prf.toExec.instruction(19, 15).orR, MuxCase(prf.toExec.rs1Data,
    fwdFrom.map(fwd => fwd.valid && (fwd.prfDest === prf.toExec.rs1Addr)) zip fwdFrom.map(_.result) map{ case(prfMatch, result ) => (prfMatch -> result)}
  ), 0.U)
  memoryRequest.address := addressGenRs1 + VecInit(
    Cat(Fill(52, prf.toExec.instruction(31)), prf.toExec.instruction(31, 20)),
    Cat(Fill(52, prf.toExec.instruction(31)), prf.toExec.instruction(31, 25), prf.toExec.instruction(11, 7)),
    0.U, 0.U
  )(Cat(prf.toExec.instruction(3), prf.toExec.instruction(5)))
  memoryRequest.branchMask := prf.toExec.branchMask
  memoryRequest.instruction := prf.toExec.instruction
  memoryRequest.prfDest := prf.toExec.prfDest
  memoryRequest.robAddr := prf.toExec.robAddr
  memoryRequest.valid := prf.toExec.valid && !Seq(6, 4).map(i => prf.toExec.instruction(i).asBool).reduce(_ || _)

  when(branchOps.valid) {
    when((prf.toExec.branchMask & branchOps.branchMask).orR) {
      memoryRequest.branchMask := prf.toExec.branchMask ^ branchOps.branchMask
    }
    when(!branchOps.passed && (prf.toExec.branchMask & branchOps.branchMask).orR) { memoryRequest.valid := false.B }
  }

  val arithmeticResult = {
    def result32bit(res: UInt) =
      Cat(Fill(32, res(31)), res(31, 0))
    
    val rs1 = singleCycleArithmeticRequest.rs1
    val rs2 = singleCycleArithmeticRequest.rs2
    val instruction = singleCycleArithmeticRequest.instruction

    val arithmetic32 = VecInit.tabulate(4)(i => i match {
      case 0 => Mux(Cat(instruction(30), instruction(5)) === "b11".U, result32bit(rs1 - rs2), result32bit(rs1 + rs2)) // add & sub
      case 1 => (result32bit(rs1 << rs2(4, 0))) // sll\iw
      case 2 => (result32bit(rs1 << rs2(4, 0))) // filler
      case 3 => Mux(instruction(30).asBool, result32bit((rs1(31, 0).asSInt >> rs2(4, 0)).asUInt), result32bit(rs1(31, 0) >> rs2(4, 0))) // sra\l\iw
    })(Cat(instruction(14), instruction(12)))

    /**
        * 64 bit operations, indexed with funct3, op-imm, op
        */
    val arithmetic64 = VecInit.tabulate(8)(i => i match {
      case 0 => Mux(Cat(instruction(30), instruction(5)) === "b11".U, rs1 - rs2, rs1 + rs2)
      case 1 => (rs1 << rs2(5, 0))
      case 2 => (rs1.asSInt < rs2.asSInt).asUInt
      case 3 => (rs1 < rs2).asUInt
      case 4 => (rs1 ^ rs2)
      case 5 => Mux(instruction(30).asBool, (rs1.asSInt >> rs2(5, 0)).asUInt, (rs1 >> rs2(5, 0)))
      case 6 => (rs1 | rs2)
      case 7 => (rs1 & rs2)
    })(instruction(14, 12))
    Mux(instruction(3).asBool, arithmetic32, arithmetic64)
  }
  fwdFrom(0).result := arithmeticResult

  val arithmeticImm = Cat(Fill(52, prf.toExec.instruction(31)), prf.toExec.instruction(31, 20))

  // getting arithmetic result
  singleCycleArithmeticRequest.valid := prf.toExec.valid && (prf.toExec.instruction(4, 2) === BitPat("b1?0")) && (prf.toExec.instruction(6).asBool || !prf.toExec.instruction(5).asBool || !prf.toExec.instruction(25).asBool)
  singleCycleArithmeticRequest.branchMask := prf.toExec.branchMask
  singleCycleArithmeticRequest.instruction := prf.toExec.instruction
  singleCycleArithmeticRequest.prfDest := prf.toExec.prfDest
  singleCycleArithmeticRequest.robAddr := prf.toExec.robAddr
  singleCycleArithmeticRequest.rs1 := Mux(prf.toExec.instruction(19, 15).orR, MuxCase(prf.toExec.rs1Data,
    fwdFrom.map(fwd => fwd.valid && (fwd.prfDest === prf.toExec.rs1Addr)) zip fwdFrom.map(_.result) map{ case(prfMatch, result ) => (prfMatch -> result)} 
  ), 0.U)
  singleCycleArithmeticRequest.rs2 := Mux(prf.toExec.instruction(5).asBool, Mux(prf.toExec.instruction(24, 20).orR, MuxCase(prf.toExec.rs2Data,
    fwdFrom.map(fwd => fwd.valid && (fwd.prfDest === prf.toExec.rs2Addr)) zip fwdFrom.map(_.result) map{ case(prfMatch, result ) => (prfMatch -> result)} 
  ), 0.U), arithmeticImm)
  
  when(branchOps.valid) {
    when((prf.toExec.branchMask & branchOps.branchMask).orR) {
      singleCycleArithmeticRequest.branchMask := prf.toExec.branchMask ^ branchOps.branchMask
    }
    when(!branchOps.passed && (prf.toExec.branchMask & branchOps.branchMask).orR) {
      singleCycleArithmeticRequest.valid := false.B
    }
  }

  // getting excuting arithmetic
  singleCycleArithmeticResponse.prfDest := singleCycleArithmeticRequest.prfDest
  singleCycleArithmeticResponse.result := arithmeticResult
  singleCycleArithmeticResponse.robAddr := singleCycleArithmeticRequest.robAddr
  singleCycleArithmeticResponse.valid := singleCycleArithmeticRequest.valid

  when(branchOps.valid) {
    when(!branchOps.passed && (branchOps.branchMask & singleCycleArithmeticRequest.branchMask).orR) {
      singleCycleArithmeticResponse.valid := false.B
    }
  }

    // getting extnM request
  extnMRequest.valid := prf.toExec.valid && (prf.toExec.instruction(6, 2) === BitPat("b011?0")) && prf.toExec.instruction(25).asBool
  extnMRequest.branchMask := prf.toExec.branchMask
  extnMRequest.instruction := prf.toExec.instruction
  when(branchOps.valid) {
    when((prf.toExec.branchMask & branchOps.branchMask).orR) {
      extnMRequest.branchMask := prf.toExec.branchMask ^ branchOps.branchMask
    }
    when(!branchOps.passed && (prf.toExec.branchMask & branchOps.branchMask).orR) {
      extnMRequest.valid := false.B
    }
  }
  extnMRequest.prfDest := prf.toExec.prfDest
  extnMRequest.robAddr := prf.toExec.robAddr
  extnMRequest.rs1 := MuxCase(prf.toExec.rs1Data,
    fwdFrom.map(fwd => fwd.valid && (fwd.prfDest === prf.toExec.rs1Addr)) zip fwdFrom.map(_.result) map{ case(prfMatch, result ) => (prfMatch -> result)} 
  )
  extnMRequest.rs2 := MuxCase(prf.toExec.rs2Data,
    fwdFrom.map(fwd => fwd.valid && (fwd.prfDest === prf.toExec.rs2Addr)) zip fwdFrom.map(_.result) map{ case(prfMatch, result ) => (prfMatch -> result)} 
  )

  val partialMuls32x32 = Seq(
    extnMRequest.rs1(31, 0) * extnMRequest.rs2(31, 0), 
    extnMRequest.rs1(63, 32) * extnMRequest.rs2(31, 0),
    Cat(extnMRequest.rs1(63), extnMRequest.rs1(63, 32)).asSInt * Cat(0.U(1.W), extnMRequest.rs2(31, 0)).asSInt,
    extnMRequest.rs1(31, 0) * extnMRequest.rs2(63, 32),
    Cat(0.U(1.W), extnMRequest.rs1(31, 0)).asSInt * Cat(extnMRequest.rs2(63), extnMRequest.rs2(63, 32)).asSInt,
    extnMRequest.rs1(63, 32) * extnMRequest.rs2(63, 32), 
    Cat(extnMRequest.rs1(63), extnMRequest.rs1(63, 32)).asSInt * Cat(0.U(1.W), extnMRequest.rs2(63, 32)).asSInt,
    extnMRequest.rs1(63, 32).asSInt * extnMRequest.rs2(63, 32).asSInt, 
    extnMRequest.rs1(31, 0).asSInt * extnMRequest.rs2(31, 0).asSInt
  )
  val narrowMuls = Reg(Vec(9, UInt(64.W)))
  narrowMuls zip partialMuls32x32.map(_.asUInt) foreach{ case(reg, mul) => reg := mul }

  muls(0) := (narrowMuls(0) + Cat(narrowMuls(1), 0.U(32.W)))// extnMRequest.rs1 * extnMRequest.rs2(31, 0)
  muls(1) := narrowMuls(3) + Cat(narrowMuls(5), 0.U(32.W)) // extnMRequest.rs1 * extnMRequest.rs2(63, 32)
  muls(2) := narrowMuls(3) + Cat(narrowMuls(6), 0.U(32.W))// (extnMRequest.rs1.asSInt * Cat(0.U(1.W),extnMRequest.rs2(63, 32)).asSInt)(95, 0).asUInt
  muls(3) := Cat(Fill(32, narrowMuls(4)(63)), narrowMuls(4)) + Cat(narrowMuls(7), 0.U(32.W)) // (extnMRequest.rs1.asSInt * extnMRequest.rs2(63, 32).asSInt)(95, 0).asUInt
  muls(4) := narrowMuls(0) + Cat(narrowMuls(2), 0.U(32.W))// (extnMRequest.rs1.asSInt * Cat(0.U(1.W), extnMRequest.rs2(31, 0)).asSInt)(95, 0).asUInt
  muls(5) := narrowMuls(8)// (extnMRequest.rs1(31, 0).asSInt * extnMRequest.rs2(31, 0).asSInt).asUInt
  
  val extnResponseInstruction = Reg(UInt(32.W))
  // getting excuting extn M
  extnMResponse.prfDest := extnMServicing.prfDest
  extnMResponse.result := Mux(extnMServicing.instruction(3).asBool, Cat(Fill(32, muls(5)(31)), muls(5)(31, 0)), 
  VecInit(
    (Cat(Fill(32, muls(4)(95)), muls(4)) + Cat(muls(3), 0.U(32.W)))(63, 0),
    (Cat(Fill(32, muls(4)(95)), muls(4)) + Cat(muls(3), 0.U(32.W)))(127, 64),
    (Cat(Fill(32, muls(4)(95)), muls(4)) + Cat(muls(2), 0.U(32.W)))(127, 64),
    (muls(0) + Cat(muls(1), 0.U(32.W)))(127, 64)
  )(extnMServicing.instruction(13, 12)))
  when(extnMServicing.valid && (!extnMServicing.instruction(24, 20).orR || !extnMServicing.instruction(19, 15).orR)) {
    extnMResponse.result := 0.U
  }
  extnMResponse.robAddr := extnMServicing.robAddr
  extnMResponse.valid := extnMServicing.valid
  extnResponseInstruction := extnMServicing.instruction

  when(branchOps.valid) {
    Seq(prf.toExec.branchMask, extnMRequest.branchMask, extnMPartialServicing.branchMask).map(i => (i ^ branchOps.branchMask, (i & branchOps.branchMask).orR))
    .zip(Seq(extnMRequest.branchMask, extnMPartialServicing.branchMask, extnMServicing.branchMask))
    .foreach{ case((branchMaskUpdate, bitMatch), reg) => when(bitMatch) {
      reg := branchMaskUpdate
    }}
    when(!branchOps.passed) {
      (Seq(prf.toExec.branchMask, extnMRequest.branchMask, extnMServicing.branchMask).map(_ & branchOps.branchMask).map(_.orR))
      .zip(Seq(extnMRequest.valid, extnMServicing.valid, extnMResponse.valid) zip Seq(prf.toExec.valid, extnMRequest.valid, extnMServicing.valid))
      .foreach{ case(branchMatch, (reg, validInstruction)) => when(branchMatch && validInstruction) { reg := false.B }}
    }
  }

  val divBranchMask = Reg(UInt(configuration.newBranchMaskWidth.W)) //leon coherency
  when(scheduler.release.fired && 
  (Cat(scheduler.release.instruction(25), scheduler.release.instruction(14), scheduler.release.instruction(6, 4)) === "b11011".U)) { 
    // There is a chance that this instruction will be flushed in the same cycle
    mExtensionReady := false.B || (branchOps.valid && (branchOps.branchMask & scheduler.release.branchMask).orR && !branchOps.passed)
    // Branch dependencies can change on the sampling clock cycle
    divBranchMask := Mux(branchOps.valid && (branchOps.branchMask & scheduler.release.branchMask).orR, scheduler.release.branchMask ^ branchOps.branchMask, scheduler.release.branchMask)
  }
  when(!mExtensionReady) {
    when(branchOps.valid && (branchOps.branchMask & divBranchMask).orR) {
      when(branchOps.passed) {
        divBranchMask := divBranchMask ^ branchOps.branchMask
      }
      when(!branchOps.passed) {
        mExtensionReady := true.B
      }
    }
  }
  when(extnMResponse.valid && extnResponseInstruction(14).asBool) { mExtensionReady := true.B }

  // Radix-8 (B6): unroll the proven non-restoring radix-2 recurrence THREE times
  // per clock and decrement the counter by 3, cutting the iteration cycles to
  // ceil(N/3). The arithmetic of each sub-step is bit-identical to the original
  // radix-2 loop, so the final quotient/remainder are unchanged after the same
  // TOTAL step count. The counter-parity select keeps the step count exact: it
  // takes 3 steps while >=3 remain, then a 2-step or 1-step tail so the counter
  // lands precisely on 0 — never overshooting. The result-extraction path
  // (counter == 0) makes no parity assumption, so it is untouched. (B5 was the
  // 2-step radix-4 form; the 3rd chained 65-bit add/sub lengthens the divider's
  // combinational path — acceptable for the sim/IPC metric, watch FPGA timing.)
  val remStep1 = (Cat(division.remainder(63, 0), division.quotient(64)) + Mux(division.remainder(64).asBool, division.divisor, - division.divisor))(64, 0)
  val quoStep1 = Cat(division.quotient(63, 0), ~remStep1(64))
  val remStep2 = (Cat(remStep1(63, 0), quoStep1(64)) + Mux(remStep1(64).asBool, division.divisor, - division.divisor))(64, 0)
  val quoStep2 = Cat(quoStep1(63, 0), ~remStep2(64))
  val remStep3 = (Cat(remStep2(63, 0), quoStep2(64)) + Mux(remStep2(64).asBool, division.divisor, - division.divisor))(64, 0)
  val quoStep3 = Cat(quoStep2(63, 0), ~remStep3(64))
  val doThree  = division.counter >= 3.U
  val doTwo    = division.counter === 2.U
  division.remainder := Mux(doThree, remStep3, Mux(doTwo, remStep2, remStep1))
  division.quotient  := Mux(doThree, quoStep3, Mux(doTwo, quoStep2, quoStep1))
  division.counter   := Mux(doThree, division.counter - 3.U, Mux(doTwo, division.counter - 2.U, division.counter - 1.U))

  when(extnMRequest.valid && extnMRequest.instruction(14).asBool) {
    division.counter := 65.U
    division.divisor := extnMRequest.rs2
    division.quotient := extnMRequest.rs1
    when(extnMRequest.instruction(3).asBool) {
      division.divisor := Cat(0.U(33.W), extnMRequest.rs2(31, 0))
      division.quotient := Cat(0.U(33.W), extnMRequest.rs1(31, 0))
    }
    division.remainder := 0.U
    division.request := extnMRequest
    division.resultNegative := false.B
    when(!extnMRequest.instruction(12).asBool){
      division.resultNegative := extnMRequest.rs1(63).asBool ^ extnMRequest.rs2(63).asBool
      when(extnMRequest.rs1(63).asBool) { division.quotient := Cat(0.U(1.W), (- extnMRequest.rs1)(63, 0)) }
      when(extnMRequest.rs2(63).asBool) { division.divisor := Cat(0.U(1.W), (- extnMRequest.rs2)(63, 0)) }
      when(extnMRequest.instruction(3).asBool) {
        when(extnMRequest.rs1(31).asBool) { division.quotient := Cat(0.U(33.W), (- extnMRequest.rs1(31, 0))(31, 0)) }
        when(extnMRequest.rs2(31).asBool) { division.divisor := Cat(0.U(33.W), (- extnMRequest.rs2(31, 0))(31, 0)) }
      }
    }
  }

  // Divider early termination: the non-restoring loop consumes the dividend
  // MSB-first out of the 65-bit quotient register, and leading-zero bits only
  // produce quotient bits of 0 while the partial remainder stays 0. So the
  // cycle after operand load we left-align the (already sign-corrected)
  // dividend and run only the iterations that carry information. divw/remw on
  // small values drop from 65 iterations to a handful. The zero-dividend and
  // zero-divisor cases keep the full-length path: the all-ones quotient that
  // RISC-V mandates for division by zero is produced by the iterations
  // themselves, which normalization would skip.
  val divNormalizePending = RegInit(false.B)
  when(extnMRequest.valid && extnMRequest.instruction(14).asBool) { divNormalizePending := true.B }
  when(divNormalizePending) {
    divNormalizePending := false.B
    division.remainder := 0.U
    when(division.quotient.orR && division.divisor.orR) {
      val leadingZeros = PriorityEncoder(Reverse(division.quotient))
      division.quotient := (division.quotient << leadingZeros)(64, 0)
      division.counter := 65.U - leadingZeros
    }.otherwise {
      division.quotient := division.quotient // hold: default iteration must not shift it
      division.counter := 65.U
    }
  }

  when(division.request.valid && !division.counter.orR) {
    extnMResponse.prfDest := division.request.prfDest
    extnMResponse.result := {
      val quotient32 = Mux((division.request.rs1(31).asBool ^ division.request.rs2(31).asBool) && !division.quotient.andR, (- division.quotient)(31, 0), division.quotient(31, 0))
      val remainder64Unsigned = Mux(division.remainder(64).asBool, division.remainder + division.divisor, division.remainder)
      val remainder32Signed = Mux((division.request.rs1(31).asBool), - remainder64Unsigned, remainder64Unsigned)
      
      VecInit(
      Mux((division.request.rs1(63).asBool ^ division.request.rs2(63).asBool) && !division.quotient.andR, - division.quotient, division.quotient),
      division.quotient,
      Mux((division.request.rs1(63).asBool), - remainder64Unsigned, remainder64Unsigned),
      remainder64Unsigned,
      Cat(Fill(32, quotient32(31)), quotient32(31, 0)),
      Cat(Fill(32, division.quotient(31)), division.quotient(31, 0)),
      Cat(Fill(32, remainder32Signed(31)), 
        remainder32Signed(31, 0)),
      Cat(Fill(32, remainder64Unsigned(31)), 
        remainder64Unsigned(31, 0))
    )(Cat(division.request.instruction(3), division.request.instruction(13, 12)))
  }
    extnMResponse.robAddr := division.request.robAddr
    extnMResponse.valid := division.request.valid
    extnResponseInstruction := division.request.instruction
    division.request.valid := false.B
    when(branchOps.valid) {
      when((division.request.branchMask & branchOps.branchMask).orR && !branchOps.passed) { extnMResponse.valid := false.B } 
    }
  }

  when(branchOps.valid) {
    when(extnMRequest.valid) {
      when ((extnMRequest.branchMask & branchOps.branchMask).orR) { division.request.branchMask := extnMRequest.branchMask ^ branchOps.branchMask }
      when(!branchOps.passed && (extnMRequest.branchMask & branchOps.branchMask).orR) { division.request.valid := false.B }
    }
    when ((division.request.branchMask & branchOps.branchMask).orR) { division.request.branchMask := division.request.branchMask ^ branchOps.branchMask }
    when(!branchOps.passed && (division.request.branchMask & branchOps.branchMask).orR) { division.request.valid := false.B }
  }

  // setting up forwarding data for next cycle
  // oldest (fwdBuffers(1) or fwdFrom(2)) from fwding
  fwdBuffers zip fwdFrom foreach{ case (reg, nextVal) => { reg := nextVal }}

  // pc of the instruction which branched
  //leon rename the depth
  val branchPCs = RegInit(VecInit(Seq.fill(configuration.branchPC_depth)(new Bundle {
    val valid = Bool()
    val pc = UInt(64.W)
    val branchMask = UInt(configuration.newBranchMaskWidth.W) //leon coherency 
  }.Lit(_.valid -> false.B))))

  // pc of the speculated instruction
  val predictedPCs = RegInit(VecInit(Seq.fill(configuration.branchPC_depth)(new Bundle {
    val valid = Bool()
    val pc = UInt(64.W)
  }.Lit(_.valid -> false.B))) )

  val branchInstruction = RegInit(new Bundle {
    val valid = Bool()
    val rs1 = UInt(64.W)
    val rs2 = UInt(64.W)
    val robAddr = UInt(configuration.robAddrWidth.W)
    val branchMask = UInt(configuration.newBranchMaskWidth.W) //leon coherency
    val instruction = UInt(32.W)
    val immediate = UInt(64.W)
  }.Lit(_.valid -> false.B))

  branchInstruction.immediate := {
    val instruction = prf.toExec.instruction

    VecInit(
      Cat(Fill(52, instruction(31)), instruction(7), instruction(30, 25), instruction(11, 8), 0.U(1.W)),
      Cat(Fill(52, instruction(31)), instruction(31, 20)),
      0.U,
      Cat(Fill(44, instruction(31)), instruction(19, 12), instruction(20), instruction(30, 21), 0.U(1.W))
    )(prf.toExec.instruction(3, 2))
  }

  val branchEvals = RegInit(new Bundle {
    val valid = Bool()
    val passed = Bool()
    val branchMask = UInt(configuration.newBranchMaskWidth.W) //leon coherency
    val robAddr = UInt(configuration.robAddrWidth.W)
    val nextPC = UInt(64.W)
  }.Lit(_.valid -> false.B))
  branchOps.valid := branchEvals.valid
  branchOps.passed := branchEvals.passed
  branchOps.branchMask := branchEvals.branchMask

  branchInstruction.valid := prf.toExec.valid && (prf.toExec.instruction(6, 4) === "b110".U)
  when(prf.toExec.valid && (prf.toExec.instruction(6, 5) === "b11".U)){
    branchInstruction.rs1 := MuxCase(prf.toExec.rs1Data,
      fwdFrom.map(fwd => fwd.valid && (fwd.prfDest === prf.toExec.rs1Addr)) zip fwdFrom.map(_.result) map{ case(prfMatch, result ) => (prfMatch -> result)} 
    )
    branchInstruction.rs2 := MuxCase(prf.toExec.rs2Data,
      fwdFrom.map(fwd => fwd.valid && (fwd.prfDest === prf.toExec.rs2Addr)) zip fwdFrom.map(_.result) map{ case(prfMatch, result ) => (prfMatch -> result)} 
    )
    branchInstruction.branchMask := prf.toExec.branchMask
    branchInstruction.robAddr := prf.toExec.robAddr
    branchInstruction.instruction := prf.toExec.instruction
  }
  
  when(branchOps.valid) {
    when((branchOps.branchMask & prf.toExec.branchMask).orR) {
      branchInstruction.branchMask := prf.toExec.branchMask ^ branchOps.branchMask
    }
    when(!branchOps.passed && (branchOps.branchMask & prf.toExec.branchMask).orR) {
      branchInstruction.valid := false.B
    }
  }

  //leon coherency need (connection)
  val coherentLoadInvalid = !memAccess.loadCommit.state && memAccess.loadCommit.valid && rob.commit.ready && rob.commit.instruction(6,2).orR===0.U

  //leon coherency
  branchEvals.valid := Mux(coherentLoadInvalid,true.B,branchInstruction.valid)
  branchEvals.robAddr := Mux(coherentLoadInvalid,rob.commit.robAddr-1.U,branchInstruction.robAddr)
  branchEvals.branchMask := Mux(coherentLoadInvalid,configuration.coherent_BranchMask,branchPCs(0).branchMask)

  when(branchOps.valid) {
    when(!branchOps.passed) {
      branchEvals.valid := false.B
    }
  }

  val branchTaken = {
    val rs1 = Mux(branchInstruction.instruction(19, 15).orR ,branchInstruction.rs1, 0.U)
    val rs2 = Mux(branchInstruction.instruction(24, 20).orR ,branchInstruction.rs2, 0.U)
    val instruction = branchInstruction.instruction
    val pc = branchPCs(0).pc
    
    val conditionEval = VecInit(Seq(rs1 === rs2, rs1 === rs2, rs1.asSInt < rs2.asSInt, rs1 < rs2).flatMap(cond => Seq(cond, !cond)))
    
    conditionEval(instruction(14, 12))
  }
  val nextCorrectPC = {
    val rs1 = branchInstruction.rs1
    val rs2 = branchInstruction.rs2
    val instruction = branchInstruction.instruction
    val immediate = branchInstruction.immediate
    val pc = branchPCs(0).pc
    val pcPredicted = predictedPCs(0).pc

    val conditionEval = VecInit(
      rs1 === rs2,
      rs1 =/= rs2,
      false.B,
      false.B,
      rs1.asSInt < rs2.asSInt,
      rs1.asSInt >= rs2.asSInt,
      rs1 < rs2,
      rs1 >= rs2
    )

    VecInit(
      Mux(conditionEval(instruction(14, 12)), pc + immediate, pc + 4.U),
      rs1 + immediate,
      0.U,
      pc + immediate
    )(instruction(3, 2))
  }

  //leon coherency
  branchEvals.nextPC := Mux(coherentLoadInvalid,rob.commit.pc,nextCorrectPC)
  branchEvals.passed := Mux(coherentLoadInvalid, 0.B, predictedPCs(0).valid && (nextCorrectPC === predictedPCs(0).pc))


  decode.branchPCs.fired := decode.branchPCs.ready && !(branchPCs).map(_.valid).reduce(_ && _)
  
  // branching PCs
  (branchPCs.map(_.valid).scanLeft(true.B)(_ && _) zip branchPCs)
  .foreach{ case(priorEntriesFull, reg) => {when(priorEntriesFull && !reg.valid) { 
    reg.valid := decode.branchPCs.branchPCReady
    reg.pc := decode.branchPCs.branchPC
    reg.branchMask := decode.branchPCs.branchMask
  }}} 
  when(branchOps.valid && !branchOps.passed) {
    branchPCs.foreach{ _.valid := false.B }
  }.elsewhen(branchInstruction.valid) {
    (branchPCs.map(_.valid).scanLeft(true.B)(_ && _) zip branchPCs)
    .map{ case(priorEntriesFull, reg) => {
      val entry = Wire(branchPCs(0).cloneType)
      entry.valid := reg.valid || (decode.branchPCs.branchPCReady && (!reg.valid && priorEntriesFull))
      entry.pc := Mux(reg.valid, reg.pc, decode.branchPCs.branchPC)
      entry.branchMask := Mux(reg.valid, reg.branchMask, decode.branchPCs.branchMask)
      entry
    }} 
    .drop(1)
    .zip(branchPCs)
    .foreach{ case(update, reg) => reg := update }
    branchPCs(branchPCs.length-1).valid := false.B
  }
  
  // predicted PCs
  (predictedPCs.map(_.valid).scanLeft(true.B)(_ && _) zip predictedPCs)
  .foreach{ case(priorEntriesFull, reg) => {when(priorEntriesFull && !reg.valid) { 
    reg.valid := decode.branchPCs.predictedPCReady
    reg.pc := decode.branchPCs.predictedPC
  }}} 
  when(branchOps.valid && !branchOps.passed) {
    predictedPCs.foreach{ _.valid := false.B }
  }.elsewhen(branchInstruction.valid) {
    (predictedPCs.map(_.valid).scanLeft(true.B)(_ && _) zip predictedPCs)
    .map{ case(priorEntriesFull, reg) => {
      val entry = Wire(predictedPCs(0).cloneType)
      entry.valid := reg.valid || (decode.branchPCs.predictedPCReady && (!reg.valid && priorEntriesFull))
      entry.pc := Mux(reg.valid, reg.pc, decode.branchPCs.predictedPC)
      entry
    }} 
    .drop(1)
    .zip(predictedPCs)
    .foreach{ case(update, reg) => reg := update }
    predictedPCs(branchPCs.length-1).valid := false.B
  }

  //leon coherency
  val coherentLoadInvalidReg = RegInit(coherentLoadInvalid)
  coherentLoadInvalidReg := coherentLoadInvalid


  fetch.branchRes.branchTaken := branchTaken
  fetch.branchRes.isBranch := true.B
  fetch.branchRes.pc := branchPCs(0).pc
  fetch.branchRes.pcAfterBrnach := branchEvals.nextPC 
  fetch.branchRes.fired := fetch.branchRes.ready && branchEvals.valid && !coherentLoadInvalidReg

  decode.writeBackResult.PRFDest := rob.commit.prfDest
  decode.writeBackResult.instruction := rob.commit.instruction
  decode.writeBackResult.pc := rob.commit.pc
  decode.writeBackResult.rdAddr := rob.commit.instruction(11, 7)

  //leon coherency
  Seq(decode.writeBackResult.fired, rob.commit.fired).foreach{ 
    _ := ((!rob.commit.instruction(6,2).orR===0.U || (!coherentLoadInvalid && memAccess.loadCommit.valid)) && decode.writeBackResult.ready && rob.commit.ready && (memAccess.writeInstructionCommit.ready || (rob.commit.instruction(6, 4) =/= "b010".U)))
  }

  memAccess.writeInstructionCommit.fired := memAccess.writeInstructionCommit.ready && (rob.commit.instruction(6, 4) === "b010".U)  && rob.commit.fired

  //leon coherency
  memAccess.loadCommit.ready := rob.commit.instruction(6,2).orR===0.U && rob.commit.ready


  decode.writeAddrPRF.exec1Addr := scheduler.instrRetired.prfAddr
  decode.writeAddrPRF.exec1Valid := scheduler.instrRetired.valid
  decode.writeAddrPRF.exec2Addr := memAccess.responseOut.prfDest
  decode.writeAddrPRF.exec3Addr := extnMResponse.prfDest
  decode.writeAddrPRF.exec2Valid := memAccess.responseOut.valid && memAccess.responseOut.instruction(11, 7).orR
  decode.writeAddrPRF.exec3Valid := extnMResponse.valid && extnResponseInstruction(11, 7).orR

  decode.branchEvalIn.branchMask := branchEvals.branchMask
  decode.branchEvalIn.passFail := branchEvals.passed
  decode.branchEvalIn.targetPC := branchEvals.nextPC
  decode.branchEvalIn.fired := decode.branchEvalIn.ready && branchEvals.valid

  rob.branch.pass := branchEvals.passed
  rob.branch.robAddr := branchEvals.robAddr
  rob.branch.valid := branchEvals.valid

  scheduler.memoryReady := memAccess.canAllocate
  scheduler.multuplyAndDivideReady := mExtensionReady

  rob.execPorts.foreach{ _.exceptionOccurred := false.B }
  rob.execPorts.foreach{ _.mcause := 0.U }
  rob.execPorts.foreach{ _.mtval := 0.U }
  rob.execPorts(0).mtval := RegNext(singleCycleArithmeticRequest.rs1)
  
  Seq(
    singleCycleArithmeticResponse.robAddr,
    branchEvals.robAddr,
    memAccess.responseOut.robAddr, // for reads
    extnMResponse.robAddr // for mul and div
  )
  .zip(Seq(
    singleCycleArithmeticResponse.valid,
    branchEvals.valid,
    memAccess.responseOut.valid,
    extnMResponse.valid
  ))
  .zip(rob.execPorts)
  .foreach { case ((robAddr, valid), execPort) =>  
    execPort.robAddr := robAddr
    execPort.valid := valid
  }

  Seq(
    (singleCycleArithmeticResponse.prfDest, singleCycleArithmeticResponse.result, singleCycleArithmeticResponse.valid && RegNext(singleCycleArithmeticRequest.instruction(11, 7).orR && (singleCycleArithmeticRequest.instruction(6, 2) =/= "b11100".U), false.B)),
    (decode.jumpAddrWrite.PRFDest, decode.jumpAddrWrite.linkAddr, decode.jumpAddrWrite.ready),
    (memAccess.responseOut.prfDest, memAccess.responseOut.result, memAccess.responseOut.valid && memAccess.responseOut.instruction(11, 7).orR),
    (extnMResponse.prfDest, extnMResponse.result, extnMResponse.valid && extnResponseInstruction(11, 7).orR)
  )
  .zip(Seq(prf.w1, prf.w2, prf.w3, prf.w4))
  .foreach{ case ((addr, data, en), writePort) => {
    writePort.addr := addr
    writePort.data := data
    writePort.en := en
  } }

  Seq(
    scheduler.instrRetired.prfAddr,
    memAccess.responseOut.prfDest,
    extnMResponse.prfDest
  )
  .zip(Seq(
    scheduler.instrRetired.valid,
    memAccess.responseOut.valid && memAccess.responseOut.instruction(11, 7).orR,
    extnMResponse.valid && extnResponseInstruction(11, 7).orR
  ))
  .zip(wakeUps)
  .foreach{ case ((prfAddr, valid), wakeup) => {
    wakeup.prfAddr := prfAddr
    wakeup.valid := valid
  }}

  decode.jumpAddrWrite.fired := decode.jumpAddrWrite.ready

  prf.branchCheck.branchMask := branchOps.branchMask
  prf.branchCheck.pass := branchOps.passed
  prf.branchCheck.valid := branchOps.valid

  // B3: single staging register on the store-data PRF read (was RegNext×2).
  // The read is triggered only once the store is at the ROB head, so rs2 is
  // architectural well before either delay; the arbiter captures writeDataIn
  // whenever it arrives, so the extra cycle bought nothing.
  prf.fromStore.branchMask := RegNext(dataQueue.toPRF.branchMask)
  prf.fromStore.instruction := RegNext(dataQueue.toPRF.instruction)
  prf.fromStore.rs2Addr := RegNext(dataQueue.toPRF.rs2Addr)
  prf.fromStore.valid := RegNext(dataQueue.toPRF.valid && dataQueue.fromROB.readyNow, false.B)
  prf.fromStore.prfDest := 0.U

  dataQueue.fromBranch.robAddr := branchEvals.robAddr
  dataQueue.fromBranch.passOrFail := branchOps.passed
  dataQueue.fromBranch.valid := branchOps.valid

  dataQueue.robMapUpdate.robAddr := rob.allocate.robAddr
  dataQueue.robMapUpdate.valid := rob.allocate.fired

  dataQueue.fromROB.readyNow := memAccess.writeCommit.fired

  memAccess.writeDataIn.data := prf.toStore.rs2Data
  memAccess.writeDataIn.valid := prf.toStore.valid

  memAccess.writeCommit.fired := memAccess.writeCommit.ready && (rob.commit.instruction(6, 4) === "b010".U) 

  (wakeUps.drop(1) zip scheduler.wakeUpExt)
  .foreach{ case (wakeup, schedulerWakeup) => {
    schedulerWakeup := wakeup
  }}

  decode.branchEvalOut.fired := decode.branchEvalOut.ready

  scheduler.branchOps := branchOps

  val dPortBusWidth = math.pow(2, dPort_SIZE).toInt * 8
  val dPort = IO(new ACE(busWidth = dPortBusWidth))
  val peripheral = IO(new  AXI)
  dPort <> memAccess.dPort
  peripheral <> memAccess.peripheral

  memAccess.initiateFence := rob.commit.fired && rob.commit.is_fence
  // When a fence is fired from fetch unit, it expects it to be executed
  // Accounting when fence belongs to a mispredicted path
  val noFence :: fenceFromFetch :: fenceFromDecode :: Nil = Enum(3)
  val fenceState = RegInit(new Bundle {
    val state = noFence.cloneType
    val branchMask = branchEvals.branchMask.cloneType
  } Lit(_.state -> noFence))
  // Executing a mispredicted fence will not violate the spec
  // However, if no executing is an option, do so in the future
  // This is beleived to give the least problem when doing verification
  switch(fenceState.state) {
    is(noFence) { 
      when(fetch.toDecode.fired && ((fetch.toDecode.instruction(19, 0) & "hFEFFF".U(20.W)) === "h0000F".U(20.W))) {
        fenceState.state := fenceFromFetch
      }
    }
    is(fenceFromFetch) {
      when(branchEvals.valid && !branchEvals.passed) {
        // misprediction means, the fence is in a mispredicted path
        memAccess.initiateFence := true.B
        fenceState.state := noFence
      }.elsewhen(decode.toExec.fired && ((decode.toExec.instruction(19, 0) & "hFEFFF".U(20.W)) === "h0000F".U(20.W))) {
        fenceState.state := fenceFromDecode
        fenceState.branchMask := decode.toExec.branchMask
      }
    }
    is(fenceFromDecode) {
      when(branchEvals.valid && (branchEvals.branchMask & fenceState.branchMask).orR && !branchEvals.passed) {
        // fence belongs to an mispredicted path
        memAccess.initiateFence := true.B
        fenceState.state := noFence
      }.elsewhen(rob.commit.fired && rob.commit.is_fence) {
        memAccess.initiateFence := true.B
        fenceState.state := noFence
      }.elsewhen(branchEvals.valid && (branchEvals.branchMask & fenceState.branchMask).orR && branchEvals.passed) {
        fenceState.branchMask := (fenceState.branchMask ^ branchOps.branchMask)
      }
    }
  }

  // dcache informs fetch unit that its cache is clean
  Seq(memAccess.fenceInstructions.fired, icache.updateAllCachelines.fired).foreach(
    _ := (memAccess.fenceInstructions.ready && icache.updateAllCachelines.ready)
  )
  fetch.carryOutFence.fired := fetch.carryOutFence.ready
  fetch.updateAllCachelines.fired := fetch.updateAllCachelines.ready
  // icache informs the fetch unit to start fetching again
  Seq(icache.cachelinesUpdatesResp.fired, fetch.cachelinesUpdatesResp.fired).foreach(
    _ := (icache.cachelinesUpdatesResp.ready && fetch.cachelinesUpdatesResp.ready)
  )

  decode.writeBackResult.robAddr := 0.U
  decode.writeBackResult.data := rob.commit.mtval

  when(branchOps.valid && !branchOps.passed && (fetch.cachelinesUpdatesResp.ready)) {
    memAccess.initiateFence := true.B
  }
  when(RegNext(branchOps.valid && !branchOps.passed && ((fetch.toDecode.instruction === "h0FF0000F".U && fetch.toDecode.fired)))) {
    memAccess.initiateFence := true.B
  }

  // removing illegal write operations
  // This assumes these belong to an mispredicted execution path, and hence will be removed anyway by a future misprediction
  when(scheduler.release.instruction(4, 2) === "b010".U(3.W)) { prf.execRead.valid := false.B }

  val minstret = RegInit(0.U(64.W))
  //val decodeReadyCount = RegInit
  val programRunning = RegInit(true.B)

  val counters = RegInit(new Bundle {
    val minstret = UInt(64.W)
    val decodeReady = UInt(64.W)
    val decodeFired = UInt(64.W)
    val branchCount = UInt(64.W)
    val branchesPassed = UInt(64.W)
    val cycleCount = UInt(64.W)
    val schedulerFull = UInt(64.W)
    val robFull = UInt(64.W)
  } Lit(
    _.branchCount -> 0.U,
    _.branchesPassed -> 0.U, 
    _.cycleCount -> 0.U,
    _.decodeFired -> 0.U,
    _.decodeReady -> 0.U,
    _.minstret -> 0.U,
    _.schedulerFull -> 0.U,
    _.robFull -> 0.U
  ))

  when(rob.commit.fired && rob.commit.pc >= "h100440".U) { programRunning := false.B }
  when(programRunning) { 
    counters.cycleCount := counters.cycleCount + 1.U
    when(branchOps.valid) {
      counters.branchCount := counters.branchCount + 1.U
      when(branchOps.passed) {
        counters.branchesPassed := counters.branchesPassed + 1.U
      }
    } 
    when(decode.toExec.ready) {
      counters.decodeReady := counters.decodeReady + 1.U
      when(decode.toExec.fired) { counters.decodeFired := counters.decodeFired + 1.U }      
      when(!scheduler.allocate.ready) { counters.schedulerFull := counters.schedulerFull + 1.U }
      when(!rob.allocate.ready) { counters.robFull := counters.robFull + 1.U }
    }
    when(rob.commit.fired) {
      counters.minstret := counters.minstret + 1.U
    }
  }

  // Drive the architectural HPM counters in decode. These are free-running
  // (not gated by `programRunning`) so software rdcycle/rdinstret/rdhpmcounterN
  // reflect the whole program, not just the profiling window above.
  decode.perfEvents.instRetire     := rob.commit.fired
  decode.perfEvents.branchResolved := branchOps.valid
  decode.perfEvents.branchMispred  := branchOps.valid && !branchOps.passed
  decode.perfEvents.schedStall     := decode.toExec.ready && !scheduler.allocate.ready
  decode.perfEvents.robStall       := decode.toExec.ready && !rob.allocate.ready

  val core_sample0, core_sample1 = IO(Output(UInt(1.W)))
  core_sample0 := decode.fromFetch.expected.valid.asUInt
  core_sample1 := decode.fromFetch.expected.pc(30)
  
  val MTIP = IO(Input(Bool()))
  /**
    * Implementing timer interrupts
    * Where do we insert the timer interrupts. when,
    * (Case-1) There are speculated instructions in the pipeline 
    * (Case-2) There are no speculated instructions in the pipeline
    * 
    * Detecting which case is valid
    *   A two way counter(branchesInPipeline) that
    *   1. Increments when branch is fired from fetch.toDecode.fired
    *   2. Decrements when branchEval.valid is asserted
    *   3. Resets when there is a branch misprediction 
    *     (i.e. branchEval.valid && !branchEval.passed)
    * 
    * When branchesInPipeline > 0 Case-1 is valid
    *   When interrupts arrives, start starving the decode. Then issue
    *   a branchMiss prediction to flush all speculated instructions
    *   Start the interrupt when the remaining branch commits, and
    *   then free fetch.fromDecode interface. (The interrupted instruction
    *   is the branch instruction)
    * 
    * When branchesInPipeline == 0 Case-2 is valid
    *   When interrupt arrives, modify the instruction from the pipeline
    *   indicating that interrupt handler should fire when the instruction
    *   commits.
    * 
    * The above assumes that when a situation where no system instruction
    * is in pipeline and MIE is high
    */
  // counter keeping track of amount of branch instructions yet to executed
  // Amount of branch instructions tracked by the rob cannot exceed branchMaskWidth
  //  A little bit of headroom is given incase there are branch instructions in the decoder
  //  Current decoder can only decode 2 instructions, this has to be re-evaluted if this
  //    changes
  //  This counter should never overflow
  val branchCounter = RegInit(0.U(log2Ceil(configuration.branchMaskWidth+4).W)) //leon coherency
  branchCounter := branchCounter +&
    // important that we don't use fetch.toDecode.instruction since, we modify
    // decode.fromFetch.instruction when injecting interrupt 
    (decode.fromFetch.fired && decode.fromFetch.instruction(6, 4) === "b110".U) -& 
    (branchEvals.valid)
  // when there is mis-prediction, then the whole pipeline is flushed
  when(branchEvals.valid && !branchEvals.passed) {
    // because of last assignment wins, this is executed when there is a 
    // branch misprediction
    branchCounter := 0.U
  }
  // Registers the rob of the last branch instruction executed
  // This is the instruction that will be interrupted
  val lastBranchExecRob = Reg(UInt(configuration.robAddrWidth.W))
  val lastBranchExecPC = Reg(UInt(64.W))
  when(branchEvals.valid) { 
    lastBranchExecRob := branchEvals.robAddr
    lastBranchExecPC := RegNext(branchPCs(0).pc) 
  }
  decode.interruptedPC := lastBranchExecPC

  /**
    * FSM to inject interrupt
    * waitForMTIP - Default state, until the MTIP is asserted and the core 
    *   can act on it
    * waitToInjectInterr - 
    *   - When all instructions in pipeline can be commited, inject new 
    *     instruction from decode (-> done)
    *   - When there are speculated instructions, moves to flush pipeline
    * flushSpeculated - Flush all speculated instructions (1 cycle)
    * waitToCommitBranch - Wait until last branch is commited, this branch
    *   is interrupted
    */
  val lastRetiredSystem = RegInit(false.B) // Is the last retired instruction a system instruction
  when(decode.fromFetch.fired) { lastRetiredSystem := fetch.toDecode.instruction(6, 0) === "b1110011".U }
  val waitForMTIP :: waitToInjectInterr :: flushSpeculated :: waitToCommitBranch :: Nil = Enum(4)
  val interruptInjectStatus = RegInit(waitForMTIP)
  switch(interruptInjectStatus) {
    is(waitForMTIP) {
      // Some system instructions enable/disable MTIP its pain to account for this in our verification method
      // Writing this few lines of code is easier (However this is technical debt!)
      when(decode.canTakeInterrupt && MTIP && !lastRetiredSystem) { interruptInjectStatus := waitToInjectInterr }
    }
    is(waitToInjectInterr) {
      // branchEvals.valid asserted is hard to control so we avoid it
			when (decode.canTakeInterrupt) {
				when(!branchEvals.valid) {
					when(branchCounter.orR) {
						when(branchInstruction.valid && branchInstruction.instruction(6, 0) === "b1100011".U) {
							interruptInjectStatus := flushSpeculated
							// Manupulating the results to flush all speculated instructions
							// this branch
							branchEvals.passed := false.B
						}
					}.otherwise {
						when((fetch.toDecode.instruction(6, 0) =/= "b0001111".U) && (fetch.toDecode.instruction(6, 0) =/= "b1110011".U)) {
							// Just easier not to deal with fences
							decode.fromFetch.instruction := "h80000073".U(64.W)
							// Since the above instruction has the same encoding as system
							// instruction, hence no instruction will be taken until
							// the instruction has exited the pipeline, after that interrupt
							// handler will begin
							when(decode.fromFetch.fired) { interruptInjectStatus := waitForMTIP }
						}
					}
				}
			}
    }
    is(flushSpeculated) {
      interruptInjectStatus := waitToCommitBranch
    }
    is(waitToCommitBranch) {
      when(rob.commit.robAddr === lastBranchExecRob) { 
        // Modifying the writeback instruction so that this intruction is interrupted
        decode.writeBackResult.instruction := "h80000073".U(64.W)
        // Interrupt injecting is done
        when(decode.writeBackResult.fired) { interruptInjectStatus := waitForMTIP } 
      }
    }
  }
  when(!((interruptInjectStatus === waitForMTIP) || ((interruptInjectStatus === waitToInjectInterr) && !branchCounter.orR))) {
    // Stops new instructions to pipeline to simplyfy things
    fetch.toDecode.fired := false.B
    decode.fromFetch.fired := false.B
  }
}


class soc extends Module {
  val io = IO(new Bundle {
    // Pull out only the non-debug signals
    val core0_iPort = new ACE(busWidth = 64)
    val core0_dPort = new ACE(busWidth = 64)
    val core0_peripheral = new AXI
    val core0_MTIP = Input(Bool())

    val core1_iPort = new ACE(busWidth = 64)
    val core1_dPort = new ACE(busWidth = 64)
    val core1_peripheral = new AXI
    val core1_MTIP = Input(Bool())
  })

  // Instantiate two cores
  val core0 = Module(new core(dPort_id = 0, peripheral_id = 0, mhart_id = 0, iPort_id = 0))
  val core1 = Module(new core(dPort_id = 1, peripheral_id = 1, mhart_id = 1, iPort_id = 1))

  // Connect core0 signals to SoC IO
  core0.iPort <> io.core0_iPort
  core0.dPort <> io.core0_dPort
  core0.peripheral <> io.core0_peripheral
  core0.MTIP := io.core0_MTIP

  // Connect core1 signals to SoC IO
  core1.iPort <> io.core1_iPort
  core1.dPort <> io.core1_dPort
  core1.peripheral <> io.core1_peripheral
  core1.MTIP := io.core1_MTIP


}


object core extends App {
  emitVerilog(new core(dPort_id = 0, peripheral_id = 0, mhart_id = 0, iPort_id = 0))
}
