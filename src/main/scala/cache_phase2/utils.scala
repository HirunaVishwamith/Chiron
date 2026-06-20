package DataCache
//Use namespaces if required

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import DataCache.constantsDCache._


class CounterModule(n: Int) extends Module {
  val count   = IO(Output(UInt(log2Ceil(n+1).W)))
  val incrm  = IO(Input(Bool()))
  
  assert(n > 0, "Number of buffer elements needs to be larger than 0")
  val cntReg = RegInit(0.U(log2Ceil(n+1).W))
  cntReg := Mux(incrm && cntReg === n.U, 0.U,
                Mux(incrm, cntReg + 1.U, cntReg))

  count := cntReg
}

class CacheGenModule(addrWidth : Int, dataWidth : Int, depth : Int) extends Module {
  val requestAddr = IO(Input(UInt(addrWidth.W)))
  val readData = IO(Output(UInt(dataWidth.W)))
  val writeData = IO(Input(UInt(dataWidth.W)))
  val writeEn = IO(Input(Bool()))

  val mem = SyncReadMem(depth, UInt(dataWidth.W))

  readData := mem.read(requestAddr)
  when(writeEn){
    mem.write(requestAddr,writeData)
  }
}
class FifoIO[T <: Data](private val gen: T) extends Bundle {
  val enq = Flipped(new DecoupledIO(gen))
  val deq = new DecoupledIO(gen)
}

/**
  * Base class for all FIFOs.
  */
abstract class Fifo[T <: Data](gen: T, depth: Int) extends Module {
  val io = IO(new FifoIO(gen))

  assert(depth > 0, "Number of buffer elements needs to be larger than 0")
}

class RegFifo[T <: Data](gen: T, depth: Int) extends Fifo(gen: T, depth: Int) {

  def counter(depth: Int, incr: Bool): (UInt, UInt) = {
    val cntReg = RegInit(0.U(log2Ceil(depth).W))
    val nextVal = Mux(cntReg === (depth-1).U, 0.U, cntReg + 1.U)
    when (incr) {
      cntReg := nextVal
    }
    (cntReg, nextVal)
  }

  // the register based memory
  val memReg = Reg(Vec(depth, gen))

  val incrRead = WireInit(false.B)
  val incrWrite = WireInit(false.B)
  val (readPtr, nextRead) = counter(depth, incrRead)
  val (writePtr, nextWrite) = counter(depth, incrWrite)

  val emptyReg = RegInit(true.B)
  val fullReg = RegInit(false.B)

  when (io.enq.valid && !fullReg) {
    memReg(writePtr) := io.enq.bits
    emptyReg := false.B
    fullReg := nextWrite === readPtr
    incrWrite := true.B
  }

  when (io.deq.ready && !emptyReg) {
    fullReg := false.B
    emptyReg := nextRead === writePtr
    incrRead := true.B
  }

  io.deq.bits := memReg(readPtr)
  io.enq.ready := !fullReg
  io.deq.valid := !emptyReg
}