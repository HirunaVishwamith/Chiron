package decode

import chisel3._
import chisel3.util.{Cat, Fill, is, switch}
import decode.constants._

object utils {
  def getInsType(opcode: UInt): UInt = {
    val insType = WireDefault(0.U(3.W))
    switch(opcode) {
      is(lui.U) {
        insType := utype.U
      }
      is(auipc.U) {
        insType := utype.U
      }
      is(jump.U) {
        insType := jtype.U
      }
      is(jumpr.U) {
        insType := itype.U
      }
      is(cjump.U) {
        insType := btype.U
      }
      is(load.U) {
        insType := itype.U
      }
      is(store.U) {
        insType := stype.U
      }
      is(iops.U) {
        insType := itype.U
      }
      is(iops32.U) {
        insType := itype.U
      }
      is(rops.U) {
        insType := rtype.U
      }
      is(rops32.U) {
        insType := rtype.U
      }
      is(system.U) {
        insType := itype.U
      }
      is(fence.U) {
        insType := ntype.U
      }
      is(amos.U) {
        insType := rtype.U
      }
    }
    insType
  }

  def getImmediate(instruction: UInt, instrucitonType: UInt): UInt = {
    val immediate = WireDefault(0.U(dataWidth.W))
    switch(instrucitonType) {
      is(itype.U) {
        immediate := Cat(Fill(53, instruction(31)), instruction(30, 20))
      }
      is(stype.U) {
        immediate := Cat(Fill(53, instruction(31)), instruction(30, 25), instruction(11, 7))
      }
      is(btype.U) {
        immediate := Cat(Fill(53, instruction(31)), instruction(7), instruction(30, 25), instruction(11, 8), 0.U(1.W))
      }
      is(utype.U) {
        immediate := Cat(Fill(32, instruction(31)), instruction(31, 12), 0.U(12.W))
      }
      is(jtype.U) {
        immediate := Cat(Fill(44, instruction(31)), instruction(19, 12), instruction(20), instruction(30, 25), instruction(24, 21), 0.U(1.W))
      }
      is(ntype.U) {
        immediate := Fill(dataWidth, 0.U)
      }
      is(rtype.U) {
        immediate := Fill(dataWidth, 0.U)
      }
    }
    immediate
  }
}