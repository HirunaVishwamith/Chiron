package l2_cache

import chisel3._
import chisel3.experimental.BundleLiterals._
import chisel3.util._


class writeBackFifo_line (idWidth: Int = 2 , addressWidth: Int=32) extends Bundle{
    val Mem_addr = UInt(addressWidth.W)
    val data = Vec(4,UInt(512.W))
    val valid = Bool()
}



class writeBackBuffer(arlen:Int=7,beat_size:Int=64 ,addr_w: Int = 3,idWidth: Int = 2, addressWidth: Int = 32, dataWidth: Int = 256,mem_dataWidth : Int = 256) extends Module {
    val io = IO(new Bundle {  
        val replace_in = new replace_in()
        val axi = Flipped(new AXIlite2(idWidth, addressWidth, dataWidth))
        val miss_address = Input(UInt(addressWidth.W))
        val miss_out = new miss_out()
        val repl_finish = Output(Bool())
  })


    val idle :: wdata :: bresp :: Nil = Enum(3)
    val state = RegInit(idle)

    val axi_WDATA_counter = RegInit(0.U(log2Ceil(arlen).W))   
    val next_axi_WDATA_counter = Mux(axi_WDATA_counter === io.axi.AWLEN, 0.U, axi_WDATA_counter + 1.U)

    val w_data_wire = Wire(Vec(8,(UInt(256.W))))

    val fifo = Module(new regFifo(new writeBackFifo_line(idWidth,addressWidth) , 3){ // have a width of 3
        class ports extends Bundle{
            val addr = Input(UInt(32.W))
            val repl_hit = Output(Bool())
            val data = Output(Vec(4,UInt(512.W)))
        } 

        val port = IO(new ports)
        val loaded = IO(Input(Bool()))

        port.repl_hit := false.B
        port.data := VecInit(Seq.fill(4)(0.U(512.W)))

        for(i <- 0 until depth ){
            when(memReg(i).valid && memReg(i).Mem_addr=== port.addr){
                port.repl_hit := true.B
                port.data := memReg(i).data
            }
        }

        when(loaded){
            memReg(readPtr).valid := false.B
        }
    })

    for(i <- 0 until 4){
        w_data_wire(2*i) := fifo.io.deq.bits.data(i)(255,0)
        w_data_wire(2*i+1) := fifo.io.deq.bits.data(i)(511,256)
    }
    

    when(io.replace_in.fired){
        fifo.io.enq.valid := true.B
    }.otherwise{
        fifo.io.enq.valid := false.B
    }

    io.replace_in.ready := fifo.io.enq.ready
    fifo.io.enq.bits.Mem_addr := io.replace_in.Mem_addr
    fifo.io.enq.bits.data := io.replace_in.data
    fifo.io.enq.bits.valid := true.B


    io.axi.AWVALID := (state===idle) && fifo.io.deq.valid
    io.axi.AWADDR := Cat(fifo.io.deq.bits.Mem_addr(31,8), 0.U(8.W))
    io.axi.AWID := 0.U
    io.axi.AWLEN := 7.U
    io.axi.AWSIZE := 5.U
    io.axi.AWCACHE := 2.U   
    io.axi.AWLOCK := 0.U
    io.axi.AWPROT :=0.U
    io.axi.AWQOS := 0.U
    io.axi.AWBURST := 1.U
    
    io.axi.WVALID := (state===wdata)
    io.axi.WDATA := w_data_wire(axi_WDATA_counter)
    io.axi.WLAST := (axi_WDATA_counter===io.axi.AWLEN) 
    io.axi.WSTRB := "b11111111".U

    io.axi.BREADY := (state===bresp)
    

    fifo.io.deq.ready := false.B  //think this
    fifo.loaded := false.B
    io.repl_finish := false.B


    switch(state){
        is(idle){
            when(io.axi.AWREADY && io.axi.AWVALID){
                state := wdata
            }

        }

        is(wdata){
            when(io.axi.WREADY && io.axi.WVALID){
                axi_WDATA_counter := next_axi_WDATA_counter
                when(io.axi.WLAST){
                    state := bresp
                }
            }
        }


        is(bresp){
            when(io.axi.BREADY && io.axi.BVALID){
                state := idle
                fifo.io.deq.ready := true.B
                fifo.loaded := true.B
                io.repl_finish := true.B
            }
        }
        
    }


    fifo.port.addr := io.miss_address
    io.miss_out.repl_hit := fifo.port.repl_hit
    io.miss_out.data := fifo.port.data



}



