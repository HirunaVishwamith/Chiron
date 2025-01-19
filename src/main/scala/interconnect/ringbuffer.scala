package Interconnect

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._

class ringbuffer(depth:Int) extends Module with RequireSyncReset{
	assert(depth > 0, "Number of buffer elements needs to be larger than 0")
	val io = IO(new Bundle{
	val enq = Flipped(new DecoupledIO(UInt(70.W))) //width=32
	val deq = new DecoupledIO(UInt(70.W))
  })

    val readReg=RegInit(0.U(log2Ceil(depth).W))
    val writeReg=RegInit(0.U(log2Ceil(depth).W))
    val nextRead=Mux(readReg===(depth-1).U,0.U,readReg+1.U)
    val nextWrite=Mux(writeReg===(depth-1).U,0.U,writeReg+1.U)

    val memReg= Mem(depth,UInt(70.W))
    val readPtr=readReg
    val writePtr=writeReg
    val fullReg=RegInit(false.B)
    val emptyReg=RegInit(true.B)

    val incrRead=WireDefault(false.B)
    val incrWrite=WireDefault(false.B)

    //val modify=IO(Input(Bool()))
    //val modifyVal=IO(Input(UInt()))

    when(incrRead){
      readReg:=nextRead
    }

    when(incrWrite){
      writeReg:=nextWrite
    }

    when(io.enq.valid && io.enq.ready && io.deq.valid &&io.deq.ready){
      incrRead:=true.B
      incrWrite:=true.B
      memReg(writePtr):=io.enq.bits
    }.elsewhen(io.enq.valid && io.enq.ready){
      incrWrite:=true.B
      emptyReg:=false.B
      fullReg:=(nextWrite===readPtr)
      memReg(writePtr):=io.enq.bits
    }.elsewhen(io.deq.valid && io.deq.ready){
      incrRead:=true.B
      fullReg:=false.B
      emptyReg:=(nextRead===writePtr)
    }

    io.deq.bits:=memReg(readPtr)
    io.enq.ready:=(!fullReg)
    io.enq.ready:=(!fullReg || (io.deq.ready && io.deq.valid))
    io.deq.valid:=(!emptyReg)

}
