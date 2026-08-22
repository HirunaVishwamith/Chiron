package Decode

import chisel3._
import chisel3.experimental.BundleLiterals._
import chisel3.util._
import Decode.constants._
import Decode.utils._
import common.configuration

class composableInterface extends Bundle {
  val ready = Output(Bool())
  val fired = Input(Bool())
}

class RecivInstrFrmFetch extends composableInterface {
  val pc          = Input(UInt(dataWidth.W))
  val instruction = Input(UInt(insAddrWidth.W))
  val predictedNextPC = Input(UInt(dataWidth.W))
  val expected    = Output(new Bundle {
    val valid = Bool()
    val pc    = UInt(dataWidth.W)
    val coherency = Bool() //leon coherency
  })
}

class PushInsToPipeline extends composableInterface {
  val instruction = Output(UInt(insAddrWidth.W))
  val pc          = Output(UInt(dataWidth.W))
  val PRFDest     = Output(UInt(PRFAddrWidth.W))
  val rs1Addr     = Output(UInt(PRFAddrWidth.W))
  val rs1Ready    = Output(Bool())
  val rs2Addr     = Output(UInt(PRFAddrWidth.W))
  val rs2Ready    = Output(Bool())
  val immediate   = Output(UInt(dataWidth.W))
  val robAddr     = Input(UInt(robAddrWidth.W))   // allocated address in rob
  val branchMask  = Output(UInt(configuration.newBranchMaskWidth.W))  // leon coherency
}

class PullCommitFrmRob extends composableInterface {
  val pc          = Input(UInt(dataWidth.W))
  val instruction = Input(UInt(insAddrWidth.W))
  val rdAddr      = Input(UInt(rdWidth.W))
  val PRFDest     = Input(UInt(PRFAddrWidth.W))
  val robAddr     = Input(UInt(robAddrWidth.W))
  val data        = Input(UInt(dataWidth.W))
}

class PrfAddrFrmExec extends Bundle {
  val exec1Addr  = Input(UInt(PRFAddrWidth.W))
  val exec2Addr  = Input(UInt(PRFAddrWidth.W))
  val exec3Addr  = Input(UInt(PRFAddrWidth.W))
  val exec1Valid = Input(Bool())
  val exec2Valid = Input(Bool())
  val exec3Valid = Input(Bool())
}

class JumpPRFWrite extends composableInterface {
  val PRFDest  = Output(UInt(PRFAddrWidth.W))
  val linkAddr = Output(UInt(dataWidth.W))
}

class BranchPCs extends composableInterface {
  val branchPCReady    = Output(Bool())
  val branchPC         = Output(UInt(dataWidth.W))
  val predictedPCReady = Output(Bool())
  val predictedPC      = Output(UInt(dataWidth.W))
  val branchMask       = Output(UInt(configuration.newBranchMaskWidth.W)) //leon coherency
}

class BranchEvalIn extends composableInterface {
  val passFail   = Input(Bool())
  val branchMask = Input(UInt(configuration.newBranchMaskWidth.W))  //leon coherency
  val targetPC   = Input(UInt(dataWidth.W))
}

class BranchEvalOut extends composableInterface {
  val passFail   = Output(Bool())
  val branchMask = Output(UInt(configuration.newBranchMaskWidth.W))
}

class RetiredRenamedTable extends Bundle {
  val table = Output(Vec(regCount, UInt(PRFAddrWidth.W)))
}

/**
  * Functionality - Must communicate the pc of the first instruction to execute
  * through from Fetch.
  * 
  * Details about the IO can be found on common/ports.scala
  *
  */
class decode (
  mhart_id : Int
) extends Module {
  /**
   * Inputs and Outputs of the module
   */
  val fromFetch       = IO(new RecivInstrFrmFetch)      /** receives instructions from fetch and communicates the pc of the expected instruction */
  val toExec          = IO(new PushInsToPipeline)       /** sends the decoded instruction to the next stage of the pipeline */
  val writeBackResult = IO(new PullCommitFrmRob)          /** receives results to write into the register file */
  val writeAddrPRF    = IO(new PrfAddrFrmExec)
  val jumpAddrWrite   = IO(new JumpPRFWrite)
  val branchPCs       = IO(new BranchPCs)
  val branchEvalIn    = IO(new BranchEvalIn)
  val branchEvalOut   = IO(new BranchEvalOut)
  val retiredRenamedTable = IO(new RetiredRenamedTable)
  /**
   * Internal of the module goes here
   */
  /** ---------------------------------------------------------------------------------------------------------------------- */
  /** Initializing a buffer for storing the input values from the fetch unit */
  val inputBuffer = RegInit(new Bundle {
    val pc              = UInt(dataWidth.W)
    val instruction     = UInt(insAddrWidth.W)
    val predictedNextPC = UInt(dataWidth.W)
  }.Lit(
    _.pc              -> initialPC.U,            /** Initial value is set for the expectedPC */
    _.instruction     -> 0.U,
    _.predictedNextPC -> 0.U
  ))

  /** Initializing a buffer for storing the output values to the exec unit */
  val outputBuffer = RegInit(new Bundle {
    val instruction     = UInt(insAddrWidth.W)
    val pc              = UInt(dataWidth.W)
    val PRFDest         = UInt(PRFAddrWidth.W)
    val rs1Addr         = UInt(PRFAddrWidth.W)
    val rs2Addr         = UInt(PRFAddrWidth.W)
    val immediate       = UInt(dataWidth.W)
    val passFail        = Bool()
    val branchMask      = UInt(configuration.newBranchMaskWidth.W)  //leon coherency
    val branchEvalReady = Bool()
  }.Lit(
    _.instruction     -> 0.U,
    _.pc              -> 0.U,
    _.PRFDest         -> 0.U,
    _.rs1Addr         -> 0.U,
    _.rs2Addr         -> 0.U,
    _.immediate       -> 0.U,
    _.passFail        -> false.B,
    _.branchMask      -> 0.U,  //leon coherency
    _.branchEvalReady -> false.B
  ))

  val branchBuffer = RegInit({
    val init = Wire(new Bundle {
      val branchPCReady    = Bool()
      val predictedPCReady = Bool()
      val branchPC         = UInt(dataWidth.W)
      val predictedPC      = UInt(dataWidth.W)
      val branchMask       = Vec(configuration.newBranchMaskWidth, UInt(1.W))
    })
    init.branchPCReady    := false.B
    init.predictedPCReady := false.B
    init.branchPC         := 0.U
    init.predictedPC      := 0.U
    for (i <- 0 until configuration.newBranchMaskWidth - 1) {
      init.branchMask(i) := 0.U
    }
    init.branchMask(configuration.newBranchMaskWidth - 1) := 1.U
    init
  })

  /** Initializing some intermediate wires */

  val opcode = WireDefault(0.U(opcodeWidth.W))
  val rs1    = WireDefault(0.U(rs1Width.W))
  val rs2    = WireDefault(0.U(rs2Width.W))
  val rd     = WireDefault(0.U(rdWidth.W))
  val fun3   = WireDefault(0.U(3.W))

  val insType   = WireDefault(0.U(3.W))
  val immediate = WireDefault(0.U(dataWidth.W))

  val rs1Addr  = WireDefault(0.U(PRFAddrWidth.W))
  val rs2Addr  = WireDefault(0.U(PRFAddrWidth.W))
  val rs1Valid = WireDefault(false.B)
  val rs2Valid = WireDefault(false.B)

  val freeRegAddr = WireDefault(0.U(PRFAddrWidth.W))

  val branchTracker = RegInit(0.U(log2Ceil(configuration.branchMaskWidth + 1).W))

  val ins = WireDefault(0.U(insAddrWidth.W))
  val pc  = WireDefault(0.U(dataWidth.W))

  val validInputBuf  = WireDefault(false.B)     /** Valid signal of input buffer */
  val readyInputBuf  = WireDefault(false.B)     /** Ready signal of input buffer */
  val validOutputBuf = WireDefault(false.B)    /** Valid signal of output buffer */
  val readyOutputBuf = WireDefault(false.B)    /** Ready signal of output buffer */

  val expectedPC = RegInit((initialPC+4).U(dataWidth.W))
  val coherency = RegInit(false.B) //leon coherency

  /** Initializing states for the FSMs for input buffer and output buffer */
  val emptyState :: fullState :: Nil = Enum(2)      /** States of FSM */
  val stateRegInputBuf  = RegInit(emptyState)
  val stateRegOutputBuf = RegInit(emptyState)

  val stallReg = RegInit(false.B)
  val ecallPC = Reg(UInt(64.W))

  // Hardware JAL issue-fence state. Combinational demand is derived later,
  // once opcode/pc are known. See the block after getImmediate.
  val jalWaitTarget = RegInit(false.B)
  val jalWaitTgtPC  = RegInit(0.U(dataWidth.W))
  val jalSuppress   = RegInit(false.B)
  // Decode-side RAS. Fetch's RAS is updated on speculative I$ requests and
  // pipelineFlush only restores the stack pointer, so wrong-path CALL after
  // RET clobbers a live slot. Decode only sees handshake instructions and
  // already checkpoints rename on every CFI, so a RAS here is rolled back
  // with the rest of decode. RET/RETCALL can then use the JAL issue-fence
  // with an architectural return address (pass iff rasTop === rs1+imm).
  private val decRasDepth = 16
  private val decRasSpW   = log2Ceil(decRasDepth)
  private val decRasCntW  = log2Ceil(decRasDepth + 1)
  val decodeRas    = Reg(Vec(decRasDepth, UInt(dataWidth.W)))
  val decodeRasSp  = RegInit(0.U(decRasSpW.W))
  val decodeRasCnt = RegInit(0.U(decRasCntW.W))
  // Most recent AUIPC/LUI at decode, valid only for the immediately next
  // instruction. Used to compute a JALR target when the pair is auipc/lui
  // then jalr (the ABI far-call). Decode has no PRF read, so any other
  // JALR keeps the fetch snapshot.
  val lastUtypeValid = RegInit(false.B)
  val lastUtypeRd    = RegInit(0.U(rdWidth.W))
  val lastUtypeVal   = RegInit(0.U(dataWidth.W))

  // ─────────── PRF valid/free lists: packed, not Reg(Vec(N, Bool())) ──────────
  // Both are read AND written at a dynamic index. As a Vec, FIRRTL's LowerTypes
  // pass explodes each into PRFCount discrete registers and every dynamic read
  // becomes a PRFCount-deep mux tree; together with the reserved* checkpoints
  // these accounted for 24% of system.v. Packed as UInt they are one register
  // each. See the predictors.scala comment for the measured cost.
  //
  // The write sites CANNOT be converted one-by-one into read-modify-writes: a
  // Vec lets several `when`s write DIFFERENT indices in the same cycle and all
  // of them land, whereas `reg := f(reg)` makes the textually-last one win and
  // silently drops the rest. Worse, the whole-vector writes (the branch-recovery
  // ones) read the register's CURRENT value, so they discard the per-bit writes
  // above them. Both effects are reproduced exactly by collecting each site into
  // an accumulator below and resolving them once, in original textual order, at
  // the bottom of this file (search "PRF list resolve"):
  //
  //   valid : set(jump) -> clear(alloc) -> [restore | flush+arch] -> set(exec)
  //   free  : clear(collide) -> clear(alloc) -> [restore | flush\arch] -> set(retire)
  //
  // Anything new that writes these lists must add to an accumulator, never
  // assign the register directly, or it will be overwritten by the resolve.
  val PRFValidList = RegInit(
    (((BigInt(1) << regCount) - 1)).U(PRFCount.W))          // x0..x31 valid at reset

  private val validSetJump  = WireDefault(0.U(PRFCount.W))  // jump link writeback
  private val validClrAlloc = WireDefault(0.U(PRFCount.W))  // freshly renamed dest
  private val listRestore   = WireDefault(false.B)          // mispredict -> checkpoint 0
  private val listFlush     = WireDefault(false.B)          // mispredict -> architectural
  private val validSetExec  = WireDefault(0.U(PRFCount.W))  // exec1/2/3 writebacks
  private val freeClrCollide = WireDefault(0.U(PRFCount.W)) // rename/free collision
  private val freeClrAlloc   = WireDefault(0.U(PRFCount.W)) // freshly renamed dest
  private val freeSetRetire  = WireDefault(0.U(PRFCount.W)) // retired physical reg
  private val reservedFreeSet = WireDefault(0.U(PRFCount.W))// same, into every ckpt

  /** Storing instruction and pc in the fetch buffer */
  when(fromFetch.fired && readyInputBuf) {     /** Data from the fetch unit is valid and fetch buffer is ready */
    inputBuffer.instruction     := fromFetch.instruction
    inputBuffer.pc              := fromFetch.pc
    inputBuffer.predictedNextPC := fromFetch.predictedNextPC
    when(fromFetch.instruction(6,0) === system.U/*  && fromFetch.instruction(14,12) =/= 0.U */) {
      stallReg := true.B
      ecallPC := fromFetch.pc
    }
  }

  /** Storing values to the decode buffer */
  when(validInputBuf && readyOutputBuf) {     /** data from the fetch buffer is valid and decode buffer is ready */
    outputBuffer.instruction := ins
    outputBuffer.pc          := pc
    outputBuffer.PRFDest     := freeRegAddr
    outputBuffer.rs1Addr     := rs1Addr
    outputBuffer.rs2Addr     := rs2Addr
    outputBuffer.immediate   := immediate
  }

  // AUIPC/LUI result for a following JALR. Valid only until the next
  // instruction is transferred — so we pair auipc/lui; jalr and nothing else.
  when(validInputBuf && readyOutputBuf && !(branchEvalIn.fired && !branchEvalIn.passFail)) {
    when((opcode === lui.U || opcode === auipc.U) && rd.orR) {
      lastUtypeValid := true.B
      lastUtypeRd    := rd
      lastUtypeVal   := Mux(opcode === lui.U, immediate, pc + immediate)
    }.otherwise {
      lastUtypeValid := false.B
    }
  }

  outputBuffer.branchEvalReady := branchEvalIn.fired
  outputBuffer.passFail        := branchEvalIn.passFail
  outputBuffer.branchMask      := branchEvalIn.branchMask

  val branchPCMask = RegInit(0.U(configuration.newBranchMaskWidth.W))
  val branchReg    = RegInit(false.B)

  val stall = WireDefault(false.B)

  val isCSR = WireDefault(false.B)
  val waitToCommit = WireDefault(false.B)
  val issueRobBuff = RegInit(0.U(robAddrWidth.W))
  val commitRobBuf = RegInit(0.U(robAddrWidth.W))
  val csrDone = RegInit(false.B)

  val unconditionalJumps = WireDefault(false.B)
  val csrIns = WireDefault(false.B)

  val csrRobAddrReg = RegInit(0.U(robAddrWidth.W))
  val csrReadDataReg = RegInit(0.U(dataWidth.W))
  val csrFunc3Reg = RegInit(0.U(3.W))
  val csrAddrReg = RegInit(0.U(12.W))
  val csrImmReg = RegInit(0.U(dataWidth.W))
  val csrInsReg = RegInit(0.U(insAddrWidth.W))


  /** Assigning outputs */
  /** -------------------------------------------------------------------------------------------------------------------- */
  toExec.ready       := validOutputBuf
  toExec.instruction := outputBuffer.instruction
  toExec.pc          := outputBuffer.pc
  toExec.PRFDest     := outputBuffer.PRFDest
  toExec.rs1Addr     := outputBuffer.rs1Addr
  toExec.rs1Ready    := PRFValidList(outputBuffer.rs1Addr)
  toExec.rs2Addr     := outputBuffer.rs2Addr
  toExec.rs2Ready    := PRFValidList(outputBuffer.rs2Addr) || Seq(itype.U, utype.U, jtype.U).map(_ === getInsType(outputBuffer.instruction(6,0))).reduce(_ || _)
  toExec.immediate   := outputBuffer.immediate
  toExec.branchMask  := branchBuffer.branchMask.asUInt

  fromFetch.ready          := readyInputBuf
  fromFetch.expected.coherency := coherency //leon coherency

  jumpAddrWrite.ready    := validOutputBuf && (unconditionalJumps || csrIns)
  jumpAddrWrite.PRFDest  := outputBuffer.PRFDest
  when(unconditionalJumps) {
    jumpAddrWrite.linkAddr := VecInit(
      (outputBuffer.pc + Cat(Fill(32, outputBuffer.instruction(31)), outputBuffer.instruction(31, 12), 0.U(12.W))),
      Cat(Fill(32, outputBuffer.instruction(31)), outputBuffer.instruction(31, 12), 0.U(12.W)),
      0.U,
      outputBuffer.pc + 4.U)(outputBuffer.instruction(6, 5)) //outputBuffer.pc + 4.U
  }.otherwise {
    jumpAddrWrite.linkAddr := csrReadDataReg
  }


  branchPCs.ready            := branchBuffer.branchPCReady || branchBuffer.predictedPCReady
  branchPCs.branchPCReady    := branchBuffer.branchPCReady
  branchPCs.predictedPCReady := branchBuffer.predictedPCReady
  branchPCs.branchPC         := branchBuffer.branchPC
  branchPCs.predictedPC      := branchBuffer.predictedPC
  branchPCs.branchMask       := branchPCMask

  branchEvalOut.ready      := outputBuffer.branchEvalReady
  branchEvalOut.branchMask := outputBuffer.branchMask
  branchEvalOut.passFail   := outputBuffer.passFail

  branchEvalIn.ready    := true.B
  writeBackResult.ready := true.B
  /** -------------------------------------------------------------------------------------------------------------------- */

  ins := inputBuffer.instruction
  pc  := inputBuffer.pc

  opcode := ins(6, 0)
  rs1    := ins(19, 15)
  rs2    := ins(24, 20)
  rd     := ins(11, 7)
  fun3   := ins(14, 12)

  insType   := getInsType(opcode)                   /** Deciding the instruction type */
  immediate := getImmediate(ins, insType)         /** Calculating the immediate value */

  // Hardware JAL issue-fence (not a software fence.i). JAL's target is
  // pc+imm, known at decode. Until fetch delivers that PC:
  //   1. overlay expected so sequential mismatches and fetch redirects
  //   2. drop ready so the handshake cannot sneak a younger insn in
  //   3. drop a younger insn that already sat in the input buffer
  // predictedPC is the architectural target, so execute PASSES and does
  // not squash the redirected path. Overlay only after the JAL is in the
  // input buffer — doing it on fromFetch would reject the JAL itself.
  //
  // Recovery (mispredict / mret / ecall / interrupt / illegal) wins the
  // expected PC. A sticky overlay after the target has already been
  // accepted would redirect target+4 back to the target forever.
  //
  // Backward-branch BTFNT (same recipe on cjump && imm<0) was tried and
  // reverted: Linux hung after bootconsole with C0 IPC 0.55 / 99.6% BPred,
  // the same fake-pass shape as 1-in-flight CFI. JAL is unconditional so
  // the taken target is always architectural; a conditional's taken target
  // is not.
  val jalTarget = pc + immediate
  // JALR target is (rs1+imm)&~1. Known at decode only for x0 or the
  // immediately preceding AUIPC/LUI that wrote rs1.
  val jalrBase = Mux(rs1 === 0.U, 0.U(dataWidth.W), lastUtypeVal)
  val jalrKnown = (opcode === jumpr.U) &&
                  ((rs1 === 0.U) || (lastUtypeValid && (rs1 === lastUtypeRd)))
  // Match execute's JALR next-PC (rs1+imm, no ~1) so a fence pass is a
  // real pass. Spec alignment is the same value on this 4-byte-only core.
  val jalrTarget = jalrBase + immediate
  // Backward conditional, sources already in the PRF: same recipe as JAL.
  // Ungated BTFNT hung Linux (C0 0.55 / 99.6% BPred, stuck at bootconsole)
  // because a branch can execute with stale rs1 and PASS a taken prediction
  // that is not architectural. PRF-valid means the scheduler will issue
  // with the real operands, so a pass is a real pass. Forward branches
  // and not-ready sources keep the fetch snapshot (fail+recover is safe).
  val brSrcReady = PRFValidList(rs1Addr) && PRFValidList(rs2Addr)
  val brTakenKnown = (opcode === cjump.U) && immediate(63).asBool && brSrcReady
  val brTarget = pc + immediate
  val rdLink  = (rd === 1.U) || (rd === 5.U)
  val rs1Link = (rs1 === 1.U) || (rs1 === 5.U)
  val isCall = ((opcode === jump.U) && rdLink) ||
               ((opcode === jumpr.U) && rdLink && !(rs1Link && (rd =/= rs1)))
  val isRet = (opcode === jumpr.U) && rs1Link && !rdLink
  val isRetCall = (opcode === jumpr.U) && rdLink && rs1Link && (rd =/= rs1)
  val rasEmpty = decodeRasCnt === 0.U
  val rasTop   = decodeRas(decodeRasSp - 1.U)
  val retKnown = (isRet || isRetCall) && !rasEmpty
  val knownTarget = (opcode === jump.U) || jalrKnown || brTakenKnown || retKnown
  val knownTargetPC = Mux(opcode === jump.U, jalTarget,
                      Mux(jalrKnown, jalrTarget,
                      Mux(retKnown, rasTop, brTarget)))
  val jalInBuf  = (stateRegInputBuf === fullState) && knownTarget
  val jalArchRedirect =
    (branchEvalIn.fired && !branchEvalIn.passFail) ||
    (writeBackResult.fired && writeBackResult.instruction(6, 0) === system.U &&
      writeBackResult.instruction(14, 12) === 0.U) ||
    (writeBackResult.fired && writeBackResult.instruction(1, 0) =/= "b11".U)
  val jalDemand   = (jalInBuf || jalWaitTarget) && !jalArchRedirect && !jalSuppress
  val jalDemandPC = Mux(jalInBuf, knownTargetPC, jalWaitTgtPC)
  fromFetch.expected.valid := (expectedPC =/= 0.U) || jalDemand
  fromFetch.expected.pc    := Mux(jalDemand, jalDemandPC, expectedPC)

  unconditionalJumps := outputBuffer.instruction(6,0) === jump.U || outputBuffer.instruction(6,0) === jumpr.U || outputBuffer.instruction(6,0) === lui.U || outputBuffer.instruction(6,0) === auipc.U
  csrIns := outputBuffer.instruction(6,0) === system.U && outputBuffer.instruction(14,12) =/= 0.U

  val frontEndRegMap      = RegInit(VecInit(Seq.tabulate(regCount)(i => i.U(PRFAddrWidth.W))))
  val architecturalRegMap = RegInit(VecInit(Seq.tabulate(regCount)(i => i.U(PRFAddrWidth.W))))
  val PRFFreeList         = RegInit(
    (((BigInt(1) << PRFCount) - 1) ^ ((BigInt(1) << regCount) - 1)).U(PRFCount.W)) // p32..p63 free at reset

  var i = 0;
  for (i <- 0 to 31) {
    retiredRenamedTable.table(i) := architecturalRegMap(i)
  }

  // One rename/freelist/valid snapshot per in-flight branch. Branches resolve
  // in order, so a hit restores slot 0 and a pass shifts the queue down.
  private val nCheckpoints = configuration.branchMaskWidth
  val reservedRegMap    = Reg(Vec(nCheckpoints, frontEndRegMap.cloneType))
  val reservedFreeList  = Reg(Vec(nCheckpoints, PRFFreeList.cloneType))
  val reservedValidList = Reg(Vec(nCheckpoints, PRFValidList.cloneType))
  val reservedRas       = Reg(Vec(nCheckpoints, decodeRas.cloneType))
  val reservedRasSp     = Reg(Vec(nCheckpoints, decodeRasSp.cloneType))
  val reservedRasCnt    = Reg(Vec(nCheckpoints, decodeRasCnt.cloneType))
  // Staging for reservedFreeList: the shift and the snapshot below write whole
  // checkpoints, but a retiring writeback then sets one bit in EVERY checkpoint
  // (reservedFreeSet). As a Vec those were independent element writes; packed,
  // the bit-set has to be applied on top of whatever the shift/snapshot chose,
  // so they write this wire and the resolve at the bottom ORs the bit in.
  // reservedValidList needs no staging — nothing writes it after the snapshot.
  private val reservedFreeNext = Wire(Vec(nCheckpoints, UInt(PRFCount.W)))
  reservedFreeNext := reservedFreeList

  rs1Addr  := frontEndRegMap(rs1)
  rs2Addr  := frontEndRegMap(rs2)

  freeRegAddr := PriorityEncoder(PRFFreeList)

  // Stall a new CFI only when every mask slot is taken. The 2-in-flight cap
  // was a bandage for a 5% predictor filling the ROB with wrong-path; with
  // BTB-hit=taken those slots are useful overlap. 1-in-flight hung Linux.
  private val cfiOpcode = Seq(jump.U, jumpr.U, cjump.U).map(_ === opcode).reduce(_ || _)
  private val cfiMaskOccupied =
    (branchBuffer.branchMask.asUInt & ((BigInt(1) << configuration.branchMaskWidth) - 1).U) ===
      ((BigInt(1) << configuration.branchMaskWidth) - 1).U
  when(freeRegAddr === 63.U || (cfiOpcode && cfiMaskOccupied)) {
    stall := true.B
  }

  when(rs1Addr === freeRegAddr || rs2Addr === freeRegAddr) {
    stall := true.B
    when(rs1Addr === freeRegAddr) {
      freeClrCollide := UIntToOH(rs1Addr, PRFCount)
    }.elsewhen(rs2Addr === freeRegAddr) {
      freeClrCollide := UIntToOH(rs2Addr, PRFCount)
    }
  }

  // Younger than a waiting JAL must not transfer input→output (rename + ROB
  // allocate). stall already gates that path; the drop at the input FSM then
  // empties the buffer so this does not stick.
  when(jalWaitTarget && (stateRegInputBuf === fullState) &&
       !knownTarget && (pc =/= jalWaitTgtPC)) {
    stall := true.B
  }
  // Do not stall backward cjump until brSrcReady. Tried; Linux hung after
  // bootconsole (C0 0.27 / 96–98% BPred, C1/C2 identical idle). Same
  // fake-pass shape as ungated BTFNT: predictedPC = taken target lets
  // execute PASS. Keep brTakenKnown for the rare already-ready case.

  /**
    * Attribution of the rename stalls above, for profiling only — no logic
    * downstream reads these.
    *
    * `stall` is what keeps decode from accepting the next instruction from
    * fetch, and it shows up in the profile twice over: it drops validInputBuf
    * (so decode has nothing for the backend) and it deasserts readyInputBuf
    * (so fetch is refused). Knowing *which* of the three terms fires is the
    * difference between widening the PRF, widening the branch mask, and fixing
    * the free-list collision — so split it here.
    *
    * Reported with the same priority the `when` blocks above impose, so the
    * three counts sum to the number of stalled cycles rather than
    * double-counting a cycle where several terms are true at once. Gated on the
    * state in which `stall` actually blocks (input buffer full, not being
    * flushed by a mispredict), so idle and squash cycles are not charged.
    */
  private val prfExhausted = freeRegAddr === 63.U
  private val branchMaskFull = cfiOpcode && cfiMaskOccupied
  private val renameCollision = (rs1Addr === freeRegAddr) || (rs2Addr === freeRegAddr)
  private val stallBlocking =
    (stateRegInputBuf === fullState) && !(branchEvalIn.fired && !branchEvalIn.passFail)

  val stallReason = IO(Output(new Bundle {
    val prfExhausted    = Bool()
    val branchMaskFull  = Bool()
    val renameCollision = Bool()
  }))
  stallReason.prfExhausted    := stallBlocking && prfExhausted
  stallReason.branchMaskFull  := stallBlocking && !prfExhausted && branchMaskFull
  stallReason.renameCollision := stallBlocking && !prfExhausted && !branchMaskFull && renameCollision

  when(jumpAddrWrite.fired && outputBuffer.instruction(11,7) =/= 0.U) {
    validSetJump := UIntToOH(outputBuffer.PRFDest, PRFCount)
  }

  when(validInputBuf && readyOutputBuf && (insType === itype.U || insType === rtype.U || insType === utype.U || insType === jtype.U) && rd =/= 0.U) {
    when(!branchEvalIn.fired || branchEvalIn.passFail){
      freeClrAlloc              := UIntToOH(freeRegAddr, PRFCount)
      validClrAlloc             := UIntToOH(freeRegAddr, PRFCount)
      frontEndRegMap(rd)        := freeRegAddr
    }
  }

  val LoadMask = ((BigInt(1) << configuration.branchMaskWidth) - 1).U(configuration.newBranchMaskWidth.W)

  when(branchEvalIn.fired) {
    branchTracker := branchTracker - 1.U

    //leon coherency
    branchBuffer.branchMask := VecInit(((branchBuffer.branchMask.asUInt & ~LoadMask) | ((branchBuffer.branchMask.asUInt & (~branchEvalIn.branchMask)) & LoadMask)).asBools)

    when(!branchEvalIn.passFail) {
      branchReg := false.B
      jalWaitTarget := false.B

      for (i <- 0 until configuration.newBranchMaskWidth - 1) {
        branchBuffer.branchMask(i) := 0.U
      }
      branchBuffer.branchMask(configuration.newBranchMaskWidth - 1) := 1.U

      expectedPC := branchEvalIn.targetPC

      when(branchEvalIn.branchMask(configuration.branchMaskWidth - 1, 0).orR){
        frontEndRegMap := reservedRegMap(0)
        // was: PRF{Free,Valid}List := reserved*(0) zip current map (_ | _)
        listRestore := true.B
        decodeRas    := reservedRas(0)
        decodeRasSp  := reservedRasSp(0)
        decodeRasCnt := reservedRasCnt(0)
      }.otherwise{
        frontEndRegMap := architecturalRegMap
        coherency := true.B  //leon coherency
        // was: free := all-ones then clear arch; valid := all-zero then set arch
        listFlush := true.B
        decodeRasSp  := 0.U
        decodeRasCnt := 0.U
      }

      branchTracker := 0.U
    }.otherwise {
      for (i <- 0 until nCheckpoints - 1) {
        reservedRegMap(i)    := reservedRegMap(i + 1)
        reservedFreeNext(i)  := reservedFreeList(i + 1)
        reservedValidList(i) := reservedValidList(i + 1)
        reservedRas(i)       := reservedRas(i + 1)
        reservedRasSp(i)     := reservedRasSp(i + 1)
        reservedRasCnt(i)    := reservedRasCnt(i + 1)
      }
    }
  }

  val bitPosition = PriorityEncoder(~branchBuffer.branchMask.asUInt)

  when(validInputBuf && readyOutputBuf) {
    when(opcode === jump.U || opcode === jumpr.U || opcode === cjump.U) {
      branchReg := true.B
      branchBuffer.branchPC := pc
      // JAL, and JALR whose rs1 is x0 or the previous AUIPC/LUI: architectural
      // target. Sequential never allocated (jalDemand refused it), so execute
      // may pass without a squash. Other CFIs keep the fetch snapshot.
      branchBuffer.predictedPC := Mux(knownTarget, knownTargetPC, inputBuffer.predictedNextPC)
      branchBuffer.branchMask(bitPosition) := 1.U
      branchPCMask := (1.U(configuration.newBranchMaskWidth.W) << bitPosition)

      val snapMap   = WireDefault(frontEndRegMap)
      val snapFree  = WireDefault(PRFFreeList)
      val snapValid = WireDefault(PRFValidList)
      when(opcode(2).asBool && rd.orR) {
        snapMap(rd) := freeRegAddr
        val allocOH = UIntToOH(freeRegAddr, PRFCount)
        snapFree    := PRFFreeList  & (~allocOH).asUInt
        snapValid   := PRFValidList & (~allocOH).asUInt
      }
      reservedRegMap(branchTracker)    := snapMap
      reservedFreeNext(branchTracker)  := snapFree
      reservedValidList(branchTracker) := snapValid

      // RAS push/pop on this CFI, then snapshot the *next* stack so a
      // restore keeps this instruction's effect and drops younger ones.
      val doPop  = (isRet || isRetCall) && !rasEmpty
      val doPush = isCall || isRetCall
      val spAfterPop  = Mux(doPop, decodeRasSp - 1.U, decodeRasSp)
      val cntAfterPop = Mux(doPop, decodeRasCnt - 1.U, decodeRasCnt)
      val nextSp  = Mux(doPush, spAfterPop + 1.U, spAfterPop)
      val nextCnt = Mux(doPush,
        Mux(cntAfterPop === decRasDepth.U, cntAfterPop, cntAfterPop + 1.U),
        cntAfterPop)
      val rasSnap = Wire(decodeRas.cloneType)
      rasSnap := decodeRas
      when(doPush) { rasSnap(spAfterPop) := pc + 4.U }
      reservedRas(branchTracker)    := rasSnap
      reservedRasSp(branchTracker)  := nextSp
      reservedRasCnt(branchTracker) := nextCnt
      // A same-cycle mispredict restore (above) must win; do not push/pop
      // onto a stack we are about to roll back.
      when(!(branchEvalIn.fired && !branchEvalIn.passFail)) {
        when(doPush) { decodeRas(spAfterPop) := pc + 4.U }
        decodeRasSp  := nextSp
        decodeRasCnt := nextCnt
      }

      branchTracker := branchTracker + 1.U
    }.otherwise {
      branchReg := false.B
    }
  }

  // Lockstep: enqueue the fetch-time next-PC with the CFI itself. The old
  // path waited for the *next* decoded instruction (branchReg delay) and
  // left predictedPCs empty when that successor lost the race to execute.
  branchBuffer.branchPCReady := (opcode === cjump.U || opcode === jump.U || opcode === jumpr.U) && validInputBuf && readyOutputBuf
  branchBuffer.predictedPCReady := (opcode === cjump.U || opcode === jump.U || opcode === jumpr.U) && validInputBuf && readyOutputBuf

  when(jalArchRedirect) {
    jalWaitTarget  := false.B
    jalSuppress    := true.B
    lastUtypeValid := false.B
  }.otherwise {
    when(jalInBuf) {
      jalWaitTgtPC := knownTargetPC
      val jalTgtHere = fromFetch.fired && (fromFetch.pc === knownTargetPC)
      jalWaitTarget := !jalTgtHere
      // Same-cycle BTB/RAS hit already handshaked the target. Leaving
      // expectedPC at the target would redirect the next sequential fetch
      // (target+4) back to the target forever. 0 = open-loop, matching
      // the existing accept path.
      when(jalTgtHere) {
        expectedPC := 0.U
      }.otherwise {
        expectedPC := knownTargetPC
      }
    }.elsewhen(jalWaitTarget && fromFetch.fired && (fromFetch.pc === jalWaitTgtPC)) {
      jalWaitTarget := false.B
    }
  }
  when(expectedPC =/= 0.U && fromFetch.fired && fromFetch.expected.pc === fromFetch.pc) {
    expectedPC  := 0.U
    jalSuppress := false.B
    coherency   := false.B //leon coherency
  }
  // Last-connect: a recovery this cycle must keep suppress on even if the
  // outgoing expectedPC happened to match a fetch (mispredict sets ready low,
  // but traps do not).
  when(jalArchRedirect) {
    jalSuppress := true.B
  }

  when(toExec.fired) { issueRobBuff := toExec.robAddr }
  when(writeBackResult.fired) { commitRobBuf := writeBackResult.robAddr }

  isCSR := outputBuffer.instruction(6,0) === system.U && outputBuffer.instruction(14,12) =/= 0.U && toExec.fired

  val ustatus     = RegInit(0.U(dataWidth.W))
  val utvec       = RegInit(0.U(dataWidth.W))
  val uepc        = RegInit(0.U(dataWidth.W))
  val ucause      = RegInit(0.U(dataWidth.W))
  val scounteren  = RegInit(0.U(dataWidth.W))
  val satp        = RegInit(0.U(dataWidth.W))
  val mstatus     = RegInit(0.U(dataWidth.W))
  val misa        = RegInit(0.U(dataWidth.W))
  val medeleg     = RegInit(0.U(dataWidth.W))
  val mideleg     = RegInit(0.U(dataWidth.W))
  val mie         = RegInit(0.U(dataWidth.W))
  val mtvec       = RegInit(0.U(dataWidth.W))
  val mcounteren  = RegInit(0.U(dataWidth.W))
  val mscratch    = RegInit(0.U(dataWidth.W))
  val mepc        = RegInit(0.U(dataWidth.W))
  val mcause      = RegInit(0.U(dataWidth.W))
  val mtval       = RegInit(0.U(dataWidth.W))
  val mip         = RegInit(0.U(dataWidth.W))
  // Hardware interrupt lines from the CLINT (driven by core.scala). Per RISC-V,
  // mip.MTIP (bit 7) and mip.MSIP (bit 3) are READ-ONLY views of these lines —
  // software clears them by writing the CLINT (mtimecmp / msip), never mip. So
  // reads of mip below splice the live lines into bits 7 and 3.
  val mtipLine = IO(Input(Bool()))
  val msipLine = IO(Input(Bool()))
  val mipLive  = Cat(mip(63,8), mtipLine, mip(6,4), msipLine, mip(2,0))
  val pmpcfg0     = RegInit(0.U(dataWidth.W))
  val pmpaddr0    = RegInit(0.U(dataWidth.W))
  val mvendorid   = RegInit(0.U(dataWidth.W))
  val marchid     = RegInit(0.U(dataWidth.W))
  val mimpid      = RegInit(0.U(dataWidth.W))
  val mhartid     = RegInit(mhart_id.U(dataWidth.W))

  mstatus := (mstatus & "h0000000000001888".U) | "h0000000a00000000".U // FIX ME: deasserting illegal bits should be blocked when bit calculating
  misa := "h101101".U | (1.U(64.W) << 63)

  when(isCSR) {
    csrRobAddrReg := toExec.robAddr
    csrFunc3Reg   := outputBuffer.instruction(14,12)
    csrAddrReg    := outputBuffer.immediate
    csrImmReg     := outputBuffer.instruction(19,15) & "h0000_0000_0000_001f".U
    csrInsReg     := outputBuffer.instruction

  }

  when(opcode === system.U && fun3 =/= 0.U && validInputBuf && readyOutputBuf) {
    switch(immediate & "hfff".U) {
      is("h000".U) { csrReadDataReg := ustatus }
      is("h005".U) { csrReadDataReg := utvec }
      is("h041".U) { csrReadDataReg := uepc }
      is("h042".U) { csrReadDataReg := ucause }
      is("h106".U) { csrReadDataReg := scounteren }
      is("h180".U) { csrReadDataReg := satp }
      is("h300".U) { csrReadDataReg := mstatus }
      is("h301".U) { csrReadDataReg := misa }
      is("h302".U) { csrReadDataReg := medeleg }
      is("h303".U) { csrReadDataReg := mideleg }
      is("h304".U) { csrReadDataReg := mie }
      is("h305".U) { csrReadDataReg := mtvec }
      is("h306".U) { csrReadDataReg := mcounteren }
      is("h340".U) { csrReadDataReg := mscratch }
      is("h341".U) { csrReadDataReg := mepc }
      is("h342".U) { csrReadDataReg := mcause }
      is("h343".U) { csrReadDataReg := mtval }
      is("h344".U) { csrReadDataReg := mipLive }  // mip with live MTIP/MSIP lines
      is("h3a0".U) { csrReadDataReg := pmpcfg0 }
      is("h3b0".U) { csrReadDataReg := pmpaddr0 }
      is("hf11".U) { csrReadDataReg := mvendorid }
      is("hf12".U) { csrReadDataReg := marchid }
      is("hf13".U) { csrReadDataReg := mimpid }
      is("hf14".U) { csrReadDataReg := mhartid }
    }
  }

  val csrWriteData = WireDefault(0.U(dataWidth.W))
  when(writeBackResult.fired && writeBackResult.instruction(6,0) === system.U) {
    stallReg := false.B
  }

  when(writeBackResult.fired && writeBackResult.instruction(6,0) === system.U && writeBackResult.instruction(14,12) =/= 0.U) {
    
    csrWriteData := writeBackResult.data
    switch(writeBackResult.instruction(14,12)) {
      is("b001".U) {
        switch(csrAddrReg & "hfff".U) {
          is("h000".U) { ustatus    := csrWriteData }
          is("h005".U) { utvec      := csrWriteData }
          is("h041".U) { uepc       := csrWriteData }
          is("h042".U) { ucause     := csrWriteData }
          is("h106".U) { scounteren := csrWriteData }
          is("h180".U) { satp       := csrWriteData }
          is("h300".U) { mstatus    := csrWriteData }
          is("h301".U) { misa       := csrWriteData }
          is("h302".U) { medeleg    := csrWriteData }
          is("h303".U) { mideleg    := csrWriteData }
          is("h304".U) { mie        := csrWriteData }
          is("h305".U) { mtvec      := csrWriteData }
          is("h306".U) { mcounteren := csrWriteData }
          is("h340".U) { mscratch   := csrWriteData }
          is("h341".U) { mepc       := csrWriteData }
          is("h342".U) { mcause     := csrWriteData }
          is("h343".U) { mtval      := csrWriteData }
          is("h344".U) { mip        := csrWriteData }
          is("h3a0".U) { pmpcfg0    := csrWriteData }
          is("h3b0".U) { pmpaddr0   := csrWriteData }
          is("hf11".U) { mvendorid  := csrWriteData }
          is("hf12".U) { marchid    := csrWriteData }
          is("hf13".U) { mimpid     := csrWriteData }
          is("hf14".U) { mhartid    := csrWriteData }
        }
      }
      is("b010".U) {
        switch(csrAddrReg & "hfff".U) {
          is("h000".U) { ustatus     := ustatus | csrWriteData }
          is("h005".U) { utvec       := utvec | csrWriteData }
          is("h041".U) { uepc        := uepc | csrWriteData }
          is("h042".U) { ucause      := ucause | csrWriteData }
          is("h106".U) { scounteren  := scounteren | csrWriteData }
          is("h180".U) { satp        := satp | csrWriteData }
          is("h300".U) { mstatus     := mstatus | csrWriteData }
          is("h301".U) { misa        := misa | csrWriteData }
          is("h302".U) { medeleg     := medeleg | csrWriteData }
          is("h303".U) { mideleg     := mideleg | csrWriteData }
          is("h304".U) { mie         := mie  | csrWriteData }
          is("h305".U) { mtvec       := mtvec | csrWriteData }
          is("h306".U) { mcounteren  := mcounteren | csrWriteData }
          is("h340".U) { mscratch    := mscratch | csrWriteData }
          is("h341".U) { mepc        := mepc | csrWriteData }
          is("h342".U) { mcause      := mcause | csrWriteData }
          is("h343".U) { mtval       := mtval | csrWriteData }
          is("h344".U) { mip         := mip | csrWriteData }
          is("h3a0".U) { pmpcfg0     := pmpcfg0 | csrWriteData }
          is("h3b0".U) { pmpaddr0    := pmpaddr0 | csrWriteData }
          is("hf11".U) { mvendorid   := mvendorid | csrWriteData }
          is("hf12".U) { marchid     := marchid | csrWriteData }
          is("hf13".U) { mimpid      := mimpid | csrWriteData }
          is("hf14".U) { mhartid     := mhartid | csrWriteData }
        }
      }
      is("b011".U) {
        switch(csrAddrReg & "hfff".U) {
          is("h000".U) { ustatus     := ustatus & ~csrWriteData }
          is("h005".U) { utvec       := mtvec & ~csrWriteData }
          is("h041".U) { uepc        := uepc & ~csrWriteData }
          is("h042".U) { ucause      := ucause & ~csrWriteData }
          is("h106".U) { scounteren  := scounteren & ~csrWriteData }
          is("h180".U) { satp        := satp & ~csrWriteData }
          is("h300".U) { mstatus     := mstatus & ~csrWriteData }
          is("h301".U) { misa        := misa & ~csrWriteData }
          is("h302".U) { medeleg     := medeleg & ~csrWriteData }
          is("h303".U) { mideleg     := mideleg & ~csrWriteData }
          is("h304".U) { mie         := mie & ~csrWriteData }
          is("h305".U) { mtvec       := mtvec & ~csrWriteData }
          is("h306".U) { mcounteren  := mcounteren & ~csrWriteData }
          is("h340".U) { mscratch    := mscratch & ~csrWriteData }
          is("h341".U) { mepc        := mepc & ~csrWriteData }
          is("h342".U) { mcause      := mcause & ~csrWriteData }
          is("h343".U) { mtval       := mtval & ~csrWriteData }
          is("h344".U) { mip         := mip & ~csrWriteData }
          is("h3a0".U) { pmpcfg0     := pmpcfg0 & ~csrWriteData }
          is("h3b0".U) { pmpaddr0    := pmpaddr0 & ~csrWriteData }
          is("hf11".U) { mvendorid   := mvendorid & ~csrWriteData }
          is("hf12".U) { marchid     := marchid & ~csrWriteData }
          is("hf13".U) { mimpid      := mimpid & ~csrWriteData }
          is("hf14".U) { mhartid     := mhartid & ~csrWriteData }
        }
      }
      is("b101".U) {
        switch(csrAddrReg & "hfff".U) {
          is("h000".U) { ustatus     := csrImmReg }
          is("h005".U) { utvec       := csrImmReg }
          is("h041".U) { uepc        := csrImmReg }
          is("h042".U) { ucause      := csrImmReg }
          is("h106".U) { scounteren  := csrImmReg }
          is("h180".U) { satp        := csrImmReg }
          is("h300".U) { mstatus     := csrImmReg }
          is("h301".U) { misa        := csrImmReg }
          is("h302".U) { medeleg     := csrImmReg }
          is("h303".U) { mideleg     := csrImmReg }
          is("h304".U) { mie         := csrImmReg }
          is("h305".U) { mtvec       := csrImmReg }
          is("h306".U) { mcounteren  := csrImmReg }
          is("h340".U) { mscratch    := csrImmReg }
          is("h341".U) { mepc        := csrImmReg }
          is("h342".U) { mcause      := csrImmReg }
          is("h343".U) { mtval       := csrImmReg }
          is("h344".U) { mip         := csrImmReg }
          is("h3a0".U) { pmpcfg0     := csrImmReg }
          is("h3b0".U) { pmpaddr0    := csrImmReg }
          is("hf11".U) { mvendorid   := csrImmReg }
          is("hf12".U) { marchid     := csrImmReg }
          is("hf13".U) { mimpid      := csrImmReg }
          is("hf14".U) { mhartid     := csrImmReg }
        }
      }
      is("b110".U) {
        switch(csrAddrReg & "hfff".U) {
          is("h000".U) { ustatus     := ustatus | csrImmReg }
          is("h005".U) { utvec       := utvec | csrImmReg }
          is("h041".U) { uepc        := uepc | csrImmReg }
          is("h042".U) { ucause      := ucause | csrImmReg }
          is("h106".U) { scounteren  := scounteren | csrImmReg }
          is("h180".U) { satp        := satp | csrImmReg }
          is("h300".U) { mstatus     := mstatus | csrImmReg }
          is("h301".U) { misa        := misa | csrImmReg }
          is("h302".U) { medeleg     := medeleg | csrImmReg }
          is("h303".U) { mideleg     := mideleg | csrImmReg }
          is("h304".U) { mie         := mie | csrImmReg }
          is("h305".U) { mtvec       := mtvec | csrImmReg }
          is("h306".U) { mcounteren  := mcounteren | csrImmReg }
          is("h340".U) { mscratch    := mscratch | csrImmReg }
          is("h341".U) { mepc        := mepc | csrImmReg }
          is("h342".U) { mcause      := mcause | csrImmReg }
          is("h343".U) { mtval       := mtval | csrImmReg }
          is("h344".U) { mip         := mip | csrImmReg }
          is("h3a0".U) { pmpcfg0     := pmpcfg0 | csrImmReg }
          is("h3b0".U) { pmpaddr0    := pmpaddr0 | csrImmReg }
          is("hf11".U) { mvendorid   := mvendorid | csrImmReg }
          is("hf12".U) { marchid     := marchid | csrImmReg }
          is("hf13".U) { mimpid      := mimpid | csrImmReg }
          is("hf14".U) { mhartid     := mhartid | csrImmReg }
        }
      }
      is("b111".U) {
        switch(csrAddrReg & "hfff".U) {
          is("h000".U) { ustatus     := ustatus & ~csrImmReg }
          is("h005".U) { utvec       := utvec & ~csrImmReg }
          is("h041".U) { uepc        := uepc & ~csrImmReg }
          is("h042".U) { ucause      := ucause & ~csrImmReg }
          is("h106".U) { scounteren  := scounteren & ~csrImmReg }
          is("h180".U) { satp        := satp & ~csrImmReg }
          is("h300".U) { mstatus     := mstatus & ~csrImmReg }
          is("h301".U) { misa        := misa & ~csrImmReg }
          is("h302".U) { medeleg     := medeleg & ~csrImmReg }
          is("h303".U) { mideleg     := mideleg & ~csrImmReg }
          is("h304".U) { mie         := mie & ~csrImmReg }
          is("h305".U) { mtvec       := mtvec & ~csrImmReg }
          is("h306".U) { mcounteren  := mcounteren & ~csrImmReg }
          is("h340".U) { mscratch    := mscratch & ~csrImmReg }
          is("h341".U) { mepc        := mepc & ~csrImmReg }
          is("h342".U) { mcause      := mcause & ~csrImmReg }
          is("h343".U) { mtval       := mtval & ~csrImmReg }
          is("h344".U) { mip         := mip & ~csrImmReg }
          is("h3a0".U) { pmpcfg0     := pmpcfg0 & ~csrImmReg }
          is("h3b0".U) { pmpaddr0    := pmpaddr0 & ~csrImmReg }
          is("hf11".U) { mvendorid   := mvendorid & ~csrImmReg }
          is("hf12".U) { marchid     := marchid & ~csrImmReg }
          is("hf13".U) { mimpid      := mimpid & ~csrImmReg }
          is("hf14".U) { mhartid     := mhartid & ~csrImmReg }
        }
      }
    }
  }
  val interruptedPC = IO(Input(UInt(64.W)))
  // Set by core.scala while the in-flight injected interrupt is a software (IPI)
  // interrupt rather than a timer interrupt, so the trap below reports the right
  // mcause (machine software int = 3 vs machine timer int = 7). Declared here
  // (before the trap logic that reads it) to avoid a forward reference.
  val softwareInterruptInject = IO(Input(Bool()))
  val currentPrivilege = RegInit(MMODE.U(dataWidth.W))
  when(writeBackResult.fired && writeBackResult.instruction(6,0) === system.U && writeBackResult.instruction(14,12) === 0.U) {
    when(writeBackResult.instruction(31, 20) === "h302".U) {
      // mret
      currentPrivilege := VecInit(UMODE.U, MMODE.U)(mstatus(12))
      expectedPC := mepc
      mstatus := Cat("h0000000A00000".U(52.W), "h08".U(8.W), mstatus(7, 4))
    }.elsewhen(!writeBackResult.instruction(31, 20).orR) {
      // ecall
      mepc := ecallPC
      when(currentPrivilege === MMODE.U) { mcause := 11.U }
      .otherwise { mcause := 8.U }
      currentPrivilege := MMODE.U
      expectedPC := mtvec
      mstatus := "h0000000A00000000".U(64.W) | Cat(0.U(51.W), Mux(currentPrivilege===MMODE.U, "b11000".U(5.W), 0.U(5.W)), mstatus(3, 0), 0.U(4.W))
    }.elsewhen(writeBackResult.instruction === "h80000073".U(64.W)) {
      // interrupt
      // stallReg is asserted due to interrupt being injected from fromFetch interface
      mepc := Mux(stallReg, ecallPC, interruptedPC)
      // machine software interrupt (cause 3) for IPIs, else machine timer (7)
      mcause := Mux(softwareInterruptInject, "h8000000000000003".U, "h8000000000000007".U)
      currentPrivilege := MMODE.U
      expectedPC := mtvec
      mstatus := "h0000000A00000000".U(64.W) | Cat(0.U(51.W), Mux(currentPrivilege===MMODE.U, "b11000".U(5.W), 0.U(5.W)), mstatus(3, 0), 0.U(4.W))
    }
  }

  // Illegal-instruction trap (mcause=2). Mutually exclusive with the SYSTEM
  // block above: every real instruction -- including SYSTEM (0x73) -- has
  // bits[1:0]=="11", so this only fires on a word that is not an instruction.
  // The ROB retires such a word with exceptionOccurred instead of waiting for a
  // completion that will never arrive (see rob.scala is_illegal); without this
  // trap the core wedges silently forever on a bad jump into unpopulated
  // memory. mtval gets the offending word, mepc the faulting PC, so a kernel
  // oops names the address instead of the machine simply stopping.
  when(writeBackResult.fired && writeBackResult.instruction(1,0) =/= "b11".U) {
    mepc := writeBackResult.pc
    mcause := 2.U
    mtval := Cat(0.U(32.W), writeBackResult.instruction)
    currentPrivilege := MMODE.U
    expectedPC := mtvec
    mstatus := "h0000000A00000000".U(64.W) | Cat(0.U(51.W), Mux(currentPrivilege===MMODE.U, "b11000".U(5.W), 0.U(5.W)), mstatus(3, 0), 0.U(4.W))
  }

  /* when(opcode === system.U && fun3 === 0.U && immediate === 0.U && validInputBuf && readyOutputBuf) {
    mepc := outputBuffer.pc
    when(currentPrivilege === MMODE.U) { mcause := 11.U }
      .otherwise { mcause := 8.U }
    mstatus := currentPrivilege
    currentPrivilege := MMODE.U
    expectedPC := mtvec
  }

  when(opcode === system.U && fun3 === 0.U && immediate === 770.U && validInputBuf && readyOutputBuf) {
    mstatus := UMODE.U
    expectedPC := mepc
  } */


  // Three independent write ports, three DIFFERENT indices in the same cycle —
  // this is exactly the case a naive `reg := reg | oh` per site would break, so
  // they are OR-ed into one accumulator instead.
  validSetExec :=
    Mux(writeAddrPRF.exec1Valid, UIntToOH(writeAddrPRF.exec1Addr, PRFCount), 0.U) |
    Mux(writeAddrPRF.exec2Valid, UIntToOH(writeAddrPRF.exec2Addr, PRFCount), 0.U) |
    Mux(writeAddrPRF.exec3Valid, UIntToOH(writeAddrPRF.exec3Addr, PRFCount), 0.U)

//  when(jumpAddrWrite.fired) { PRFValidList(outputBuffer.PRFDest) := true.B }

  /** FSM for ready valid interface of input buffer */
  /** ------------------------------------------------------------------------------------------------------------------- */
  switch(stateRegInputBuf) {
    is(emptyState) {
      when(branchEvalIn.fired && !branchEvalIn.passFail) {
        stateRegInputBuf := emptyState
        validInputBuf    := false.B
        readyInputBuf    := false.B
        stallReg := false.B
      }.otherwise {
        validInputBuf := false.B
        readyInputBuf := true.B
        when(fromFetch.fired) {
          when(fromFetch.expected.valid) {
            when(fromFetch.expected.pc === fromFetch.pc) {
              stateRegInputBuf := fullState
            }
          }.otherwise {
            stateRegInputBuf := fullState
          }
        }
      }
      when(stallReg) {
        readyInputBuf := false.B
      }
    }
    is(fullState) {
      when(branchEvalIn.fired && !branchEvalIn.passFail) {
        stateRegInputBuf := emptyState
        validInputBuf    := false.B
        readyInputBuf    := false.B
        stallReg := false.B
      }.otherwise {
        when(!stall && !(branchEvalIn.fired && (opcode === cjump.U || opcode === jump.U || opcode === jumpr.U))) {
          validInputBuf := true.B
          when(readyOutputBuf) {
            readyInputBuf := true.B
            when(!fromFetch.fired || (opcode === system.U && fun3 === 0.U && immediate === 770.U)) {
              stateRegInputBuf := emptyState
            }
          } otherwise {
            readyInputBuf := false.B
          }
        }.otherwise {
          validInputBuf := false.B
        }
      }
      when(stallReg) {
        readyInputBuf := false.B
      }
    }
  }
  // Issue-fence: do not handshake a sequential fetch while a JAL has
  // named its target. expectedPC mismatch also refuses fired; this
  // drops ready so fetch takes the redirect_bit drain path.
  when(jalDemand && (fromFetch.pc =/= jalDemandPC)) {
    readyInputBuf := false.B
  }
  // If a sequential insn snuck into the input buffer, drop it without
  // transferring to output. Otherwise it would issue behind the JAL and
  // a passing JAL would commit the wrong path.
  when(jalWaitTarget && (stateRegInputBuf === fullState) &&
       !knownTarget && (pc =/= jalWaitTgtPC) && !jalArchRedirect) {
    stateRegInputBuf := emptyState
    validInputBuf    := false.B
    readyInputBuf    := false.B
  }
  /** ------------------------------------------------------------------------------------------------------------------- */

  /** FSM for ready valid interface of output buffer */
  /** ------------------------------------------------------------------------------------------------------------------- */
  switch(stateRegOutputBuf) {
    is(emptyState) {
      when(branchEvalIn.fired && !branchEvalIn.passFail) {
        stateRegOutputBuf := emptyState
        validOutputBuf    := false.B
        readyOutputBuf    := false.B
      }.otherwise {
        validOutputBuf := false.B
        readyOutputBuf := true.B
        when(validInputBuf) {
          stateRegOutputBuf := fullState
        }
      }
    }
    is(fullState) {
      when(branchEvalIn.fired && !branchEvalIn.passFail) {
        stateRegOutputBuf := emptyState
        validOutputBuf    := false.B
        readyOutputBuf    := false.B
      }.otherwise {
        validOutputBuf := true.B
        when(toExec.fired) {
          readyOutputBuf := true.B
          when(!validInputBuf) {
            stateRegOutputBuf := emptyState
          }
        } otherwise {
          readyOutputBuf := false.B
        }
      }
    }
  }
  val freeCount = IO(Output(UInt(7.W)))
  freeCount := PopCount(PRFFreeList)



  when(
    writeBackResult.fired && writeBackResult.rdAddr =/= 0.U && 
    writeBackResult.instruction(6,0) =/= cjump.U && 
    writeBackResult.instruction(6,0) =/= store.U && 
    architecturalRegMap(writeBackResult.rdAddr) =/= writeBackResult.PRFDest &&
    writeBackResult.instruction =/= "h80000073".U
  ) {
    architecturalRegMap(writeBackResult.rdAddr) := writeBackResult.PRFDest
    // Frees the PREVIOUS physical register for this architectural reg (the read
    // of architecturalRegMap sees the register's current value, not the write
    // above), in the main free list and in every branch checkpoint.
    freeSetRetire   := UIntToOH(architecturalRegMap(writeBackResult.rdAddr), PRFCount)
    reservedFreeSet := UIntToOH(architecturalRegMap(writeBackResult.rdAddr), PRFCount)
  }

  // ───────────────────────────── PRF list resolve ─────────────────────────────
  // Single point of truth for the two packed lists. Layers are applied in the
  // same order the original Vec write sites appeared, and the branch-recovery
  // layer deliberately rebuilds from the CURRENT register value (not from
  // layer 1) because a whole-Vec assignment used to override the per-bit writes
  // above it. Keep this block LAST — it is the only writer of these registers.
  private val archOneHot =
    (0 until regCount).map(i => UIntToOH(architecturalRegMap(i), PRFCount)).reduce(_ | _)

  PRFValidList := Mux(listRestore, reservedValidList(0) | PRFValidList,
                  Mux(listFlush,   archOneHot,
                                   (PRFValidList | validSetJump) & (~validClrAlloc).asUInt)) |
                  validSetExec

  PRFFreeList  := Mux(listRestore, reservedFreeList(0) | PRFFreeList,
                  Mux(listFlush,   (~archOneHot).asUInt,
                                   PRFFreeList & (~freeClrCollide).asUInt & (~freeClrAlloc).asUInt)) |
                  freeSetRetire

  for (i <- 0 until nCheckpoints) {
    reservedFreeList(i) := reservedFreeNext(i) | reservedFreeSet
  }

  val canTakeInterrupt = IO(Output(Bool()))
  when(stallReg) {
    // when system instructions are being processed in the pipeline,-
    // don't allow interrupts
    canTakeInterrupt := false.B
  }.elsewhen(currentPrivilege === UMODE.U) {
    // u-mode can be interrupted regardless of mstatus
    canTakeInterrupt := mie(7).asBool // true.B
  }.otherwise {
    // mstatus.MIE && mstatus.MTIE
    canTakeInterrupt := mstatus(3).asBool && mie(7).asBool
  }

  // Machine software interrupt (IPI) enable — same gating as the timer path but
  // keyed on mie.MSIE (bit 3) instead of mie.MTIE (bit 7). Drives the MSIP arm
  // of the interrupt-inject FSM in core.scala (used for SMP cross-hart IPIs).
  val canTakeSoftInterrupt = IO(Output(Bool()))
  when(stallReg) {
    canTakeSoftInterrupt := false.B
  }.elsewhen(currentPrivilege === UMODE.U) {
    canTakeSoftInterrupt := mie(3).asBool
  }.otherwise {
    canTakeSoftInterrupt := mstatus(3).asBool && mie(3).asBool
  }
}

object DecodeUnit extends App{
  emitVerilog(new decode(mhart_id = 0))
}
