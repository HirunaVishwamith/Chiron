package pipeline.fifo

import Chisel.log2Ceil
import chisel3._
import pipeline.ports._
import chisel3.util._

class regFifo[T <: Data ]( gen: T, depth: Int) extends Fifo(gen:
  T, depth: Int) {

  def counter(depth: Int , incr: Bool): (UInt , UInt) = {
    val cntReg = RegInit (0.U(log2Ceil(depth).W))
    val nextVal = Mux(cntReg === (depth -1).U, 0.U, cntReg + 1.U)
    when (incr) {
      cntReg := nextVal
    }
    (cntReg , nextVal)
  }

  // the register based memory
  val memReg = Mem(depth , gen)
  val incrRead = WireDefault (false.B)
  val incrWrite = WireDefault (false.B)
  val (readPtr , nextRead) = counter(depth , incrRead)
  val (writePtr , nextWrite ) = counter(depth , incrWrite )
  val emptyReg = RegInit(true.B)
  val fullReg = RegInit(false.B)


  when(io.deq.ready && io.deq.valid && io.enq.valid && io.enq.ready) {
    memReg(writePtr) := io.enq.bits
    incrWrite := true.B
    incrRead := true.B
  }.elsewhen(io.enq.valid && io.enq.ready) {
    memReg(writePtr) := io.enq.bits
    emptyReg := false.B
    fullReg := nextWrite === readPtr
    incrWrite := true.B
  }.elsewhen(io.deq.ready && io.deq.valid) {
    fullReg := false.B
    emptyReg := nextRead === writePtr
    incrRead := true.B
  }

  io.deq.bits := memReg(readPtr)
  io.enq.ready := !fullReg | (io.deq.valid & io.deq.ready)
  io.deq.valid := !emptyReg
  //printf(p"$io\n")
  val isEmpty = IO(Output(Bool()))
  isEmpty := emptyReg
}

/**
  * FIFO IO with enqueue and dequeue ports using the ready/valid interface.
  */
class FifoIO[T <: Data](private val gen: T) extends Bundle {
  val enq = Flipped(new DecoupledIO(gen))
  val deq = new DecoupledIO(gen)
}

abstract class Fifo[T <: Data ]( gen: T, val depth: Int) extends Module  with RequireSyncReset{
  val io = IO(new FifoIO(gen))
  assert(depth > 0, "Number of buffer elements needs to be larger than 0")
}

class robFifo[T <: Data ]( gen: T, depth: Int) extends Fifo(gen:
  T, depth: Int) {

  val incrRead = WireDefault(false.B)
  val incrWrite = WireDefault(false.B)

  val modify = IO(Input(Bool()))
  val modifyVal = IO(Input(UInt(log2Ceil(depth).W)))

  val readReg = RegInit (0.U(log2Ceil(depth).W))
  val nextRead = Mux(readReg === (depth -1).U, 0.U, readReg + 1.U)
  when (incrRead) {
    readReg := nextRead
  }

  val writeReg = RegInit(0.U(log2Ceil(depth).W))
  val nextWrite = Mux(writeReg === (depth - 1).U, 0.U, writeReg + 1.U)

  val fullReg = RegInit(false.B)
  // the register based memory
  val memReg = Mem(depth, gen)
  val readPtr = readReg
  val writePtr = writeReg
  val emptyReg = RegInit(true.B)

  val nextval = Mux(modifyVal === (depth - 1).U, 0.U, modifyVal + 1.U)

  when (modify){
    //val nextval = modifyVal
    writeReg := nextval
    //fullReg := nextval === readPtr
    emptyReg := nextval === readPtr  //leon
  }.elsewhen(incrWrite){
    writeReg := nextWrite
  }




  when(io.deq.ready && io.deq.valid && io.enq.valid && io.enq.ready) {
    memReg(writePtr) := io.enq.bits
    incrWrite := true.B
    incrRead := true.B
  }.elsewhen(io.enq.valid && io.enq.ready) {
    memReg(writePtr) := io.enq.bits
    emptyReg := false.B
    fullReg := nextWrite === readPtr
    incrWrite := true.B
  }.elsewhen(io.deq.ready && io.deq.valid) {
    fullReg := false.B
    emptyReg := nextRead === writePtr
    incrRead := true.B
  }

  io.deq.bits := memReg(readPtr)
  io.enq.ready := (!fullReg | (io.deq.valid & io.deq.ready)) & !modify
  io.deq.valid := !emptyReg & !modify
  //printf(p"$io\n")
}

class robResultsFifo[T <: Data ]( gen: T, depth: Int, numWritePorts: Int) extends robFifo(gen: T, depth: Int){
  class robWriteport extends Bundle{
    val valid = Input(Bool())
    val data = Input(gen)
    val addr = Input(UInt(log2Ceil(depth).W))
  }

  // Result write ports
  val writeports = IO(Vec(numWritePorts,new robWriteport))


  for (i <- 0 until writeports.length){
    when(writeports(i).valid) {
      memReg(writeports(i).addr) := writeports(i).data
    }
  }

  val allocatedAddr = IO(Output(UInt(log2Ceil(depth).W)))

  allocatedAddr := writePtr

}
