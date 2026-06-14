import chisel3._
import chisel3.util._
import common.configuration

class ReadWriteSmem extends Module {
  val width: Int = 64
  val depth: Int = 1 << configuration.prfAddrWidth
  val io = IO(new Bundle {
    val wenable = Input(Bool())
    val renable = Input(Bool())
    val raddr = Input(UInt(configuration.prfAddrWidth.W))
    val waddr = Input(UInt(configuration.prfAddrWidth.W))
    val dataIn = Input(UInt(width.W))
    val dataOut = Output(UInt(width.W))
  })

  val mem = SyncReadMem(depth, UInt(width.W))
  // Create one write port and one read port
  when(io.wenable){
    mem.write(io.waddr, io.dataIn)
  }

  io.dataOut := mem.read(io.raddr, io.renable)
}

