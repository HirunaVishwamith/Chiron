// No `package testbench` — same reason as chironCore.scala/system.scala:
// needs unqualified access to chironCore, which lives in the default
// (unnamed) package.
//
// fpgaTop — chiron's FPGA build target (Kintex-7, PCIe/XDMA). Same
// chironCore (4 cores + Interconnect + LLC) as the sim top (system.scala),
// but:
//   - LLC's mem_read_axi/mem_write_axi are exposed directly at the chip
//     boundary (for Vivado to route to a MIG DDR3 controller, via the
//     smartconnect fan-in described in fpga/build_kintex7.tcl) instead of
//     feeding the simulation-only mainMemory.
//   - MultiUart is reused unchanged, but its host-bridge port (only core0 is
//     wired to it — the FPGA build only brings up one console) is now driven
//     by hostBridge, a real AXI-Lite peripheral on XDMA's M_AXI_LITE, instead
//     of being tied off. That's what lets the host (fpga/run.py, over PCIe)
//     release the cores from reset and run a two-way UART console.
//
// Reset: the cores/interconnect/LLC/MultiUart subsystem is held in reset
// until the host writes CTRL.RUN=1 (hostBridge.run) -- ANDed with this
// module's own top-level `reset` (driven by Vivado, e.g. off a proc_sys_reset
// tied to PCIe link-up) so the subsystem also resets on a real board reset.
// hostBridge itself must stay OUTSIDE that derived reset domain: it owns the
// very `run` register that releases it, so if hostBridge were reset by
// `!run`, the host could never reach the register that raises run
// (chicken-and-egg).
import chisel3._
import chisel3.util._

import _root_.testbench.MultiUart
import _root_.testbench.hostBridge

class fpgaTop extends Module {
  val bridge = Module(new hostBridge)
  val s_axi = IO(Flipped(new Icache.AXI))
  s_axi <> bridge.axi

  // One combined AXI4 master port (all 5 channels sharing the m_axi_ prefix)
  // toward MIG, not chironCore's own two split mem_read_axi/mem_write_axi
  // ports — Vivado's IP packager infers a single AXI4 bus interface from
  // signal-name prefix, and a split pair of half-duplex ports wouldn't be
  // recognized as one. Field-mapped below onto chironCore's split ports.
  val m_axi = IO(new Icache.AXI(idWidth = 3, addressWidth = 32, dataWidth = 256))

  // proc_sys_reset (Vivado) asserts this module's own implicit reset
  // externally (e.g. off PCIe link-up); ANDed with !run so the core
  // subsystem stays held until the host finishes DMA-loading the image and
  // writes CTRL.RUN=1.
  val coreReset = reset.asBool || !bridge.run

  withReset(coreReset) {
    val chiron = Module(new chironCore)
    val peripherals = Module(new MultiUart())

    // m_axi's AR/R channels <-> chiron.mem_read_axi (both master-shaped:
    // straightforward field forwarding, no direction flip needed).
    m_axi.ARADDR  := chiron.mem_read_axi.ARADDR
    m_axi.ARID    := chiron.mem_read_axi.ARID
    m_axi.ARVALID := chiron.mem_read_axi.ARVALID
    m_axi.ARLEN   := chiron.mem_read_axi.ARLEN
    m_axi.ARSIZE  := chiron.mem_read_axi.ARSIZE
    m_axi.ARBURST := chiron.mem_read_axi.ARBURST
    m_axi.ARLOCK  := chiron.mem_read_axi.ARLOCK(0)
    m_axi.ARCACHE := chiron.mem_read_axi.ARCACHE
    m_axi.ARPROT  := chiron.mem_read_axi.ARPROT
    m_axi.ARQOS   := chiron.mem_read_axi.ARQOS
    m_axi.RREADY  := chiron.mem_read_axi.RREADY
    chiron.mem_read_axi.ARREADY := m_axi.ARREADY
    chiron.mem_read_axi.RDATA   := m_axi.RDATA
    chiron.mem_read_axi.RID     := m_axi.RID
    chiron.mem_read_axi.RRESP   := m_axi.RRESP
    chiron.mem_read_axi.RVALID  := m_axi.RVALID
    chiron.mem_read_axi.RLAST   := m_axi.RLAST

    // m_axi's AW/W/B channels <-> chiron.mem_write_axi.
    m_axi.AWADDR  := chiron.mem_write_axi.AWADDR
    m_axi.AWVALID := chiron.mem_write_axi.AWVALID
    m_axi.AWLEN   := chiron.mem_write_axi.AWLEN
    m_axi.AWCACHE := chiron.mem_write_axi.AWCACHE
    m_axi.AWSIZE  := chiron.mem_write_axi.AWSIZE
    m_axi.AWLOCK  := chiron.mem_write_axi.AWLOCK(0)
    m_axi.AWPROT  := chiron.mem_write_axi.AWPROT
    m_axi.AWQOS   := chiron.mem_write_axi.AWQOS
    m_axi.AWBURST := chiron.mem_write_axi.AWBURST
    m_axi.AWID    := chiron.mem_write_axi.AWID
    m_axi.WDATA   := chiron.mem_write_axi.WDATA
    m_axi.WVALID  := chiron.mem_write_axi.WVALID
    m_axi.WSTRB   := chiron.mem_write_axi.WSTRB
    m_axi.WLAST   := chiron.mem_write_axi.WLAST
    m_axi.BREADY  := chiron.mem_write_axi.BREADY
    chiron.mem_write_axi.AWREADY := m_axi.AWREADY
    chiron.mem_write_axi.WREADY  := m_axi.WREADY
    chiron.mem_write_axi.BVALID  := m_axi.BVALID
    chiron.mem_write_axi.BRESP   := m_axi.BRESP
    chiron.mem_write_axi.BID     := m_axi.BID

    chiron.uartClient0 <> peripherals.client0
    chiron.uartClient1 <> peripherals.client1
    chiron.uartClient2 <> peripherals.client2
    chiron.uartClient3 <> peripherals.client3

    chiron.mtip0 := peripherals.MTIP0
    chiron.mtip1 := peripherals.MTIP1
    chiron.mtip2 := peripherals.MTIP2
    chiron.mtip3 := peripherals.MTIP3

    chiron.msip0 := peripherals.MSIP0
    chiron.msip1 := peripherals.MSIP1
    chiron.msip2 := peripherals.MSIP2
    chiron.msip3 := peripherals.MSIP3

    // Only core0's console is bridged to the host.
    peripherals.hostInput0 := bridge.hostInput
    bridge.hostInputConsumed := peripherals.hostInputConsumed0
    bridge.putCharIn := peripherals.putChar0
  }
}

object fpgaTop extends App {
  emitVerilog(new fpgaTop)
}
