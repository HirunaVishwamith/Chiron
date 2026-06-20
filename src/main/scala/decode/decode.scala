package decode

import chisel3._
import chisel3.experimental.BundleLiterals._
import chisel3.util._
import decode.constants._
import decode.utils._
import common.configuration

class composableInterface extends Bundle {
  val ready = Output(Bool())
  val fired = Input(Bool())
}

class RecivInstrFrmFetch extends composableInterface {
  val pc          = Input(UInt(dataWidth.W))
  val instruction = Input(UInt(insAddrWidth.W))
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
  val branchMask = Output(UInt(4.W))
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
    val pc          = UInt(dataWidth.W)
    val instruction = UInt(insAddrWidth.W)
  }.Lit(
    _.pc          -> initialPC.U,            /** Initial value is set for the expectedPC */
    _.instruction -> 0.U
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

  val branchBuffer = RegInit(new Bundle {
    val branchPCReady    = Bool()
    val predictedPCReady = Bool()
    val branchPC         = UInt(dataWidth.W)
    val predictedPC      = UInt(dataWidth.W)
    val branchMask       = Vec(configuration.newBranchMaskWidth, UInt(1.W)) //leon coherency
  }.Lit(
    _.branchPCReady    -> false.B,
    _.predictedPCReady -> false.B,
    _.branchPC         -> 0.U,
    _.predictedPC      -> 0.U,
    _.branchMask(0)    -> 0.U,
    _.branchMask(1)    -> 0.U,
    _.branchMask(2)    -> 0.U,
    _.branchMask(3)    -> 0.U,
    _.branchMask(4)    -> 1.U  //leon coherency
  ))

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

  val branchTracker = RegInit(0.U(3.W))

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

  val PRFValidList = RegInit(VecInit(Seq.fill(regCount)(true.B) ++ Seq.fill(PRFCount-regCount)(false.B)))

  /** Storing instruction and pc in the fetch buffer */
  when(fromFetch.fired && readyInputBuf) {     /** Data from the fetch unit is valid and fetch buffer is ready */
    inputBuffer.instruction := fromFetch.instruction
    inputBuffer.pc          := fromFetch.pc
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

  outputBuffer.branchEvalReady := branchEvalIn.fired
  outputBuffer.passFail        := branchEvalIn.passFail
  outputBuffer.branchMask      := branchEvalIn.branchMask

  val branchPCMask = RegInit("b00000".U(configuration.newBranchMaskWidth.W))  //leon coherency
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
  fromFetch.expected.valid := expectedPC =/= 0.U
  fromFetch.expected.pc    := expectedPC
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

  unconditionalJumps := outputBuffer.instruction(6,0) === jump.U || outputBuffer.instruction(6,0) === jumpr.U || outputBuffer.instruction(6,0) === lui.U || outputBuffer.instruction(6,0) === auipc.U
  csrIns := outputBuffer.instruction(6,0) === system.U && outputBuffer.instruction(14,12) =/= 0.U

  val frontEndRegMap      = RegInit(VecInit(Seq.tabulate(regCount)(i => i.U(PRFAddrWidth.W))))
  val architecturalRegMap = RegInit(VecInit(Seq.tabulate(regCount)(i => i.U(PRFAddrWidth.W))))
  val PRFFreeList         = RegInit(VecInit(Seq.fill(regCount)(false.B) ++ Seq.fill(PRFCount-regCount)(true.B)))

  var i = 0;
  for (i <- 0 to 31) {
    retiredRenamedTable.table(i) := architecturalRegMap(i)
  }

  val reservedRegMap1 = Reg(frontEndRegMap.cloneType)
  val reservedRegMap2 = Reg(frontEndRegMap.cloneType)
  val reservedRegMap3 = Reg(frontEndRegMap.cloneType)
  val reservedRegMap4 = Reg(frontEndRegMap.cloneType)

  val reservedFreeList1 = Reg(PRFFreeList.cloneType)
  val reservedFreeList2 = Reg(PRFFreeList.cloneType)
  val reservedFreeList3 = Reg(PRFFreeList.cloneType)
  val reservedFreeList4 = Reg(PRFFreeList.cloneType)

  val reservedValidList1 = Reg(PRFValidList.cloneType)
  val reservedValidList2 = Reg(PRFValidList.cloneType)
  val reservedValidList3 = Reg(PRFValidList.cloneType)
  val reservedValidList4 = Reg(PRFValidList.cloneType)

  rs1Addr  := frontEndRegMap(rs1)
  rs2Addr  := frontEndRegMap(rs2)

  freeRegAddr := PriorityEncoder(PRFFreeList)

  //leon kept the logic as it is because branchBuffer.branchMask=10000 and should stall when 11111
  when(freeRegAddr === 63.U || (Seq(jump.U, jumpr.U, cjump.U).map(_ === opcode).reduce(_ || _) && branchBuffer.branchMask.map(_.asBool).reduce(_ && _))) {
    stall := true.B
  }

  when(rs1Addr === freeRegAddr || rs2Addr === freeRegAddr) {
    stall := true.B
    when(rs1Addr === freeRegAddr) {
      PRFFreeList(rs1Addr) := false.B
    }.elsewhen(rs2Addr === freeRegAddr) {
      PRFFreeList(rs2Addr) := false.B
    }
  }

  when(jumpAddrWrite.fired && outputBuffer.instruction(11,7) =/= 0.U) { PRFValidList(outputBuffer.PRFDest) := true.B }

  when(validInputBuf && readyOutputBuf && (insType === itype.U || insType === rtype.U || insType === utype.U || insType === jtype.U) && rd =/= 0.U) {
    when(!branchEvalIn.fired || branchEvalIn.passFail){
      PRFFreeList(freeRegAddr)  := false.B
      PRFValidList(freeRegAddr) := false.B
      frontEndRegMap(rd)        := freeRegAddr
    }
  }

  val LoadMask = "b01111".U(configuration.newBranchMaskWidth.W) //leon has done this

  when(branchEvalIn.fired) {
    branchTracker := branchTracker - 1.U

    //leon coherency
    branchBuffer.branchMask := VecInit(((branchBuffer.branchMask.asUInt & ~LoadMask) | ((branchBuffer.branchMask.asUInt & (~branchEvalIn.branchMask)) & LoadMask)).asBools)

    when(!branchEvalIn.passFail) {
      branchReg := false.B

      branchBuffer.branchMask(0) := 0.U
      branchBuffer.branchMask(1) := 0.U
      branchBuffer.branchMask(2) := 0.U
      branchBuffer.branchMask(3) := 0.U
      branchBuffer.branchMask(4) := 1.U  //leon coherency

      expectedPC := branchEvalIn.targetPC

      //leon coherency //here
      when(branchEvalIn.branchMask(3,0).orR){
      frontEndRegMap := reservedRegMap1
      PRFFreeList    := (reservedFreeList1 zip PRFFreeList map{case(reserved, current) => reserved | current})
      PRFValidList   := (reservedValidList1 zip PRFValidList map{case(reserved, current) => reserved | current})
      }.otherwise{
        frontEndRegMap := architecturalRegMap
        PRFFreeList    := VecInit(Seq.fill(64)(true.B))
        PRFValidList   := VecInit(Seq.fill(64)(false.B))
        coherency := true.B  //leon coherency

        for (i <- 0 until regCount){
          PRFValidList(architecturalRegMap(i)):=true.B
          PRFFreeList(architecturalRegMap(i)):=false.B
        }
      }
      //here

      branchTracker := 0.U
    }.otherwise {
      reservedRegMap1 := reservedRegMap2
      reservedRegMap2 := reservedRegMap3
      reservedRegMap3 := reservedRegMap4

      reservedFreeList1 := reservedFreeList2
      reservedFreeList2 := reservedFreeList3
      reservedFreeList3 := reservedFreeList4

      reservedValidList1 := reservedValidList2
      reservedValidList2 := reservedValidList3
      reservedValidList3 := reservedValidList4
    }
  }

  val bitPosition = WireDefault(0.U(2.W))

  //leon kept it because branchBuffer.branchMask=10000
  bitPosition := PriorityEncoder(~branchBuffer.branchMask.asUInt)

  when(validInputBuf && readyOutputBuf) {
    when(opcode === jump.U || opcode === jumpr.U || opcode === cjump.U) {
      branchReg := true.B
      branchBuffer.branchPC := pc
      branchBuffer.branchMask(bitPosition) := 1.U
      switch(bitPosition) {
        is(0.U) { branchPCMask := 1.U }
        is(1.U) { branchPCMask := 2.U }
        is(2.U) { branchPCMask := 4.U }
        is(3.U) { branchPCMask := 8.U }
      }

      switch(branchTracker) {
        is(0.U) {
          reservedRegMap1    := frontEndRegMap
          reservedFreeList1  := PRFFreeList
          reservedValidList1 := PRFValidList
          when(opcode(2).asBool && rd.orR) {
            reservedRegMap1(rd)             := freeRegAddr
            reservedFreeList1(freeRegAddr)  := false.B
            reservedValidList1(freeRegAddr) := false.B
          }
        }
        is(1.U) {
          reservedRegMap2    := frontEndRegMap
          reservedFreeList2  := PRFFreeList
          reservedValidList2 := PRFValidList
          when(opcode(2).asBool && rd.orR) {
            reservedRegMap2(rd)             := freeRegAddr
            reservedFreeList2(freeRegAddr)  := false.B
            reservedValidList2(freeRegAddr) := false.B
          }
        }
        is(2.U) {
          reservedRegMap3    := frontEndRegMap
          reservedFreeList3  := PRFFreeList
          reservedValidList3 := PRFValidList
          when(opcode(2).asBool && rd.orR) {
            reservedRegMap3(rd)             := freeRegAddr
            reservedFreeList3(freeRegAddr)  := false.B
            reservedValidList3(freeRegAddr) := false.B
          }
        }
        is(3.U) {
          reservedRegMap4    := frontEndRegMap
          reservedFreeList4  := PRFFreeList
          reservedValidList4 := PRFValidList
          when(opcode(2).asBool && rd.orR) {
            reservedRegMap4(rd)             := freeRegAddr
            reservedFreeList4(freeRegAddr)  := false.B
            reservedValidList4(freeRegAddr) := false.B
          }
        }
      }
      branchTracker := branchTracker + 1.U
    }.otherwise {
      branchReg := false.B
    }
  }

  //leon (no need to change this also branchBuffer.predictedPCReady become false)
  when(branchBuffer.branchMask.asUInt =/= 0.U && validInputBuf && readyOutputBuf) {
    branchBuffer.predictedPC := pc
  }

  branchBuffer.branchPCReady := (opcode === cjump.U || opcode === jump.U || opcode === jumpr.U) && validInputBuf && readyOutputBuf
  branchBuffer.predictedPCReady := branchReg && validInputBuf && readyOutputBuf

  when(expectedPC =/= 0.U && fromFetch.fired && fromFetch.expected.pc === fromFetch.pc) {
    expectedPC := 0.U
    coherency := false.B //leon coherency
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
      is("h344".U) { csrReadDataReg := mip }
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
      mcause := "h8000000000000007".U 
      currentPrivilege := MMODE.U
      expectedPC := mtvec
      mstatus := "h0000000A00000000".U(64.W) | Cat(0.U(51.W), Mux(currentPrivilege===MMODE.U, "b11000".U(5.W), 0.U(5.W)), mstatus(3, 0), 0.U(4.W))
    }
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


  when(writeAddrPRF.exec1Valid) { PRFValidList(writeAddrPRF.exec1Addr) := true.B }
  when(writeAddrPRF.exec2Valid) { PRFValidList(writeAddrPRF.exec2Addr) := true.B }
  when(writeAddrPRF.exec3Valid) { PRFValidList(writeAddrPRF.exec3Addr) := true.B }

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
  freeCount := PRFFreeList.map(_.asUInt).reduce(_ +& _)



  when(
    writeBackResult.fired && writeBackResult.rdAddr =/= 0.U && 
    writeBackResult.instruction(6,0) =/= cjump.U && 
    writeBackResult.instruction(6,0) =/= store.U && 
    architecturalRegMap(writeBackResult.rdAddr) =/= writeBackResult.PRFDest &&
    writeBackResult.instruction =/= "h80000073".U
  ) {
    architecturalRegMap(writeBackResult.rdAddr)              := writeBackResult.PRFDest
    PRFFreeList(architecturalRegMap(writeBackResult.rdAddr)) := true.B
    reservedFreeList1(architecturalRegMap(writeBackResult.rdAddr)) := true.B
    reservedFreeList2(architecturalRegMap(writeBackResult.rdAddr)) := true.B
    reservedFreeList3(architecturalRegMap(writeBackResult.rdAddr)) := true.B
    reservedFreeList4(architecturalRegMap(writeBackResult.rdAddr)) := true.B
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
}

object DecodeUnit extends App{
  emitVerilog(new decode(mhart_id = 0))
}
