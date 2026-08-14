package L2_cache

import chisel3._
import chisel3.experimental.BundleLiterals._
import chisel3.util._


class mem_fifo_line (idWidth: Int = 3 , addressWidth: Int=32) extends Bundle{
    val valid=Bool()
    val Mem_addr = UInt(addressWidth.W)
    val Rob_addr = UInt(3.W)
    val data = UInt(512.W)
    val is_R = Bool()
}



class MSHR(arlen:Int=7,beat_size:Int=64 ,addr_w: Int = 3,idWidth: Int = 3, addressWidth: Int = 32, dataWidth: Int = 256,mem_dataWidth : Int = 256) extends Module {
    val io = IO(new Bundle {  
    val axi = Flipped(new AXIlite1(idWidth, addressWidth, dataWidth))
    val Mem_read_in = new Mem_read_in()
    val Mem_read_out = new Mem_read_out()
  })

 


    //fifo for the Rob structure
    val enq_fifo = Module(new regFifo(new mem_fifo_line(idWidth,addressWidth) , scala.math.pow(2,addr_w).asInstanceOf[Int]){
        val loaded = IO(Input(Bool()))

        when(loaded){
            memReg(readPtr).valid := true.B
        }
    })



    val R_combined = Wire(Vec(4, UInt(512.W)))
    val temp_reg = RegInit(VecInit(Seq.fill(8)(0.U(256.W))))

    for (i <- 0 until 4) {
        R_combined(i) := Cat(temp_reg(2 * i + 1), temp_reg(2 * i))
    }
        
    val offset = enq_fifo.io.deq.bits.Mem_addr(7,6)

    // Patch a completing WRITE miss with the head's OWN write data, stored in
    // the FIFO at enqueue. io.Mem_read_in.W_data is a live wire owned by
    // whichever NEWER miss sits at cache_miss_out when the DRAM refill lands
    // ~100 cycles later — using it here installed another request's bytes
    // (typically the hottest read line in outputBuffer, e.g. kernel text) into
    // this line's written slice, dirty, then evicted that poison to DRAM
    // (Linux SMP: spinlock 0x806b2c00 clobbered with text from 0x80274c40 →
    // "CPU2 failed to come online").
    when(!enq_fifo.io.deq.bits.is_R){
        R_combined(offset) := enq_fifo.io.deq.bits.data
    }


    val writeBufferhit_data = Wire(Vec(4, UInt(512.W)))//io.Mem_read_in.repl_data

    writeBufferhit_data := io.Mem_read_in.repl_data

    // The repl_hit bypass serves the INCOMING request, so the write patch must
    // use the incoming request's slice offset — not the FIFO head's.
    val offset_in = io.Mem_read_in.Mem_addr(7,6)
    when(!io.Mem_read_in.is_R){
        writeBufferhit_data(offset_in) := io.Mem_read_in.W_data
    }


    io.Mem_read_in.ready := enq_fifo.io.enq.ready 
    enq_fifo.io.enq.bits.valid := false.B
    enq_fifo.io.enq.bits.Mem_addr := io.Mem_read_in.Mem_addr
    enq_fifo.io.enq.bits.Rob_addr := io.Mem_read_in.Rob_addr
    enq_fifo.io.enq.bits.data := io.Mem_read_in.W_data
    enq_fifo.io.enq.bits.is_R := io.Mem_read_in.is_R





    when(io.Mem_read_in.fired && !io.Mem_read_in.repl_hit){
        enq_fifo.io.enq.valid := true.B
    }.otherwise{
        enq_fifo.io.enq.valid := false.B
    }




    val idle :: rdata :: Nil = Enum(2)


    val state = RegInit(idle)


    val axi_RDATA_counter = RegInit(0.U(log2Ceil(arlen).W))
    val next_axi_RDATA_counter = Mux(axi_RDATA_counter === io.axi.ARLEN, 0.U, axi_RDATA_counter + 1.U)



    io.axi.ARVALID := (state===idle)  && enq_fifo.io.deq.valid && !enq_fifo.io.deq.bits.valid
    io.axi.ARADDR := Cat(enq_fifo.io.deq.bits.Mem_addr(31,8), 0.U(8.W))
    io.axi.ARID := 0.U
    io.axi.ARLEN := 7.U
    io.axi.ARSIZE := 5.U
    io.axi.ARBURST := 1.U
    io.axi.ARCACHE := 2.U
    io.axi.ARLOCK := 0.U
    io.axi.ARPROT := 0.U
    io.axi.ARQOS := 0.U


    io.axi.RREADY := (state===rdata)

    
    enq_fifo.loaded := false.B

    switch(state){
        is(idle){
            when(io.axi.ARREADY && io.axi.ARVALID){
                state := rdata
            }
        }

        is(rdata){
            when(io.axi.RREADY && io.axi.RVALID){
                axi_RDATA_counter := next_axi_RDATA_counter
                temp_reg(axi_RDATA_counter) := io.axi.RDATA
                when(io.axi.RLAST){
                    state := idle
                    enq_fifo.loaded := true.B
                }
            }
        }

    }




    when(!io.Mem_read_in.repl_hit && io.Mem_read_out.fired){
        enq_fifo.io.deq.ready := true.B
    }.otherwise{
        enq_fifo.io.deq.ready := false.B
    }


    io.Mem_read_out.Rob_addr := Mux(io.Mem_read_in.repl_hit,io.Mem_read_in.Rob_addr,enq_fifo.io.deq.bits.Rob_addr)
    io.Mem_read_out.Mem_addr := Mux(io.Mem_read_in.repl_hit,io.Mem_read_in.Mem_addr,enq_fifo.io.deq.bits.Mem_addr)
    io.Mem_read_out.valid := Mux(io.Mem_read_in.repl_hit, io.Mem_read_in.repl_hit , enq_fifo.io.deq.bits.valid)
    io.Mem_read_out.R_data := Mux(io.Mem_read_in.repl_hit,writeBufferhit_data,R_combined)
    io.Mem_read_out.is_R := Mux(io.Mem_read_in.repl_hit,io.Mem_read_in.is_R,enq_fifo.io.deq.bits.is_R)

    io.Mem_read_out.ready := io.Mem_read_in.repl_hit || (enq_fifo.io.deq.valid && enq_fifo.io.deq.bits.valid)

}


