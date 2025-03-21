
package l2_cache

import Chisel.log2Ceil
import chisel3._
import chisel3.util._

class FifoIO[T <: Data](private val gen: T) extends Bundle {
  val enq = Flipped(new DecoupledIO(gen))
  val deq = new DecoupledIO(gen)
}

abstract class Fifo[T <: Data ]( gen: T, val depth: Int) extends Module  with RequireSyncReset{
  val io = IO(new FifoIO(gen))
  assert(depth > 0, "Number of buffer elements needs to be larger than 0")
}

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
}