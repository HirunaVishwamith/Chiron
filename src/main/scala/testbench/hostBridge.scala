package testbench

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import Icache.AXI

// AXI-Lite-compatible (single-beat AXI4) host<->FPGA control/console bridge —
// FPGA-only, sits on XDMA's M_AXI_LITE. The host (fpga/run.py) drives it over
// /dev/xdma0_user to release the cores from reset and run a two-way UART
// console (drain core0's boot/console output, inject typed keystrokes)
// without needing any custom PCIe protocol beyond plain 32-bit register
// reads/writes. Structured like uartPort's AXI4 slave (buffered AR/AW+W,
// single outstanding transaction, echoed ID) for consistency with the rest
// of the peripheral set.
//
// Register map (byte offsets, 32-bit registers, word-addressed on bits [3:2]):
//   0x00 CTRL    [0]=RUN (0=cores held in reset, 1=released)       R/W
//   0x04 STATUS  [0]=RUN mirror  [1]=TX not-empty  [2]=RX full     R
//   0x08 TX_DATA [7:0]=next console byte, [8]=valid; read dequeues R
//   0x0C RX_DATA [7:0]=byte to inject; write enqueues              W
class hostBridge(txDepth: Int = 1024, rxDepth: Int = 64) extends Module {
  val axi = IO(Flipped(new AXI))

  // core0's uartPort.putChar tap (console output source).
  val putCharIn = IO(Input(new Bundle {
    val valid = Bool()
    val byte  = UInt(8.W)
  }))

  // Held low out of reset; the host raises it once the image is loaded.
  val run = IO(Output(Bool()))

  // Feeds core0's uartPort.hostInput port directly.
  val hostInput = IO(Output(new Bundle {
    val valid = Bool()
    val char  = UInt(8.W)
  }))
  val hostInputConsumed = IO(Input(Bool()))

  val runReg = RegInit(false.B)
  run := runReg

  // Console-output FIFO: core0 can print faster than a PCIe register-poll
  // loop can drain it (e.g. a printk burst), so buffer instead of a single
  // register. Dropping on overflow only loses console bytes, never affects
  // core correctness — enq simply isn't asserted when the FIFO is full.
  val txFifo = Module(new Queue(UInt(8.W), txDepth))
  txFifo.io.enq.valid := putCharIn.valid && txFifo.io.enq.ready
  txFifo.io.enq.bits  := putCharIn.byte

  // Host-input FIFO: lets the host queue up a few typed characters ahead of
  // uartPort consuming them one at a time via hostInputConsumed.
  val rxFifo = Module(new Queue(UInt(8.W), rxDepth))
  hostInput.valid     := rxFifo.io.deq.valid
  hostInput.char      := rxFifo.io.deq.bits
  rxFifo.io.deq.ready := hostInputConsumed

  // ---- AXI4 read channel (single outstanding, single-beat only) ----------
  val readRequestBuffer = RegInit(new Bundle {
    val valid   = Bool()
    val address = UInt(32.W)
    val id      = axi.ARID.cloneType
  } Lit(_.valid -> false.B))

  axi.ARREADY := !readRequestBuffer.valid
  when(axi.ARREADY && axi.ARVALID) {
    readRequestBuffer.valid   := true.B
    readRequestBuffer.address := axi.ARADDR
    readRequestBuffer.id      := axi.ARID
  }

  val rdData = WireDefault(0.U(32.W))
  switch(readRequestBuffer.address(3, 2)) {
    is(0.U) { rdData := runReg.asUInt }
    is(1.U) { rdData := Cat(0.U(29.W), !rxFifo.io.enq.ready, txFifo.io.deq.valid, runReg) }
    is(2.U) { rdData := Cat(0.U(23.W), txFifo.io.deq.valid, txFifo.io.deq.bits) }
  }
  val txDequeue = readRequestBuffer.valid && axi.RVALID && axi.RREADY &&
                  (readRequestBuffer.address(3, 2) === 2.U)
  txFifo.io.deq.ready := txDequeue

  axi.RVALID := readRequestBuffer.valid
  axi.RDATA  := rdData
  axi.RID    := readRequestBuffer.id
  axi.RRESP  := 0.U
  axi.RLAST  := true.B
  when(readRequestBuffer.valid && axi.RREADY) { readRequestBuffer.valid := false.B }

  // ---- AXI4 write channel (single outstanding, single-beat only) --------
  val writeRequestBuffer = RegInit(new Bundle {
    val address = new Bundle {
      val valid = Bool()
      val offset = UInt(32.W)
      val id = axi.AWID.cloneType
    }
    val data = new Bundle {
      val valid = Bool()
      val data  = UInt(32.W)
    }
  } Lit(_.address.valid -> false.B, _.data.valid -> false.B))

  axi.AWREADY := !writeRequestBuffer.address.valid
  when(axi.AWREADY && axi.AWVALID) {
    writeRequestBuffer.address.valid  := true.B
    writeRequestBuffer.address.offset := axi.AWADDR
    writeRequestBuffer.address.id     := axi.AWID
  }

  axi.WREADY := !writeRequestBuffer.data.valid || writeRequestBuffer.address.valid
  when(axi.WREADY && axi.WVALID) {
    writeRequestBuffer.data.valid := true.B
    writeRequestBuffer.data.data  := axi.WDATA
  }

  val writeFire = writeRequestBuffer.address.valid && writeRequestBuffer.data.valid
  rxFifo.io.enq.valid := writeFire && (writeRequestBuffer.address.offset(3, 2) === 3.U) &&
                         rxFifo.io.enq.ready
  rxFifo.io.enq.bits  := writeRequestBuffer.data.data(7, 0)
  when(writeFire) {
    when(writeRequestBuffer.address.offset(3, 2) === 0.U) {
      runReg := writeRequestBuffer.data.data(0)
    }
    writeRequestBuffer.address.valid := false.B
    writeRequestBuffer.data.valid    := false.B
  }

  val writeFinished = RegInit(false.B)
  when(writeFire) { writeFinished := true.B }
  .elsewhen(axi.BVALID && axi.BREADY) { writeFinished := false.B }

  axi.BVALID := writeFinished
  axi.BID    := writeRequestBuffer.address.id
  axi.BRESP  := 0.U
}

object hostBridge extends App {
  emitVerilog(new hostBridge)
}
