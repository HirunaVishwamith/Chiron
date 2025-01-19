import chisel3._
import chisel3.util._

class readPort extends Bundle{
  val addr = Input(UInt(6.W))
  val data = Output(UInt(64.W))
  val en = Input(Bool())
}

class writePort extends Bundle{
  val addr = Input(UInt(6.W))
  val data = Input(UInt(64.W))
  val en = Input(Bool())
}

class LVT_Mem extends Module {
  val io = IO(new Bundle {

    // readports
    val R1 = new readPort
    val R2 = new readPort
    val R3 = new readPort

    // writeports
    val W1 = new writePort
    val W2 = new writePort
    val W3 = new writePort
    val W4 = new writePort
  })

  // define LVT
  val LVT = Mem(64,UInt(2.W))

  // define block memory sets
  val s1 = Module(new LVT_set)
  val s2 = Module(new LVT_set)
  val s3 = Module(new LVT_set)
  val s4 = Module(new LVT_set)

  // update LVT during write
  when(io.W1.en){
    LVT(io.W1.addr) := 0.U
  }
  when(io.W2.en) {
    LVT(io.W2.addr) := 1.U
  }
  when(io.W3.en) {
    LVT(io.W3.addr) := 2.U
  }
  when(io.W4.en) {
    LVT(io.W4.addr) := 3.U
  }

  // connect write ports
  s1.io.wenable := io.W1.en
  s2.io.wenable := io.W2.en
  s3.io.wenable := io.W3.en
  s4.io.wenable := io.W4.en
  s1.io.wdata := io.W1.data
  s2.io.wdata := io.W2.data
  s3.io.wdata := io.W3.data
  s4.io.wdata := io.W4.data
  s1.io.waddr := io.W1.addr
  s2.io.waddr := io.W2.addr
  s3.io.waddr := io.W3.addr
  s4.io.waddr := io.W4.addr

  // read mux selection registers
  val r1_sel_reg = RegInit(0.U(2.W))
  val r2_sel_reg = RegInit(0.U(2.W))
  val r3_sel_reg = RegInit(0.U(2.W))

  r1_sel_reg := LVT(io.R1.addr)
  r2_sel_reg := LVT(io.R2.addr)
  r3_sel_reg := LVT(io.R3.addr)

  // connect readports to blockram sets
  s1.io.r1addr := io.R1.addr
  s2.io.r1addr := io.R1.addr
  s3.io.r1addr := io.R1.addr
  s4.io.r1addr := io.R1.addr

  s1.io.r2addr := io.R2.addr
  s2.io.r2addr := io.R2.addr
  s3.io.r2addr := io.R2.addr
  s4.io.r2addr := io.R2.addr

  s1.io.r3addr := io.R3.addr
  s2.io.r3addr := io.R3.addr
  s3.io.r3addr := io.R3.addr
  s4.io.r3addr := io.R3.addr

  s1.io.r1enable := io.R1.en
  s2.io.r1enable := io.R1.en
  s3.io.r1enable := io.R1.en
  s4.io.r1enable := io.R1.en

  s1.io.r2enable := io.R2.en
  s2.io.r2enable := io.R2.en
  s3.io.r2enable := io.R2.en
  s4.io.r2enable := io.R2.en

  s1.io.r3enable := io.R3.en
  s2.io.r3enable := io.R3.en
  s3.io.r3enable := io.R3.en
  s4.io.r3enable := io.R3.en


  // Mux out Read data
  io.R1.data := MuxCase(0.U, Seq((r1_sel_reg===0.U)->s1.io.r1data, (r1_sel_reg===1.U)->s2.io.r1data, (r1_sel_reg===2.U)->s3.io.r1data, (r1_sel_reg===3.U)->s4.io.r1data))
  io.R2.data := MuxCase(0.U, Seq((r2_sel_reg===0.U)->s1.io.r2data, (r2_sel_reg===1.U)->s2.io.r2data, (r2_sel_reg===2.U)->s3.io.r2data, (r2_sel_reg===3.U)->s4.io.r2data))
  io.R3.data := MuxCase(0.U, Seq((r3_sel_reg===0.U)->s1.io.r3data, (r3_sel_reg===1.U)->s2.io.r3data, (r3_sel_reg===2.U)->s3.io.r3data, (r3_sel_reg===3.U)->s4.io.r3data))

  //printf(p"${io.R1} ${io.R2} ${io.R3}\n")
}

