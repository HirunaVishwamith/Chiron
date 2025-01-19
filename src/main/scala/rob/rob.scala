package pipeline.rob

import chisel3._
import chisel3.util._
import pipeline.ports._
import pipeline.fifo._
import common.configuration
import decode.constants
import decode.utils
import os.truncate

class rob(addr_w: Int, numWritePorts: Int) extends Module{
  // IO definitions
  val allocate = IO(new robAllocate(addr_w))
  val commit = IO(new retireInstruction(addr_w) {
    val robAddr = Output(UInt(configuration.robAddrWidth.W))
  })
  val branch = IO(new branchCheck(addr_w))
  val execPorts = IO(Vec(numWritePorts,new pullExecResult(addr_w)))

  // Fifo Initialization

  // PC || Instruction || PRFDest(6)
  val fifo = Module(new robFifo(UInt(102.W),scala.math.pow(2,addr_w).asInstanceOf[Int]) {
    val debugFIFO = Wire(Vec(depth, new Bundle {
      val valid = Bool()
      val instruction = UInt(32.W)
      val prfDest = UInt(configuration.prfAddrWidth.W)
      val pc = UInt(64.W)
      val writeBackGPR = Bool()
    }))

    (Seq.tabulate(depth)(i => memReg(i)) zip debugFIFO)
    .foreach{ case (mem, debug) => {
      debug.prfDest := mem(configuration.prfAddrWidth-1, 0)
      debug.instruction := mem(configuration.prfAddrWidth + 31, configuration.prfAddrWidth)
      debug.pc := mem(101, configuration.prfAddrWidth + 32)
    }}
    debugFIFO.foreach(_.valid := false.B)
    when(readPtr =/= writePtr) {
      Seq.tabulate(depth)(i => (i.U >= readPtr) && (i.U < writePtr))
      .zip(debugFIFO)
      .foreach{ case (valid, debug) => debug.valid := valid}
      when (readPtr > writePtr) {
        Seq.tabulate(depth)(i => (i.U >= readPtr) || (i.U < writePtr))
        .zip(debugFIFO)
        .foreach{ case (valid, debug) => debug.valid := valid}
      }
    }.elsewhen(fullReg) {
      debugFIFO.foreach(_.valid := true.B)
    }
    debugFIFO.foreach{ debug => debug.writeBackGPR := Seq(constants.itype.U, constants.rtype.U, constants.utype.U, constants.jtype.U).map(_ === utils.getInsType(debug.instruction(6, 0))).reduce(_ || _) && debug.instruction(11, 7).orR && debug.valid}

    val debugFIFOOut = IO(Output(debugFIFO.cloneType))
    debugFIFOOut := debugFIFO

    val allocatedPRFCount = IO(Output(UInt(16.W)))
    allocatedPRFCount := debugFIFO.map(_.writeBackGPR.asUInt).reduce(_ +& _)
  })
  // exceptionOccured(1) || mtval(64) || mcause(64) || Ready(1)
  val results = Module(new robResultsFifo(UInt(130.W),scala.math.pow(2,addr_w).asInstanceOf[Int],numWritePorts+1) {
    val robAddrRelease = IO(Output(readPtr.cloneType))
    robAddrRelease := readPtr
  })

  // Allocation logic
  allocate.ready := fifo.io.enq.ready & results.io.enq.ready
  val fifo_data = Cat(allocate.pc,Cat(allocate.instruction,allocate.prfDest))
  val resultsdata = Cat(0.U(129.W),allocate.isReady)
  fifo.io.enq.bits := fifo_data
  results.io.enq.bits := resultsdata
  allocate.robAddr := results.allocatedAddr
  when(allocate.fired){
    fifo.io.enq.valid := 1.U
    results.io.enq.valid := 1.U
  }.otherwise{
    fifo.io.enq.valid := 0.U
    results.io.enq.valid := 0.U
  }

  val is_fence = commit.instruction(6,0) === "b0001111".U

  // Commit Logic
  commit.ready := (results.io.deq.bits(0) | is_fence | commit.isStore) & fifo.io.deq.valid & results.io.deq.valid
  commit.mcause := results.io.deq.bits(64,1)
  commit.mtval := results.io.deq.bits(128,65)
  commit.exceptionOccurred := results.io.deq.bits(129)
  commit.prfDest := fifo.io.deq.bits(5,0)
  commit.instruction := fifo.io.deq.bits(37,6)
  commit.pc := fifo.io.deq.bits(101,38)
  commit.is_fence := is_fence
  commit.robAddr := results.robAddrRelease

  when (commit.fired){
    fifo.io.deq.ready := 1.U
    results.io.deq.ready := 1.U
  }.otherwise{
    fifo.io.deq.ready := 0.U
    results.io.deq.ready := 0.U
  }

  commit.isStore := fifo.io.deq.bits(12,6) === "b0100011".U

  // Branch Handling logic
  fifo.modify := branch.valid & !branch.pass
  fifo.modifyVal := branch.robAddr
  results.modify := branch.valid & !branch.pass
  results.modifyVal := branch.robAddr
  results.writeports(numWritePorts).valid := branch.valid
  results.writeports(numWritePorts).addr := branch.robAddr
  results.writeports(numWritePorts).data := Cat(0.U(129.W),1.U)

  // connect writeports
  for (i <- 0 until numWritePorts){
    val writeval = Cat(execPorts(i).exceptionOccurred,Cat(execPorts(i).mtval,Cat(execPorts(i).mcause,1.U(1.W))))
    //val writeval = Seq(execPorts(i).mtval,execPorts(i).mcause,1.U(1.W)).foldRight(execPorts(i).exceptionOccurred)(Cat(_,_))
    results.writeports(i).valid := execPorts(i).valid
    results.writeports(i).data := writeval
    results.writeports(i).addr := execPorts(i).robAddr
  }


  when(commit.exceptionOccurred & commit.fired) {
    results.reset := 1.U
    fifo.reset := 1.U
  }


  // printf(p"${allocate} ${commit}\n")
  val debugFIFOOut = IO(Output(fifo.debugFIFOOut.cloneType))
  debugFIFOOut := fifo.debugFIFOOut

  val allocatedPRFCount = IO(Output(fifo.allocatedPRFCount.cloneType))
  allocatedPRFCount := fifo.allocatedPRFCount

  val robAddrRelease = IO(Output(results.robAddrRelease.cloneType))
  robAddrRelease := results.robAddrRelease

  // To avoid the rob address of an instruction not yet commited
  // (i.e. commit.ready and !commit.fired) from being prematurely
  // reused in a new instruction
  when(commit.ready && !commit.fired) { allocate.ready := false.B }
}

object robVerilog extends App {
  (new chisel3.stage.ChiselStage).emitVerilog(new rob(4,3))
}




