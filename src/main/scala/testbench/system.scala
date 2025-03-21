//soc simulation implementation


import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import chisel3.experimental.IO

import pipeline.ports._
import common.coreConfiguration._
import cache.AXI
import _root_.testbench.mainMemory
import _root_.testbench.uart
import decode.constants
import dataclass.data
import _root_.testbench.simulatedMemory
import Interconnect._


class system extends Module {

  val core0 = Module(new core(
    dPort_id = 0,
    peripheral_id = 0,
    iPort_id = 1,
    mhart_id = 0
  ){
    val registersOut = IO(Output(decode.registersOut.cloneType))
    val architecturalRegisterFile = VecInit(decode.retiredRenamedTable.table.map(i => prf.registerFileOutput(i)))
    registersOut zip architecturalRegisterFile foreach { case(x, y) => x := y }
    registersOut.reverse.head := decode.registersOut.head

    val robOut = IO(Output(new Bundle() {
      val commitFired = Bool()
      val pc         = UInt(64.W)
      val interrupt = Bool()
    }))
    robOut.commitFired := rob.commit.fired
    robOut.pc          := rob.commit.pc
    robOut.interrupt   := decode.writeBackResult.instruction === "h80000073".U(64.W)
    when((rob.commit.instruction(6, 0) === "b1110011".U) && (rob.commit.instruction(14, 12).orR)) { robOut.commitFired := false.B }

    val allRobFiresOut = IO(Output(Bool()))
    allRobFiresOut := rob.commit.fired
  })

  val memory = Module(new mainMemory)
  val interconnect = Module(new Interconnect)
  //val LLC = Module(new l2_mem)  

  //core's IOS
  //iPort ACE, dPort ACE, peripheral port AXI, MTIP

  //core0.dPort to interconnect connection
  //AW
  interconnect.io.acePort0.AWVALID := core0.dPort.AWVALID
  core0.dPort.AWREADY := interconnect.io.acePort0.AWREADY
  interconnect.io.acePort0.AWID := core0.dPort.AWID
  interconnect.io.acePort0.AWADDR := core0.dPort.AWADDR
  interconnect.io.acePort0.AWSNOOP := core0.dPort.AWSNOOP
  interconnect.io.acePort0.AWBAR := core0.dPort.AWBAR

  //W
  interconnect.io.acePort0.WVALID := core0.dPort.WVALID
  interconnect.io.acePort0.WDATA := core0.dPort.WDATA
  interconnect.io.acePort0.WLAST := core0.dPort.WLAST
  core0.dPort.WREADY := interconnect.io.acePort0.WREADY

  //B
  core0.dPort.BVALID := interconnect.io.acePort0.BVALID
  core0.dPort.BID := interconnect.io.acePort0.BID
  core0.dPort.BRESP := interconnect.io.acePort0.BRESP
  interconnect.io.acePort0.BREADY := core0.dPort.BREADY

  //AR
  interconnect.io.acePort0.ARVALID := core0.dPort.ARVALID
  core0.dPort.ARREADY := interconnect.io.acePort0.ARREADY
  interconnect.io.acePort0.ARID := core0.dPort.ARID
  interconnect.io.acePort0.ARADDR := core0.dPort.ARADDR
  interconnect.io.acePort0.ARSNOOP := core0.dPort.ARSNOOP
  interconnect.io.acePort0.ARBAR := core0.dPort.ARBAR

  //R
  core0.dPort.RVALID := interconnect.io.acePort0.RVALID
  interconnect.io.acePort0.RREADY := core0.dPort.RREADY
  core0.dPort.RID := interconnect.io.acePort0.RID
  core0.dPort.RDATA := interconnect.io.acePort0.RDATA
  core0.dPort.RRESP := interconnect.io.acePort0.RRESP
  core0.dPort.RLAST := interconnect.io.acePort0.RLAST

  //AC
  core0.dPort.ACVALID := interconnect.io.acePort0.ACVALID
  core0.dPort.ACADDR := interconnect.io.acePort0.ACADDR
  core0.dPort.ACSNOOP := interconnect.io.acePort0.ACSNOOP
  core0.dPort.ACPROT := 2.U
  interconnect.io.acePort0.ACREADY := core0.dPort.ACREADY

  //CR
  interconnect.io.acePort0.CRVALID := core0.dPort.CRVALID
  interconnect.io.acePort0.CRRESP := core0.dPort.CRRESP
  core0.dPort.CRREADY := interconnect.io.acePort0.CRREADY

  //CD
  interconnect.io.acePort0.CDVALID := core0.dPort.CDVALID
  core0.dPort.CDREADY := interconnect.io.acePort0.CDREADY
  interconnect.io.acePort0.CDDATA := core0.dPort.CDDATA
  interconnect.io.acePort0.CDLAST := core0.dPort.CDLAST

  //core0.iPort to interconnect connection
  //AW
  interconnect.io.acePort1.AWVALID := core0.iPort.AWVALID
  core0.iPort.AWREADY := interconnect.io.acePort1.AWREADY
  interconnect.io.acePort1.AWID := core0.iPort.AWID
  interconnect.io.acePort1.AWADDR := core0.iPort.AWADDR
  interconnect.io.acePort1.AWSNOOP := core0.iPort.AWSNOOP
  interconnect.io.acePort1.AWBAR := core0.iPort.AWBAR

  //W
  interconnect.io.acePort1.WVALID := core0.iPort.WVALID
  interconnect.io.acePort1.WDATA := core0.iPort.WDATA
  interconnect.io.acePort1.WLAST := core0.iPort.WLAST
  core0.iPort.WREADY := interconnect.io.acePort1.WREADY

  //B
  core0.iPort.BVALID := interconnect.io.acePort1.BVALID
  core0.iPort.BID := interconnect.io.acePort1.BID
  core0.iPort.BRESP := interconnect.io.acePort1.BRESP
  interconnect.io.acePort1.BREADY := core0.iPort.BREADY

  //AR
  interconnect.io.acePort1.ARVALID := core0.iPort.ARVALID
  core0.iPort.ARREADY := interconnect.io.acePort1.ARREADY
  interconnect.io.acePort1.ARID := core0.iPort.ARID
  interconnect.io.acePort1.ARADDR := core0.iPort.ARADDR
  interconnect.io.acePort1.ARSNOOP := core0.iPort.ARSNOOP
  interconnect.io.acePort1.ARBAR := core0.iPort.ARBAR

  //R
  core0.iPort.RVALID := interconnect.io.acePort1.RVALID
  interconnect.io.acePort1.RREADY := core0.iPort.RREADY
  core0.iPort.RID := interconnect.io.acePort1.RID
  core0.iPort.RDATA := interconnect.io.acePort1.RDATA
  core0.iPort.RRESP := interconnect.io.acePort1.RRESP
  core0.iPort.RLAST := interconnect.io.acePort1.RLAST

  //AC
  core0.iPort.ACVALID := interconnect.io.acePort1.ACVALID
  core0.iPort.ACADDR := interconnect.io.acePort1.ACADDR
  core0.iPort.ACSNOOP := interconnect.io.acePort1.ACSNOOP
  core0.iPort.ACPROT := 2.U
  interconnect.io.acePort1.ACREADY := core0.iPort.ACREADY

  //CR
  interconnect.io.acePort1.CRVALID := core0.iPort.CRVALID
  interconnect.io.acePort1.CRRESP := core0.iPort.CRRESP
  core0.iPort.CRREADY := interconnect.io.acePort1.CRREADY

  //CD
  interconnect.io.acePort1.CDVALID := core0.iPort.CDVALID
  core0.iPort.CDREADY := interconnect.io.acePort1.CDREADY
  interconnect.io.acePort1.CDDATA := core0.iPort.CDDATA
  interconnect.io.acePort1.CDLAST := core0.iPort.CDLAST

  
  //LLC connection with memory
  //AW
  memory.clients(1).AWVALID := interconnect.io.L2.AWVALID
  interconnect.io.L2.AWREADY := memory.clients(1).AWREADY
  memory.clients(1).AWID := interconnect.io.L2.AWID
  memory.clients(1).AWADDR := interconnect.io.L2.AWADDR
  //memory.clients(1).AWSNOOP := 0.U
  //memory.clients(1).AWBAR := 0.U
  //memory.clients(1).AWDOMAIN := 0.U
  memory.clients(1).AWLEN := 7.U
  memory.clients(1).AWSIZE := 3.U
  memory.clients(1).AWBURST := 1.U
  memory.clients(1).AWLOCK := 0.U
  memory.clients(1).AWCACHE := 2.U
  memory.clients(1).AWPROT := 0.U
  memory.clients(1).AWQOS := 0.U

  //AR
  memory.clients(1).ARVALID := interconnect.io.L2.ARVALID
  interconnect.io.L2.ARREADY := memory.clients(1).ARREADY
  memory.clients(1).ARID := interconnect.io.L2.ARID
  memory.clients(1).ARADDR := interconnect.io.L2.ARADDR
  //memory.clients(1).ARSNOOP := 0.U
  //memory.clients(1).ARBAR := 0.U
  //memory.clients(1).ARDOMAIN := 0.U
  memory.clients(1).ARLEN := 7.U
  memory.clients(1).ARSIZE := 3.U
  memory.clients(1).ARBURST := 1.U
  memory.clients(1).ARLOCK := 0.U
  memory.clients(1).ARCACHE := 2.U
  memory.clients(1).ARPROT := 0.U
  memory.clients(1).ARQOS := 0.U


  //W
  memory.clients(1).WVALID := interconnect.io.L2.WVALID
  interconnect.io.L2.WREADY := memory.clients(1).WREADY
  memory.clients(1).WDATA := interconnect.io.L2.WDATA
  memory.clients(1).WLAST := interconnect.io.L2.WLAST
  memory.clients(1).WSTRB := "b11111111".U

  //R
  interconnect.io.L2.RVALID := memory.clients(1).RVALID
  memory.clients(1).RREADY := interconnect.io.L2.RREADY
  interconnect.io.L2.RID := memory.clients(1).RID
  interconnect.io.L2.RDATA := memory.clients(1).RDATA
  interconnect.io.L2.RLAST := memory.clients(1).RLAST
  interconnect.io.L2.RRESP := memory.clients(1).RRESP(1,0)


  //B
  interconnect.io.L2.BVALID := memory.clients(1).BVALID
  memory.clients(1).BREADY := interconnect.io.L2.BREADY
  interconnect.io.L2.BID := memory.clients(1).BID
  interconnect.io.L2.BRESP := memory.clients(1).BRESP




  //memory.clients(0) should be unconnedted and pulled down
  memory.clients(0).AWVALID := false.B
  memory.clients(0).AWID := 0.U
  memory.clients(0).AWADDR := 0.U
  memory.clients(0).AWLEN := 7.U
  memory.clients(0).AWSIZE := 5.U
  memory.clients(0).AWBURST := 1.U
  memory.clients(0).AWLOCK := 0.U
  memory.clients(0).AWCACHE := 2.U
  memory.clients(0).AWPROT := 0.U
  memory.clients(0).AWQOS := 0.U

  //AR
  memory.clients(0).ARVALID := false.B
  memory.clients(0).ARID := 0.U
  memory.clients(0).ARADDR := 0.U
  memory.clients(0).ARLEN := 7.U
  memory.clients(0).ARSIZE := 5.U
  memory.clients(0).ARBURST := 1.U
  memory.clients(0).ARLOCK := 0.U
  memory.clients(0).ARCACHE := 2.U
  memory.clients(0).ARPROT := 0.U
  memory.clients(0).ARQOS := 0.U

  //W
  memory.clients(0).WVALID := false.B
  memory.clients(0).WDATA := 0.U
  memory.clients(0).WLAST := 0.U
  memory.clients(0).WSTRB := "b11111111".U

  //R
  memory.clients(0).RREADY := false.B

  //B
  memory.clients(0).BREADY := false.B


  //Programming mainMemory
  val programmer = IO(Input(memory.programmer.cloneType))
  memory.programmer := programmer

  val finishedProgramming = IO(Input(memory.finishedProgramming.cloneType))
  memory.finishedProgramming := finishedProgramming

  val peripheralUart = Module(new uart{
    val putCharOut = IO(Output(putChar.cloneType))
    putCharOut := putChar
  })

  val putChar = IO(Output(peripheralUart.putCharOut.cloneType))
  putChar := peripheralUart.putCharOut

  core0.peripheral <> peripheralUart.client

  val prober = IO(memory.externalProbe.cloneType)
  prober <> memory.externalProbe

  val registersOut = IO(Output(core0.registersOut.cloneType))
  val registersOutBuffer = Reg(registersOut.cloneType)
  registersOut := Mux(core0.robOut.commitFired && RegNext(core0.robOut.commitFired, false.B), core0.registersOut ,registersOutBuffer)
  registersOut(32) := core0.registersOut(32)

  val robOut = IO(Output(core0.robOut.cloneType))
  robOut := core0.robOut
  when(RegNext(core0.allRobFiresOut, false.B)) { registersOutBuffer := core0.registersOut }

  /* val sampleOut = IO(Output(core0.sampleOut.cloneType))
  sampleOut := core0.sampleOut

  core0.MTIP := peripheralUart.MTIP
 */
  core0.MTIP := peripheralUart.MTIP
  // core0.PWR_ON := true.B
  //val sample = IO(Output(core0.status.cloneType))
  //sample := core0.status

  //val storesPendingOut = IO(Output(core0.storesPendingOut.cloneType))
  //storesPendingOut := core0.storesPendingOut

  //val robAddrRelease = IO(Output(core0.robAddrRelease.cloneType))
  //robAddrRelease := core0.robAddrRelease

  //val robOfDataQueue = IO(Output(core0.dataQueueRobRelease.cloneType))
  //robOfDataQueue := core0.dataQueueRobRelease
}

object system extends App {
  emitVerilog(new system)
}
