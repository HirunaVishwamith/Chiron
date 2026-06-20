package l2_cache

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._

class CacheLine(n_way: Int=4 , mem_size : Int=13) extends Bundle {
  val state = UInt((n_way-1).W)
  val valid = Vec(n_way,Bool())
  val tag = Vec(n_way,UInt((24-mem_size+log2Ceil(n_way)).W))
  val data = Vec(n_way,Vec(4, UInt(512.W))) // 256 bytes of data
  val dirty = Vec(n_way,Bool())
}

class Memory (
  addressWidth : Int=32,
  n_way : Int=4,    //4
  mem_size : Int=13  //13
  )extends Module {
  val io = IO(new Bundle {
    val cache_in = new cache_in()
    val cache_hit_out = new cache_hit_out()
    val cache_miss_out = new cache_miss_out()
  })

  val empty::full::Nil = Enum(2)

  val inReg = RegInit(empty)
  val outReg = RegInit(empty)
  val replreg = RegInit(empty)

  val readyInputBuffer = WireDefault(true.B)
  val readyOutputBuffer = WireDefault(true.B)



  io.cache_in.ready := readyInputBuffer




  val inputBuffer = RegInit(new Bundle {
        val Mem_addr = UInt(addressWidth.W)
        val data = UInt(512.W)
        val R_data = Vec(4,UInt(512.W))
        val from_MSHR = Bool()
        val is_R = Bool()
        val Rob_address = UInt(3.W)
        val valid = Bool()
    }.Lit(
        _.Mem_addr -> 132345.U,
        _.Rob_address -> 0.U,
        _.is_R -> false.B,
        _.valid -> false.B,
        _.data -> 0.U,
        _.from_MSHR -> false.B,
    ))



    val outputBuffer = RegInit(new Bundle{
        val Mem_addr = UInt(addressWidth.W)
        val Rob_addr = UInt(3.W)
        val is_R = Bool()
        val hit = Bool() 
        val data = UInt(512.W)
        val from_MSHR = Bool()
        val valid = Bool()
        val replace = Bool()
    }.Lit(
        _.Mem_addr -> 132345.U,
        _.is_R -> false.B,
        _.Rob_addr -> 0.U,
        _.valid -> false.B,
        _.data->0.U,
        _.hit->false.B,
        _.from_MSHR->false.B,
        _.replace -> false.B
    ))


    val replaceBuffer = RegInit(new Bundle{
      val Mem_addr = UInt(addressWidth.W)
      val data = Vec(4,UInt(512.W))
      val valid = Bool()
    }.Lit(
      _.Mem_addr -> 0.U,
      _.valid -> false.B
    ))



  // Create a memory of 1024 cache lines
  val mem = SyncReadMem(1 << (mem_size-log2Ceil(n_way)), new CacheLine(n_way,mem_size))

  val pseudoLRU = Module(new PseudoLRU(n_way))

  //this delay is due to SYNCREADMEM one cycle delay
  val delay_input_valid = RegInit(inputBuffer.valid)
  delay_input_valid := inputBuffer.valid
  

  //slicing the memory address
  val offset = inputBuffer.Mem_addr(7,6)
  val index = inputBuffer.Mem_addr(7+ (mem_size-log2Ceil(n_way)),8)
  val tag = inputBuffer.Mem_addr(31,8+(mem_size-log2Ceil(n_way)))


  // Read logic: Read the full cache line
  val cacheLineRead = mem.read(index)

  val matchVec = VecInit((0 until n_way).map { way =>
  cacheLineRead.valid(way) && (cacheLineRead.tag(way) === tag)
  })


  val data = Wire(UInt(512.W))
  val hit = inputBuffer.valid && matchVec.asUInt.orR && delay_input_valid
  val matchedWay = OHToUInt(matchVec.asUInt)

  val repl_way = pseudoLRU.get_replace_way(cacheLineRead.state)
  val replace = !hit && inputBuffer.valid && delay_input_valid && cacheLineRead.valid(repl_way) && !(cacheLineRead.tag(repl_way)===tag) && cacheLineRead.dirty(repl_way)

  val cache_hit_write = !inputBuffer.is_R && hit && !inputBuffer.from_MSHR
  val delay_MSHR_write = inputBuffer.from_MSHR && inputBuffer.valid && delay_input_valid && hit && !inputBuffer.is_R
  val cache_miss_write = inputBuffer.valid && inputBuffer.from_MSHR && delay_input_valid && !hit
  val cache_hit_read = hit && !inputBuffer.from_MSHR && inputBuffer.is_R


  when(inputBuffer.from_MSHR){
    data := inputBuffer.R_data(offset)
  }.elsewhen(!inputBuffer.is_R){
    data := inputBuffer.data
  }.otherwise{
    data := cacheLineRead.data(matchedWay)(offset)
  }


  //when(hit && !inputBuffer.from_MSHR && inputBuffer.is_R){
   // cacheLineRead.state := pseudoLRU.get_next_state(cacheLineRead.state,matchedWay)
   // mem.write(index, cacheLineRead)
 // }

  
  
  //input register state diagram cycle
  switch(inReg){
    is(empty){
      readyInputBuffer := true.B
      when(io.cache_in.fired){
        inputBuffer.Mem_addr := io.cache_in.addr
        inputBuffer.Rob_address := io.cache_in.Rob_address
        inputBuffer.is_R := io.cache_in.is_R
        inputBuffer.data := io.cache_in.data
        inputBuffer.valid := io.cache_in.valid
        inputBuffer.from_MSHR := io.cache_in.from_MSHR
        inputBuffer.R_data := io.cache_in.R_data
        inReg := full
      }
    }

    is(full){
      readyInputBuffer := false.B
    }
  }


  //output register cycle 
  switch(outReg){
    is(empty){
      readyOutputBuffer := true.B
      when(delay_input_valid){
        inReg := empty
        inputBuffer.valid := false.B

        outputBuffer.Mem_addr := inputBuffer.Mem_addr
        outputBuffer.is_R := inputBuffer.is_R
        outputBuffer.Rob_addr := inputBuffer.Rob_address
        outputBuffer.data := data
        outputBuffer.hit := hit || (inputBuffer.from_MSHR && inputBuffer.valid)
        outputBuffer.replace := replace
        outputBuffer.valid := true.B
        outputBuffer.from_MSHR := inputBuffer.from_MSHR

        outReg:=full
      }
    }

    is(full){
        readyOutputBuffer := false.B
        when(io.cache_hit_out.fired || (io.cache_miss_out.fired && !replaceBuffer.valid)){   //replace
          outputBuffer.valid := false.B
          outReg := empty
        }
    }
  }



  //replace buffer
  switch(replreg){
    is(empty){
      when(replace && cache_miss_write){
        replreg := full
        replaceBuffer.Mem_addr := Cat(cacheLineRead.tag(repl_way),index,0.U(8.W))
        replaceBuffer.data := cacheLineRead.data(repl_way)
        replaceBuffer.valid := true.B
      }
    }

    is(full){
        when(io.cache_miss_out.fired && replaceBuffer.valid){
          replaceBuffer.valid := false.B
          replreg := empty
        }
    }
  }


    //output when there is a cache hit
    io.cache_hit_out.Rob_address := outputBuffer.Rob_addr
    io.cache_hit_out.addr := outputBuffer.Mem_addr
    io.cache_hit_out.is_R := outputBuffer.is_R
    io.cache_hit_out.valid := outputBuffer.valid && outputBuffer.hit
    io.cache_hit_out.readData := outputBuffer.data

    io.cache_hit_out.ready := outputBuffer.valid && outputBuffer.hit




    io.cache_miss_out.Rob_addr := outputBuffer.Rob_addr
    io.cache_miss_out.Mem_addr := Mux(replaceBuffer.valid,replaceBuffer.Mem_addr,outputBuffer.Mem_addr)
    io.cache_miss_out.w_data := outputBuffer.data
    io.cache_miss_out.is_R := outputBuffer.is_R
    io.cache_miss_out.valid := (outputBuffer.valid && !outputBuffer.hit) 
    io.cache_miss_out.replace := replaceBuffer.valid
    io.cache_miss_out.rep_data := replaceBuffer.data
    //io.cache_miss_out.replaWire := outputBuffer.replace && outputBuffer.valid
    io.cache_miss_out.replaWire := replace && cache_miss_write

    io.cache_miss_out.ready := (outputBuffer.valid && !outputBuffer.hit) || replaceBuffer.valid //replace



  


  val newCacheLine = Wire(new CacheLine(n_way,mem_size))
  newCacheLine := cacheLineRead
  //when there is a cache hit
  when(cache_hit_write || cache_miss_write || delay_MSHR_write || cache_hit_read) {
  
    when(cache_hit_write || delay_MSHR_write){
      when(cache_hit_write){
        newCacheLine.data(matchedWay)(offset) := inputBuffer.data
      }.otherwise{
        newCacheLine.data(matchedWay)(offset) := inputBuffer.R_data(offset)
      }
      newCacheLine.dirty(matchedWay) := true.B
      newCacheLine.tag(matchedWay) := tag
      newCacheLine.valid(matchedWay) := true.B
      newCacheLine.state := pseudoLRU.get_next_state(cacheLineRead.state,matchedWay)
    }.elsewhen(cache_hit_read){
      newCacheLine.state := pseudoLRU.get_next_state(cacheLineRead.state,matchedWay)
    }.otherwise{
      newCacheLine.state := pseudoLRU.get_next_state(cacheLineRead.state,repl_way)
      newCacheLine.data(repl_way) := inputBuffer.R_data
      newCacheLine.tag(repl_way) := tag
      newCacheLine.valid(repl_way) := true.B
      when(!inputBuffer.is_R){
        newCacheLine.dirty(repl_way) := true.B
      }.otherwise{
        newCacheLine.dirty(repl_way) := false.B
      }
    }


    mem.write(index, newCacheLine)

  }

}