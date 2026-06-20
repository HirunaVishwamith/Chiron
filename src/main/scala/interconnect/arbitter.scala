/*
Assuming AW and W channels assert valid signal same time
*/

//aribitter IO

package Interconnect

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._


class AribiterIO extends Bundle {
    //core_0_D$
    //AR,AW,W
    val AWVALID_0 = Input(Bool())
	val AWREADY_0 = Output(Bool())
	val AWBAR_0 = Input(Bool())

	val WVALID_0 = Input(Bool())
	val WLAST_0 = Input(Bool())
	val WREADY_0 = Output(Bool())

	val ARVALID_0 = Input(Bool())
	val ARREADY_0 = Output(Bool())


    //core_0_I$
    val AWVALID_1 = Input(Bool())
	val AWREADY_1 = Output(Bool())

	val WVALID_1 = Input(Bool())
	val WLAST_1 = Input(Bool())
	val WREADY_1 = Output(Bool())

	val ARVALID_1 = Input(Bool())
	val ARREADY_1 = Output(Bool())

    //core_1_D$
    val AWVALID_2 = Input(Bool())
	val AWREADY_2 = Output(Bool())
	val AWBAR_2 = Input(Bool())

	val WVALID_2 = Input(Bool())
	val WLAST_2 = Input(Bool())
	val WREADY_2 = Output(Bool())

	val ARVALID_2 = Input(Bool())
	val ARREADY_2 = Output(Bool())

    //core_1_I$
    val AWVALID_3 = Input(Bool())
	val AWREADY_3 = Output(Bool())

	val WVALID_3 = Input(Bool())
	val WLAST_3 = Input(Bool())
	val WREADY_3 = Output(Bool())

	val ARVALID_3 = Input(Bool())
	val ARREADY_3 = Output(Bool())

	//new cores
    //core_2_D$
    //AR,AW,W
    val AWVALID_4 = Input(Bool())
	val AWREADY_4 = Output(Bool())
	val AWBAR_4 = Input(Bool())

	val WVALID_4 = Input(Bool())
	val WLAST_4 = Input(Bool())
	val WREADY_4 = Output(Bool())

	val ARVALID_4 = Input(Bool())
	val ARREADY_4 = Output(Bool())


    //core_2_I$
    val AWVALID_5 = Input(Bool())
	val AWREADY_5 = Output(Bool())

	val WVALID_5 = Input(Bool())
	val WLAST_5 = Input(Bool())
	val WREADY_5 = Output(Bool())

	val ARVALID_5 = Input(Bool())
	val ARREADY_5 = Output(Bool())

    //core_3_D$
    val AWVALID_6 = Input(Bool())
	val AWREADY_6 = Output(Bool())
	val AWBAR_6 = Input(Bool())

	val WVALID_6 = Input(Bool())
	val WLAST_6 = Input(Bool())
	val WREADY_6 = Output(Bool())

	val ARVALID_6 = Input(Bool())
	val ARREADY_6 = Output(Bool())

    //core_3_I$
    val AWVALID_7 = Input(Bool())
	val AWREADY_7 = Output(Bool())

	val WVALID_7 = Input(Bool())
	val WLAST_7 = Input(Bool())
	val WREADY_7 = Output(Bool())

	val ARVALID_7 = Input(Bool())
	val ARREADY_7 = Output(Bool())

    //mux out
	val select = Output(UInt(5.W))

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

	io.AWREADY_4 := false.B
	io.WREADY_4 := false.B
	io.ARREADY_4 := false.B

	io.AWREADY_5 := false.B
	io.WREADY_5 := false.B
	io.ARREADY_5 := false.B

	io.AWREADY_6 := false.B
	io.WREADY_6 := false.B
	io.ARREADY_6 := false.B

	io.AWREADY_7 := false.B
	io.WREADY_7 := false.B
	io.ARREADY_7 := false.B

	io.enq_valid := false.B
	io.select := 0.U

	val stateReg = RegInit(0.U(6.W))
	//000_000: core_0, 000_001: core_0r, 000_010: core_0r_enq, 000_100: core_0wa, 000_101: core_0wa_enq, 000_110:core_0wd, 000_111:core_0wd_enq
	//001_000: core_1, 001_001: core_1r, 001_010: core_1r_enq, 001_100: core_1wa, 001_101: core_1wa_enq, 001_110:core_1wd, 001_111:core_1wd_enq
	//010_000: core_2, 010_001: core_2r, 010_010: core_2r_enq, 010_100: core_2wa, 010_101: core_2wa_enq, 010_110:core_2wd, 010_111:core_2wd_enq
	//011_000: core_3, 011_001: core_3r, 011_010: core_3r_enq, 011_100: core_3wa, 011_101: core_3wa_enq, 011_110:core_3wd, 011_111:core_3wd_enq
	//100_000: core_4, 100_001: core_4r, 100_010: core_4r_enq, 100_100: core_4wa, 100_101: core_4wa_enq, 100_110:core_4wd, 100_111:core_4wd_enq
	//101_000: core_5, 101_001: core_5r, 101_010: core_5r_enq, 101_100: core_5wa, 101_101: core_5wa_enq, 101_110:core_5wd, 101_111:core_5wd_enq
	//110_000: core_6, 110_001: core_6r, 110_010: core_6r_enq, 110_100: core_6wa, 110_101: core_6wa_enq, 110_110:core_6wd, 110_111:core_6wd_enq
	//111_000: core_7, 111_001: core_7r, 111_010: core_7r_enq, 111_100: core_7wa, 111_101: core_7wa_enq, 111_110:core_7wd, 111_111:core_7wd_enq

	val barreg_core0 = RegInit(false.B)
	val wlast_core0 = RegInit(false.B)

	val barreg_core1 = RegInit(false.B)
	val wlast_core1 = RegInit(false.B)

	val barreg_core2 = RegInit(false.B)
	val wlast_core2 = RegInit(false.B)

	val barreg_core3 = RegInit(false.B)
	val wlast_core3 = RegInit(false.B)

    switch(stateReg){
		is(0.U){//000_000: core_0_D$
            when(!io.AWVALID_0 && !io.ARVALID_0){
                stateReg := 8.U
            }.elsewhen(io.ARVALID_0){
				stateReg := 1.U
            }.elsewhen(io.AWVALID_0){
				stateReg := 4.U
            }
            io.enq_valid := false.B
		}
		is(1.U){ //000_001: core_0r
            when(io.enq_ready){
                stateReg := 2.U
            }.otherwise{
				stateReg := 1.U
            }
            io.enq_valid := false.B
		}
		is(2.U){ //000_010: core_0_D$_r_enq
            stateReg := 8.U
            io.ARREADY_0 := true.B && io.ARVALID_0
            io.enq_valid := true.B && io.ARVALID_0
            io.select := 0.U
		}
		is(4.U){ //000_100: core_0_D$_wa
            when(io.enq_ready){
                stateReg := 5.U
            }.otherwise{
				stateReg := 4.U
            }
            io.enq_valid := false.B
            barreg_core0 := io.AWBAR_0
		}
		is(5.U){ //000_101: core_0_D$_wa_enq
            when(barreg_core0){
                stateReg := 8.U
            }.otherwise{
                stateReg := 6.U
            }
            io.AWREADY_0 := true.B && io.AWVALID_0
            io.enq_valid := true.B && io.AWVALID_0
            io.select := 1.U
		}
		is(6.U){ //000_110:core_0_D$_wd
            when(io.enq_ready && io.WVALID_0){
                stateReg := 7.U
            }.otherwise{
				stateReg := 6.U
            }
            io.enq_valid := false.B
            wlast_core0 := io.WLAST_0
		}
		is(7.U){ //000_111:core_0_D$_wd_enq
            when(!wlast_core0){
                stateReg := 6.U
            }.otherwise{
				stateReg := 8.U
            }
			io.WREADY_0 := true.B
			io.enq_valid := true.B
			io.select := 2.U

		}
		is(8.U){ //001_000: core_0_I$
            when(!io.AWVALID_1 && !io.ARVALID_1){
                stateReg := 16.U
            }.elsewhen(io.ARVALID_1){
				stateReg := 9.U
            }.elsewhen(io.AWVALID_1){
				stateReg := 12.U
            }
            io.enq_valid := false.B
		}
		is(9.U){ //001_001: core_0_I$_r
            when(io.enq_ready){
                stateReg := 10.U
            }.otherwise{
				stateReg := 9.U
            }
            io.enq_valid := false.B
		}
		is(10.U){ //001_010: core_0_I$_r_enq
            stateReg := 16.U
            io.ARREADY_1 := true.B && io.ARVALID_1
            io.enq_valid := true.B && io.ARVALID_1
            io.select := 4.U
		}
		is(12.U){ //001_100: core_0_I$_wa
            when(io.enq_ready){
                stateReg := 13.U
            }.otherwise{
				stateReg := 12.U
            }
            io.enq_valid := false.B
		}
		is(13.U){ //001_101: core_0_I$_wa_enq
            stateReg := 14.U
            io.AWREADY_1 := true.B && io.AWVALID_1
            io.enq_valid := true.B && io.AWVALID_1
            io.select := 5.U
		}
		is(14.U){ //001_110:core_0_I$_wd
            when(io.enq_ready && io.WVALID_1){
                stateReg := 15.U
            }.otherwise{
				stateReg := 14.U
            }
            io.enq_valid := false.B
		}
		is(15.U){ //001_111:core_0_I$_wd_enq
            when(!io.WLAST_1){
                stateReg := 14.U
            }.otherwise{
				stateReg := 16.U
            }
            io.WREADY_1 := true.B && io.WVALID_1
			io.enq_valid := true.B && io.WVALID_1
			io.select := 6.U
		}
		is(16.U){ //010_000: core_1_D$
            when(!io.AWVALID_2 && !io.ARVALID_2){
                stateReg := 24.U
            }.elsewhen(io.ARVALID_2){
				stateReg := 17.U
            }.elsewhen(io.AWVALID_2){
				stateReg := 20.U
            }
            io.enq_valid := false.B
		}
		is(17.U){ //010_001: core_1_D$_r
            when(io.enq_ready){
                stateReg := 18.U
            }.otherwise{
				stateReg := 17.U
            }
            io.enq_valid := false.B
		}
		is(18.U){ //010_010: core_1_D$_r_enq
            stateReg := 24.U
            io.ARREADY_2 := true.B && io.ARVALID_2
            io.enq_valid := true.B && io.ARVALID_2
            io.select := 8.U
		}
		is(20.U){ //010_100: core_1_D$_wa
            when(io.enq_ready){
                stateReg := 21.U
            }.otherwise{
				stateReg := 20.U
            }
            io.enq_valid := false.B
            barreg_core1 := io.AWBAR_2
		}
		is(21.U){ //010_101: core_1_D$_wa_enq
            when(barreg_core1){
                stateReg := 24.U
            }.otherwise{
                stateReg := 22.U
            }
            io.AWREADY_2 := true.B && io.AWVALID_2
            io.enq_valid := true.B && io.AWVALID_2
            io.select := 9.U
		}
		is(22.U){ //010_110:core_1_D$_wd
            when(io.enq_ready && io.WVALID_2){
                stateReg := 23.U
            }.otherwise{
				stateReg := 22.U
            }
            io.enq_valid := false.B
            wlast_core1 := io.WLAST_2
		}
		is(23.U){ //010_111:core_1_D$_wd_enq
            when(!wlast_core1){
                stateReg := 22.U
            }.otherwise{
				stateReg := 24.U
            }
            io.WREADY_2 := true.B && io.WVALID_2
			io.enq_valid := true.B && io.WVALID_2
			io.select := 10.U
		}
		is(24.U){ //011_000: core_1_I$
            when(!io.AWVALID_3 && !io.ARVALID_3){
                stateReg := 32.U
            }.elsewhen(io.ARVALID_3){
				stateReg := 25.U
            }.elsewhen(io.AWVALID_3){
				stateReg := 28.U
            }
            io.enq_valid := false.B
		}
		is(25.U){ //011_001: core_1_I$_r
            when(io.enq_ready){
                stateReg := 26.U
            }.otherwise{
				stateReg := 25.U
            }
            io.enq_valid := false.B
		}
		is(26.U){ //011_010: core_1_I$_r_enq
            stateReg := 32.U
            io.ARREADY_3 := true.B && io.ARVALID_3
            io.enq_valid := true.B && io.ARVALID_3
            io.select := 12.U
		}
		is(28.U){ //011_100: core_1_I$_wa
            when(io.enq_ready){
                stateReg := 29.U
            }.otherwise{
				stateReg := 28.U
            }
            io.enq_valid := false.B
		}
		is(29.U){ //011_101: core_1_I$_wa_enq
            stateReg := 30.U
            io.AWREADY_3 := true.B && io.AWVALID_3
            io.enq_valid := true.B && io.AWVALID_3
            io.select := 13.U
		}
		is(30.U){ //011_110:core_1_I$_wd
            when(io.enq_ready && io.WVALID_3){
                stateReg := 31.U
            }.otherwise{
				stateReg := 30.U
            }
            io.enq_valid := false.B
		}
		is(31.U){ //011_111:core_1_I$_wd_enq
            when(!io.WLAST_3){
                stateReg := 30.U
            }.otherwise{
				stateReg := 32.U
            }
            io.WREADY_3 := true.B && io.WVALID_3
			io.enq_valid := true.B && io.WVALID_3
			io.select := 14.U
		}
		is(32.U){//100_000: core_2_D$
            when(!io.AWVALID_4 && !io.ARVALID_4){
                stateReg := 40.U
            }.elsewhen(io.ARVALID_4){
				stateReg := 33.U
            }.elsewhen(io.AWVALID_4){
				stateReg := 36.U
            }
            io.enq_valid := false.B
		}
		is(33.U){ //100_001: core_2r
            when(io.enq_ready){
                stateReg := 34.U
            }.otherwise{
				stateReg := 33.U
            }
            io.enq_valid := false.B
		}
		is(34.U){ //100_010: core_2_D$_r_enq
            stateReg := 40.U
            io.ARREADY_4 := true.B && io.ARVALID_4
            io.enq_valid := true.B && io.ARVALID_4
            io.select := 16.U
		}
		is(36.U){ //100_100: core_2_D$_wa
            when(io.enq_ready){
                stateReg := 37.U
            }.otherwise{
				stateReg := 36.U
            }
            io.enq_valid := false.B
            barreg_core2 := io.AWBAR_4
		}
		is(37.U){ //100_101: core_2_D$_wa_enq
            when(barreg_core2){
                stateReg := 40.U
            }.otherwise{
                stateReg := 38.U
            }
            io.AWREADY_4 := true.B && io.AWVALID_4
            io.enq_valid := true.B && io.AWVALID_4
            io.select := 17.U
		}
		is(38.U){ //100_110:core_2_D$_wd
            when(io.enq_ready && io.WVALID_4){
                stateReg := 39.U
            }.otherwise{
				stateReg := 38.U
            }
            io.enq_valid := false.B
            wlast_core2 := io.WLAST_4
		}
		is(39.U){ //100_111:core_2_D$_wd_enq
            when(!wlast_core2){
                stateReg := 38.U
            }.otherwise{
				stateReg := 40.U
            }
			io.WREADY_4 := true.B
			io.enq_valid := true.B
			io.select := 18.U

		}
		is(40.U){ //101_000: core_2_I$
            when(!io.AWVALID_5 && !io.ARVALID_5){
                stateReg := 48.U
            }.elsewhen(io.ARVALID_5){
				stateReg := 41.U
            }.elsewhen(io.AWVALID_5){
				stateReg := 44.U
            }
            io.enq_valid := false.B
		}
		is(41.U){ //101_001: core_2_I$_r
            when(io.enq_ready){
                stateReg := 42.U
            }.otherwise{
				stateReg := 41.U
            }
            io.enq_valid := false.B
		}
		is(42.U){ //101_010: core_2_I$_r_enq
            stateReg := 48.U
            io.ARREADY_5 := true.B && io.ARVALID_5
            io.enq_valid := true.B && io.ARVALID_5
            io.select := 20.U
		}
		is(44.U){ //101_100: core_2_I$_wa
            when(io.enq_ready){
                stateReg := 45.U
            }.otherwise{
				stateReg := 44.U
            }
            io.enq_valid := false.B
		}
		is(45.U){ //101_101: core_2_I$_wa_enq
            stateReg := 46.U
            io.AWREADY_5 := true.B && io.AWVALID_5
            io.enq_valid := true.B && io.AWVALID_5
            io.select := 21.U
		}
		is(46.U){ //101_110:core_2_I$_wd
            when(io.enq_ready && io.WVALID_5){
                stateReg := 47.U
            }.otherwise{
				stateReg := 46.U
            }
            io.enq_valid := false.B
		}
		is(47.U){ //101_111:core_2_I$_wd_enq
            when(!io.WLAST_5){
                stateReg := 46.U
            }.otherwise{
				stateReg := 48.U
            }
            io.WREADY_5 := true.B && io.WVALID_5
			io.enq_valid := true.B && io.WVALID_5
			io.select := 22.U
		}
		is(48.U){ //110_000: core_3_D$
            when(!io.AWVALID_6 && !io.ARVALID_6){
                stateReg := 56.U
            }.elsewhen(io.ARVALID_6){
				stateReg := 49.U
            }.elsewhen(io.AWVALID_6){
				stateReg := 52.U
            }
            io.enq_valid := false.B
		}
		is(49.U){ //110_001: core_3_D$_r
            when(io.enq_ready){
                stateReg := 50.U
            }.otherwise{
				stateReg := 49.U
            }
            io.enq_valid := false.B
		}
		is(50.U){ //110_010: core_3_D$_r_enq
            stateReg := 56.U
            io.ARREADY_6 := true.B && io.ARVALID_6
            io.enq_valid := true.B && io.ARVALID_6
            io.select := 24.U
		}
		is(52.U){ //110_100: core_3_D$_wa
            when(io.enq_ready){
                stateReg := 53.U
            }.otherwise{
				stateReg := 52.U
            }
            io.enq_valid := false.B
            barreg_core3 := io.AWBAR_6
		}
		is(53.U){ //110_101: core_3_D$_wa_enq
            when(barreg_core3){
                stateReg := 56.U
            }.otherwise{
                stateReg := 54.U
            }
            io.AWREADY_6 := true.B && io.AWVALID_6
            io.enq_valid := true.B && io.AWVALID_6
            io.select := 25.U
		}
		is(54.U){ //110_110:core_3_D$_wd
            when(io.enq_ready && io.WVALID_6){
                stateReg := 55.U
            }.otherwise{
				stateReg := 54.U
            }
            io.enq_valid := false.B
            wlast_core3 := io.WLAST_6
		}
		is(55.U){ //110_111:core_3_D$_wd_enq
            when(!wlast_core3){
                stateReg := 54.U
            }.otherwise{
				stateReg := 56.U
            }
            io.WREADY_6 := true.B && io.WVALID_6
			io.enq_valid := true.B && io.WVALID_6
			io.select := 26.U
		}
		is(56.U){ //111_000: core_3_I$
            when(!io.AWVALID_7 && !io.ARVALID_7){
                stateReg := 0.U
            }.elsewhen(io.ARVALID_7){
				stateReg := 57.U
            }.elsewhen(io.AWVALID_7){
				stateReg := 60.U
            }
            io.enq_valid := false.B
		}
		is(57.U){ //111_001: core_3_I$_r
            when(io.enq_ready){
                stateReg := 58.U
            }.otherwise{
				stateReg := 57.U
            }
            io.enq_valid := false.B
		}
		is(58.U){ //111_010: core_3_I$_r_enq
            stateReg := 0.U
            io.ARREADY_7 := true.B && io.ARVALID_7
            io.enq_valid := true.B && io.ARVALID_7
            io.select := 28.U
		}
		is(60.U){ //111_100: core_3_I$_wa
            when(io.enq_ready){
                stateReg := 61.U
            }.otherwise{
				stateReg := 60.U
            }
            io.enq_valid := false.B
		}
		is(61.U){ //111_101: core_3_I$_wa_enq
            stateReg := 62.U
            io.AWREADY_7 := true.B && io.AWVALID_7
            io.enq_valid := true.B && io.AWVALID_7
            io.select := 29.U
		}
		is(62.U){ //111_110:core_3_I$_wd
            when(io.enq_ready && io.WVALID_7){
                stateReg := 63.U
            }.otherwise{
				stateReg := 62.U
            }
            io.enq_valid := false.B
		}
		is(63.U){ //111_111:core_3_I$_wd_enq
            when(!io.WLAST_7){
                stateReg := 62.U
            }.otherwise{
				stateReg := 0.U
            }
            io.WREADY_7 := true.B && io.WVALID_7
			io.enq_valid := true.B && io.WVALID_7
			io.select := 30.U
		}

	}


}


//AXI ready valid rules
//A source is not permitted to wait until READY is asserted before asserting VALID
//Once VALID is asserted it must remain asserted until the handshake occurs, at a rising clock edge at which VALID and READY are both asserted.
//A destination is permitted to wait for VALID to be asserted before asserting the corresponding READY. If READY is asserted, it is permitted to deassert READY before VALID is asserted.

//Cache line width is 64bytes and data bus width width is 64 bits therefore ther can be maximum up to 8 beats
