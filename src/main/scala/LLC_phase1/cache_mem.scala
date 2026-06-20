package LLC_cache
import chisel3._
import chisel3.util._


class CacheLine(n_way: Int=4) extends Bundle {
  val valid = Vec(n_way,Bool())
  val tag = Vec(n_way,UInt(13.W))
  val data = Vec(n_way,Vec(4, UInt(512.W))) // 256 bytes of data
  val dirty = Vec(n_way,Bool())
}



class Memory extends Module {
  val io = IO(new Bundle {
    val addr = Input(UInt(32.W))
    val readData = Output(Vec(4, UInt(512.W))) // Full cache line output 
    val writeData = Input(Vec(4, UInt(512.W)))
    val writeEnable = Input(Bool())
    val hit = Output(Bool())
    val hit_ready = Input(Bool())
    val replace_ready = Input(Bool())
    val replace = Output(Bool())
    val replace_addr = Output(UInt(32.W))
    val full_line = Input(Bool())
    //val leo = Input(Bool())
  })
  
  // Create a memory of 1024 cache lines
  val mem = SyncReadMem(1 << 11, new CacheLine)
  val mem_state = Mem(1<<11,UInt(3.W))
  val pseudoLRU = Module(new PseudoLRU(4))

  val offset = io.addr(7,6)
  val index = io.addr(18,8)
  val tag = io.addr(31,19)

  // Read logic: Read the full cache line
  val cacheLineRead = mem.read(index)
  val repl_state = mem_state.read(index)

  val matchVec = VecInit((0 until 4).map { way =>
  cacheLineRead.valid(way) && (cacheLineRead.tag(way) === tag)
  })

  val matchedWay = OHToUInt(matchVec.asUInt)

  io.hit := io.hit_ready && matchVec.asUInt.orR
  val repl_way = pseudoLRU.get_replace_way(repl_state)

  when(io.hit){
    io.readData := cacheLineRead.data(matchedWay)
    mem_state.write(index,pseudoLRU.get_next_state(repl_state,matchedWay))
  }.otherwise{
    io.readData := cacheLineRead.data(repl_way)
  }
  
  io.replace_addr := Cat(cacheLineRead.tag(repl_way),index,0.U(8.W))
  io.replace := !io.hit && cacheLineRead.valid(repl_way) && io.replace_ready && !(cacheLineRead.tag(repl_way)===tag) && cacheLineRead.dirty(repl_way)
  // Write logic

  when(io.writeEnable) {
    //val newCacheLine = Wire(new CacheLine)
    when(io.full_line){
      cacheLineRead.dirty(repl_way) := false.B
      cacheLineRead.valid(repl_way) := true.B
      cacheLineRead.data(repl_way) := io.writeData
      cacheLineRead.tag(repl_way) := tag 
      mem_state.write(index,pseudoLRU.get_next_state(repl_state,repl_way))

    }.otherwise{
      cacheLineRead.dirty(matchedWay) := true.B
      cacheLineRead.valid(matchedWay) := true.B
      cacheLineRead.data(matchedWay) := io.writeData
      cacheLineRead.tag(matchedWay) := tag 
      mem_state.write(index,pseudoLRU.get_next_state(repl_state,matchedWay))
    }
    
    mem.write(index, cacheLineRead)
  }
}

/*
object Memory extends App {
  (new chisel3.stage.ChiselStage).emitVerilog(new Memory)
}
*/