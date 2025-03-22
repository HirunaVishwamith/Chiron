package cache_phase3

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import cache_phase3.constants._
import cache_phase3._
import cache_phase3.ChiselUtils._

class Scheduler extends Module{
  val requestIn = IO(new request)
  val canAllocate = IO(Output(Bool()))
  val requestOut = IO(Flipped(new request))
  val controlSignal = IO(new Bundle {
    val isSpeculative = Output(Bool())
    val inorderReady = Input(Bool())
    val speculativeReady = Input(Bool())
  })
  val fenceReady = IO(Output(Bool()))
  val branchOps = IO(new branchOps())

  canAllocate := false.B
  zeroInit(requestOut)
  controlSignal.isSpeculative := false.B
  fenceReady := false.B
  
  val inorderQueue = Module(new fifoWithAddrCheck(
    depth = schedulerDepth,
    traitType = new requestWire
  ))
  val speculativeQueue = Module(new fifoWithBranchOpsI(
    depth = schedulerDepth,
    traitType = new requestWire
  ))

  inorderQueue.checkAddress := requestIn.address
  inorderQueue.read.ready := false.B
  zeroInit(inorderQueue.write.data)

  speculativeQueue.read.ready := false.B
  zeroInit(speculativeQueue.write.data)

  val speculativeEntryWire = WireDefault(requestIn.instruction(6,2) === "b00000".U && requestIn.address =/= FIFO_ADDR_RX.U)

  //-------------Enqueue----------------//
  when(requestIn.valid){
    when(speculativeEntryWire && inorderQueue.matchFound){
      inorderQueue.write.data <> requestIn
    } .elsewhen(speculativeEntryWire){
      speculativeQueue.write.data <> requestIn
    }.otherwise{
      inorderQueue.write.data <> requestIn
    }
  } .otherwise{
    inorderQueue.write.data.valid := false.B
    speculativeQueue.write.data.valid := false.B
  }
  inorderQueue.branchOps := branchOps
  speculativeQueue.branchOps := branchOps

  //-------------Dequeue----------------//
  //* Priority Order when either speculative or inorder are requested
  //*    1.  Branch resolved speculative
  //*    2.  Branch resolved inorder
  //*    3.  Branch unresolved speculative
  val speculativeBranchResolved = WireDefault(!speculativeQueue.read.data.branchMask(3,0).orR && !speculativeQueue.isEmpty)
  val speculativeBranchInvalidated = WireDefault(!speculativeQueue.read.data.valid && speculativeQueue.read.data.branchMask(3,0).orR)
  val inorderBranchResolved = WireDefault(!inorderQueue.read.data.branchMask(3,0).orR && !inorderQueue.isEmpty)
  val inorderBranchInvalidated = WireDefault(!inorderQueue.read.data.valid && inorderQueue.read.data.branchMask(3,0).orR)

  when(controlSignal.inorderReady || controlSignal.speculativeReady){
    switch(controlSignal.inorderReady ## controlSignal.speculativeReady){
      is("b00".U){}
      is("b01".U){
        speculativeQueue.read.ready:= !speculativeQueue.isEmpty
        controlSignal.isSpeculative := true.B
        requestOut := speculativeQueue.read.data
      }
      is("b10".U){
        inorderQueue.read.ready := !inorderQueue.isEmpty && !inorderQueue.read.data.branchMask(3,0).orR
        controlSignal.isSpeculative  := false.B
        requestOut := inorderQueue.read.data
      }
      is("b11".U){
        when(speculativeBranchResolved|| speculativeBranchInvalidated){
          speculativeQueue.read.ready:= !speculativeQueue.isEmpty
          controlSignal.isSpeculative  := true.B
          requestOut := speculativeQueue.read.data
        } .elsewhen(inorderBranchResolved || inorderBranchInvalidated){
          inorderQueue.read.ready := !inorderQueue.isEmpty
          controlSignal.isSpeculative  := false.B
          requestOut := inorderQueue.read.data
        } .elsewhen(!speculativeQueue.isEmpty || speculativeBranchInvalidated){
          speculativeQueue.read.ready:= !speculativeQueue.isEmpty
          controlSignal.isSpeculative  := true.B
          requestOut := speculativeQueue.read.data
        }.otherwise{
          inorderQueue.read.ready := false.B
          speculativeQueue.read.ready := false.B  
        }
      }
    }
  }
  // Output connections
  canAllocate := inorderQueue.write.ready && speculativeQueue.write.ready
  fenceReady := inorderQueue.isEmpty && speculativeQueue.isEmpty
}