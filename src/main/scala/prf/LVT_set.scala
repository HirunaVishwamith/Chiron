import chisel3._
import chisel3.util._

class LVT_set extends Module {
  val io = IO(new Bundle {
    val wenable = Input(Bool())
    val r1enable = Input(Bool())
    val r2enable = Input(Bool())
    val r3enable = Input(Bool())
    val r1addr = Input(UInt(6.W))
    val r2addr = Input(UInt(6.W))
    val r3addr = Input(UInt(6.W))
    val waddr = Input(UInt(6.W))
    val wdata = Input(UInt(64.W))
    val r1data = Output(UInt(64.W))
    val r2data = Output(UInt(64.W))
    val r3data = Output(UInt(64.W))
  })

  val b1 = Module(new ReadWriteSmem)
  val b2 = Module(new ReadWriteSmem)
  val b3 = Module(new ReadWriteSmem)

  b1.io.wenable := io.wenable
  b2.io.wenable := io.wenable
  b3.io.wenable := io.wenable
  b1.io.dataIn := io.wdata
  b2.io.dataIn := io.wdata
  b3.io.dataIn := io.wdata

  b1.io.renable := io.r1enable
  b2.io.renable := io.r2enable
  b3.io.renable := io.r3enable

  io.r1data := b1.io.dataOut
  io.r2data := b2.io.dataOut
  io.r3data := b3.io.dataOut

  b1.io.raddr := io.r1addr
  b2.io.raddr := io.r2addr
  b3.io.raddr := io.r3addr

  b1.io.waddr := io.waddr
  b2.io.waddr := io.waddr
  b3.io.waddr := io.waddr

}

