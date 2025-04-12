package cache_phase3

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import cache_phase3.constants._
import cache_phase3._
import cache_phase3.ChiselUtils._

class requestScheduler extends Module{
  val requestIn = IO(Input(new requestPipelineWire))
  val canAllocate = IO(Output(Bool()))
  val requestOut = IO(Output(new requestPipelineWire))
  val controlSignal = IO(new Bundle {
    val isSpeculative = Output(Bool())
    val inorderReady = Input(Bool())
    val speculativeReady = Input(Bool())
  })
  val fenceReady = IO(Output(Bool()))
  val branchOps = IO(new branchOps())

  //!Debug only
  val isPauseForBranch = WireDefault(pauseForBranch.B)

  canAllocate := false.B
  zeroInit(requestOut)
  controlSignal.isSpeculative := false.B
  fenceReady := false.B
  
  val inorderQueue = Module(new fifoWithAddrCheck(
    depth = schedulerDepth,
    traitType = new requestPipelineWire,
    width = 3
  ))
  val speculativeQueue = Module(new fifoWithBranchOps(
    depth = schedulerDepth,
    traitType = new requestPipelineWire
  ))

  inorderQueue.checkAddress := requestIn.address
  inorderQueue.read.ready := false.B
  zeroInit(inorderQueue.write.data)

  speculativeQueue.read.ready := false.B
  zeroInit(speculativeQueue.write.data)

  val speculativeEntryWire = WireDefault(requestIn.core.instruction(6,2) === "b00000".U && requestIn.address =/= FIFO_ADDR_RX.U)

  //-------------Enqueue----------------//
  when(requestIn.valid && requestIn.branch.valid){
    when(speculativeEntryWire && inorderQueue.matchFound){
      inorderQueue.write.data := requestIn
      regReadUpdate(inorderQueue.write.data.branch, branchOps, requestIn.branch)
    } .elsewhen(speculativeEntryWire){
      speculativeQueue.write.data := requestIn
      regReadUpdate(speculativeQueue.write.data.branch, branchOps, requestIn.branch)
    }.otherwise{
      inorderQueue.write.data := requestIn
      regReadUpdate(inorderQueue.write.data.branch, branchOps, requestIn.branch)
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
  val speculativeBranchResolved = WireDefault(!speculativeQueue.read.data.branch.mask(3,0).orR && !speculativeQueue.isEmpty)
  val speculativeBranchInvalidated = WireDefault(!speculativeQueue.read.data.branch.valid && speculativeQueue.read.data.branch.mask(3,0).orR && !speculativeQueue.isEmpty)
  val inorderBranchResolved = WireDefault(!inorderQueue.read.data.branch.mask(3,0).orR && !inorderQueue.isEmpty)
  val inorderBranchInvalidated = WireDefault(!inorderQueue.read.data.branch.valid && inorderQueue.read.data.branch.mask(3,0).orR && !inorderQueue.isEmpty)

  when((controlSignal.inorderReady || controlSignal.speculativeReady) && !(isPauseForBranch && branchOps.valid)){
    switch(controlSignal.inorderReady ## controlSignal.speculativeReady){
      is("b00".U){}
      is("b01".U){
        when(!speculativeQueue.isEmpty || speculativeBranchInvalidated){
          speculativeQueue.read.ready:= !speculativeQueue.isEmpty
          controlSignal.isSpeculative := true.B
          requestOut := speculativeQueue.read.data
        }
      }
      is("b10".U){
        when(inorderBranchResolved || inorderBranchInvalidated){
          inorderQueue.read.ready := !inorderQueue.isEmpty
          controlSignal.isSpeculative  := false.B
          requestOut := inorderQueue.read.data
        }
      }
      is("b11".U){
        when(speculativeBranchResolved || speculativeBranchInvalidated){

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

  //Resource optimization
  speculativeQueue.write.data.cacheLine.cacheLine := 0.U
  speculativeQueue.write.data.cacheLine.required := false.B
  speculativeQueue.write.data.cacheLine.response := 0.U

  inorderQueue.write.data.cacheLine.cacheLine := 0.U
  inorderQueue.write.data.cacheLine.required := false.B
  inorderQueue.write.data.cacheLine.response := 0.U
}