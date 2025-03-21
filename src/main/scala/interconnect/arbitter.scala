/*
Assuming AW and W channels assert valid signal same time
*/

//aribitter IO

package Interconnect

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._


class AribiterIO extends Bundle {
    //D$
    //AR,AW,W
    val AWVALID_0 = Input(Bool())
	val AWREADY_0 = Output(Bool())
	val AWBAR_0 = Input(Bool())

	val WVALID_0 = Input(Bool())
	val WLAST_0 = Input(Bool())
	val WREADY_0 = Output(Bool())

	val ARVALID_0 = Input(Bool())
	val ARREADY_0 = Output(Bool())


    //I$
    val AWVALID_1 = Input(Bool())
	val AWREADY_1 = Output(Bool())


	val WVALID_1 = Input(Bool())
	val WLAST_1 = Input(Bool())
	val WREADY_1 = Output(Bool())

	val ARVALID_1 = Input(Bool())
	val ARREADY_1 = Output(Bool())

    //mux out
	val select = Output(UInt(3.W))

    //fifo enq
    val enq_valid = Output(Bool())
    val enq_ready = Input(Bool())
    val nstall = Input(Bool())
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

	io.enq_valid := false.B
	io.select := 0.U

	val stateReg = RegInit(0.U(4.W))
	//0_000: D$, 0_001: D$r, 0_010: D$r_enq, 0_100: D$wa, 0_101: D$wa_enq, 0_110:D$wd, 0_111:D$wd_enq
	//1_000: I$, 1_001: I$r, 1_010: I$r_enq, 1_100: I$wa, 1_101: I$wa_enq, 1_110:I$wd, 1_111:I$wd_enq

	val barreg = RegInit(false.B)
	val wlast = RegInit(false.B)

    switch(stateReg){
		is(0.U){//0_000: D$
            when(!io.AWVALID_0 & !io.ARVALID_0){
                stateReg := 8.U
            }.elsewhen(io.ARVALID_0){
				stateReg := 1.U
            }.elsewhen(io.AWVALID_0){
				stateReg := 4.U
            }
            io.enq_valid := false.B
		}
		is(1.U){ //0_001: D$r
            when(io.enq_ready){
                stateReg := 2.U
            }.otherwise{
				stateReg := 1.U
            }
            io.enq_valid := false.B
		}
		is(2.U){ //0_010: D$r_enq
            io.ARREADY_0 := true.B && io.ARVALID_0
            io.enq_valid := true.B && io.ARVALID_0
            io.select := 0.U
            when(io.nstall){
                stateReg := 8.U
            }.otherwise{
                stateReg := 2.U
            }
		}
		is(4.U){ //0_100: D$wa
            when(io.enq_ready){
                stateReg := 5.U
            }.otherwise{
				stateReg := 4.U
            }
            io.enq_valid := false.B
            barreg := io.AWBAR_0
		}
		is(5.U){ //0_101: D$wa_enq
            when(barreg){
                when(io.nstall){
                    stateReg := 8.U
                }.otherwise{
                    stateReg := 5.U
                }
            }.otherwise{
				stateReg := 6.U
            }

            io.AWREADY_0 := true.B && io.AWVALID_0
            io.enq_valid := true.B && io.AWVALID_0
            io.select := 1.U
		}
		is(6.U){ //0_110:D$wd
            when(io.enq_ready && io.WVALID_0){
                stateReg := 7.U
            }.otherwise{
				stateReg := 6.U
            }
            io.enq_valid := false.B
            wlast := io.WLAST_0
		}
		is(7.U){ //0_111:D$wd_enq
            when(!wlast){
                stateReg := 6.U
            }.otherwise{
				when(io.nstall){
                    stateReg := 8.U
				}.otherwise{
                    stateReg := 7.U
				}
            }
			io.WREADY_0 := true.B && io.WVALID_0
			io.enq_valid := true.B && io.WVALID_0
			io.select := 2.U

		}
		is(8.U){ //1_000: I$
            when(!io.AWVALID_1 & !io.ARVALID_1){
                stateReg := 0.U
            }.elsewhen(io.ARVALID_1){
				stateReg := 9.U
            }.elsewhen(io.AWVALID_1){
				stateReg := 12.U
            }
            io.enq_valid := false.B
		}
		is(9.U){ //1_001: I$r
            when(io.enq_ready){
                stateReg := 10.U
            }.otherwise{
				stateReg := 9.U
            }
            io.enq_valid := false.B
		}
		is(10.U){ //1_010: I$r_enq
            io.ARREADY_1 := true.B && io.ARVALID_1
            io.enq_valid := true.B && io.ARVALID_1
            io.select := 4.U
            when(io.nstall){
                stateReg := 0.U
            }.otherwise{
                stateReg := 10.U
            }
		}
		is(12.U){ //1_100: I$wa
            when(io.enq_ready){
                stateReg := 13.U
            }.otherwise{
				stateReg := 12.U
            }
            io.enq_valid := false.B
		}
		is(13.U){ //1_101: I$wa_enq
            stateReg := 14.U
            io.AWREADY_1 := true.B && io.AWVALID_1
            io.enq_valid := true.B && io.AWVALID_1
            io.select := 5.U
		}
		is(14.U){ //1_110:I$wd
            when(io.enq_ready && io.WVALID_1){
                stateReg := 15.U
            }.otherwise{
				stateReg := 14.U
            }
            io.enq_valid := false.B
		}
		is(15.U){ //1_111:I$wd_enq
            when(!io.WLAST_1){
                stateReg := 14.U
            }.otherwise{
				stateReg := 0.U
            }
            io.WREADY_1 := true.B && io.WVALID_1
			io.enq_valid := true.B && io.WVALID_1
			io.select := 6.U
		}
	}


}


//AXI ready valid rules
//A source is not permitted to wait until READY is asserted before asserting VALID
//Once VALID is asserted it must remain asserted until the handshake occurs, at a rising clock edge at which VALID and READY are both asserted.
//A destination is permitted to wait for VALID to be asserted before asserting the corresponding READY. If READY is asserted, it is permitted to deassert READY before VALID is asserted.

//Cache line width is 64bytes and data bus width width is 64 bits therefore ther can be maximum up to 8 beats
