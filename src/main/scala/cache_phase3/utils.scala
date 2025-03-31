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
  //To initalize given object fields to zero, or false
  def zeroInit[T <: Bundle](bundle: T): Unit = {
    bundle.getElements.foreach {
      case nestedBundle: Bundle => zeroInit(nestedBundle) // Recursive call for nested Bundles
      case boolField: Bool => boolField := false.B
      case uintField: UInt => uintField := 0.U
      case _ => // Do nothing for unsupported types
    }
  }

  def regWriteUpdate[T <: branchTrait](
      buffer: branchTrait,
      branchOps: branchOps, 
      wireBundle: branchTrait
  ): Unit = {
    when(branchOps.valid){
      when(branchOps.passed) {
        // BranchPass and match
        when((wireBundle.mask & branchOps.branchMask).orR) {
          buffer.mask := wireBundle.mask ^ branchOps.branchMask
        }.otherwise {
          buffer.mask := wireBundle.mask
        }
        buffer.valid := wireBundle.valid 
      }.otherwise {
        // BranchFail and match
        when((buffer.mask & branchOps.branchMask).orR) {
          buffer.valid := false.B
          buffer.mask := 0.U
        }.otherwise {
          buffer.valid := wireBundle.valid
          buffer.mask := wireBundle.mask
        }
      }
    } .otherwise {
        buffer.valid := wireBundle.valid 
        buffer.mask := wireBundle.mask
    }
  }

  def regReadUpdate[T <: branchTrait](
    wireBundle: branchTrait,
    branchOps: branchOps,
    buffer: branchTrait
  ): Unit = {
    when(branchOps.valid) {
      when(branchOps.passed) {
        //BranchPass and match
        when((buffer.mask & branchOps.branchMask).orR) {
          wireBundle.mask := buffer.mask ^ branchOps.branchMask
        }.otherwise {
          //BranchPass and no match
          wireBundle.mask := buffer.mask
        }
        wireBundle.valid := buffer.valid
      }.otherwise {
        //BranchFail and match
        when((buffer.mask & branchOps.branchMask).orR) {
          wireBundle.mask := 0.U
          wireBundle.valid := false.B
        } .otherwise{
          //BranchFail and no match
          wireBundle.mask := buffer.mask
          wireBundle.valid := buffer.valid
        }
      }
    }.otherwise {
      wireBundle.mask := buffer.mask
      wireBundle.valid := buffer.valid
    }    
  }

  def regRecordUpdate[T <: branchTrait](
  buffer: T, branchOps: branchOps
  ): Unit = {
    when(buffer.valid) {
      when(branchOps.valid) {
        when(branchOps.passed) {
          when((buffer.mask & branchOps.branchMask).orR) {
            buffer.mask := buffer.mask ^ branchOps.branchMask
          }
          buffer.valid := buffer.valid
        }.otherwise {
          when((buffer.mask & branchOps.branchMask).orR) {
            buffer.valid := false.B
            buffer.mask := 0.U
          }
        }
      // } .otherwise {
      //   buffer.mask := buffer.mask
      //   buffer.valid := buffer.valid      
      }
    }
  }
}