package cache

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._

import common._
import decode.constants

/**
  * Pipeline makes requests through this insterface
  *
  */
class pipelineMemoryRequest extends Bundle {
  val valid = Bool()
  val address = UInt(32.W)
  val instruction = UInt(32.W)
  val branchMask = UInt(configuration.newBranchMaskWidth.W) //leon coherency
  val robAddr = UInt(configuration.robAddrWidth.W)
  val prfDest = UInt(configuration.prfAddrWidth.W)
}

class pipelineMemoryRequestWithData extends pipelineMemoryRequest {
  val data = UInt(64.W)
  val replay = Bool()

  def :=(that: pipelineMemoryRequest): Unit = {
    this.valid        := that.valid
    this.address      := that.address
    this.branchMask   := that.branchMask
    this.instruction  := that.instruction
    this.prfDest      := that.prfDest
    this.robAddr      := that.robAddr
    this.replay       := false.B
  }
}

/**
  * Responsible scheduling memory requests, taking into
  * account the dependencies
  */
class queryScheduler extends Module {
  /**
    * This the instruction that will be scheduled in to the pipeline. There a few
    * sources for this instruction
    *   1. replayQueue - Instructions being replayed after handling of a miss
    *   2. storeInstructionQueue - buffers store instructions until they are committed
    *   3. dependentReads - These are reads that depend on a store that have not yet 
    *       been committed
    *   4. fromPipeline - Youngest instruction in pipeline (least priority)
    */
  val scheduledInstruction = RegInit(new Bundle {
    val query = new pipelineMemoryRequest
    val data = UInt(64.W)
  } Lit(_.query.valid -> false.B))
  val toCache = IO(Output(new Bundle {
    val queryWithData = scheduledInstruction.cloneType
    val replaying = Bool()
  }))
  toCache.queryWithData := scheduledInstruction

  /**
    * reads wait for the dependent store to commit
    */
  val dependentReads = RegInit(VecInit(Seq.fill(configuration.cache.dependentReadsDepth)(new pipelineMemoryRequest{
    val dependency = new Bundle {
      val free = Bool()
      val robAddr = UInt(configuration.robAddrWidth.W)
    }
  }.Lit(_.valid -> false.B))))
  val dependentRead = MuxCase(dependentReads(dependentReads.length-1), dependentReads.reverse.drop(1).reverse.map{ entry => (entry.valid -> entry) })

  /**
    * Store instruction waits for data and the cache not to be stalled
    */
  val storeInstructions = RegInit(VecInit(Seq.fill(configuration.cache.storeInstructionDepth)(new pipelineMemoryRequest Lit(_.valid -> false.B))))
  val storeInstruction = MuxCase(storeInstructions(storeInstructions.length-1), storeInstructions.map(_.valid) zip storeInstructions)

  val storeCommit = IO(new composableInterface)
  /**
    * storeCommit should not fire when cacheStalled or replaying instructions
    * "dcache" top module should take care of this
    */
  storeCommit.ready := storeInstructions(0).valid//storeInstructions.map(_.valid).reduce(_ || _)

  val fromBuffer = RegInit(new pipelineMemoryRequest Lit(_.valid -> false.B))

  /**
    * All the instructions that need to be replayed will be entered through here
    */
  val replaying = IO(Input(Bool()))
  toCache.replaying := RegNext(replaying)
  /**
    * All cache stall conditions will be explained later
    * They include
    *   1. Write Through module stalled
    *   2. Miss handler saturation
    */
  val cacheStalled = IO(Input(Bool()))

  val replayQueue = IO(Input(scheduledInstruction.cloneType))

  val nextScheduled = Wire(scheduledInstruction.cloneType)

  val branchOps = IO(Input(new Bundle {
    val valid = Bool()
    val branchMask = UInt(configuration.newBranchMaskWidth.W)  //leon coherency
    val passed = Bool()
  }))

  val peripheral = IO(new Bundle {
    val ready = Input(Bool())
    val bits = Output(new pipelineMemoryRequest)
  })

  val dequeuedFromBuffer = WireInit(false.B)
  val toPeripheral = Reg(Bool())
  val dependentReadsUpdate = Seq.fill(dependentReads.length+1)(Wire(dependentReads(0).cloneType))//Vec(dependentReads.length+1, dependentReads(0).cloneType)
  
  /* 
  (
    (!toPeripheral || peripheral.ready) &&
    (!fromBuffer.valid || 
    // stpres
    ((fromBuffer.instruction(5).asBool && !fromBuffer.instruction(3).asBool) && (storeCommit.fired || !storeInstructions.map(_.valid).reduce(_ && _))) ||
    // There is a dependent read
    (dependentReadsUpdate(dependentReads.length).valid && (!dependentReads.map(_.valid).reduce(_ && _) || (dependentRead.valid && dependentRead.dependency.free))) ||
    // Normal read directly to cache
    !((fromBuffer.instruction(5).asBool) || dependentReadsUpdate(dependentReads.length).valid || replaying || cacheStalled || (dependentRead.valid && dependentRead.dependency.free) || storeCommit.fired) ||
    // atomics
    (fromBuffer.instruction(3).asBool && (storeCommit.fired || !storeInstructions.map(_.valid).reduce(_ && _)) &&
    ((dependentReadsUpdate(dependentReads.length).valid && (!dependentReads.map(_.valid).reduce(_ && _) || (dependentRead.valid && dependentRead.dependency.free))) ||
    !(dependentReadsUpdate(dependentReads.length).valid || replaying || cacheStalled || (dependentRead.valid && dependentRead.dependency.free) || storeCommit.fired))))
    ) */
  when(fromBuffer.valid) {
    // There needs to be entry in it for there to dequeue
    // peripheral accesses are different
    when(true.B) /* { dequeuedFromBuffer := peripheral.ready && (fromBuffer.instruction(6, 4) === "b010".U) }
    .otherwise */ {
      // Not peripheral access
      val storeInstructionsFree = (storeCommit.fired || !storeInstructions.map(_.valid).reduce(_ && _))
      val readCanDequeue = Mux(
        dependentReadsUpdate(dependentReads.length).valid,
        // Reads have RAW dependency with a memory write
        (!dependentReads.map(_.valid).reduce(_ && _) || (dependentRead.valid && dependentRead.dependency.free)),
        // No dependencies
        !(replaying || cacheStalled || (dependentRead.valid && dependentRead.dependency.free) || storeCommit.fired)
      )
      switch(fromBuffer.instruction(6, 2)) {
        is ("b00000".U) {
          dequeuedFromBuffer := readCanDequeue
        }
        is ("b01000".U) {
          dequeuedFromBuffer := storeInstructionsFree && (!toPeripheral || peripheral.ready)
        }
        is ("b01011".U) {
          dequeuedFromBuffer := readCanDequeue && storeInstructionsFree
        }
        // Other cases should not happen
      }
    }
  }
  when( replaying || !cacheStalled ) { scheduledInstruction := nextScheduled }
  .elsewhen(cacheStalled) { scheduledInstruction.query.valid := false.B }
  when(branchOps.valid) {
    when((nextScheduled.query.branchMask & branchOps.branchMask).orR){
      scheduledInstruction.query.branchMask := nextScheduled.query.branchMask ^ branchOps.branchMask
    }
    when(!branchOps.passed && (nextScheduled.query.branchMask & branchOps.branchMask).orR) { scheduledInstruction.query.valid := false.B }
  }



  val storeInstructionsUpdate = Seq.fill(storeInstructions.length+1)(Wire(storeInstructions(0).cloneType))//Vec(storeInstructions.length+1, storeInstructions(0).cloneType)
  storeInstructionsUpdate zip (storeInstructions :+ fromBuffer) foreach{ case (temp, curr) => {
    temp := curr
    when(branchOps.valid) {
      when((curr.branchMask & branchOps.branchMask).orR)  { temp.branchMask := curr.branchMask ^ branchOps.branchMask }
      when(!branchOps.passed && (curr.branchMask & branchOps.branchMask).orR) { temp.valid := false.B }
    }
  }}

  // validating appending a new entry, append only if its a memory modification entry (i.e. includes atmoics)
  // atomics are stalled on "fromBuffer" until both read and write parts can happen
  // For peripherals, they must first be accepted by the peripheral handler first
  storeInstructionsUpdate(storeInstructions.length).valid := (
    fromBuffer.valid && fromBuffer.instruction(5).asBool && (!toPeripheral || peripheral.ready) && dequeuedFromBuffer
  ) && !(branchOps.valid && (branchOps.branchMask & fromBuffer.branchMask).orR && !branchOps.passed)
  when(storeCommit.fired) {
    storeInstructionsUpdate.head.valid := false.B // oldest instruction will be dequeued
  }

  // updating instructions when no store instruction is fired
  storeInstructions.map(_.valid).scanLeft(true.B)(_ && _) // detecting the first empty entry
  .zip(storeInstructions)
  .zip(storeInstructionsUpdate zip storeInstructionsUpdate.drop(1))
  .foreach { case((priorEntriesFull, reg), (old, next)) => when(priorEntriesFull && reg.valid) { reg := old } otherwise { reg := next }}
  when(storeCommit.fired) {
    // all instructions move fwd in queue
    // because its always the oldest instruction picked
    storeInstructions zip storeInstructionsUpdate.drop(1) foreach{ case(reg, next) => reg := next }
  }
  

  val currDependentReads = dependentReads :+ {
    val newEntry = Wire(dependentReads(0).cloneType)
    newEntry.address := fromBuffer.address
    newEntry.branchMask := fromBuffer.branchMask
    newEntry.instruction := Cat(fromBuffer.instruction(31, 7), "b0000011".U(7.W))
    newEntry.prfDest := fromBuffer.prfDest
    newEntry.robAddr := fromBuffer.robAddr
    newEntry.dependency.robAddr := MuxCase(scheduledInstruction.query.robAddr, storeInstructions.reverse.map{ 
      entry => ((entry.valid && (entry.address(31, 3) === fromBuffer.address(31, 3))) -> entry.robAddr)
    })
    newEntry.dependency.free := false.B
    newEntry.valid := (
      // This valid is asserted when ever there is a dependency with a write in progress
      // (Doesn't matter if it will be dequeued from fromBuffer)
      fromBuffer.valid && (!fromBuffer.instruction(5).asBool || (fromBuffer.instruction(3).asBool && (storeCommit.fired || !storeInstructions.map(_.valid).reduce(_ && _)))) &&
      ((scheduledInstruction.query.instruction(5).asBool && scheduledInstruction.query.address(31, 3) === fromBuffer.address(31, 3) && scheduledInstruction.query.valid && !toCache.replaying) ||
      storeInstructions.map{ entry => entry.valid && (entry.address(31, 3) === fromBuffer.address(31, 3)) }.reduce(_ || _))
    )
    when(Cat(Seq(28, 27, 3).map(i => fromBuffer.instruction(i))) === "b111".U) {
      /* newEntry.valid := fromBuffer.valid && (storeInstructions.map(_.valid).reduce(_ || _) || (toCache.queryWithData.query.valid && toCache.queryWithData.query.instruction(5).asBool))
      newEntry.dependency.robAddr := MuxCase(scheduledInstruction.query.robAddr, storeInstructions.reverse.map{ 
        entry => (entry.valid  -> entry.robAddr)
      }) 
      Lets see what happens when this commented
      From the looks of things, it tries to make sure that the read of sc.d/w happens before the write
      This should not be necessary, lets see */
      newEntry.instruction := Cat(fromBuffer.instruction(31, 7), "b0001111".U(7.W))
    }

    newEntry
  }

  dependentReadsUpdate zip currDependentReads foreach { case (temp, curr) => {
    temp := curr
    when(branchOps.valid) {
      when((curr.branchMask & branchOps.branchMask).orR) {
        temp.branchMask := curr.branchMask ^ branchOps.branchMask
      }
      when(!branchOps.passed && (curr.branchMask & branchOps.branchMask).orR) { temp.valid := false.B }
    }
    when(RegNext(storeCommit.fired)) {
      // This is indicating resolving a dependency
      when(curr.dependency.robAddr === scheduledInstruction.query.robAddr) { temp.dependency.free := true.B }
    }
  }}

  // updating an element dequeuing
  when(!replaying && !cacheStalled && !storeCommit.fired && dependentRead.valid && dependentRead.dependency.free) {
    dependentReads.map(entry => entry.valid && entry.dependency.free)
    .scanLeft(false.B)(_ || _)
    .zip(dependentReads zip dependentReadsUpdate)
    .foreach { case(olderEntryDequeued, (reg, update)) => when(!olderEntryDequeued && reg.dependency.free) { update.valid := false.B }} // dequeuing entry
  }

  (dependentReads.map(_.valid).scanLeft(true.B)(_ && _) // looking for empty entries in queue
  .zip(dependentReads.map(entry => entry.valid && entry.dependency.free).scanLeft(false.B)(_ || _))) // looking for entries affected by dequeuing
  .zip(dependentReads.zip(dependentReadsUpdate zip dependentReadsUpdate.drop(1)))
  .foreach{ case((queueNotEmptyAtFront, olderEntryDequeued), (reg, (old, next))) => {
    when(!queueNotEmptyAtFront || !reg.valid) { reg := next } // if there is an empty entry in queue, all instructions behind it can move forward
    .elsewhen((olderEntryDequeued || (reg.dependency.free && reg.valid)) && (!replaying && !cacheStalled && !storeCommit.fired && dependentRead.valid && dependentRead.dependency.free)) {
      // accounting for dequeuing from this queue
      reg := next
    }.otherwise { reg := old }
  }}

  when(replaying) { nextScheduled := replayQueue }
  .elsewhen(storeCommit.fired) { nextScheduled.query := storeInstruction }
  .elsewhen(dependentRead.valid && dependentRead.dependency.free) { 
    nextScheduled.query.address := dependentRead.address
    nextScheduled.query.branchMask := dependentRead.branchMask
    nextScheduled.query.instruction := dependentRead.instruction
    nextScheduled.query.prfDest := dependentRead.prfDest
    nextScheduled.query.robAddr := dependentRead.robAddr
    nextScheduled.query.valid := dependentRead.valid 
  }
  .otherwise { 
    nextScheduled.query := fromBuffer 
    nextScheduled.query.instruction := Cat(fromBuffer.instruction(31, 7), "b0000011".U(7.W))
    when(toPeripheral || dependentReadsUpdate(dependentReads.length).valid || (storeInstructionsUpdate(storeInstructions.length).valid && !fromBuffer.instruction(3).asBool)) {
      nextScheduled.query.valid := false.B
    }
    when(Cat(Seq(28, 27, 3).map(i => fromBuffer.instruction(i))) === "b111".U) {
      nextScheduled.query.instruction := Cat(fromBuffer.instruction(31, 7), "b0001111".U(7.W))
    }
    when(!dequeuedFromBuffer || toPeripheral) {
      nextScheduled.query.valid := false.B
    }
  }

  val bufferQueue = RegInit(VecInit(Seq.fill(configuration.cache.newRequestBufferDepth + 8)(new pipelineMemoryRequest Lit(_.valid -> false.B))))

  val newInstruction = IO(Input(new pipelineMemoryRequest))

  val bufferQueueUpdate = Wire(Vec(bufferQueue.length+1, bufferQueue(0).cloneType)) //Seq.fill(bufferQueue.length+1)(Wire(bufferQueue(0).cloneType))// Vec(bufferQueue.length+1, bufferQueue(0).cloneType)

  (bufferQueue.map(_.valid).scanLeft(true.B)(_ && _) zip bufferQueue)
  .zip(bufferQueueUpdate zip bufferQueueUpdate.drop(1))
  .foreach { case((priorEntriesFull, reg), (old, next)) => when(!priorEntriesFull || !reg.valid) {reg := next} otherwise {reg := old}}

  bufferQueueUpdate zip (bufferQueue :+ newInstruction) foreach{ case (update, curr) => 
    update := curr
    when (branchOps.valid) {
      when((curr.branchMask & branchOps.branchMask).orR) {update.branchMask := curr.branchMask ^ branchOps.branchMask}
      when(!branchOps.passed && (curr.branchMask & branchOps.branchMask).orR) { update.valid := false.B }
    } 
  }

  when(peripheral.ready){
    toPeripheral := false.B
  }
  val nextFrmBuffer = Mux(bufferQueue.map(_.valid).reduce(_ || _), 
  MuxCase(bufferQueue(bufferQueue.length-1), bufferQueue.reverse.drop(1).reverse.map(entry => (entry.valid -> entry) )), newInstruction)

  when(branchOps.valid) {
    when ((fromBuffer.branchMask & branchOps.branchMask).orR) {
      fromBuffer.branchMask := (fromBuffer.branchMask ^ branchOps.branchMask)
    }
    when(!branchOps.passed && (fromBuffer.branchMask & branchOps.branchMask).orR) { 
      fromBuffer.valid := false.B
      toPeripheral := false.B 
    }
  }
  // update "fromBuffer" register
  /**
    * An instruction is "fromBuffer" register
    *  1. Can be taken into storeInstructions queue (iff memory modification and storeInstructionsQueue can accomodate)
    *  2. Can be taken into dependentReads (iff there are write dependencies && dependentRead queue can accomadate)
    *  3. Directly sent to pipeline (iff not memory modification && !(replay || cacheStalled || storeCommit.fired || dependentRead))
    */
  when((dequeuedFromBuffer || !fromBuffer.valid) && (!toPeripheral || peripheral.ready)) {
    fromBuffer := nextFrmBuffer
    when(!configuration.inRangeRAM(nextFrmBuffer.address) && !nextFrmBuffer.instruction(5).asBool) { fromBuffer.valid := false.B }
    toPeripheral := nextFrmBuffer.valid && !configuration.inRangeRAM(nextFrmBuffer.address)

    bufferQueue.map(_.valid).scanLeft(false.B)(_ || _)
    .zip(bufferQueue zip bufferQueueUpdate)
    .foreach { case(validEntryFound, (reg, update)) => when(!validEntryFound && reg.valid) { update.valid := false.B }} // dequeuinh oldest entry

    // when buffer queue is empty the new instruction has priority
    when(!bufferQueue.map(_.valid).reduce(_ || _)) { bufferQueueUpdate(bufferQueue.length).valid := false.B }

    bufferQueue zip bufferQueueUpdate.drop(1) foreach { case (reg, next) => reg := next }

    when(branchOps.valid) {
      when((branchOps.branchMask & nextFrmBuffer.branchMask).orR) {fromBuffer.branchMask := branchOps.branchMask ^ nextFrmBuffer.branchMask}
      when(!branchOps.passed && (branchOps.branchMask & nextFrmBuffer.branchMask).orR) { 
        fromBuffer.valid := false.B 
        toPeripheral := false.B
      }
    }
  }

  peripheral.bits := fromBuffer
  peripheral.bits.valid := toPeripheral

  val canAllocate = IO(Output(Bool()))

  // there is space for atleast 8 entries
  canAllocate := !bufferQueue.reverse.drop(configuration.cache.newRequestBufferDepth).map(_.valid).reduce(_ || _)
  nextScheduled.data := replayQueue.data

  val clean = IO(Output(Bool()))
  clean := !((bufferQueue.map(_.valid) ++ dependentReads.map(_.valid) ++ storeInstructions.map(_.valid) :+ fromBuffer.valid) reduce(_ || _))
  val pendingStores = storeInstructions.map(_.valid.asUInt).reduce(_ +& _)
  val pendingStoresOut = IO(Output(pendingStores.cloneType))
  pendingStoresOut := pendingStores
}

object queryScheduler extends App {
  emitVerilog(new queryScheduler)
}
