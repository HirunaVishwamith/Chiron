//soc simulation implementation


import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import chisel3.experimental.IO

import pipeline.ports._
import common.coreConfiguration._
import Icache.AXI
import _root_.testbench.mainMemory
import _root_.testbench.MultiUart
import Decode.constants
import _root_.testbench.simulatedMemory
import Interconnect._
import L2_cache._

// Verilator sim top: chironCore (see testbench/chironCore.scala for the
// 4-core/Interconnect/LLC wiring, shared with the FPGA top in
// testbench/fpgaTop.scala) backed by the simulation-only mainMemory
// (internal SyncReadMem, loaded via the directly-poked programmer port) and
// MultiUart (UART/CLINT peripheral set).
class system extends Module {

  val chiron = Module(new chironCore)
  val memory = Module(new mainMemory)

  //Interconnect L2 connection to Memory (LLC's mem AXI ports <-> mainMemory.clients(1))
  //AW
  memory.clients(1).AWVALID := chiron.mem_write_axi.AWVALID
  memory.clients(1).AWID := chiron.mem_write_axi.AWID
  memory.clients(1).AWADDR := chiron.mem_write_axi.AWADDR
  memory.clients(1).AWLEN := chiron.mem_write_axi.AWLEN
  memory.clients(1).AWSIZE := chiron.mem_write_axi.AWSIZE
  memory.clients(1).AWBURST := chiron.mem_write_axi.AWBURST
  memory.clients(1).AWLOCK := chiron.mem_write_axi.AWLOCK
  memory.clients(1).AWCACHE := chiron.mem_write_axi.AWCACHE
  memory.clients(1).AWPROT := chiron.mem_write_axi.AWPROT
  memory.clients(1).AWQOS := chiron.mem_write_axi.AWQOS
  chiron.mem_write_axi.AWREADY := memory.clients(1).AWREADY

  //AR
  memory.clients(1).ARVALID := chiron.mem_read_axi.ARVALID
  memory.clients(1).ARID := chiron.mem_read_axi.ARID
  memory.clients(1).ARADDR := chiron.mem_read_axi.ARADDR
  memory.clients(1).ARLEN := chiron.mem_read_axi.ARLEN
  memory.clients(1).ARSIZE := chiron.mem_read_axi.ARSIZE
  memory.clients(1).ARBURST := chiron.mem_read_axi.ARBURST
  memory.clients(1).ARLOCK := chiron.mem_read_axi.ARLOCK
  memory.clients(1).ARCACHE := chiron.mem_read_axi.ARCACHE
  memory.clients(1).ARPROT := chiron.mem_read_axi.ARPROT
  memory.clients(1).ARQOS := chiron.mem_read_axi.ARQOS
  chiron.mem_read_axi.ARREADY := memory.clients(1).ARREADY

  //W
  memory.clients(1).WVALID := chiron.mem_write_axi.WVALID
  memory.clients(1).WDATA := chiron.mem_write_axi.WDATA
  memory.clients(1).WLAST := chiron.mem_write_axi.WLAST
  memory.clients(1).WSTRB := chiron.mem_write_axi.WSTRB
  chiron.mem_write_axi.WREADY := memory.clients(1).WREADY

  //R
  memory.clients(1).RREADY := chiron.mem_read_axi.RREADY
  chiron.mem_read_axi.RID := memory.clients(1).RID
  chiron.mem_read_axi.RDATA := memory.clients(1).RDATA
  chiron.mem_read_axi.RRESP := memory.clients(1).RRESP
  chiron.mem_read_axi.RLAST := memory.clients(1).RLAST
  chiron.mem_read_axi.RVALID := memory.clients(1).RVALID

  //B
  memory.clients(1).BREADY := chiron.mem_write_axi.BREADY
  chiron.mem_write_axi.BVALID := memory.clients(1).BVALID
  chiron.mem_write_axi.BRESP := memory.clients(1).BRESP
  chiron.mem_write_axi.BID := 0.U

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

  //mainMemory Prober
  val prober = IO(memory.externalProbe.cloneType)
  prober <> memory.externalProbe

  //Peripherals & MTIPs

  val peripherals = Module(new MultiUart())
  // Host console input. This used to be tied off, so the only way to type at the
  // simulated console was uartPort's compiled-in ROM — and that ROM speaks the
  // legacy UART map, which Linux's xilinx-uartlite driver never reads. Net
  // effect: the quad-core boot reached "buildroot login: " and sat there with no
  // possible way to enter anything.
  //
  // Exposing it as a top-level port lets the C++ harness forward the host's
  // stdin, which is what makes `make linux-sim` interactive. A harness that does
  // not drive it simply leaves valid=0, and uart0 falls back to the ROM exactly
  // as before — so every existing harness is unaffected.
  val hostInput = IO(Input(peripherals.hostInput0.cloneType))
  val hostInputConsumed = IO(Output(Bool()))
  peripherals.hostInput0 := hostInput
  hostInputConsumed := peripherals.hostInputConsumed0

  val core0OutChar = IO(Output(peripherals.putChar0.cloneType))
  val core1OutChar = IO(Output(peripherals.putChar1.cloneType))
  val core2OutChar = IO(Output(peripherals.putChar2.cloneType))
  val core3OutChar = IO(Output(peripherals.putChar3.cloneType))

  core0OutChar := peripherals.putChar0
  core1OutChar := peripherals.putChar1
  core2OutChar := peripherals.putChar2
  core3OutChar := peripherals.putChar3


  chiron.uartClient0 <> peripherals.client0
  chiron.uartClient1 <> peripherals.client1
  chiron.uartClient2 <> peripherals.client2
  chiron.uartClient3 <> peripherals.client3

  chiron.mtip0 := peripherals.MTIP0
  chiron.mtip1 := peripherals.MTIP1
  chiron.mtip2 := peripherals.MTIP2
  chiron.mtip3 := peripherals.MTIP3

  // Machine software interrupts (SMP IPIs) from the shared CLINT msip array.
  chiron.msip0 := peripherals.MSIP0
  chiron.msip1 := peripherals.MSIP1
  chiron.msip2 := peripherals.MSIP2
  chiron.msip3 := peripherals.MSIP3

  // Per-core debug/profiling outputs, forwarded unchanged from chironCore so
  // every existing C++ harness (lockstep, profile_quad, ...) sees the exact
  // same top-level port names/shapes as before this file was split.
  val registersOut0 = IO(Output(chiron.registersOut0.cloneType))
  registersOut0 := chiron.registersOut0
  val robOut0 = IO(Output(chiron.robOut0.cloneType))
  robOut0 := chiron.robOut0

  val registersOut1 = IO(Output(chiron.registersOut1.cloneType))
  registersOut1 := chiron.registersOut1
  val robOut1 = IO(Output(chiron.robOut1.cloneType))
  robOut1 := chiron.robOut1

  val registersOut2 = IO(Output(chiron.registersOut2.cloneType))
  registersOut2 := chiron.registersOut2
  val robOut2 = IO(Output(chiron.robOut2.cloneType))
  robOut2 := chiron.robOut2

  val registersOut3 = IO(Output(chiron.registersOut3.cloneType))
  registersOut3 := chiron.registersOut3
  val robOut3 = IO(Output(chiron.robOut3.cloneType))
  robOut3 := chiron.robOut3

  val perfCountersOut0 = IO(Output(chiron.perfCountersOut0.cloneType))
  perfCountersOut0 := chiron.perfCountersOut0
  val perfCountersOut1 = IO(Output(chiron.perfCountersOut1.cloneType))
  perfCountersOut1 := chiron.perfCountersOut1
  val perfCountersOut2 = IO(Output(chiron.perfCountersOut2.cloneType))
  perfCountersOut2 := chiron.perfCountersOut2
  val perfCountersOut3 = IO(Output(chiron.perfCountersOut3.cloneType))
  perfCountersOut3 := chiron.perfCountersOut3
}

object system extends App {
  emitVerilog(new system)
}
