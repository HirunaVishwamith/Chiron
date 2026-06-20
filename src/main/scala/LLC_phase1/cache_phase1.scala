package LLC_cache

import chisel3._
import chisel3.experimental.BundleLiterals._
import chisel3.util._
import java.rmi.server.UID



class LLC_cache(idWidth: Int = 2, addressWidth: Int = 32, dataWidth: Int = 64,mem_dataWidth : Int = 256) extends Module {
  val io = IO(new Bundle {  
    val axi = new AXIlite(idWidth,addressWidth,dataWidth)
    val mem_axi=Flipped(new AXIlite2(idWidth,addressWidth,mem_dataWidth))
  })


    val cacheMem = Module(new Memory)

    val idle::read_mem::get_mem_data::data_out::get_data::send_addr::send_data::write_resp::send_resp_back::write_process::Nil=Enum(10)


    val state=RegInit(idle)

    //counters related to read acions
    val memReadBeatCounter = RegInit(0.U(3.W))
    val coreReadBeatCounter = RegInit(0.U(3.W))

    //counters related to write actions
    val coreDataWriteCounter = RegInit(0.U(3.W))
    val memDataWriteCounter = RegInit(0.U(3.W))


    val replace_address_Buffer=RegInit(new Bundle{
        val addr=UInt(addressWidth.W)
        val valid=Bool()
    }.Lit(
      _.addr->0.U,
      _.valid->false.B
    ))

    val replace_data_buffer=RegInit(VecInit(Seq.fill(8)((new Bundle{
        val data= UInt(mem_dataWidth.W)
        val valid = Bool()
    }).Lit(
        _.data->0.U,
        _.valid->false.B
    ))))


    //input addr registers
    val inputReadAddrBuffer = RegInit(new Bundle{
      val id = UInt(idWidth.W)
      val addr=UInt(addressWidth.W)
      val len=UInt(8.W)
      val valid=Bool()
    }.Lit(
      _.addr->5675743.U,
      _.valid->false.B,
      _.len->7.U,
      _.id->0.U
    ))


    val inputDataBuffer=RegInit(VecInit(Seq.fill(8)((new Bundle{
        val data = UInt(dataWidth.W)
        val valid = Bool()
        val wstrb= UInt((dataWidth/8).W)
    }
    ).Lit(
        _.data->0.U,
        _.valid->false.B
    ))))


    val memDataReadAddressBuffer=RegInit(new Bundle{
      val id=UInt(idWidth.W) 
      val addr=UInt(addressWidth.W)
      val len=UInt(8.W)
      val addr_valid =Bool()
    }.Lit(
        _.id->0.U,
      _.addr->0.U,
      _.len->7.U,
      _.addr_valid->false.B
    ))


    val memDataReadBuffer=RegInit(VecInit(Seq.fill(8)((new Bundle{
        val data= UInt(mem_dataWidth.W)
        val resp = UInt(2.W)
        val valid = Bool()
    }).Lit(
        _.data->0.U,
        _.resp->0.U,
        _.valid->false.B
    ))))

    val memDataReadBufferID=RegInit(0.U(idWidth.W))

        
    val B_data =RegInit(0.U(2.W))


    val DataReadBuffer=RegInit(VecInit(Seq.fill(8)((new Bundle{
        val data= UInt(dataWidth.W)
        val resp = UInt(2.W)
        val valid = Bool()
    }).Lit(
        _.data->0.U,
        _.resp->0.U,
        _.valid->false.B
    ))))

    val offset = inputReadAddrBuffer.addr(7,6)

    val delay_read_mem = RegInit(state===read_mem)
    delay_read_mem := state===read_mem



    switch(state){
        is(idle){

            when(io.axi.ARREADY && io.axi.ARVALID){
                inputReadAddrBuffer.addr:= io.axi.ARADDR
                inputReadAddrBuffer.valid := io.axi.ARVALID
                inputReadAddrBuffer.id :=io.axi.ARID
                state:=read_mem
            }  

            when(io.axi.AWREADY && io.axi.AWVALID){
                inputReadAddrBuffer.addr:= io.axi.AWADDR
                inputReadAddrBuffer.valid := io.axi.AWVALID
                inputReadAddrBuffer.id := io.axi.AWID
                state:=get_data
            }
        }

        is(read_mem){

            when(cacheMem.io.hit){

                for (i <- 0 until 8) {
                    DataReadBuffer(i).data := cacheMem.io.readData(offset)(64*(i)+63,64*(i))
                    DataReadBuffer(i).valid := 1.B
                    DataReadBuffer(i).resp := 0.B
                }
                    state := data_out
            }
            
            when(cacheMem.io.replace){
                for (i <- 0 until 4) {
                    replace_data_buffer(2*i).data := cacheMem.io.readData(i)(255,0)
                    replace_data_buffer(2*i+1).data := cacheMem.io.readData(i)(511,256)
                    replace_data_buffer(2*i).valid := 1.B
                    replace_data_buffer(2*i+1).valid :=1.B
                }

                replace_address_Buffer.valid:=true.B
                replace_address_Buffer.addr := cacheMem.io.replace_addr
            }

            when(io.mem_axi.ARREADY && io.mem_axi.ARVALID){
                state := get_mem_data
            }
        }

        is(get_mem_data){
            when(io.mem_axi.RVALID && io.mem_axi.RREADY){
                memDataReadBuffer(memReadBeatCounter).data:= io.mem_axi.RDATA
                memDataReadBuffer(memReadBeatCounter).valid:= io.mem_axi.RVALID
                memDataReadBuffer(memReadBeatCounter).resp := io.mem_axi.RRESP
                memReadBeatCounter := memReadBeatCounter+1.U
                memDataReadBufferID:=io.mem_axi.RID

                when(io.mem_axi.RLAST){
                    when(inputDataBuffer(0).valid){
                        when(!replace_address_Buffer.valid){
                            //memDataReadAddressBuffer.addr_valid :=false.B
                            inputReadAddrBuffer.valid :=false.B
                            inputDataBuffer(0).valid := false.B
                            state:= idle
                        }.otherwise{
                            state:=send_addr
                        }
                    }.otherwise{
                        state:=data_out
                        cacheMem.io.addr := memDataReadAddressBuffer.addr
                    }
                }
            }

        }

        is(data_out){

            when(io.axi.RVALID && io.axi.RREADY) {
                coreReadBeatCounter := coreReadBeatCounter + 1.U              
                when(io.axi.RLAST){
                    inputReadAddrBuffer.valid :=false.B
                    memDataReadBuffer.foreach(memData=>memData.valid:=false.B)
                    when(!replace_address_Buffer.valid){
                        state:=idle
                    }.otherwise{
                        state:=send_addr
                    }
                }           
            }
        }
    
        is(get_data){
       
            when(io.axi.WREADY && io.axi.WVALID){
                inputDataBuffer(coreDataWriteCounter).data:= io.axi.WDATA
                inputDataBuffer(coreDataWriteCounter).valid := io.axi.WVALID
                coreDataWriteCounter := coreDataWriteCounter + 1.U
                
                when(io.axi.WLAST){
                        state := send_resp_back
                }

            }

        }

        is(send_addr){

            when(io.mem_axi.AWREADY && io.mem_axi.AWVALID){
                state := send_data
            }
        }

        is(send_data){
            when(io.mem_axi.WREADY && io.mem_axi.WVALID){
                memDataWriteCounter := memDataWriteCounter + 1.U

                when(io.mem_axi.WLAST){
                    state := write_resp
                }        
            }
        }

        is(write_resp){
            when(io.mem_axi.BREADY && io.mem_axi.BVALID){
                B_data := io.mem_axi.BRESP
                state := idle
                inputReadAddrBuffer.valid :=false.B
                inputDataBuffer(0).valid := false.B
                replace_address_Buffer.valid:= false.B
                replace_data_buffer.foreach(_.valid := 0.B)
            }
        }

        is(send_resp_back){

            when(io.axi.BREADY && io.axi.BVALID){
                state := write_process
            }
        }

        is(write_process){
            when(cacheMem.io.hit){
                inputReadAddrBuffer.valid :=false.B
                inputDataBuffer(0).valid := false.B
                B_data:=0.U 
                state := idle
            }.otherwise{
                state := read_mem
            }
        }
    }

    //AXI signals related to read
    io.axi.ARREADY := (state===idle)


    io.mem_axi.ARID := memDataReadAddressBuffer.id
    io.mem_axi.ARADDR := memDataReadAddressBuffer.addr
    io.mem_axi.ARVALID := ((state===read_mem)  && !cacheMem.io.hit && delay_read_mem)
    io.mem_axi.ARLEN := 7.U
    io.mem_axi.ARSIZE := 5.U
    io.mem_axi.ARBURST := 1.U
    io.mem_axi.ARCACHE := 2.U
    io.mem_axi.ARLOCK := 0.U
    io.mem_axi.ARPROT := 0.U
    io.mem_axi.ARQOS := 0.U


    io.mem_axi.RREADY := (state===get_mem_data)

    io.axi.RDATA := DataReadBuffer(coreReadBeatCounter).data
    io.axi.RID := inputReadAddrBuffer.id
    io.axi.RVALID := (state===data_out && DataReadBuffer(coreReadBeatCounter).valid)
    io.axi.RRESP := DataReadBuffer(coreReadBeatCounter).resp
    io.axi.RLAST := (coreReadBeatCounter===inputReadAddrBuffer.len && state===data_out)


    //AXI signals related to write
    io.axi.AWREADY := (state===idle)
    
    io.axi.WREADY := (state===get_data)

    io.mem_axi.WDATA:=replace_data_buffer(memDataWriteCounter).data
    io.mem_axi.WVALID:=(state===send_data && replace_data_buffer(memDataWriteCounter).valid )
    io.mem_axi.WSTRB := "b11111111".U
    io.mem_axi.WLAST := memDataWriteCounter===memDataReadAddressBuffer.len

    io.mem_axi.AWADDR :=replace_address_Buffer.addr
    io.mem_axi.AWVALID := (state===send_addr && replace_address_Buffer.valid)
    io.mem_axi.AWBURST := 1.U
    io.mem_axi.AWID := 0.U
    io.mem_axi.AWLEN := 7.U
    io.mem_axi.AWCACHE := 2.U
    io.mem_axi.AWLOCK := 0.U
    io.mem_axi.AWSIZE := 5.U
    io.mem_axi.AWPROT :=0.U
    io.mem_axi.AWQOS := 0.U


    io.mem_axi.BREADY := (state===write_resp)

    io.axi.BRESP := B_data
    io.axi.BID := inputReadAddrBuffer.id
    io.axi.BVALID := (state===send_resp_back) 

    //data trasnfer in between
    memDataReadAddressBuffer.addr := inputReadAddrBuffer.addr
    memDataReadAddressBuffer.addr_valid := inputReadAddrBuffer.valid 
    memDataReadAddressBuffer.len := inputReadAddrBuffer.len
    memDataReadAddressBuffer.id := inputReadAddrBuffer.id




    //cache access
    cacheMem.io.addr := inputReadAddrBuffer.addr

    val delay_RLAST = RegInit(0.B) //to make the RLAST delay by one to store data
    val delay_WLAST = RegInit(0.B)

    delay_RLAST := io.mem_axi.RLAST && io.mem_axi.RREADY && io.mem_axi.RVALID
    delay_WLAST := io.axi.WLAST && io.axi.WREADY && io.axi.WVALID


    cacheMem.io.full_line := delay_RLAST
    cacheMem.io.writeEnable := delay_RLAST || (state===write_process && cacheMem.io.hit && inputDataBuffer(0).valid)

    val temp = Wire(Vec(4,UInt(512.W)))
    temp := cacheMem.io.readData

    temp(offset) := Cat( inputDataBuffer(7).data,
                        inputDataBuffer(6).data,
                        inputDataBuffer(5).data,
                        inputDataBuffer(4).data,
                        inputDataBuffer(3).data,
                        inputDataBuffer(2).data,
                        inputDataBuffer(1).data,
                        inputDataBuffer(0).data)




    val data_wire= Wire(Vec(4,UInt(512.W)))
    val data_wire_valid = Wire(Vec(4,Bool()))


    for(i<-0 until 4){
        data_wire(i) := Cat(memDataReadBuffer(Cat(i.U, 1.U(1.W))).data, memDataReadBuffer(Cat(i.U, 0.U(1.W))).data)
        data_wire_valid(i) := memDataReadBuffer(Cat(i.U, 1.U(1.W))).valid && memDataReadBuffer(Cat(i.U, 0.U(1.W))).valid
    }


    when(inputDataBuffer(0).valid){

        memDataReadBuffer(offset+1.U).data := Cat( inputDataBuffer(7).data,
                                            inputDataBuffer(6).data,
                                            inputDataBuffer(5).data,
                                            inputDataBuffer(4).data)

        memDataReadBuffer(offset+0.U).data :=Cat(inputDataBuffer(3).data,
                                            inputDataBuffer(2).data,
                                            inputDataBuffer(1).data,
                                            inputDataBuffer(0).data)
    }




    when(data_wire_valid(offset)){
        for(i<- 0 until 8){
            DataReadBuffer(i).data := data_wire(offset)(64*(i)+63,64*(i))
            DataReadBuffer(i).valid := data_wire_valid(offset) 
            DataReadBuffer(i).resp := 0.U
        }
    }



    cacheMem.io.writeData := Mux(delay_RLAST,data_wire,temp)
    //io.axi.cacheData := Cat(cacheMem.io.readData.reverse)


    cacheMem.io.hit_ready := ((delay_read_mem && state===read_mem) || state===write_process)
    cacheMem.io.replace_ready := (delay_read_mem && state===read_mem) && !replace_address_Buffer.valid
}




