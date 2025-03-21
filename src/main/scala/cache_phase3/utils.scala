package cache_phase3

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import cache_phase3.constants._
import dataclass.data

class moduleCounter(n: Int) extends Module {
  val width = if (n == 1) 1 else log2Ceil(n)
  val count  = IO(Output(UInt(width.W)))  // Apply the same width logic to count
  val incrm  = IO(Input(Bool()))
  
  // Determine the bit width dynamically
  assert(n > 0, "Number of buffer elements needs to be larger than 0")

  val cntReg = RegInit(0.U(width.W))  // Apply the width logic to cntReg
  cntReg := Mux(incrm && cntReg === n.U, 0.U,
                Mux(incrm, cntReg + 1.U, cntReg))
  count := cntReg
}

class moduleForwardingMemory (addrWidth : Int, dataWidth : Int, depth : Int) extends Module {
  val rdAddr = IO(Input(UInt (addrWidth.W)))
  val rdData = IO(Output (UInt (dataWidth.W)))
  val wrAddr = IO(Input(UInt (addrWidth.W)))
  val wrData = IO(Input(UInt (dataWidth.W)))
  val wrEna = IO(Input(Bool ()))

  val mem = SyncReadMem (depth , UInt (dataWidth.W))

  val wrDataReg = RegNext (wrData )
  val doForwardReg = RegNext (wrAddr === rdAddr && wrEna)

  val memData = mem.read(rdAddr )

  when(wrEna) {
    mem. write (wrAddr , wrData )
  }
  rdData:= Mux( doForwardReg , wrDataReg , memData )
}

object ChiselUtils {
  def zeroInit[T <: Bundle](bundle: T): Unit = {
    bundle.getElements.foreach {
      case nestedBundle: Bundle => zeroInit(nestedBundle) // Recursive call for nested Bundles
      case boolField: Bool => boolField := false.B
      case uintField: UInt => uintField := 0.U
      case _ => // Do nothing for unsupported types
    }
  }
  def fifoWriteUpdate[bufT <: requestTrait, reqT <:  requestTrait](
      buffer: requestTrait,
      branchOps: branchOps, 
      request: requestTrait
  ): Unit = {
    when(branchOps.passed) {
      // BranchPass and match
      when((request.branchMask & branchOps.branchMask).orR) {
        buffer.branchMask := request.branchMask ^ branchOps.branchMask
      }.otherwise {
        buffer.branchMask := request.branchMask
      }
      buffer.valid := request.valid 
    }.otherwise {
      // BranchFail and match
      when((buffer.branchMask & branchOps.branchMask).orR) {
        buffer.valid := false.B
      }.otherwise {
        buffer.valid := request.valid
      }
      buffer.branchMask := request.branchMask
    }
  }
  def fifoReadUpdate[bufT <: requestTrait, reqT <:  requestTrait](
      buffer: requestTrait,
      branchOps: branchOps, 
      request: requestTrait,
      reg : Bool
  ): Unit = {
    when(branchOps.valid) {
      when(branchOps.passed) {
        //BranchPass and match
        when((buffer.branchMask & branchOps.branchMask).orR) {
          request.branchMask := buffer.branchMask ^ branchOps.branchMask
        }.otherwise {
          //BranchPass and no match
          request.branchMask := buffer.branchMask
        }
        request.valid := !reg && buffer.valid
      }.otherwise {
        //BranchFail and match
        when((buffer.branchMask & branchOps.branchMask).orR) {
          request.branchMask := 0.U
          request.valid := false.B
        } .otherwise{
          //BranchFail and no match
          request.branchMask := buffer.branchMask
          request.valid := !reg && buffer.valid
        }
      }
    }.otherwise {
      request.branchMask := buffer.branchMask
      request.valid := !reg && buffer.valid
    }    
  }
  def branchUpdateWithinReg[T <: requestTrait](buffer: T, branchOps: branchOps): Unit = {
    when(buffer.valid && branchOps.valid) {
      when(branchOps.passed) {
        when((buffer.branchMask & branchOps.branchMask).orR) {
          buffer.branchMask := buffer.branchMask ^ branchOps.branchMask
        }
      }.otherwise {
        when((buffer.branchMask & branchOps.branchMask).orR) {
          buffer match {
            case buf: requestWithBranchInvalid => buf.branchInvalid := false.B
            case buf: requestTrait => buf.valid := false.B
          }
        }
      }
    }
  }
  def outgoingBranchUpdateInvalidate[bufT <: requestWithBranchInvalid, reqT <:  cacheLookupTrait](
      buffer: requestWithBranchInvalid, 
      branchOps: branchOps, 
      request: cacheLookupTrait
  ): Unit = {
    when(branchOps.passed) {
      // BranchPass and match
      when((buffer.branchMask & branchOps.branchMask).orR) {
        request.branchMask := buffer.branchMask ^ branchOps.branchMask
      }.otherwise {
        request.branchMask := buffer.branchMask
      }
      request.valid := buffer.valid && !buffer.branchInvalid
    }.otherwise {
      // BranchFail and match
      when((buffer.branchMask & branchOps.branchMask).orR) {
        request.valid := false.B
      }.otherwise {
        request.valid := buffer.valid && !buffer.branchInvalid
      }
      request.branchMask := buffer.branchMask
    }
  }
  def outgoingBranchUpdateData[bufT <: requestTrait](
      buffer: requestTrait, 
      branchOps: branchOps, 
      request: requestTrait
  ): Unit = {
    when(branchOps.passed) {
      // BranchPass and match
      when((buffer.branchMask & branchOps.branchMask).orR) {
        request.branchMask := buffer.branchMask ^ branchOps.branchMask
      }.otherwise {
        request.branchMask := buffer.branchMask
      }
      request.valid := buffer.valid
    }.otherwise {
      // BranchFail and match
      when((buffer.branchMask & branchOps.branchMask).orR) {
        request.valid := false.B
      }.otherwise {
        request.valid := buffer.valid
      }
      request.branchMask := buffer.branchMask
    }
  }
  def cacheBranchWriteUpdate[bufT <: requestTrait](
      buffer: requestTrait, 
      branchOps: branchOps, 
      request: requestTrait
  ): Unit = {
    when(branchOps.passed) {
      // BranchPass and match
      when((request.branchMask & branchOps.branchMask).orR) {
        buffer.branchMask := request.branchMask ^ branchOps.branchMask
      }
    }.otherwise {
      // BranchFail and match
      when((request.branchMask & branchOps.branchMask).orR) {
        buffer.valid := false.B
      }
      request.branchMask := buffer.branchMask
    }
  }
  def cacheBranchRegUpdate[bufT <: requestTrait, reqT <:  cacheLookupTrait](
      buffer: requestTrait, 
      branchOps: branchOps, 
  ): Unit = {
    when(branchOps.passed) {
      // BranchPass and match
      when((buffer.branchMask & branchOps.branchMask).orR) {
        buffer.branchMask := buffer.branchMask ^ branchOps.branchMask
      }
    }.otherwise {
      // BranchFail and match
      when((buffer.branchMask & branchOps.branchMask).orR) {
        buffer.valid := false.B
      }
      buffer.branchMask := buffer.branchMask
    }
  }
}