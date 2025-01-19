/*
Assuming AW and W channels assert valid signal same time
*/

//aribitter IO

package Interconnect

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._


class AribiterIO extends Bundle {
    //core_0
    //AR,AW,W
    val AWVALID_0 = Input(Bool())
	val AWREADY_0 = Output(Bool())

	val WVALID_0 = Input(Bool())
	val WLAST_0 = Input(Bool())
	val WREADY_0 = Output(Bool())

	val ARVALID_0 = Input(Bool())
	val ARREADY_0 = Output(Bool())


    //core_1
    val AWVALID_1 = Input(Bool())
	val AWREADY_1 = Output(Bool())

	val WVALID_1 = Input(Bool())
	val WLAST_1 = Input(Bool())
	val WREADY_1 = Output(Bool())

	val ARVALID_1 = Input(Bool())
	val ARREADY_1 = Output(Bool())

    //core_2
    val AWVALID_2 = Input(Bool())
	val AWREADY_2 = Output(Bool())

	val WVALID_2 = Input(Bool())
	val WLAST_2 = Input(Bool())
	val WREADY_2 = Output(Bool())

	val ARVALID_2 = Input(Bool())
	val ARREADY_2 = Output(Bool())

    //core_3
    val AWVALID_3 = Input(Bool())
	val AWREADY_3 = Output(Bool())

	val WVALID_3 = Input(Bool())
	val WLAST_3 = Input(Bool())
	val WREADY_3 = Output(Bool())

	val ARVALID_3 = Input(Bool())
	val ARREADY_3 = Output(Bool())

    //mux out
	val select = Output(UInt(4.W))

    //fifo enq
    val enq_valid = Output(Bool())
    val enq_ready = Input(Bool())
}

class arbiter extends Module {
	val io= IO(new AribiterIO)

	//assigning default values to outputs

	io.AWREADY_0 := false.B
	io.WREADY_0 := false.B
	io.ARREADY_0 := false.B

	io.AWREADY_1 := false.B
	io.WREADY_1 := false.B
	io.ARREADY_1 := false.B

	io.AWREADY_2 := false.B
	io.WREADY_2 := false.B
	io.ARREADY_2 := false.B

	io.AWREADY_3 := false.B
	io.WREADY_3 := false.B
	io.ARREADY_3 := false.B

	io.enq_valid := false.B
	io.select := 0.U

	val stateReg = RegInit(0.U(5.W))
	//00_000: core_0, 00_001: core_0r, 00_010: core_0r_enq, 00_100: core_0wa, 00_101: core_0wa_enq, 00_110:core_0wd, 00_111:core_0wd_enq
	//01_000: core_1, 01_001: core_1r, 01_010: core_1r_enq, 01_100: core_1wa, 01_101: core_1wa_enq, 01_110:core_1wd, 01_111:core_1wd_enq
	//10_000: core_2, 10_001: core_2r, 10_010: core_2r_enq, 10_100: core_2wa, 10_101: core_2wa_enq, 10_110:core_2wd, 10_111:core_2wd_enq
	//11_000: core_3, 11_001: core_3r, 11_010: core_3r_enq, 11_100: core_3wa, 11_101: core_3wa_enq, 11_110:core_3wd, 11_111:core_3wd_enq

    switch(stateReg){
		is(0.U){//00_000: core_0
            when(!io.AWVALID_0 & !io.ARVALID_0){
                stateReg := 8.U
            }.elsewhen(io.ARVALID_0){
				stateReg := 1.U
            }.elsewhen(io.AWVALID_0){
				stateReg := 4.U
            }
            io.enq_valid := false.B
		}
		is(1.U){ //00_001: core_0r
            when(io.enq_ready){
                stateReg := 2.U
            }.otherwise{
				stateReg := 1.U
            }
            io.enq_valid := false.B
		}
		is(2.U){ //00_010: core_0r_enq
            stateReg := 8.U
            io.ARREADY_0 := true.B
            io.enq_valid := true.B
            io.select := 0.U
		}
		is(4.U){ //00_100: core_0wa
            when(io.enq_ready){
                stateReg := 5.U
            }.otherwise{
				stateReg := 4.U
            }
            io.enq_valid := false.B
		}
		is(5.U){ //00_101: core_0wa_enq
            stateReg := 6.U
            io.AWREADY_0 := true.B
            io.enq_valid := true.B
            io.select := 1.U
		}
		is(6.U){ //00_110:core_0wd
            when(io.enq_ready && io.WVALID_0){
                stateReg := 7.U
            }.otherwise{
				stateReg := 6.U
            }
            io.enq_valid := false.B
		}
		is(7.U){ //00_111:core_0wd_enq
            when(!io.WLAST_0){
                stateReg := 6.U
            }.otherwise{
				stateReg := 8.U
            }
			io.WREADY_0 := true.B
			io.enq_valid := true.B
			io.select := 2.U

		}
		is(8.U){ //01_000: core_1
            when(!io.AWVALID_1 & !io.ARVALID_1){
                stateReg := 16.U
            }.elsewhen(io.ARVALID_1){
				stateReg := 9.U
            }.elsewhen(io.AWVALID_1){
				stateReg := 12.U
            }
            io.enq_valid := false.B
		}
		is(9.U){ //01_001: core_1r
            when(io.enq_ready){
                stateReg := 10.U
            }.otherwise{
				stateReg := 9.U
            }
            io.enq_valid := false.B
		}
		is(10.U){ //01_010: core_1r_enq
            stateReg := 16.U
            io.ARREADY_1 := true.B
            io.enq_valid := true.B
            io.select := 4.U
		}
		is(12.U){ //01_100: core_1wa
            when(io.enq_ready){
                stateReg := 13.U
            }.otherwise{
				stateReg := 12.U
            }
            io.enq_valid := false.B
		}
		is(13.U){ //01_101: core_1wa_enq
            stateReg := 14.U
            io.AWREADY_1 := true.B
            io.enq_valid := true.B
            io.select := 5.U
		}
		is(14.U){ //01_110:core_1wd
            when(io.enq_ready && io.WVALID_1){
                stateReg := 15.U
            }.otherwise{
				stateReg := 14.U
            }
            io.enq_valid := false.B
		}
		is(15.U){ //01_111:core_1wd_enq
            when(!io.WLAST_1){
                stateReg := 14.U
            }.otherwise{
				stateReg := 16.U
            }
            io.WREADY_1 := true.B
			io.enq_valid := true.B
			io.select := 6.U
		}
		is(16.U){ //10_000: core_2
            when(!io.AWVALID_2 & !io.ARVALID_2){
                stateReg := 24.U
            }.elsewhen(io.ARVALID_2){
				stateReg := 17.U
            }.elsewhen(io.AWVALID_2){
				stateReg := 20.U
            }
            io.enq_valid := false.B
		}
		is(17.U){ //10_001: core_2r
            when(io.enq_ready){
                stateReg := 18.U
            }.otherwise{
				stateReg := 17.U
            }
            io.enq_valid := false.B
		}
		is(18.U){ //10_010: core_2r_enq
            stateReg := 24.U
            io.ARREADY_2 := true.B
            io.enq_valid := true.B
            io.select := 8.U
		}
		is(20.U){ //10_100: core_2wa
            when(io.enq_ready){
                stateReg := 21.U
            }.otherwise{
				stateReg := 20.U
            }
            io.enq_valid := false.B
		}
		is(21.U){ //10_101: core_2wa_enq
            stateReg := 22.U
            io.AWREADY_2 := true.B
            io.enq_valid := true.B
            io.select := 9.U
		}
		is(22.U){ //10_110:core_2wd
            when(io.enq_ready && io.WVALID_2){
                stateReg := 23.U
            }.otherwise{
				stateReg := 22.U
            }
            io.enq_valid := false.B
		}
		is(23.U){ //10_111:core_2wd_enq
            when(!io.WLAST_2){
                stateReg := 22.U
            }.otherwise{
				stateReg := 24.U
            }
            io.WREADY_2 := true.B
			io.enq_valid := true.B
			io.select := 10.U
		}
		is(24.U){ //11_000: core_3
            when(!io.AWVALID_3 & !io.ARVALID_3){
                stateReg := 0.U
            }.elsewhen(io.ARVALID_3){
				stateReg := 25.U
            }.elsewhen(io.AWVALID_3){
				stateReg := 28.U
            }
            io.enq_valid := false.B
		}
		is(25.U){ //11_001: core_3r
            when(io.enq_ready){
                stateReg := 26.U
            }.otherwise{
				stateReg := 25.U
            }
            io.enq_valid := false.B
		}
		is(26.U){ //11_010: core_3r_enq
            stateReg := 0.U
            io.ARREADY_3 := true.B
            io.enq_valid := true.B
            io.select := 12.U
		}
		is(28.U){ //11_100: core_3wa
            when(io.enq_ready){
                stateReg := 29.U
            }.otherwise{
				stateReg := 28.U
            }
            io.enq_valid := false.B
		}
		is(29.U){ //11_101: core_3wa_enq
            stateReg := 30.U
            io.AWREADY_3 := true.B
            io.enq_valid := true.B
            io.select := 13.U
		}
		is(30.U){ //11_110:core_3wd
            when(io.enq_ready && io.WVALID_3){
                stateReg := 31.U
            }.otherwise{
				stateReg := 30.U
            }
            io.enq_valid := false.B
		}
		is(31.U){ //11_111:core_3wd_enq
            when(!io.WLAST_3){
                stateReg := 30.U
            }.otherwise{
				stateReg := 0.U
            }
            io.WREADY_3 := true.B
			io.enq_valid := true.B
			io.select := 14.U
		}

	}


}


//AXI ready valid rules
//A source is not permitted to wait until READY is asserted before asserting VALID
//Once VALID is asserted it must remain asserted until the handshake occurs, at a rising clock edge at which VALID and READY are both asserted.
//A destination is permitted to wait for VALID to be asserted before asserting the corresponding READY. If READY is asserted, it is permitted to deassert READY before VALID is asserted.

//Cache line width is 64bytes and data bus width width is 64 bits therefore ther can be maximum up to 8 beats
