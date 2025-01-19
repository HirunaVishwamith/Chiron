package scheduler

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._

import common.configuration._
import common._
import os.makeDir

class scheduler extends Module {
  val allocate = IO(new instrIssueAllocate)
  val release = IO(new instrIssueRelease)

  val wakeUpExt = IO(Input(Vec(2, new Bundle {
    val valid = Bool()
    val prfAddr = UInt(prfAddrWidth.W)
  })))

  val branchOps = IO(Input(new Bundle {
    val valid = Bool()
    val branchMask = UInt(newBranchMaskWidth.W) //leon coherency
    val passed = Bool()
  }))

  val queue = RegInit(VecInit(Seq.fill(instrIssueDepth)((new Bundle {
    val opcodeMeta = new Bundle {
      val isBranch = Bool()
      val isMemAccess = Bool()
      val isM = Bool()
    }
    val valid = Bool()
    val instruction = UInt(32.W)
    val branchMask = UInt(newBranchMaskWidth.W)  //leon coherency
    val rs1 = new Bundle{
      val ready = Bool()
      val prfAddr = UInt(prfAddrWidth.W)
    }
    val rs2 = rs1.cloneType
    val prfDest = UInt(prfAddrWidth.W)
    val robAddr = UInt(robAddrWidth.W)
  }).Lit(
    _.valid -> false.B
  ))))

  val memoryReady = IO(Input(Bool()))
  val multuplyAndDivideReady = IO(Input(Bool()))

  // takes readyness of each queue entry
  val readyVector = Cat(Seq.tabulate(instrIssueDepth)(i => {
    queue(i).valid && queue(i).rs1.ready && queue(i).rs2.ready &&
    (!queue(i).opcodeMeta.isBranch || (if (i == 0) true.B else !Seq.tabulate(i)(j => queue(j).valid && queue(j).opcodeMeta.isBranch).reduce(_ || _))) &&
    (!queue(i).opcodeMeta.isMemAccess || ((if (i == 0) true.B else !Seq.tabulate(i)(j => queue(j).valid && queue(j).opcodeMeta.isMemAccess).reduce(_ || _)) && memoryReady)) &&
    (!queue(i).opcodeMeta.isM || multuplyAndDivideReady)
  }).reverse)
  // picks the oldest ready instruction in the issue queue
  val dequeuedIndex = MuxCase(0.U, Seq.tabulate(instrIssueDepth)(i => (readyVector(i).asBool -> i.U)))
  val dequeued = MuxCase(queue(instrIssueDepth-1), Seq.tabulate(instrIssueDepth-1)(i => (readyVector(i).asBool -> queue(i)))) //queue(dequeuedIndex)
  val releasedBuffer = RegInit((new Bundle {
    val valid = Bool()
    val instruction = UInt(32.W)
    val branchMask = UInt(newBranchMaskWidth.W) //leon coherency  
    val rs1prfAddr = UInt(prfAddrWidth.W)
    val rs2prfAddr = UInt(prfAddrWidth.W)
    val prfDest = UInt(prfAddrWidth.W)
    val robAddr = UInt(robAddrWidth.W)
  }).Lit(
    _.valid -> false.B
  ))
  val dequeue = !releasedBuffer.valid || release.fired

  val tempQueue = Seq.fill(instrIssueDepth+1)(Wire(queue(0).cloneType)) 
  tempQueue zip queue foreach{ case ( tempEntry, queueEntry ) => tempEntry := queueEntry }
  tempQueue(instrIssueDepth).valid := allocate.fired
  tempQueue(instrIssueDepth).branchMask := allocate.branchMask
  tempQueue(instrIssueDepth).opcodeMeta := {
    val meta = Wire(queue(0).opcodeMeta.cloneType)
    meta.isM := (allocate.instruction(6,2) === BitPat("b011?0")) && allocate.instruction(25).asBool
    meta.isMemAccess := !allocate.instruction(6).asBool && !allocate.instruction(4).asBool && (allocate.instruction(6,2) =/= BitPat("b00011"))
    meta.isBranch := allocate.instruction(6,5) === 3.U
    meta
  }
  tempQueue(instrIssueDepth).prfDest := allocate.prfDest
  tempQueue(instrIssueDepth).robAddr := allocate.robAddr
  tempQueue(instrIssueDepth).rs1 := allocate.rs1
  tempQueue(instrIssueDepth).rs2 := allocate.rs2
  tempQueue(instrIssueDepth).instruction := allocate.instruction

  when(allocate.fired && tempQueue(instrIssueDepth).opcodeMeta.isMemAccess) { tempQueue(instrIssueDepth).rs2.ready := true.B }

  val updatedEntries = Seq.fill(instrIssueDepth+1)(Wire(queue(0).cloneType))
  updatedEntries zip tempQueue foreach{ case ( newEntry, oldEntry ) => newEntry := oldEntry }

  val wakeUpInt = Wire(wakeUpExt(0).cloneType)
  wakeUpInt.valid := (dequeued.instruction(6,2) === BitPat("b0?1?0")) && !dequeued.opcodeMeta.isM && dequeue && readyVector.orR && dequeued.instruction(11, 7).orR
  wakeUpInt.prfAddr := dequeued.prfDest
  val wakeup = wakeUpExt :+ wakeUpInt
  updatedEntries zip tempQueue foreach{ case ( newEntry, oldEntry ) => newEntry.rs1.ready := oldEntry.valid && (oldEntry.rs1.ready || wakeup.map(i => i.valid && (i.prfAddr === oldEntry.rs1.prfAddr)).reduce(_ || _)) } 

  val instrRetired = IO(Output(wakeUpInt.cloneType))
  instrRetired := RegNext(wakeUpInt)

  updatedEntries zip tempQueue foreach{ case ( newEntry, oldEntry ) => {
    newEntry.rs2.ready := oldEntry.valid && (oldEntry.rs2.ready || wakeup.map(i => i.valid && (i.prfAddr === oldEntry.rs2.prfAddr)).reduce(_ || _)) 
  } }

  updatedEntries zip tempQueue foreach{ case (newEntry, oldEntry) => {
    when(branchOps.valid) {
      when(branchOps.passed && (oldEntry.branchMask & branchOps.branchMask).orR) { newEntry.branchMask := oldEntry.branchMask ^ branchOps.branchMask }
      .otherwise { newEntry.valid := oldEntry.valid && !(oldEntry.branchMask & branchOps.branchMask).orR }
    }
  }}

  when(dequeue && readyVector.orR) {(0 until (instrIssueDepth)).foreach(i => when(i.U === dequeuedIndex) { updatedEntries(i).valid := false.B })}

  val newQueue = VecInit.tabulate(instrIssueDepth)(i => Mux(!Seq.tabulate(i+1)(j => queue(j).valid).reduce(_ && _) || (dequeue && readyVector(i,0).orR), updatedEntries(i+1), updatedEntries(i)))
  val oldestMemMask = newQueue.dropRight(1).map(i => Mux(i.valid && i.opcodeMeta.isMemAccess, i.rs2.prfAddr, 0.U)).reduce(_ | _)
  val newMask = oldestMemMask | MuxCase(0.U, Seq.tabulate(prfAddrWidth)(i => (!oldestMemMask(i).asBool -> (1 << i).U)))
  val haveMemMasks = RegInit(true.B)
  when(haveMemMasks) { haveMemMasks := !newMask.andR || !newQueue(instrIssueDepth-1).valid || !newQueue(instrIssueDepth-1).opcodeMeta.isMemAccess }
  .otherwise { haveMemMasks := !newMask.andR }

  queue zip newQueue foreach{ case (buffer, updated) => buffer := updated }

  

  release.branchMask := releasedBuffer.branchMask
  release.instruction := releasedBuffer.instruction
  release.prfDest := releasedBuffer.prfDest
  release.robAddr := releasedBuffer.robAddr
  release.rs1prfAddr := releasedBuffer.rs1prfAddr
  release.rs2prfAddr := releasedBuffer.rs2prfAddr
  release.ready := releasedBuffer.valid
  allocate.ready := !queue.map(_.valid).reduce(_ && _)

  when(dequeue) {
    releasedBuffer.branchMask := Mux(branchOps.valid && (branchOps.branchMask & dequeued.branchMask).orR && branchOps.passed, dequeued.branchMask ^ branchOps.branchMask, dequeued.branchMask)
    releasedBuffer.instruction := dequeued.instruction
    releasedBuffer.prfDest := dequeued.prfDest
    releasedBuffer.robAddr := dequeued.robAddr
    releasedBuffer.rs1prfAddr := dequeued.rs1.prfAddr
    releasedBuffer.rs2prfAddr := dequeued.rs2.prfAddr
    releasedBuffer.valid := dequeued.valid && (!branchOps.valid || !(dequeued.branchMask & branchOps.branchMask).orR || branchOps.passed) && readyVector.orR
  }.elsewhen(branchOps.valid) {
    releasedBuffer.branchMask := releasedBuffer.branchMask ^ branchOps.branchMask
    releasedBuffer.valid := releasedBuffer.valid && (!branchOps.valid || !(releasedBuffer.branchMask & branchOps.branchMask).orR || branchOps.passed)
  }
}

object scheduler extends App {
  emitVerilog(new scheduler)
}