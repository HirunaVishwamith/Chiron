
package l2_cache

import chisel3._
import chisel3.experimental.BundleLiterals._
import chisel3.util._


class fifo_line (idWidth: Int = 2 , addressWidth: Int=32) extends Bundle{
    val valid=Bool()
    val RW_address = UInt(addressWidth.W)
    val RW_data = UInt(512.W)
    val is_R = Bool()
    val id = UInt(idWidth.W)
}



class l2_Rob(arlen:Int=7,beat_size:Int=64 ,addr_w: Int = 3,idWidth: Int = 2, addressWidth: Int = 32, dataWidth: Int = 64,mem_dataWidth : Int = 256) extends Module {
    val io = IO(new Bundle {  
    val axi = new AXIlite(idWidth,addressWidth,dataWidth)
    val Rob_out = new Rob_out(8,32)
    val Rob_in = new Rob_in()
    //val mem_axi=Flipped(new AXIlite2(idWidth,addressWidth,mem_dataWidth))
  })


    //input buffer
    val inputBuffer = RegInit(new Bundle {
        val addr = UInt(addressWidth.W)
        val data = Vec(8, UInt(64.W))
        val is_R = Bool()
        val valid = Bool()
        val id = UInt(idWidth.W)
    }.Lit(
        _.addr -> 0.U,
        _.is_R -> false.B,
        _.valid -> false.B,
        _.id -> 0.U
    ))

    

    //output buffer
    val outputBuffer = RegInit(new Bundle{
        val addr = UInt(addressWidth.W)
        val Rob_addr = UInt(3.W)
        val is_R = Bool()
        val data = UInt(512.W)
        val valid = Bool()
    }.Lit(
        _.addr -> 0.U,
        _.is_R -> false.B,
        _.Rob_addr -> 0.U,
        _.valid -> false.B,
        _.data->0.U
    ))


    val empty :: full :: half  :: Nil = Enum(3)
    val inputBufferState = RegInit(empty)
    val outputBufferState = RegInit(empty)

    val readyInputBuffer = WireDefault(true.B) 
    val readyOutputBuffer = WireDefault(true.B) 

    val concatData = Cat(inputBuffer.data.reverse)
    val arlen_ = 7.U




    //when there is WRITE axi the stalling the read axi until write data is read
    val stall = RegInit(false.B)

    //the couter of the RDATA
    val axi_RDATA_counter = RegInit(0.U(log2Ceil(arlen).W))
    val next_axi_RDATA_counter = Mux(axi_RDATA_counter === io.axi.ARLEN, 0.U, axi_RDATA_counter + 1.U)

    //the counter of the WDATA
    val axi_WDATA_counter = RegInit(0.U(log2Ceil(arlen).W))
    val next_axi_WDATA_counter = Mux(axi_WDATA_counter === io.axi.AWLEN, 0.U, axi_WDATA_counter + 1.U)




    //fifo for the Rob structure
    val enq_fifo = Module(new regFifo(new fifo_line(idWidth,addressWidth) , scala.math.pow(2,addr_w).asInstanceOf[Int]){

        class ports extends Bundle{
            val addr = Input(UInt(log2Ceil(depth).W))
            val data = Input(UInt(512.W))
            val is_R = Input(Bool())
            //val data_counter = Input(UInt(3.W)) //need to think the width here
            val data_valid = Input(Bool())
            val valid = Input(Bool())
        } 

        val writePort = IO(new ports)
        val allocatedAddr = IO(Output(UInt(log2Ceil(depth).W)))
        allocatedAddr := writePtr


        when(writePort.valid){
            memReg(writePort.addr).RW_data := writePort.data 
            memReg(writePort.addr).is_R := writePort.is_R
            memReg(writePort.addr).valid := writePort.data_valid 
        }

        



    })







   // inputBuffer.addr := mux(io.axi.ARVALID,io.axi.ARADDR,io.axi.AWADDR)
    //inputBuffer.is_R := true.B

    val R_data = VecInit((0 until arlen+1).map { i =>
            enq_fifo.io.deq.bits.RW_data((beat_size * i)+ (beat_size-1), beat_size * (i))
    })



    io.axi.ARREADY := readyInputBuffer

    io.axi.RDATA := R_data(axi_RDATA_counter)
    io.axi.RLAST := (axi_RDATA_counter===io.axi.ARLEN) 
    io.axi.RID := enq_fifo.io.deq.bits.id
    io.axi.RVALID :=  enq_fifo.io.deq.valid && enq_fifo.io.deq.bits.valid && enq_fifo.io.deq.bits.is_R
    io.axi.RRESP := 0.U

    io.axi.AWREADY := readyInputBuffer

    io.axi.WREADY := stall  // rethink about this

    io.axi.BVALID := enq_fifo.io.deq.valid && enq_fifo.io.deq.bits.valid && !enq_fifo.io.deq.bits.is_R
    io.axi.BRESP := 0.U
    io.axi.BID := enq_fifo.io.deq.bits.id


    enq_fifo.io.enq.bits.RW_address := inputBuffer.addr
    enq_fifo.io.enq.bits.is_R := inputBuffer.is_R
    enq_fifo.io.enq.bits.RW_data := concatData
    enq_fifo.io.enq.bits.valid := false.B
    enq_fifo.io.enq.bits.id := inputBuffer.id 
    enq_fifo.io.enq.valid := false.B

    //enq_fifo.io.enq.valid := false.B
    //io.Rob_out.ready := false.B

    
    //for AXI R transaction
    when(io.axi.RREADY && io.axi.RVALID){
        axi_RDATA_counter := next_axi_RDATA_counter
        when(axi_RDATA_counter===io.axi.ARLEN){
            enq_fifo.io.deq.ready := 1.U
        }.otherwise{
            enq_fifo.io.deq.ready := 0.U
        }
    }.elsewhen(io.axi.BREADY && io.axi.BVALID) {
        enq_fifo.io.deq.ready := 1.U
    }.otherwise{
        enq_fifo.io.deq.ready := 0.U
    }


    //for the input buffer state cycle
    switch(inputBufferState){
        is(empty){

            readyInputBuffer := true.B

            when(io.axi.ARVALID && !stall){
                inputBuffer.id := io.axi.ARID
                inputBuffer.addr := io.axi.ARADDR
                inputBuffer.is_R := true.B
                inputBuffer.data := VecInit(Seq.fill(8)(0.U(64.W)))
                inputBuffer.valid := true.B
                inputBufferState := full
            }

            when(io.axi.AWVALID && !stall){
                inputBuffer.id := io.axi.AWID
                inputBuffer.addr := io.axi.AWADDR
                inputBuffer.is_R := false.B
                stall := true.B
                inputBufferState := half
            }

        }

        is(half){

            readyInputBuffer := false.B

            when(io.axi.WREADY && io.axi.WVALID){
                axi_WDATA_counter := next_axi_WDATA_counter
                inputBuffer.data(axi_WDATA_counter) := io.axi.WDATA
                when(io.axi.WLAST){
                    stall := false.B
                    inputBuffer.valid := true.B
                    inputBufferState := full
                }
            }
        }

        is(full){
            readyInputBuffer := false.B

            when(readyOutputBuffer && enq_fifo.io.enq.ready && inputBuffer.valid){
                outputBuffer.valid := true.B
                outputBuffer.addr := inputBuffer.addr
                outputBuffer.Rob_addr := enq_fifo.allocatedAddr
                outputBuffer.data := concatData
                outputBuffer.is_R := inputBuffer.is_R

                inputBuffer.valid := false.B
                outputBufferState := full
                inputBufferState := empty

                enq_fifo.io.enq.valid := true.B

            }.otherwise{
                enq_fifo.io.enq.valid := false.B
            }
        }
    }


    //for the outputbuffer state cycle
    switch(outputBufferState){
        is(empty){
            readyOutputBuffer := true.B
        }

        is(full){
            io.Rob_out.ready := true.B
            readyOutputBuffer :=false.B

            when(io.Rob_out.fired){
                outputBuffer.valid := false.B 
                outputBufferState := empty
            }
        }
    }
    



    //write to fifo after read from the cache
    enq_fifo.writePort.addr := io.Rob_in.Rob_address
    enq_fifo.writePort.data := io.Rob_in.data
    enq_fifo.writePort.valid := io.Rob_in.valid
    enq_fifo.writePort.data_valid := io.Rob_in.fired
    enq_fifo.writePort.is_R := io.Rob_in.is_R

    io.Rob_in.ready := enq_fifo.io.deq.valid



    //out for the cache to read data
    io.Rob_out.Rob_address := outputBuffer.Rob_addr
    io.Rob_out.Mem_address := outputBuffer.addr
    io.Rob_out.valid := outputBuffer.valid
    io.Rob_out.is_R := outputBuffer.is_R
    io.Rob_out.ready := outputBuffer.valid
    io.Rob_out.data := outputBuffer.data


}

