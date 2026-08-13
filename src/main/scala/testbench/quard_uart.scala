package testbench

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import chisel3.experimental.IO

import pipeline.ports._
import common.coreConfiguration._
import Icache.AXI
import os.read
import os.readLink
import os.write

class uartPort extends Module {
  val client = IO(Flipped(new AXI))

  val io_mtime = IO(Input(UInt(64.W)))

  // Shared CLINT msip: a real CLINT is one region in which any hart can write any
  // hart's msip (this is how SMP IPIs work). The 4 uartPorts are separate AXI
  // peripherals, so msip can't live locally — MultiUart owns the shared array.
  // This port reads it (for msip loads) and forwards msip stores up to MultiUart.
  val msipShared = IO(Input(Vec(4, UInt(32.W))))
  val msipWrite  = IO(Output(new Bundle {
    val valid = Bool()
    val hart  = UInt(2.W)
    val data  = UInt(32.W)
  }))
  // CLINT msip[hart] is memory-mapped at 0x0200_0000 + 4*hart.
  def msipHartOf(addr: UInt): UInt = MuxCase(0.U(2.W), Seq(
    (addr === "h02000004".U) -> 1.U,
    (addr === "h02000008".U) -> 2.U,
    (addr === "h0200000C".U) -> 3.U))

  val readRequestBuffer = RegInit(new Bundle {
    val valid = Bool()
    val address = UInt(32.W)
    val size = UInt(3.W)
    val len = UInt(8.W)
    val id = client.ARID.cloneType
  } Lit(_.valid -> false.B))

  val writeRequestBuffer = RegInit(new Bundle {
    val address = new Bundle {
      val valid = Bool()
      val offset = UInt(32.W)
      val size = UInt(3.W)
      val len = UInt(8.W)
      val id = client.AWID.cloneType
    }
    val data = new Bundle {
      val valid = Bool()
      val data = UInt(32.W)
      val last = Bool()
      val strb = UInt(4.W)
    }
  } Lit(_.address.valid -> false.B, _.data.valid -> false.B))

  when(client.ARREADY && client.ARVALID) { 
    readRequestBuffer.valid := true.B
    readRequestBuffer.address := client.ARADDR
    readRequestBuffer.len := client.ARLEN
    readRequestBuffer.size := client.ARSIZE
    readRequestBuffer.id := client.ARID
  }

  when(readRequestBuffer.valid && client.RREADY) {
    readRequestBuffer.len := readRequestBuffer.len - 1.U
    when(!readRequestBuffer.len.orR) { readRequestBuffer.valid := false.B }
  }
  
  val mtimecmp = RegInit(0.U(64.W))
  val mtimecmplowtemp = Reg(UInt(32.W))

  val mtimeRead = Reg(UInt(64.W))
  val mtimecmpRead = Reg(UInt(64.W))
  val msipRead = Reg(UInt(32.W))

  when(client.ARREADY && client.ARVALID) {
    mtimeRead := io_mtime
  }

  when(client.ARREADY && client.ARVALID) {
    mtimecmpRead := mtimecmp
  }

  when(client.ARREADY && client.ARVALID) {
    msipRead := msipShared(msipHartOf(client.ARADDR))
  }

  // we don't expect writes larger than 64-bits to uart or clint
  val writeData = Reg(UInt(64.W))

  // client.RDATA := Mux((readRequestBuffer.address&("hff".U)) === ("h2c".U), 8.U, 0.U)
  val ps_stat = RegInit(0.U(32.W))
  client.RDATA := 8.U
  switch(readRequestBuffer.address) {
    is("he000002c".U) { client.RDATA := 2.U }
    is("h40600000".U) { client.RDATA := 2.U }
    is("h0200bff8".U) { client.RDATA := Mux(readRequestBuffer.len.orR, mtimeRead(31, 0), mtimeRead(63, 32)) }
    is("h02004000".U) { client.RDATA := Mux(readRequestBuffer.len.orR, mtimecmpRead(31,0),mtimecmpRead(63,32))} // check this only core 0 should access this adress
    is("h02004008".U) { client.RDATA := Mux(readRequestBuffer.len.orR, mtimecmpRead(31,0),mtimecmpRead(63,32))} // check this only core 1 should access this adress
    is("h02004010".U) { client.RDATA := Mux(readRequestBuffer.len.orR, mtimecmpRead(31,0),mtimecmpRead(63,32))} // check this only core 2 should access this adress
    is("h02004018".U) { client.RDATA := Mux(readRequestBuffer.len.orR, mtimecmpRead(31,0),mtimecmpRead(63,32))} // check this only core 3 should access this adress
    is("h02000000".U) { client.RDATA := msipRead } // check this only core 0 should access this adress
    is("h02000004".U) { client.RDATA := msipRead } // check this only core 1 should access this adress
    is("h02000008".U) { client.RDATA := msipRead } // check this only core 2 should access this adress
    is("h0200000C".U) { client.RDATA := msipRead } // check this only core 3 should access this adress
    is("h04000000".U) { client.RDATA := ps_stat }
    // uartlite STATUS: TXEMPTY (bit 2) set, TXFULL/RXVALID clear — matches the
    // golden model's uartlite model (hart_execute.inc) and real hardware. The
    // old 4096 (bit 12, undefined) diverged from the emulator's 0x4 and failed
    // lockstep at the benchmark print loop's status poll.
    is("h040600008".U){client.RDATA := 4.U}
  }
  client.RID := readRequestBuffer.id
  client.RLAST := !readRequestBuffer.len.orR
  client.RRESP := 0.U
  client.RVALID := readRequestBuffer.valid

  val putChar = Wire(new Bundle {
    val valid = Bool()
    val byte = UInt(8.W)
  })
  // TX char register. Match full addresses only: any store whose low byte is
  // 0x30 (the old Zynq PS-UART FIFO offset) used to be treated as a character,
  // so framebuffer / stack traffic at ...30 leaked into the terminal and
  // smashed ANSI frames (cube/solid, and fire to a lesser extent).
  val isTxWrite = (writeRequestBuffer.address.offset === "h40600004".U) ||
                  (writeRequestBuffer.address.offset === "he0001030".U) ||
                  (writeRequestBuffer.address.offset === "he0000030".U)
  putChar.valid := Seq(isTxWrite, writeRequestBuffer.address.valid, writeRequestBuffer.data.valid).reduce(_ && _)
  putChar.byte := writeRequestBuffer.data.data(7, 0)

  val lastUartChars = RegInit(VecInit(Seq.fill(17)(0.U(8.W))))
  when(putChar.valid) {
    lastUartChars.zip(putChar.byte +: lastUartChars.dropRight(1))
    .foreach { case(buffer, next) => { buffer := next } }  
  }

  val terminalReady = RegInit(false.B)
  when(!terminalReady) {
    terminalReady := "buildroot login: ".reverse.toCharArray().toSeq.zip(lastUartChars.toSeq) map { case(char, uchar) => (char.U === uchar)} reduce(_ && _)
  }

  val afterLogin = RegInit(false.B)
  when(!afterLogin) {
    afterLogin := "~ # ".reverse.toCharArray().toSeq.zip(lastUartChars.toSeq) map { case(char, uchar) => (char.U === uchar)} reduce(_ && _)
  }

  val hardInput = RegInit(VecInit("root\nls ..".map(c => new Bundle {
    val valid = Bool()
    val char = UInt(8.W)
  } Lit(_.valid -> true.B, _.char -> c.U))))

  val command = RegInit(VecInit("ls .. && poweroff\n".map(c => new Bundle {
    val valid = Bool()
    val char = UInt(8.W)
  } Lit(_.valid -> true.B, _.char -> c.U))))

  when(
    ((readRequestBuffer.address & "hffff0fff".U) === "h40600000".U) && 
    readRequestBuffer.valid && terminalReady && !afterLogin
  ) {
    client.RDATA := (8.U(32.W) | Cat(!(hardInput(0).valid.asUInt),0.U(1.W)))
  }

  when(
    ((readRequestBuffer.address & "hffff0fff".U) === "h40600004".U) && 
    readRequestBuffer.valid && terminalReady && !afterLogin
  ) {
    client.RDATA := hardInput(0).char
    when(client.RREADY) {
      hardInput.dropRight(1).zip(hardInput.drop(1)).foreach { case(curr, next) => curr := next }
      hardInput.last.valid := false.B
    }
  }

  when(
    ((readRequestBuffer.address & "hffff0fff".U) === "h40600000".U) && 
    readRequestBuffer.valid && afterLogin
  ) {
    client.RDATA := (8.U(32.W) | Cat(!(command(0).valid.asUInt),0.U(1.W)))
  }

  when(
    ((readRequestBuffer.address & "hffff0fff".U) === "h40600004".U) &&
    readRequestBuffer.valid && afterLogin
  ) {
    client.RDATA := command(0).char
    when(client.RREADY) {
      command.dropRight(1).zip(command.drop(1)).foreach { case(curr, next) => curr := next }
      command.last.valid := false.B
    }
  }

  // Host-driven input (FPGA only): a live keyboard/console bridge feeds one
  // character at a time through this port instead of the compiled-in
  // hardInput/command ROM above. Placed after those blocks so Chisel's
  // last-connect semantics give it priority whenever hostInput.valid is
  // asserted; every simulation instantiation (MultiUart in testbench/
  // system.scala) ties hostInput.valid to false, so this is a no-op there and
  // sim behavior is completely unchanged. Not gated on terminalReady/
  // afterLogin — a live host decides what to type and when, it doesn't need
  // the boot-string auto-detection the compiled-in ROM relies on.
  val hostInput = IO(Input(new Bundle {
    val valid = Bool()
    val char  = UInt(8.W)
  }))
  val hostInputConsumed = IO(Output(Bool()))
  hostInputConsumed := false.B

  when(
    ((readRequestBuffer.address & "hffff0fff".U) === "h40600000".U) &&
    readRequestBuffer.valid && hostInput.valid
  ) {
    client.RDATA := (8.U(32.W) | Cat(!(hostInput.valid.asUInt), 0.U(1.W)))
  }

  when(
    ((readRequestBuffer.address & "hffff0fff".U) === "h40600004".U) &&
    readRequestBuffer.valid && hostInput.valid
  ) {
    client.RDATA := hostInput.char
    when(client.RREADY) { hostInputConsumed := true.B }
  }

  when(writeRequestBuffer.address.valid && writeRequestBuffer.data.valid) {
    writeRequestBuffer.data.valid := false.B
    when(writeRequestBuffer.data.last) {
      writeRequestBuffer.address.valid := false.B
    }
  }

  when(client.AWREADY && client.AWVALID) {
    writeRequestBuffer.address.valid := true.B
    writeRequestBuffer.address.offset := client.AWADDR
    writeRequestBuffer.address.id := client.AWID
    writeRequestBuffer.address.len := client.AWLEN
    writeRequestBuffer.address.size := client.AWSIZE
  }

  when(client.WREADY && client.WVALID) {
    writeRequestBuffer.data.valid := true.B
    writeRequestBuffer.data.data := client.WDATA
    writeRequestBuffer.data.last := client.WLAST
    writeRequestBuffer.data.strb := client.WSTRB
  }

  when(writeRequestBuffer.data.valid && !writeRequestBuffer.data.last) { mtimecmplowtemp := writeRequestBuffer.data.data }
  when(writeRequestBuffer.address.valid && (writeRequestBuffer.address.offset === "h02004000".U) && writeRequestBuffer.data.valid && writeRequestBuffer.data.last) { // check this only used in core 0
    mtimecmp := Cat(writeRequestBuffer.data.data, mtimecmplowtemp)
  }.elsewhen(writeRequestBuffer.address.valid && (writeRequestBuffer.address.offset === "h02004008".U) && writeRequestBuffer.data.valid && writeRequestBuffer.data.last) { // check this only use in core 1
    mtimecmp := Cat(writeRequestBuffer.data.data, mtimecmplowtemp)
  }.elsewhen(writeRequestBuffer.address.valid && (writeRequestBuffer.address.offset === "h02004010".U) && writeRequestBuffer.data.valid && writeRequestBuffer.data.last) { // check this only use in core 2
    mtimecmp := Cat(writeRequestBuffer.data.data, mtimecmplowtemp)
  }.elsewhen(writeRequestBuffer.address.valid && (writeRequestBuffer.address.offset === "h02004018".U) && writeRequestBuffer.data.valid && writeRequestBuffer.data.last) { // check this only use in core 3
    mtimecmp := Cat(writeRequestBuffer.data.data, mtimecmplowtemp)
  }

  // msip stores: forward to MultiUart's shared array, addressed by hart. Real
  // CLINT addresses are 0x0200_0000 + 4*hart (any hart may write any hart's msip
  // — this is the SMP IPI mechanism). The receiver clears its own by writing 0.
  val isMsipWrite = (writeRequestBuffer.address.offset === "h02000000".U) ||
                    (writeRequestBuffer.address.offset === "h02000004".U) ||
                    (writeRequestBuffer.address.offset === "h02000008".U) ||
                    (writeRequestBuffer.address.offset === "h0200000C".U)
  msipWrite.valid := writeRequestBuffer.address.valid && isMsipWrite &&
                     writeRequestBuffer.data.valid && writeRequestBuffer.data.last
  msipWrite.hart  := msipHartOf(writeRequestBuffer.address.offset)
  msipWrite.data  := writeRequestBuffer.data.data

  client.ARREADY := !readRequestBuffer.valid

  client.AWREADY := !writeRequestBuffer.address.valid
  client.WREADY := !writeRequestBuffer.data.valid || writeRequestBuffer.address.valid

  client.BID := writeRequestBuffer.address.id
  client.BRESP := 0.U
  client.BVALID := writeRequestBuffer.address.valid && writeRequestBuffer.data.valid && writeRequestBuffer.data.last

  val MTIP = IO(Output(Bool()))
  MTIP := (io_mtime > mtimecmp)
}


class MultiUart extends Module {
  val client0 = IO(Flipped(new AXI))
  val client1 = IO(Flipped(new AXI))
  val client2 = IO(Flipped(new AXI))
  val client3 = IO(Flipped(new AXI))

  val mtime = RegInit(0.U(64.W)) // only need one mtime for all clients
  val couter_wrap = RegInit(0.U(4.W))
  couter_wrap := couter_wrap + 1.U
  // mtime advances once per 16 cycles (4-bit prescaler wrap) — the correct
  // ~6.25 MHz timebase the device tree advertises. (An EXPERIMENTAL `+ 1.U`
  // every cycle was used to probe whether the __switch_to boot stall is timer-
  // rate-sensitive; reverted to the real timer here.)
  mtime := mtime + couter_wrap.andR.asUInt

  val uart0 = Module(new uartPort{
    val putCharOut0 = IO(Output(putChar.cloneType))
    // val ps_start_port0 = IO(Input(ps_stat.cloneType))
    putCharOut0 := putChar
    // ps_stat := ps_start_port0
  })
  val uart1 = Module(new uartPort{
    val putCharOut1 = IO(Output(putChar.cloneType))
    // val ps_start_port1 = IO(Input(ps_stat.cloneType))
    putCharOut1 := putChar
    // ps_stat := ps_start_port1

  })

  val uart2 = Module(new uartPort{
    val putCharOut2 = IO(Output(putChar.cloneType))
    // val ps_start_port2 = IO(Input(ps_stat.cloneType))
    putCharOut2 := putChar
    // ps_stat := ps_start_port2

  })

  val uart3 = Module(new uartPort{
    val putCharOut3 = IO(Output(putChar.cloneType))
    // val ps_start_port3 = IO(Input(ps_stat.cloneType))
    putCharOut3 := putChar
    // ps_stat := ps_start_port3

  })

  uart0.client <> client0
  uart1.client <> client1
  uart2.client <> client2
  uart3.client <> client3

  uart0.io_mtime := mtime
  uart1.io_mtime := mtime
  uart2.io_mtime := mtime
  uart3.io_mtime := mtime

  // Host-driven console input (FPGA only): only core0's console is bridged
  // to the host (the FPGA top's AXI-Lite bridge feeds hostInput0); every
  // simulation instantiation (system.scala) ties hostInput0.valid to false,
  // which keeps uart0's own hostInput tied off too, so the compiled-in ROM
  // path is unaffected there. uart1-3 never have a host bridge.
  val hostInput0 = IO(Input(new Bundle {
    val valid = Bool()
    val char  = UInt(8.W)
  }))
  val hostInputConsumed0 = IO(Output(Bool()))
  uart0.hostInput := hostInput0
  hostInputConsumed0 := uart0.hostInputConsumed
  uart1.hostInput.valid := false.B
  uart1.hostInput.char  := 0.U
  uart2.hostInput.valid := false.B
  uart2.hostInput.char  := 0.U
  uart3.hostInput.valid := false.B
  uart3.hostInput.char  := 0.U

  // ── Shared CLINT msip (one array, any hart can write any hart's bit) ─────────
  // This is the SMP IPI register file. Each uartPort reads it and forwards its
  // client's msip stores here; we apply them addressed by target hart (lower
  // port wins a same-cycle same-hart race — harmless, writes are idempotent).
  val msipShared = RegInit(VecInit(Seq.fill(4)(0.U(32.W))))
  val uarts = Seq(uart0, uart1, uart2, uart3)
  uarts.foreach { u => u.msipShared := msipShared }
  for (hart <- 0 until 4) {
    // last matching port in this loop wins; iterate high→low so port0 has priority
    for (u <- uarts.reverse) {
      when(u.msipWrite.valid && u.msipWrite.hart === hart.U) {
        msipShared(hart) := u.msipWrite.data
      }
    }
  }

  val putChar0 = IO(Output(uart0.putCharOut0.cloneType))
  putChar0 := uart0.putCharOut0

  val putChar1 = IO(Output(uart1.putCharOut1.cloneType))
  putChar1 := uart1.putCharOut1

  val putChar2 = IO(Output(uart2.putCharOut2.cloneType))
  putChar2 := uart2.putCharOut2

  val putChar3 = IO(Output(uart3.putCharOut3.cloneType))
  putChar3 := uart3.putCharOut3

  val MTIP0 = IO(Output(Bool()))
  val MTIP1 = IO(Output(Bool()))
  val MTIP2 = IO(Output(Bool()))
  val MTIP3 = IO(Output(Bool()))

  MTIP0 := uart0.MTIP
  MTIP1 := uart1.MTIP
  MTIP2 := uart2.MTIP
  MTIP3 := uart3.MTIP

  // Per-hart machine software interrupt pending (bit 0 of the shared msip).
  val MSIP0 = IO(Output(Bool()))
  val MSIP1 = IO(Output(Bool()))
  val MSIP2 = IO(Output(Bool()))
  val MSIP3 = IO(Output(Bool()))
  MSIP0 := msipShared(0)(0)
  MSIP1 := msipShared(1)(0)
  MSIP2 := msipShared(2)(0)
  MSIP3 := msipShared(3)(0)

}


object MultiUart extends App {
  emitVerilog(new MultiUart)
}

// class MultiPSClint extends MultiUart {
//   val psMaster = IO(Flipped(new AXI))

//   val awFired, wFired, bValid, finished = RegInit(false.B)
//   psMaster.AWREADY := !awFired
//   psMaster.WREADY := !wFired
//   psMaster.BVALID := awFired && wFired

//   when(psMaster.AWVALID && psMaster.AWREADY) { awFired := true.B }
//   when(psMaster.WVALID && psMaster.WREADY) { wFired := true.B }
//   when(psMaster.BVALID && psMaster.BREADY) { finished := true.B }

//   when(finished) {
//     psMaster.AWREADY := false.B
//     psMaster.WREADY := false.B
//     psMaster.BVALID := false.B
//   }
//   val bid = Reg(psMaster.AWID.cloneType)
//   when(psMaster.AWREADY && psMaster.AWVALID) { bid := psMaster.AWID }
//   psMaster.BID := bid
//   psMaster.BRESP := 0.U

//   psMaster.ARREADY := false.B

//   psMaster.RVALID := false.B
//   psMaster.RDATA := 0.U
//   psMaster.RID := 0.U
//   psMaster.RLAST := false.B
//   psMaster.RRESP := 0.U

//   val psStartReg0 = RegInit(0.U(32.W))
//   val psStartReg1 = RegInit(0.U(32.W))
//   val psStartReg2 = RegInit(0.U(32.W))
//   val psStartReg3 = RegInit(0.U(32.W))

//   when(psMaster.WREADY && psMaster.WVALID) { 
//     // ps_stat := psMaster.WDATA 
//     psStartReg0 := psMaster.WDATA
//     psStartReg1 := psMaster.WDATA
//     psStartReg2 := psMaster.WDATA
//     psStartReg3 := psMaster.WDATA

//   }

//   uart0.ps_start_port0 := psStartReg0
//   uart1.ps_start_port1 := psStartReg1
//   uart2.ps_start_port2 := psStartReg2
//   uart3.ps_start_port3 := psStartReg3

//   val STANDBY0, RUNNING0 = IO(Output(Bool()))
//   // val STANDBY1, RUNNING1 = IO(Output(Bool()))
//   // val STANDBY1, RUNNING1 = IO(Output(Bool()))
//   // val STANDBY1, RUNNING1 = IO(Output(Bool()))

//   STANDBY0 := !psStartReg0.orR
//   // STANDBY1 := !psStartReg1.orR

//   RUNNING0 := psStartReg0.orR
//   // RUNNING1 := psStartReg1.orR

//   // val STANDBY, RUNNING = IO(Output(Bool()))
//   // STANDBY := !ps_stat.orR
//   // RUNNING := ps_stat.orR
// }



// object MultiPSClint extends App {
//   emitVerilog(new MultiPSClint)
// }



// need to give seperate read buffer to the uart ports but only core zero will do the linux stuff booting
// neet to make a common clint such that it has two ports but the uart stuff might not be needed for fpga
//  need MSIP port also