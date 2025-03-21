package l2_cache

import chisel3._
import chisel3.experimental.BundleLiterals._
import chisel3.util._
import java.rmi.server.UID


class l2_mem(arlen:Int=7 ,addr_w: Int = 3,idWidth: Int = 2, addressWidth: Int = 32, dataWidth: Int = 64,mem_dataWidth : Int = 256)extends Module{
   val io = IO(new Bundle {  
    val cache_axi = new AXIlite(idWidth,addressWidth,dataWidth)
    val mem_read_axi = Flipped(new AXIlite1(idWidth, addressWidth, mem_dataWidth))
    val mem_write_axi = Flipped(new AXIlite2(idWidth, addressWidth, mem_dataWidth))
  }) 


  //front end rob structure
  val l2_front_Rob = Module(new l2_Rob())

  //cache
  val cache = Module (new Memory())

  //read memory module
  val MSHR = Module(new MSHR())

  //writeBackbuffer
  val writeBackBuffer = Module(new writeBackBuffer())


  //connection for the AXI cache and core
  io.cache_axi <> l2_front_Rob.io.axi


  io.mem_read_axi <> MSHR.io.axi

  io.mem_write_axi <> writeBackBuffer.io.axi

  val count = RegInit(0.U(2.W))
  val replace_count = RegInit(0.U(2.W))

  when(cache.io.cache_miss_out.replaWire){
    replace_count := replace_count + 1.U
  }.elsewhen(writeBackBuffer.io.repl_finish){
    replace_count := replace_count - 1.U
  }

  

  val replace_full = replace_count.asBools.reduce(_ & _)


  count := Mux(count===2.U, 0.U, count + 1.U)



  //connection for cache in and rob out and mshr
  cache.io.cache_in.Rob_address := Mux(MSHR.io.Mem_read_out.ready,MSHR.io.Mem_read_out.Rob_addr,l2_front_Rob.io.Rob_out.Rob_address)
  cache.io.cache_in.addr := Mux(MSHR.io.Mem_read_out.ready,MSHR.io.Mem_read_out.Mem_addr,l2_front_Rob.io.Rob_out.Mem_address)
  cache.io.cache_in.valid := Mux(MSHR.io.Mem_read_out.ready,MSHR.io.Mem_read_out.valid,l2_front_Rob.io.Rob_out.valid)
  cache.io.cache_in.is_R := Mux(MSHR.io.Mem_read_out.ready,MSHR.io.Mem_read_out.is_R,l2_front_Rob.io.Rob_out.is_R)  

  cache.io.cache_in.data := l2_front_Rob.io.Rob_out.data
  cache.io.cache_in.R_data := MSHR.io.Mem_read_out.R_data
  cache.io.cache_in.from_MSHR := MSHR.io.Mem_read_out.ready


  l2_front_Rob.io.Rob_out.fired := l2_front_Rob.io.Rob_out.ready && cache.io.cache_in.ready && !MSHR.io.Mem_read_out.ready && (count===0.U) && !replace_full
  MSHR.io.Mem_read_out.fired := cache.io.cache_in.ready && MSHR.io.Mem_read_out.ready && count===0.U
  cache.io.cache_in.fired := cache.io.cache_in.ready && (l2_front_Rob.io.Rob_out.ready && !replace_full || MSHR.io.Mem_read_out.ready) && count ===0.U


  //connection signals for rob in and cache_hit_out
  l2_front_Rob.io.Rob_in.fired := l2_front_Rob.io.Rob_in.ready && cache.io.cache_hit_out.ready
  cache.io.cache_hit_out.fired := l2_front_Rob.io.Rob_in.ready && cache.io.cache_hit_out.ready

  l2_front_Rob.io.Rob_in.Rob_address := cache.io.cache_hit_out.Rob_address
  l2_front_Rob.io.Rob_in.Mem_address:=cache.io.cache_hit_out.addr
  l2_front_Rob.io.Rob_in.valid:=cache.io.cache_hit_out.valid
  l2_front_Rob.io.Rob_in.is_R := cache.io.cache_hit_out.is_R
  l2_front_Rob.io.Rob_in.data := cache.io.cache_hit_out.readData





  //connection for cache_miss_out and read_mem_in
  MSHR.io.Mem_read_in.Mem_addr := cache.io.cache_miss_out.Mem_addr
  MSHR.io.Mem_read_in.W_data := cache.io.cache_miss_out.w_data
  MSHR.io.Mem_read_in.Rob_addr := cache.io.cache_miss_out.Rob_addr
  MSHR.io.Mem_read_in.valid := cache.io.cache_miss_out.valid
  MSHR.io.Mem_read_in.repl_hit := writeBackBuffer.io.miss_out.repl_hit && cache.io.cache_miss_out.valid
  MSHR.io.Mem_read_in.repl_data := writeBackBuffer.io.miss_out.data
  MSHR.io.Mem_read_in.is_R := cache.io.cache_miss_out.is_R

  writeBackBuffer.io.miss_address := cache.io.cache_miss_out.Mem_addr

  MSHR.io.Mem_read_in.fired := MSHR.io.Mem_read_in.ready && cache.io.cache_miss_out.ready && !cache.io.cache_miss_out.replace 
  cache.io.cache_miss_out.fired := cache.io.cache_miss_out.ready && (MSHR.io.Mem_read_in.ready  || writeBackBuffer.io.replace_in.ready )



  writeBackBuffer.io.replace_in.Mem_addr := cache.io.cache_miss_out.Mem_addr
  writeBackBuffer.io.replace_in.data := cache.io.cache_miss_out.rep_data

  writeBackBuffer.io.replace_in.fired := writeBackBuffer.io.replace_in.ready && cache.io.cache_miss_out.ready && cache.io.cache_miss_out.replace 

}

