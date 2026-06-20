
//CCU
//Intelectual Propert Made by Yasiru Eranda Amarasinghe
//What are IOs
/*
FIFO IOs
L2 AXI Port IOs
core 0 CA,CR,CD,R,B IOs
core 1 CA,CR,CD,R,B IOs
core 2 CA,CR,CD,R,B IOs
core 3 CA,CR,CD,R,B IOs
core 4 CA,CR,CD,R,B IOs
core 5 CA,CR,CD,R,B IOs
core 6 CA,CR,CD,R,B IOs
core 7 CA,CR,CD,R,B IOs

*/

package Interconnect

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import chisel3.experimental.IO


class ccu extends Module {
	//Debug signals
	/**
	val debug = IO(new Bundle {
		val stateReg_1 = Output(UInt(3.W))
        val stateReg_2 = Output(UInt(3.W))
        val stateReg_3 = Output(UInt(3.W))
        val stateReg_4 = Output(UInt(3.W))
        val stateReg_5 = Output(UInt(3.W))
        val stateReg_6 = Output(UInt(3.W))
        val stateReg_7 = Output(UInt(3.W))
        val stateReg_8 = Output(UInt(3.W))
        val stateReg_9 = Output(UInt(3.W))
        val stateReg_10 = Output(UInt(3.W))
        val stateReg_11 = Output(UInt(3.W))
        val stateReg_12 = Output(UInt(3.W))
	})
	*/

	// FIFO IOs
	val deq = IO(new Bundle {
		val ready = Output(Bool())
		val valid = Input(Bool())
		val data  = Input(UInt(71.W))
	})

	// L2 AXI port
	val L2 = IO(new Bundle{
		//AW
		val AWVALID = Output(Bool())
		val AWREADY = Input(Bool())
		//metadata
		val AWID = Output(UInt(3.W))
		val AWADDR = Output(UInt(64.W))
		//val AWLEN = Output(UInt(8.W))
		//val AWSIZE = Output(UInt(3.W))
		//val AWBURST = Output(UInt(2.W))
		//val AWLOCK = Output(UInt(1.W))
		//val AWCACHE = Output(UInt(4.W))
		//val AWPROT = Output(UInt(3.W))
		//val AWQOS = Output(UInt(4.W))
		//val AWREGION = Output(UInt())
		//val AWUSER = Output(UInt())

		//AR
		val ARVALID = Output(Bool())
		val ARREADY = Input(Bool())
		//metadata
		val ARID = Output(UInt(3.W))
		val ARADDR = Output(UInt(64.W))
		//val ARLEN = Output(UInt(8.W))
		//val ARSIZE = Output(UInt(3.W))
		//val ARBURST = Output(UInt(2.W))
		//val ARLOCK = Output(UInt(1.W))
		//val ARCACHE = Output(UInt(4.W))
		//val ARPROT = Output(UInt(3.W))
		//val ARQOS = Output(UInt(4.W))
		//val ARREGION = Output(UInt())
		//val ARUSER = Output(UInt())

		//W
		val WVALID = Output(Bool())
		val WREADY = Input(Bool())
		//metadata
		val WDATA = Output(UInt(64.W))
		//val WSTRB = Output(UInt((dataWidth/8).W))
		val WLAST = Output(Bool())
		//val WUSER = Output(UInt())

		//R
		val RVALID = Input(Bool())
		val RREADY = Output(Bool())
		//metadata
		val RID = Input(UInt(3.W))
		val RDATA = Input(UInt(64.W))
		val RRESP = Input(UInt(2.W))          //0:1 is AXI
		val RLAST = Input(Bool())
		//val RUSER = Input(UInt())

		//B
		val BVALID = Input(Bool())
		val BREADY = Output(Bool())
		//metadata
		val BID = Input(UInt(3.W))
		val BRESP = Input(UInt(2.W))
		//val BUSER = Input(UInt())
	})

	//core0
	val core0 = IO(new Bundle {
		//AC
		val ACVALID = Output(Bool())
		val ACREADY = Input(Bool())
		//metadata
		val ACADDR = Output(UInt(64.W))
		val ACSNOOP = Output(UInt(4.W))
		//val ACPROT = Output(UInt(3.W))

		//CR
		val CRVALID = Input(Bool())
		val CRREADY = Output(Bool())
		//metadata
		val CRRESP = Input(UInt(5.W))

		//CD
		val CDVALID = Input(Bool())
		val CDREADY = Output(Bool())
		//metadata
		val CDDATA = Input(UInt(64.W))
		val CDLAST = Input(Bool())

		//R
		val RVALID = Output(Bool())
		val RREADY = Input(Bool())
		//metadata
		val RID = Output(UInt(3.W))
		val RDATA = Output(UInt(64.W))
		val RRESP = Output(UInt(4.W))          //0:1 is AXI, 2:3 is ACE
		val RLAST = Output(Bool())
		//val RUSER = Output(UInt())

		//B
		val BVALID = Output(Bool())
		val BREADY = Input(Bool())
		//metadata
		val BID = Output(UInt(3.W))
		val BRESP = Output(UInt(2.W))
		//val BUSER = Output(UInt())
	})

	//core1
	val core1 = IO(new Bundle {
		//AC
		val ACVALID = Output(Bool())
		val ACREADY = Input(Bool())
		//metadata
		val ACADDR = Output(UInt(64.W))
		val ACSNOOP = Output(UInt(4.W))
		//val ACPROT = Output(UInt(3.W))

		//CR
		val CRVALID = Input(Bool())
		val CRREADY = Output(Bool())
		//metadata
		val CRRESP = Input(UInt(5.W))

		//CD
		val CDVALID = Input(Bool())
		val CDREADY = Output(Bool())
		//metadata
		val CDDATA = Input(UInt(64.W))
		val CDLAST = Input(Bool())

		//R
		val RVALID = Output(Bool())
		val RREADY = Input(Bool())
		//metadata
		val RID = Output(UInt(3.W))
		val RDATA = Output(UInt(64.W))
		val RRESP = Output(UInt(4.W))          //0:1 is AXI, 2:3 is ACE
		val RLAST = Output(Bool())
		//val RUSER = Output(UInt())

		//B
		val BVALID = Output(Bool())
		val BREADY = Input(Bool())
		//metadata
		val BID = Output(UInt(3.W))
		val BRESP = Output(UInt(2.W))
		//val BUSER = Input(UInt())
	})

	//core2
	val core2 = IO(new Bundle {
		//AC
		val ACVALID = Output(Bool())
		val ACREADY = Input(Bool())
		//metadata
		val ACADDR = Output(UInt(64.W))
		val ACSNOOP = Output(UInt(4.W))
		//val ACPROT = Output(UInt(3.W))

		//CR
		val CRVALID = Input(Bool())
		val CRREADY = Output(Bool())
		//metadata
		val CRRESP = Input(UInt(5.W))

		//CD
		val CDVALID = Input(Bool())
		val CDREADY = Output(Bool())
		//metadata
		val CDDATA = Input(UInt(64.W))
		val CDLAST = Input(Bool())

		//R
		val RVALID = Output(Bool())
		val RREADY = Input(Bool())
		//metadata
		val RID = Output(UInt(3.W))
		val RDATA = Output(UInt(64.W))
		val RRESP = Output(UInt(4.W))          //0:1 is AXI, 2:3 is ACE
		val RLAST = Output(Bool())
		//val RUSER = Output(UInt())

		//B
		val BVALID = Output(Bool())
		val BREADY = Input(Bool())
		//metadata
		val BID = Output(UInt(3.W))
		val BRESP = Output(UInt(2.W))
		//val BUSER = Input(UInt())
	})

	//core3
	val core3 = IO(new Bundle {
		//AC
		val ACVALID = Output(Bool())
		val ACREADY = Input(Bool())
		//metadata
		val ACADDR = Output(UInt(64.W))
		val ACSNOOP = Output(UInt(4.W))
		//val ACPROT = Output(UInt(3.W))

		//CR
		val CRVALID = Input(Bool())
		val CRREADY = Output(Bool())
		//metadata
		val CRRESP = Input(UInt(5.W))

		//CD
		val CDVALID = Input(Bool())
		val CDREADY = Output(Bool())
		//metadata
		val CDDATA = Input(UInt(64.W))
		val CDLAST = Input(Bool())

		//R
		val RVALID = Output(Bool())
		val RREADY = Input(Bool())
		//metadata
		val RID = Output(UInt(3.W))
		val RDATA = Output(UInt(64.W))
		val RRESP = Output(UInt(4.W))          //0:1 is AXI, 2:3 is ACE
		val RLAST = Output(Bool())
		//val RUSER = Output(UInt())

		//B
		val BVALID = Output(Bool())
		val BREADY = Input(Bool())
		//metadata
		val BID = Output(UInt(3.W))
		val BRESP = Output(UInt(2.W))
		//val BUSER = Input(UInt())
	})

	//core4
	val core4 = IO(new Bundle {
		//AC
		val ACVALID = Output(Bool())
		val ACREADY = Input(Bool())
		//metadata
		val ACADDR = Output(UInt(64.W))
		val ACSNOOP = Output(UInt(4.W))
		//val ACPROT = Output(UInt(3.W))

		//CR
		val CRVALID = Input(Bool())
		val CRREADY = Output(Bool())
		//metadata
		val CRRESP = Input(UInt(5.W))

		//CD
		val CDVALID = Input(Bool())
		val CDREADY = Output(Bool())
		//metadata
		val CDDATA = Input(UInt(64.W))
		val CDLAST = Input(Bool())

		//R
		val RVALID = Output(Bool())
		val RREADY = Input(Bool())
		//metadata
		val RID = Output(UInt(3.W))
		val RDATA = Output(UInt(64.W))
		val RRESP = Output(UInt(4.W))          //0:1 is AXI, 2:3 is ACE
		val RLAST = Output(Bool())
		//val RUSER = Output(UInt())

		//B
		val BVALID = Output(Bool())
		val BREADY = Input(Bool())
		//metadata
		val BID = Output(UInt(3.W))
		val BRESP = Output(UInt(2.W))
		//val BUSER = Input(UInt())
	})

	//core5
	val core5 = IO(new Bundle {
		//AC
		val ACVALID = Output(Bool())
		val ACREADY = Input(Bool())
		//metadata
		val ACADDR = Output(UInt(64.W))
		val ACSNOOP = Output(UInt(4.W))
		//val ACPROT = Output(UInt(3.W))

		//CR
		val CRVALID = Input(Bool())
		val CRREADY = Output(Bool())
		//metadata
		val CRRESP = Input(UInt(5.W))

		//CD
		val CDVALID = Input(Bool())
		val CDREADY = Output(Bool())
		//metadata
		val CDDATA = Input(UInt(64.W))
		val CDLAST = Input(Bool())

		//R
		val RVALID = Output(Bool())
		val RREADY = Input(Bool())
		//metadata
		val RID = Output(UInt(3.W))
		val RDATA = Output(UInt(64.W))
		val RRESP = Output(UInt(4.W))          //0:1 is AXI, 2:3 is ACE
		val RLAST = Output(Bool())
		//val RUSER = Output(UInt())

		//B
		val BVALID = Output(Bool())
		val BREADY = Input(Bool())
		//metadata
		val BID = Output(UInt(3.W))
		val BRESP = Output(UInt(2.W))
		//val BUSER = Input(UInt())
	})

	//core6
	val core6 = IO(new Bundle {
		//AC
		val ACVALID = Output(Bool())
		val ACREADY = Input(Bool())
		//metadata
		val ACADDR = Output(UInt(64.W))
		val ACSNOOP = Output(UInt(4.W))
		//val ACPROT = Output(UInt(3.W))

		//CR
		val CRVALID = Input(Bool())
		val CRREADY = Output(Bool())
		//metadata
		val CRRESP = Input(UInt(5.W))

		//CD
		val CDVALID = Input(Bool())
		val CDREADY = Output(Bool())
		//metadata
		val CDDATA = Input(UInt(64.W))
		val CDLAST = Input(Bool())

		//R
		val RVALID = Output(Bool())
		val RREADY = Input(Bool())
		//metadata
		val RID = Output(UInt(3.W))
		val RDATA = Output(UInt(64.W))
		val RRESP = Output(UInt(4.W))          //0:1 is AXI, 2:3 is ACE
		val RLAST = Output(Bool())
		//val RUSER = Output(UInt())

		//B
		val BVALID = Output(Bool())
		val BREADY = Input(Bool())
		//metadata
		val BID = Output(UInt(3.W))
		val BRESP = Output(UInt(2.W))
		//val BUSER = Input(UInt())
	})

	//core7
	val core7 = IO(new Bundle {
		//AC
		val ACVALID = Output(Bool())
		val ACREADY = Input(Bool())
		//metadata
		val ACADDR = Output(UInt(64.W))
		val ACSNOOP = Output(UInt(4.W))
		//val ACPROT = Output(UInt(3.W))

		//CR
		val CRVALID = Input(Bool())
		val CRREADY = Output(Bool())
		//metadata
		val CRRESP = Input(UInt(5.W))

		//CD
		val CDVALID = Input(Bool())
		val CDREADY = Output(Bool())
		//metadata
		val CDDATA = Input(UInt(64.W))
		val CDLAST = Input(Bool())

		//R
		val RVALID = Output(Bool())
		val RREADY = Input(Bool())
		//metadata
		val RID = Output(UInt(3.W))
		val RDATA = Output(UInt(64.W))
		val RRESP = Output(UInt(4.W))          //0:1 is AXI, 2:3 is ACE
		val RLAST = Output(Bool())
		//val RUSER = Output(UInt())

		//B
		val BVALID = Output(Bool())
		val BREADY = Input(Bool())
		//metadata
		val BID = Output(UInt(3.W))
		val BRESP = Output(UInt(2.W))
		//val BUSER = Input(UInt())
	})

	//Default values for L2 AW and W channels
	L2.AWVALID := false.B
	L2.AWID := deq.data(70,68)
	L2.AWADDR := deq.data(67,4)
	L2.WVALID := false.B
	L2.WDATA := deq.data(67,4)
	L2.WLAST := deq.data(0)

	//Default values for L2 AR channel
	L2.ARVALID := false.B
	L2.ARADDR := deq.data(67,4)
	L2.ARID := deq.data(70,68)

	//Default values for L2 R channel
	L2.RREADY := false.B

	//Default values for L2 B channel
	L2.BREADY := false.B



	//Default values
	core0.BVALID := false.B
	core0.BRESP := L2.BRESP
	core0.BID := 0.U
	core0.RVALID := false.B
	core0.CDREADY := false.B

	core1.BVALID := false.B
	core1.BRESP := L2.BRESP
	core1.BID := 1.U
	core1.RVALID := false.B
	core1.CDREADY := false.B

	core2.BVALID := false.B
	core2.BRESP := L2.BRESP
	core2.BID := 2.U
	core2.RVALID := false.B
	core2.CDREADY := false.B

	core3.BVALID := false.B
	core3.BRESP := L2.BRESP
	core3.BID := 3.U
	core3.RVALID := false.B
	core3.CDREADY := false.B

	core4.BVALID := false.B
	core4.BRESP := L2.BRESP
	core4.BID := 4.U
	core4.RVALID := false.B
	core4.CDREADY := false.B

	core5.BVALID := false.B
	core5.BRESP := L2.BRESP
	core5.BID := 5.U
	core5.RVALID := false.B
	core5.CDREADY := false.B

	core6.BVALID := false.B
	core6.BRESP := L2.BRESP
	core6.BID := 6.U
	core6.RVALID := false.B
	core6.CDREADY := false.B

	core7.BVALID := false.B
	core7.BRESP := L2.BRESP
	core7.BID := 7.U
	core7.RVALID := false.B
	core7.CDREADY := false.B

	deq.ready := false.B

  //*************************************************************************************************************************************************************************************************************************
  //Handeling writebacks and writeevicts
  //FSM_1


	val deq_1 = Wire(Bool())
	val deq_2 = Wire(Bool())
	deq_1 := false.B
	deq_2 := false.B
	val stateReg_1 = RegInit(0.U(3.W))
	val stateReg_2 = RegInit(0.U(4.W))

	val write_back = RegInit(false.B)

	//FSM_1
	switch(stateReg_1){
		is(0.U){//IDLE
			when(deq.valid && (deq.data(3,0) === "b0011".U(4.W))){ //writeback
				stateReg_1 := 1.U
				write_back := true.B
			}.elsewhen(deq.valid && (deq.data(3,0) === "b1000".U(4.W))){ //write barrier
				stateReg_1 := 7.U
			}.otherwise{
				stateReg_1 := 0.U
			}
		}
		is(1.U){//AW
			when(L2.AWREADY){
				stateReg_1 := 2.U
			}.otherwise{
				stateReg_1 := 1.U
			}
			L2.AWVALID := true.B
		}
		is(2.U){//DEQ_1
			stateReg_1 := 3.U
			deq_1 := true.B
		}
		is(3.U){//WAIT, this state is a fail safe state if deq valid is not asserted in case everything gonna wrong
			when(deq.valid){
				stateReg_1 := 4.U
			}.otherwise{
				stateReg_1 := 3.U
			}
		}
		is(4.U){//WD
			when(L2.WREADY){
				stateReg_1 := 5.U
			}.otherwise{
				stateReg_1 := 4.U
			}
			L2.WVALID := true.B
		}
		is(5.U){//DEQ_2
			when(deq.data(0)){
				stateReg_1 := 6.U
			}.otherwise{
				stateReg_1 := 3.U
			}
			deq_1 := true.B
		}
		is(6.U){//SYNC
			when(stateReg_2 === "b000".U(3.W)){
				stateReg_1 := 0.U
				write_back := false.B
			}.otherwise{
				stateReg_1 := 6.U
			}
		}
		is(7.U){//AWBAR
			when(stateReg_2 === "b000".U(3.W)){
				stateReg_1 := 0.U
				write_back := false.B
			}.otherwise{
				stateReg_1 := 7.U
			}
		}
	}

	//FSM_2
    switch(stateReg_2){
		is(0.U){//IDLE
			when(stateReg_1 === "b110".U(3.W)){
				stateReg_2 := 1.U
			}.elsewhen(stateReg_1 === "b111".U(3.W)){
				stateReg_2 := 6.U
			}.otherwise{
				stateReg_2 := 0.U
			}
		}
		is(1.U){//RSP
			when(!L2.BVALID){
				stateReg_2 := 1.U
			}.elsewhen(L2.BID === "b000".U(3.W)){
				stateReg_2 := 2.U
			}.elsewhen(L2.BID === "b001".U(3.W)){
				stateReg_2 := 3.U
			}.elsewhen(L2.BID === "b010".U(3.W)){
				stateReg_2 := 4.U
			}.elsewhen(L2.BID === "b011".U(3.W)){
				stateReg_2 := 5.U
			}.elsewhen(L2.BID === "b100".U(3.W)){
				stateReg_2 := 6.U
			}.elsewhen(L2.BID === "b101".U(3.W)){
				stateReg_2 := 7.U
			}.elsewhen(L2.BID === "b110".U(3.W)){
				stateReg_2 := 8.U
			}.otherwise{
				stateReg_2 := 9.U
			}
			L2.BREADY := true.B
		}
		is(2.U){//B0
			when(core0.BREADY){
				stateReg_2 := 0.U
			}.otherwise{
				stateReg_2 := 2.U
			}
			core0.BVALID := true.B
		}
		is(3.U){//B1
			when(core1.BREADY){
				stateReg_2 := 0.U
			}.otherwise{
				stateReg_2 := 3.U
			}
			core1.BVALID := true.B
		}
		is(4.U){//B2
			when(core2.BREADY){
				stateReg_2 := 0.U
			}.otherwise{
				stateReg_2 := 4.U
			}
			core2.BVALID := true.B
		}
		is(5.U){//B3
			when(core3.BREADY){
				stateReg_2 := 0.U
			}.otherwise{
				stateReg_2 := 5.U
			}
			core3.BVALID := true.B
		}
		is(6.U){//B4
			when(core4.BREADY){
				stateReg_2 := 0.U
			}.otherwise{
				stateReg_2 := 6.U
			}
			core4.BVALID := true.B
		}
		is(7.U){//B5
			when(core5.BREADY){
				stateReg_2 := 0.U
			}.otherwise{
				stateReg_2 := 7.U
			}
			core5.BVALID := true.B
		}
		is(8.U){//B6
			when(core6.BREADY){
				stateReg_2 := 0.U
			}.otherwise{
				stateReg_2 := 8.U
			}
			core6.BVALID := true.B
		}
		is(9.U){//B7
			when(core7.BREADY){
				stateReg_2 := 0.U
			}.otherwise{
				stateReg_2 := 9.U
			}
			core7.BVALID := true.B
		}
		is(10.U){//AWBAR
			when(deq.data(70,68) === "b000".U(3.W) && core0.BREADY){
				stateReg_2 := 7.U
			}.elsewhen(deq.data(70,68) === "b001".U(3.W) && core1.BREADY){
				stateReg_2 := 7.U
			}.elsewhen(deq.data(70,68) === "b010".U(3.W) && core2.BREADY){
				stateReg_2 := 7.U
			}.elsewhen(deq.data(70,68) === "b011".U(3.W) && core3.BREADY){
				stateReg_2 := 7.U
			}.elsewhen(deq.data(70,68) === "b100".U(3.W) && core4.BREADY){
				stateReg_2 := 7.U
			}.elsewhen(deq.data(70,68) === "b101".U(3.W) && core5.BREADY){
				stateReg_2 := 7.U
			}.elsewhen(deq.data(70,68) === "b110".U(3.W) && core6.BREADY){
				stateReg_2 := 7.U
			}.elsewhen(deq.data(70,68) === "b111".U(3.W) && core7.BREADY){
				stateReg_2 := 7.U
			}.otherwise{
				stateReg_2 := 6.U
			}

			when(deq.data(70,68) === "b000".U(3.W)){
				core0.BVALID := true.B
			}.elsewhen(deq.data(70,68) === "b001".U(3.W)){
				core1.BVALID := true.B
			}.elsewhen(deq.data(70,68) === "b010".U(3.W)){
				core2.BVALID := true.B
			}.elsewhen(deq.data(70,68) === "b011".U(3.W)){
				core3.BVALID := true.B
			}.elsewhen(deq.data(70,68) === "b100".U(3.W)){
				core4.BVALID := true.B
			}.elsewhen(deq.data(70,68) === "b101".U(3.W)){
				core5.BVALID := true.B
			}.elsewhen(deq.data(70,68) === "b110".U(3.W)){
				core6.BVALID := true.B
			}.otherwise{
				core7.BVALID := true.B
			}
		}
		is(11.U){//DEQ
			deq_2 := true.B
			stateReg_2 := 0.U
		}
	}


    //************************************************************************************************************************************************************************************************************************
	//Handeling CleanUnique,Readshared,ReadUnique,ReadNoSnoop,Read_Barrier
	val deq_3 = Wire(Bool())
	deq_3 := false.B

    //FSM_3 pipeline bufferes(pbuf_1)
    val core_id_pbuf_1 = RegInit(0.U(3.W))
    val tran_pbuf_1 = RegInit(0.U(4.W))
    val addr_pbuf_1 = RegInit(0.U(64.W))

    //FSM_4,5,6,7 pipeline bufferes(pbuf_2)
    val core_id_pbuf_2 = RegInit(0.U(3.W))
    val tran_pbuf_2 = RegInit(0.U(4.W))
    val addr_pbuf_2 = RegInit(0.U(64.W))
    val crpbuf_2_0 = Reg(UInt(5.W))
    val crpbuf_2_1 = Reg(UInt(5.W))
    val crpbuf_2_2 = Reg(UInt(5.W))
    val crpbuf_2_3 = Reg(UInt(5.W))
    val crpbuf_2_4 = Reg(UInt(5.W))
    val crpbuf_2_5 = Reg(UInt(5.W))
    val crpbuf_2_6 = Reg(UInt(5.W))
    val crpbuf_2_7 = Reg(UInt(5.W))


    //FSM_8 pipeline bufferes(pbuf_3)
    val core_id_pbuf_3 = RegInit(0.U(3.W))
    val tran_pbuf_3 = RegInit(0.U(4.W))
    val addr_pbuf_3 = RegInit(0.U(64.W))
    val crpbuf_3_0 = Reg(UInt(5.W))
    val crpbuf_3_1 = Reg(UInt(5.W))
    val crpbuf_3_2 = Reg(UInt(5.W))
    val crpbuf_3_3 = Reg(UInt(5.W))
    val crpbuf_3_4 = Reg(UInt(5.W))
    val crpbuf_3_5 = Reg(UInt(5.W))
    val crpbuf_3_6 = Reg(UInt(5.W))
    val crpbuf_3_7 = Reg(UInt(5.W))



	val stateReg_3 = RegInit(0.U(3.W))
    val stateReg_4 = RegInit(0.U(4.W))
    val stateReg_5 = RegInit(0.U(4.W))
    val stateReg_6 = RegInit(0.U(4.W))
    val stateReg_7 = RegInit(0.U(4.W))
    val stateReg_8 = RegInit(0.U(4.W))
    val stateReg_9 = RegInit(0.U(4.W))
    val stateReg_10 = RegInit(0.U(4.W))
    val stateReg_11 = RegInit(0.U(4.W))
    val stateReg_12 = RegInit(0.U(3.W))


	//FSM_3, 0000 : readNoSnoop, 0100 : read memory barrier, 0001: readshared, 0111: read unique, 1011 : clean unique
    switch(stateReg_3){
		is(0.U){//IDLE
			when(write_back){
				stateReg_3 := 0.U
			}.elsewhen(deq.valid && ((deq.data(3,0) === "b0001".U(4.W)) || (deq.data(3,0) === "b0111".U(4.W)) || (deq.data(3,0) === "b0000".U(4.W)))){
				stateReg_3 := 1.U
			}.elsewhen(deq.valid && ((deq.data(3,0) === "b0100".U(4.W)) || (deq.data(3,0) === "b1011".U(4.W)))){
				stateReg_3 := 2.U
			}.otherwise{
				stateReg_3 := 0.U
			}
		}
		is(1.U){//AR
			when(!L2.ARREADY){
				stateReg_3 := 1.U
			}.otherwise{
				stateReg_3 := 2.U
			}
			L2.ARVALID := true.B
		}
		is(2.U){//BUFF_1
			core_id_pbuf_1 := deq.data(70,68)
			tran_pbuf_1 := deq.data(3,0)
			addr_pbuf_1 := deq.data(67,4)
			stateReg_3 := 3.U
		}
		is(3.U){//DEQ
			deq_3 := true.B
			stateReg_3 := 4.U
		}
		is(4.U){//SYNC
			when(stateReg_4 === "b0000".U(4.W) && stateReg_5 === "b0000".U(4.W) && stateReg_6 === "b0000".U(4.W) && stateReg_7 === "b0000".U(4.W) && stateReg_8 === "b0000".U(4.W) && stateReg_9 === "b0000".U(4.W) && stateReg_10 === "b0000".U(4.W) && stateReg_11 === "b0000".U(4.W)){
				stateReg_3 := 5.U
			}.otherwise{
				stateReg_3 := 4.U
			}
		}
		is(5.U){//BUFF_2
			core_id_pbuf_2 := core_id_pbuf_1
			tran_pbuf_2 := tran_pbuf_1
			addr_pbuf_2 := addr_pbuf_1
			stateReg_3 := 6.U
		}
		is(6.U){//SNOOP
			stateReg_3 := 0.U
		}
	}

	deq.ready := deq_1 || deq_2 || deq_3

	//FSM_4
	core0.ACVALID := false.B
	core0.ACADDR := addr_pbuf_2
	core0.ACSNOOP := 0.U

	core0.CRREADY := false.B

	switch(stateReg_4){
		is(0.U){//IDLE
			when((stateReg_3 === "b110".U(3.W)) && ((tran_pbuf_2 === "b0100".U(4.W)) || (tran_pbuf_2 === "b0000".U(4.W)))){
				stateReg_4 := 4.U
			}.elsewhen((stateReg_3 === "b110".U(3.W)) && (addr_pbuf_3 === addr_pbuf_2)){
				stateReg_4 := 8.U
			}.elsewhen((stateReg_3 === "b110".U(3.W)) && (core_id_pbuf_2 === "b000".U(3.W))){
				stateReg_4 := 4.U
			}.elsewhen((stateReg_3 === "b110".U(3.W)) && !(core_id_pbuf_2 === "b000".U(3.W))){
				stateReg_4 := 1.U
			}.otherwise{
				stateReg_4 := 0.U
			}
			crpbuf_2_0 := 0.U
		}
		is(1.U){//CA
			when(!core0.ACREADY){
				stateReg_4 := 1.U
			}.otherwise{
				stateReg_4 := 2.U
			}

			core0.ACVALID := true.B
			when(tran_pbuf_2 === "b0001".U(4.W)){
				core0.ACSNOOP := 1.U
			}.elsewhen(tran_pbuf_2 === "b0111".U(4.W)){
				core0.ACSNOOP := 7.U
			}.elsewhen(tran_pbuf_2 === "b1011".U(4.W)){
				core0.ACSNOOP := 9.U
			}.otherwise{
				core0.ACSNOOP := 0.U
			}

		}
		is(2.U){//CR_BUFF
			when(!core0.CRVALID){
				stateReg_4 := 2.U
			}.otherwise{
				stateReg_4 := 3.U
			}
			crpbuf_2_0 := core0.CRRESP
		}
		is(3.U){//CR
			core0.CRREADY := true.B
			stateReg_4 := 4.U
		}
		is(4.U){//FINISH after this state all 4 controllers synchronized
			when((stateReg_4 === "b0100".U(4.W)) && (stateReg_5 === "b0100".U(4.W)) && (stateReg_6 === "b0100".U(4.W)) && (stateReg_7 === "b0100".U(4.W)) && (stateReg_8 === "b0100".U(4.W)) && (stateReg_9 === "b0100".U(4.W)) && (stateReg_10 === "b0100".U(4.W)) && (stateReg_11 === "b0100".U(4.W)) ){
				stateReg_4 := 5.U
			}.otherwise{
				stateReg_4 := 4.U
			}
		}
		is(5.U){//SYNC
			when(stateReg_12 === "b000".U(3.W)){
				stateReg_4 := 6.U
			}.otherwise{
				stateReg_4 := 5.U
			}
		}
		is(6.U){//BUF
			core_id_pbuf_3 := core_id_pbuf_2	//This is done only in FSM_4
			tran_pbuf_3 := tran_pbuf_2			//This is done only in FSM_4
			addr_pbuf_3 := addr_pbuf_2			//This is done only in FSM_4
			crpbuf_3_0 := crpbuf_2_0
			stateReg_4 := 7.U
		}
		is(7.U){//RSP
			stateReg_4 := 0.U
		}
		is(8.U){
			when(stateReg_12 === "b000".U(3.W)){
				when(core_id_pbuf_2 === "b000".U(3.W)){
					stateReg_4 := 4.U
				}.otherwise{
					stateReg_4 := 1.U
				}
			}.otherwise{
				stateReg_4 := 8.U
			}
		}
	}


	//FSM_5
	core1.ACVALID := false.B
	core1.ACADDR := addr_pbuf_2
	core1.ACSNOOP := 0.U

	core1.CRREADY := false.B

	switch(stateReg_5){
		is(0.U){//IDLE
			when((stateReg_3 === "b110".U(3.W)) && ((tran_pbuf_2 === "b0100".U(4.W)) || (tran_pbuf_2 === "b0000".U(4.W)))){
				stateReg_5 := 4.U
			}.elsewhen((stateReg_3 === "b110".U(3.W)) && (addr_pbuf_3 === addr_pbuf_2)){
				stateReg_5 := 8.U
			}.elsewhen((stateReg_3 === "b110".U(3.W)) && (core_id_pbuf_2 === "b001".U(3.W))){
				stateReg_5 := 4.U
			}.elsewhen((stateReg_3 === "b110".U(3.W)) && !(core_id_pbuf_2 === "b001".U(3.W))){
				stateReg_5 := 1.U
			}.otherwise{
				stateReg_5 := 0.U
			}

			crpbuf_2_1 := 0.U
		}
		is(1.U){//CA
			when(!core1.ACREADY){
				stateReg_5 := 1.U
			}.otherwise{
				stateReg_5 := 2.U
			}

			core1.ACVALID := true.B
			when(tran_pbuf_2 === "b0001".U(4.W)){
				core1.ACSNOOP := 1.U
			}.elsewhen(tran_pbuf_2 === "b0111".U(4.W)){
				core1.ACSNOOP := 7.U
			}.elsewhen(tran_pbuf_2 === "b1011".U(4.W)){
				core1.ACSNOOP := 9.U
			}.otherwise{
				core1.ACSNOOP := 0.U
			}

		}
		is(2.U){//CR_BUFF
			when(!core1.CRVALID){
				stateReg_5 := 2.U
			}.otherwise{
				stateReg_5 := 3.U
			}
			crpbuf_2_1 := core1.CRRESP
		}
		is(3.U){//CR
			core1.CRREADY := true.B
			stateReg_5 := 4.U
		}
		is(4.U){//FINISH after this state all 4 controllers synchronized
			when((stateReg_4 === "b0100".U(4.W)) && (stateReg_5 === "b0100".U(4.W)) && (stateReg_6 === "b0100".U(4.W)) && (stateReg_7 === "b0100".U(4.W)) && (stateReg_8 === "b0100".U(4.W)) && (stateReg_9 === "b0100".U(4.W)) && (stateReg_10 === "b0100".U(4.W)) && (stateReg_11 === "b0100".U(4.W)) ){
				stateReg_5 := 5.U
			}.otherwise{
				stateReg_5 := 4.U
			}
		}
		is(5.U){//SYNC
			when(stateReg_12 === "b000".U(3.W)){
				stateReg_5 := 6.U
			}.otherwise{
				stateReg_5 := 5.U
			}
		}
		is(6.U){//BUF
			crpbuf_3_1 := crpbuf_2_1
			stateReg_5 := 7.U
		}
		is(7.U){//RSP
			stateReg_5 := 0.U
		}
		is(8.U){
			when(stateReg_12 === "b000".U(3.W)){
				when(core_id_pbuf_2 === "b001".U(3.W)){
					stateReg_5 := 4.U
				}.otherwise{
					stateReg_5 := 1.U
				}
			}.otherwise{
				stateReg_5 := 8.U
			}
		}
	}

	//FSM_6
	core2.ACVALID := false.B
	core2.ACADDR := addr_pbuf_2
	core2.ACSNOOP := 0.U

	core2.CRREADY := false.B

	switch(stateReg_6){
		is(0.U){//IDLE
			when((stateReg_3 === "b110".U(3.W)) && ((tran_pbuf_2 === "b0100".U(4.W)) || (tran_pbuf_2 === "b0000".U(4.W)))){
				stateReg_6 := 4.U
			}.elsewhen((stateReg_3 === "b110".U(3.W)) && (addr_pbuf_3 === addr_pbuf_2)){
				stateReg_6 := 8.U
			}.elsewhen((stateReg_3 === "b110".U(3.W)) && (core_id_pbuf_2 === "b010".U(3.W))){
				stateReg_6 := 4.U
			}.elsewhen((stateReg_3 === "b110".U(3.W)) && !(core_id_pbuf_2 === "b010".U(3.W))){
				stateReg_6 := 1.U
			}.otherwise{
				stateReg_6 := 0.U
			}
			crpbuf_2_2 := 0.U
		}
		is(1.U){//CA
			when(!core2.ACREADY){
				stateReg_6 := 1.U
			}.otherwise{
				stateReg_6 := 2.U
			}

			core2.ACVALID := true.B
			when(tran_pbuf_2 === "b0001".U(4.W)){
				core2.ACSNOOP := 1.U
			}.elsewhen(tran_pbuf_2 === "b0111".U(4.W)){
				core2.ACSNOOP := 7.U
			}.elsewhen(tran_pbuf_2 === "b1011".U(4.W)){
				core2.ACSNOOP := 9.U
			}.otherwise{
				core2.ACSNOOP := 0.U
			}

		}
		is(2.U){//CR_BUFF
			when(!core2.CRVALID){
				stateReg_6 := 2.U
			}.otherwise{
				stateReg_6 := 3.U
			}
			crpbuf_2_2 := core2.CRRESP
		}
		is(3.U){//CR
			core2.CRREADY := true.B
			stateReg_6 := 4.U
		}
		is(4.U){//FINISH after this state all 4 controllers synchronized
			when((stateReg_4 === "b0100".U(4.W)) && (stateReg_5 === "b0100".U(4.W)) && (stateReg_6 === "b0100".U(4.W)) && (stateReg_7 === "b0100".U(4.W)) && (stateReg_8 === "b0100".U(4.W)) && (stateReg_9 === "b0100".U(4.W)) && (stateReg_10 === "b0100".U(4.W)) && (stateReg_11 === "b0100".U(4.W))){
				stateReg_6 := 5.U
			}.otherwise{
				stateReg_6 := 4.U
			}
		}
		is(5.U){//SYNC
			when(stateReg_12 === "b000".U(3.W)){
				stateReg_6 := 6.U
			}.otherwise{
				stateReg_6 := 5.U
			}
		}
		is(6.U){//BUF
			crpbuf_3_2 := crpbuf_2_2
			stateReg_6 := 7.U
		}
		is(7.U){//RSP
			stateReg_6 := 0.U
		}
		is(8.U){
			when(stateReg_12 === "b000".U(3.W)){
				when(core_id_pbuf_2 === "b010".U(3.W)){
					stateReg_6 := 4.U
				}.otherwise{
					stateReg_6 := 1.U
				}
			}.otherwise{
				stateReg_6 := 8.U
			}
		}
	}


	//FSM_7
	core3.ACVALID := false.B
	core3.ACADDR := addr_pbuf_2
	core3.ACSNOOP := 0.U

	core3.CRREADY := false.B

	switch(stateReg_7){
		is(0.U){//IDLE
			when((stateReg_3 === "b110".U(3.W)) && ((tran_pbuf_2 === "b0100".U(4.W)) || (tran_pbuf_2 === "b0000".U(4.W)))){
				stateReg_7 := 4.U
			}.elsewhen((stateReg_3 === "b110".U(3.W)) && (addr_pbuf_3 === addr_pbuf_2)){
				stateReg_7 := 8.U
			}.elsewhen((stateReg_3 === "b110".U(3.W)) && (core_id_pbuf_2 === "b011".U(3.W))){
				stateReg_7 := 4.U
			}.elsewhen((stateReg_3 === "b110".U(3.W)) && !(core_id_pbuf_2 === "b011".U(3.W))){
				stateReg_7 := 1.U
			}.otherwise{
				stateReg_7 := 0.U
			}
			crpbuf_2_3 := 0.U
		}
		is(1.U){//CA
			when(!core3.ACREADY){
				stateReg_7 := 1.U
			}.otherwise{
				stateReg_7 := 2.U
			}

			core3.ACVALID := true.B
			when(tran_pbuf_2 === "b0001".U(4.W)){
				core3.ACSNOOP := 1.U
			}.elsewhen(tran_pbuf_2 === "b0111".U(4.W)){
				core3.ACSNOOP := 7.U
			}.elsewhen(tran_pbuf_2 === "b1011".U(4.W)){
				core3.ACSNOOP := 9.U
			}.otherwise{
				core3.ACSNOOP := 0.U
			}

		}
		is(2.U){//CR_BUFF
			when(!core3.CRVALID){
				stateReg_7 := 2.U
			}.otherwise{
				stateReg_7 := 3.U
			}
			crpbuf_2_3 := core3.CRRESP
		}
		is(3.U){//CR
			core3.CRREADY := true.B
			stateReg_7 := 4.U
		}
		is(4.U){//FINISH after this state all 4 controllers synchronized
			when((stateReg_4 === "b0100".U(4.W)) && (stateReg_5 === "b0100".U(4.W)) && (stateReg_6 === "b0100".U(4.W)) && (stateReg_7 === "b0100".U(4.W)) && (stateReg_8 === "b0100".U(4.W)) && (stateReg_9 === "b0100".U(4.W)) && (stateReg_10 === "b0100".U(4.W)) && (stateReg_11 === "b0100".U(4.W))){
				stateReg_7 := 5.U
			}.otherwise{
				stateReg_7 := 4.U
			}
		}
		is(5.U){//SYNC
			when(stateReg_12 === "b000".U(3.W)){
				stateReg_7 := 6.U
			}.otherwise{
				stateReg_7 := 5.U
			}
		}
		is(6.U){//BUF
			crpbuf_3_3 := crpbuf_2_3
			stateReg_7 := 7.U
		}
		is(7.U){//RSP
			stateReg_7 := 0.U
		}
		is(8.U){
			when(stateReg_12 === "b000".U(3.W)){
				when(core_id_pbuf_2 === "b011".U(3.W)){
					stateReg_7 := 4.U
				}.otherwise{
					stateReg_7 := 1.U
				}
			}.otherwise{
				stateReg_7 := 8.U
			}
		}
	}

	//FSM_8
	core4.ACVALID := false.B
	core4.ACADDR := addr_pbuf_2
	core4.ACSNOOP := 0.U

	core4.CRREADY := false.B

	switch(stateReg_8){
		is(0.U){//IDLE
			when((stateReg_3 === "b110".U(3.W)) && ((tran_pbuf_2 === "b0100".U(4.W)) || (tran_pbuf_2 === "b0000".U(4.W)))){
				stateReg_8 := 4.U
			}.elsewhen((stateReg_3 === "b110".U(3.W)) && (addr_pbuf_3 === addr_pbuf_2)){
				stateReg_8 := 8.U
			}.elsewhen((stateReg_3 === "b110".U(3.W)) && (core_id_pbuf_2 === "b100".U(3.W))){
				stateReg_8 := 4.U
			}.elsewhen((stateReg_3 === "b110".U(3.W)) && !(core_id_pbuf_2 === "b100".U(3.W))){
				stateReg_8 := 1.U
			}.otherwise{
				stateReg_8 := 0.U
			}
			crpbuf_2_4 := 0.U
		}
		is(1.U){//CA
			when(!core4.ACREADY){
				stateReg_8 := 1.U
			}.otherwise{
				stateReg_8 := 2.U
			}

			core4.ACVALID := true.B
			when(tran_pbuf_2 === "b0001".U(4.W)){
				core4.ACSNOOP := 1.U
			}.elsewhen(tran_pbuf_2 === "b0111".U(4.W)){
				core4.ACSNOOP := 7.U
			}.elsewhen(tran_pbuf_2 === "b1011".U(4.W)){
				core4.ACSNOOP := 9.U
			}.otherwise{
				core4.ACSNOOP := 0.U
			}

		}
		is(2.U){//CR_BUFF
			when(!core4.CRVALID){
				stateReg_8 := 2.U
			}.otherwise{
				stateReg_8 := 3.U
			}
			crpbuf_2_4 := core4.CRRESP
		}
		is(3.U){//CR
			core4.CRREADY := true.B
			stateReg_8 := 4.U
		}
		is(4.U){//FINISH after this state all 4 controllers synchronized
			when((stateReg_4 === "b0100".U(4.W)) && (stateReg_5 === "b0100".U(4.W)) && (stateReg_6 === "b0100".U(4.W)) && (stateReg_7 === "b0100".U(4.W)) && (stateReg_8 === "b0100".U(4.W)) && (stateReg_9 === "b0100".U(4.W)) && (stateReg_10 === "b0100".U(4.W)) && (stateReg_11 === "b0100".U(4.W))){
				stateReg_8 := 5.U
			}.otherwise{
				stateReg_8 := 4.U
			}
		}
		is(5.U){//SYNC
			when(stateReg_12 === "b000".U(3.W)){
				stateReg_8 := 6.U
			}.otherwise{
				stateReg_8 := 5.U
			}
		}
		is(6.U){//BUF
			crpbuf_3_4 := crpbuf_2_4
			stateReg_8 := 7.U
		}
		is(7.U){//RSP
			stateReg_8 := 0.U
		}
		is(8.U){
			when(stateReg_12 === "b000".U(3.W)){
				when(core_id_pbuf_2 === "b100".U(3.W)){
					stateReg_8 := 4.U
				}.otherwise{
					stateReg_8 := 1.U
				}
			}.otherwise{
				stateReg_8 := 8.U
			}
		}
	}

	//FSM_9
	core5.ACVALID := false.B
	core5.ACADDR := addr_pbuf_2
	core5.ACSNOOP := 0.U

	core5.CRREADY := false.B

	switch(stateReg_9){
		is(0.U){//IDLE
			when((stateReg_3 === "b110".U(3.W)) && ((tran_pbuf_2 === "b0100".U(4.W)) || (tran_pbuf_2 === "b0000".U(4.W)))){
				stateReg_9 := 4.U
			}.elsewhen((stateReg_3 === "b110".U(3.W)) && (addr_pbuf_3 === addr_pbuf_2)){
				stateReg_9 := 8.U
			}.elsewhen((stateReg_3 === "b110".U(3.W)) && (core_id_pbuf_2 === "b101".U(3.W))){
				stateReg_9 := 4.U
			}.elsewhen((stateReg_3 === "b110".U(3.W)) && !(core_id_pbuf_2 === "b101".U(3.W))){
				stateReg_9 := 1.U
			}.otherwise{
				stateReg_9 := 0.U
			}
			crpbuf_2_5 := 0.U
		}
		is(1.U){//CA
			when(!core5.ACREADY){
				stateReg_9 := 1.U
			}.otherwise{
				stateReg_9 := 2.U
			}

			core5.ACVALID := true.B
			when(tran_pbuf_2 === "b0001".U(4.W)){
				core5.ACSNOOP := 1.U
			}.elsewhen(tran_pbuf_2 === "b0111".U(4.W)){
				core5.ACSNOOP := 7.U
			}.elsewhen(tran_pbuf_2 === "b1011".U(4.W)){
				core5.ACSNOOP := 9.U
			}.otherwise{
				core5.ACSNOOP := 0.U
			}

		}
		is(2.U){//CR_BUFF
			when(!core5.CRVALID){
				stateReg_9 := 2.U
			}.otherwise{
				stateReg_9 := 3.U
			}
			crpbuf_2_5 := core3.CRRESP
		}
		is(3.U){//CR
			core5.CRREADY := true.B
			stateReg_9 := 4.U
		}
		is(4.U){//FINISH after this state all 4 controllers synchronized
			when((stateReg_4 === "b0100".U(4.W)) && (stateReg_5 === "b0100".U(4.W)) && (stateReg_6 === "b0100".U(4.W)) && (stateReg_7 === "b0100".U(4.W)) && (stateReg_8 === "b0100".U(4.W)) && (stateReg_9 === "b0100".U(4.W)) && (stateReg_10 === "b0100".U(4.W)) && (stateReg_11 === "b0100".U(4.W))){
				stateReg_9 := 5.U
			}.otherwise{
				stateReg_9 := 4.U
			}
		}
		is(5.U){//SYNC
			when(stateReg_12 === "b000".U(3.W)){
				stateReg_9 := 6.U
			}.otherwise{
				stateReg_9 := 5.U
			}
		}
		is(6.U){//BUF
			crpbuf_3_5 := crpbuf_2_5
			stateReg_9 := 7.U
		}
		is(7.U){//RSP
			stateReg_9 := 0.U
		}
		is(8.U){
			when(stateReg_12 === "b000".U(3.W)){
				when(core_id_pbuf_2 === "b101".U(3.W)){
					stateReg_9 := 4.U
				}.otherwise{
					stateReg_9 := 1.U
				}
			}.otherwise{
				stateReg_9 := 8.U
			}
		}
	}

	//FSM_10
	core6.ACVALID := false.B
	core6.ACADDR := addr_pbuf_2
	core6.ACSNOOP := 0.U

	core6.CRREADY := false.B

	switch(stateReg_10){
		is(0.U){//IDLE
			when((stateReg_3 === "b110".U(3.W)) && ((tran_pbuf_2 === "b0100".U(4.W)) || (tran_pbuf_2 === "b0000".U(4.W)))){
				stateReg_10 := 4.U
			}.elsewhen((stateReg_3 === "b110".U(3.W)) && (addr_pbuf_3 === addr_pbuf_2)){
				stateReg_10 := 8.U
			}.elsewhen((stateReg_3 === "b110".U(3.W)) && (core_id_pbuf_2 === "b110".U(3.W))){
				stateReg_10 := 4.U
			}.elsewhen((stateReg_3 === "b110".U(3.W)) && !(core_id_pbuf_2 === "b110".U(3.W))){
				stateReg_10 := 1.U
			}.otherwise{
				stateReg_10 := 0.U
			}
			crpbuf_2_6 := 0.U
		}
		is(1.U){//CA
			when(!core6.ACREADY){
				stateReg_10 := 1.U
			}.otherwise{
				stateReg_10 := 2.U
			}

			core6.ACVALID := true.B
			when(tran_pbuf_2 === "b0001".U(4.W)){
				core6.ACSNOOP := 1.U
			}.elsewhen(tran_pbuf_2 === "b0111".U(4.W)){
				core6.ACSNOOP := 7.U
			}.elsewhen(tran_pbuf_2 === "b1011".U(4.W)){
				core6.ACSNOOP := 9.U
			}.otherwise{
				core6.ACSNOOP := 0.U
			}

		}
		is(2.U){//CR_BUFF
			when(!core6.CRVALID){
				stateReg_10 := 2.U
			}.otherwise{
				stateReg_10 := 3.U
			}
			crpbuf_2_6 := core6.CRRESP
		}
		is(3.U){//CR
			core6.CRREADY := true.B
			stateReg_10 := 4.U
		}
		is(4.U){//FINISH after this state all 4 controllers synchronized
			when((stateReg_4 === "b0100".U(4.W)) && (stateReg_5 === "b0100".U(4.W)) && (stateReg_6 === "b0100".U(4.W)) && (stateReg_7 === "b0100".U(4.W)) && (stateReg_8 === "b0100".U(4.W)) && (stateReg_9 === "b0100".U(4.W)) && (stateReg_10 === "b0100".U(4.W)) && (stateReg_11 === "b0100".U(4.W))){
				stateReg_10 := 5.U
			}.otherwise{
				stateReg_10 := 4.U
			}
		}
		is(5.U){//SYNC
			when(stateReg_12 === "b000".U(3.W)){
				stateReg_10 := 6.U
			}.otherwise{
				stateReg_10 := 5.U
			}
		}
		is(6.U){//BUF
			crpbuf_3_6 := crpbuf_2_6
			stateReg_10 := 7.U
		}
		is(7.U){//RSP
			stateReg_10 := 0.U
		}
		is(8.U){
			when(stateReg_12 === "b000".U(3.W)){
				when(core_id_pbuf_2 === "b110".U(3.W)){
					stateReg_10 := 4.U
				}.otherwise{
					stateReg_10 := 1.U
				}
			}.otherwise{
				stateReg_10 := 8.U
			}
		}
	}

	//FSM_11
	core7.ACVALID := false.B
	core7.ACADDR := addr_pbuf_2
	core7.ACSNOOP := 0.U

	core7.CRREADY := false.B

	switch(stateReg_11){
		is(0.U){//IDLE
			when((stateReg_3 === "b110".U(3.W)) && ((tran_pbuf_2 === "b0100".U(4.W)) || (tran_pbuf_2 === "b0000".U(4.W)))){
				stateReg_11 := 4.U
			}.elsewhen((stateReg_3 === "b110".U(3.W)) && (addr_pbuf_3 === addr_pbuf_2)){
				stateReg_11 := 8.U
			}.elsewhen((stateReg_3 === "b110".U(3.W)) && (core_id_pbuf_2 === "b111".U(3.W))){
				stateReg_11 := 4.U
			}.elsewhen((stateReg_3 === "b110".U(3.W)) && !(core_id_pbuf_2 === "b111".U(3.W))){
				stateReg_11 := 1.U
			}.otherwise{
				stateReg_11 := 0.U
			}
			crpbuf_2_7 := 0.U
		}
		is(1.U){//CA
			when(!core7.ACREADY){
				stateReg_11 := 1.U
			}.otherwise{
				stateReg_11 := 2.U
			}

			core7.ACVALID := true.B
			when(tran_pbuf_2 === "b0001".U(4.W)){
				core7.ACSNOOP := 1.U
			}.elsewhen(tran_pbuf_2 === "b0111".U(4.W)){
				core7.ACSNOOP := 7.U
			}.elsewhen(tran_pbuf_2 === "b1011".U(4.W)){
				core7.ACSNOOP := 9.U
			}.otherwise{
				core7.ACSNOOP := 0.U
			}

		}
		is(2.U){//CR_BUFF
			when(!core7.CRVALID){
				stateReg_11 := 2.U
			}.otherwise{
				stateReg_11 := 3.U
			}
			crpbuf_2_7 := core7.CRRESP
		}
		is(3.U){//CR
			core7.CRREADY := true.B
			stateReg_11 := 4.U
		}
		is(4.U){//FINISH after this state all 4 controllers synchronized
			when((stateReg_4 === "b0100".U(4.W)) && (stateReg_5 === "b0100".U(4.W)) && (stateReg_6 === "b0100".U(4.W)) && (stateReg_7 === "b0100".U(4.W)) && (stateReg_8 === "b0100".U(4.W)) && (stateReg_9 === "b0100".U(4.W)) && (stateReg_10 === "b0100".U(4.W)) && (stateReg_11 === "b0100".U(4.W))){
				stateReg_11 := 5.U
			}.otherwise{
				stateReg_11 := 4.U
			}
		}
		is(5.U){//SYNC
			when(stateReg_12 === "b000".U(3.W)){
				stateReg_11 := 6.U
			}.otherwise{
				stateReg_11 := 5.U
			}
		}
		is(6.U){//BUF
			crpbuf_3_7 := crpbuf_2_7
			stateReg_11 := 7.U
		}
		is(7.U){//RSP
			stateReg_11 := 0.U
		}
		is(8.U){
			when(stateReg_12 === "b000".U(3.W)){
				when(core_id_pbuf_2 === "b111".U(3.W)){
					stateReg_11 := 4.U
				}.otherwise{
					stateReg_11 := 1.U
				}
			}.otherwise{
				stateReg_11 := 8.U
			}
		}
	}


	//FSM_12
	val	select_buff = RegInit(0.U(4.W))  //0000:CD0, 0001:CD1, 0010:CD2, 0011:CD3, 0100:CD4, 0101:CD5, 0110:CD6, 0111:CD7, 1000:L2
	val	beat_buff = RegInit(0.U(64.W))	//buffer to store a one beat
	val	last_buff = RegInit(false.B)	//buffer to store last signal
	val rsp_buff = RegInit(0.U(4.W))	//buffer to store RRSP
	core0.RVALID := false.B
	core0.RID := 0.U(3.W)
	core0.RDATA := beat_buff
	core0.RLAST := last_buff
	core0.RRESP := rsp_buff

	core1.RVALID := false.B
	core1.RID := 1.U(3.W)
	core1.RDATA := beat_buff
	core1.RLAST := last_buff
	core1.RRESP := rsp_buff

	core2.RVALID := false.B
	core2.RID := 2.U(3.W)
	core2.RDATA := beat_buff
	core2.RLAST := last_buff
	core2.RRESP := rsp_buff

	core3.RVALID := false.B
	core3.RID := 3.U(3.W)
	core3.RDATA := beat_buff
	core3.RLAST := last_buff
	core3.RRESP := rsp_buff

	core4.RVALID := false.B
	core4.RID := 4.U(3.W)
	core4.RDATA := beat_buff
	core4.RLAST := last_buff
	core4.RRESP := rsp_buff

	core5.RVALID := false.B
	core5.RID := 5.U(3.W)
	core5.RDATA := beat_buff
	core5.RLAST := last_buff
	core5.RRESP := rsp_buff

	core6.RVALID := false.B
	core6.RID := 6.U(3.W)
	core6.RDATA := beat_buff
	core6.RLAST := last_buff
	core6.RRESP := rsp_buff

	core7.RVALID := false.B
	core7.RID := 7.U(3.W)
	core7.RDATA := beat_buff
	core7.RLAST := last_buff
	core7.RRESP := rsp_buff
	switch(stateReg_12){
		is(0.U){//IDLE
			when(stateReg_4 === "b0111".U(4.W) && ((tran_pbuf_3 === "b0001".U(4.W)) || (tran_pbuf_3 === "b0111".U(4.W)) || (tran_pbuf_3 === "b0000".U(4.W)))){
				stateReg_12 := 1.U
			}.elsewhen(stateReg_4 === "b0111".U(4.W) && ((tran_pbuf_3 === "b0100".U(4.W)))){
				stateReg_12 := 6.U
			}.elsewhen(stateReg_4 === "b0111".U(4.W) && ((tran_pbuf_3 === "b1011".U(4.W)))){
				when(crpbuf_3_0(0) || crpbuf_3_1(0)  || crpbuf_3_2(0) ||  crpbuf_3_3(0) || crpbuf_3_4(0) || crpbuf_3_5(0) || crpbuf_3_6(0) || crpbuf_3_7(0)){
					stateReg_12 := 1.U
				}.otherwise{
					stateReg_12 := 6.U
					last_buff := true.B
				}
			}.otherwise{
				stateReg_12 := 0.U
			}
		}
		is(1.U){//SELECT
			stateReg_12 := 2.U
			when(crpbuf_3_0(0)){ //here i am only checking it is shared or not because according to MOESI protocol.
				select_buff := "b0000".U(4.W)
			}.elsewhen(crpbuf_3_1(0)){
				select_buff := "b0001".U(4.W)
			}.elsewhen(crpbuf_3_2(0)){
				select_buff := "b0010".U(4.W)
			}.elsewhen(crpbuf_3_3(0)){
				select_buff := "b0011".U(4.W)
			}.elsewhen(crpbuf_3_4(0)){
				select_buff := "b0100".U(4.W)
			}.elsewhen(crpbuf_3_5(0)){
				select_buff := "b0101".U(4.W)
			}.elsewhen(crpbuf_3_6(0)){
				select_buff := "b0110".U(4.W)
			}.elsewhen(crpbuf_3_7(0)){
				select_buff := "b0111".U(4.W)
			}.otherwise{          //if it is data is not shared even if it is in the other local caches i take from L2
				select_buff := "b1000".U(4.W)
			}
		}
		is(2.U){//SYNC wait untill all the data available channels asserted its valid signal
			when((!crpbuf_3_0(0) || core0.CDVALID) && (!crpbuf_3_1(0) || core1.CDVALID) && (!crpbuf_3_2(0) || core2.CDVALID) && (!crpbuf_3_3(0) || core3.CDVALID) && (!crpbuf_3_4(0) || core4.CDVALID) && (!crpbuf_3_5(0) || core5.CDVALID) && (!crpbuf_3_6(0) || core6.CDVALID) && (!crpbuf_3_7(0) || core7.CDVALID) && (L2.RVALID || (tran_pbuf_3 === "b1011".U(4.W)))){
				stateReg_12 := 3.U
			}.otherwise{
				stateReg_12 := 2.U
			}

		}
		is(3.U){//BUFFER
			stateReg_12 := 4.U
			when(select_buff === "b0000".U(4.W)){
				beat_buff := core0.CDDATA
				last_buff := core0.CDLAST
				rsp_buff := Cat(crpbuf_3_0(3),crpbuf_3_0(2),"b00".U(2.W))
			}.elsewhen(select_buff === "b0001".U(4.W)){
				beat_buff := core1.CDDATA
				last_buff := core1.CDLAST
				rsp_buff := Cat(crpbuf_3_1(3),crpbuf_3_1(2),"b00".U(2.W))
			}.elsewhen(select_buff === "b0010".U(4.W)){
				beat_buff := core2.CDDATA
				last_buff := core2.CDLAST
				rsp_buff := Cat(crpbuf_3_2(3),crpbuf_3_2(2),"b00".U(2.W))
			}.elsewhen(select_buff === "b0011".U(4.W)){
				beat_buff := core3.CDDATA
				last_buff := core3.CDLAST
				rsp_buff := Cat(crpbuf_3_3(3),crpbuf_3_3(2),"b00".U(2.W))
			}.elsewhen(select_buff === "b0100".U(4.W)){
				beat_buff := core4.CDDATA
				last_buff := core4.CDLAST
				rsp_buff := Cat(crpbuf_3_4(3),crpbuf_3_4(2),"b00".U(2.W))
			}.elsewhen(select_buff === "b0101".U(4.W)){
				beat_buff := core5.CDDATA
				last_buff := core5.CDLAST
				rsp_buff := Cat(crpbuf_3_5(3),crpbuf_3_5(2),"b00".U(2.W))
			}.elsewhen(select_buff === "b0110".U(4.W)){
				beat_buff := core6.CDDATA
				last_buff := core6.CDLAST
				rsp_buff := Cat(crpbuf_3_6(3),crpbuf_3_6(2),"b00".U(2.W))
			}.elsewhen(select_buff === "b0111".U(4.W)){
				beat_buff := core7.CDDATA
				last_buff := core7.CDLAST
				rsp_buff := Cat(crpbuf_3_7(3),crpbuf_3_7(2),"b00".U(2.W))
			}.otherwise{
				beat_buff := L2.RDATA
				last_buff := L2.RLAST
				rsp_buff := Cat("b0".U(1.W),"b0".U(1.W),L2.RRESP)
			}

		}
		is(4.U){//COMPLETE_HANDSHAKE_RCV
			stateReg_12 := 5.U
			when(crpbuf_3_0(0)){
				core0.CDREADY := true.B
			}.otherwise{
				core0.CDREADY := false.B
			}
			when(crpbuf_3_1(0)){
				core1.CDREADY := true.B
			}.otherwise{
				core1.CDREADY := false.B
			}
			when(crpbuf_3_2(0)){
				core2.CDREADY := true.B
			}.otherwise{
				core2.CDREADY := false.B
			}
			when(crpbuf_3_3(0)){
				core3.CDREADY := true.B
			}.otherwise{
				core3.CDREADY := false.B
			}
			when(crpbuf_3_4(0)){
				core4.CDREADY := true.B
			}.otherwise{
				core4.CDREADY := false.B
			}
			when(crpbuf_3_5(0)){
				core5.CDREADY := true.B
			}.otherwise{
				core5.CDREADY := false.B
			}
			when(crpbuf_3_6(0)){
				core6.CDREADY := true.B
			}.otherwise{
				core6.CDREADY := false.B
			}
			when(crpbuf_3_7(0)){
				core7.CDREADY := true.B
			}.otherwise{
				core7.CDREADY := false.B
			}
			when((tran_pbuf_3 === "b1011".U(4.W))){
				L2.RREADY := false.B
			}.otherwise{
				L2.RREADY := true.B
			}
		}
		is(5.U){//RSP

			/*
			when(last_buff){
				stateReg_8 := 0.U
			}.otherwise{
				stateReg_8 := 2.U
			}
			*/
			when(!(((core_id_pbuf_3 === "b000".U(3.W)) && core0.RREADY) || ((core_id_pbuf_3 === "b001".U(3.W)) && core1.RREADY) || ((core_id_pbuf_3 === "b010".U(3.W)) && core2.RREADY) || ((core_id_pbuf_3 === "b011".U(3.W)) && core3.RREADY) || ((core_id_pbuf_3 === "b100".U(3.W)) && core4.RREADY) || ((core_id_pbuf_3 === "b101".U(3.W)) && core5.RREADY) || ((core_id_pbuf_3 === "b110".U(3.W)) && core6.RREADY) || ((core_id_pbuf_3 === "b111".U(3.W)) && core7.RREADY) )){
				stateReg_12 := 5.U
			}.elsewhen(last_buff){
				stateReg_12 := 7.U
			}.otherwise{
				stateReg_12 := 2.U
			}


			when(core_id_pbuf_3 === "b000".U(3.W)){
				core0.RVALID := true.B
			}.elsewhen(core_id_pbuf_3 === "b001".U(3.W)){
				core1.RVALID := true.B
			}.elsewhen(core_id_pbuf_3 === "b010".U(3.W)){
				core2.RVALID := true.B
			}.elsewhen(core_id_pbuf_3 === "b011".U(3.W)){
				core3.RVALID := true.B
			}.elsewhen(core_id_pbuf_3 === "b100".U(3.W)){
				core4.RVALID := true.B
			}.elsewhen(core_id_pbuf_3 === "b101".U(3.W)){
				core5.RVALID := true.B
			}.elsewhen(core_id_pbuf_3 === "b110".U(3.W)){
				core6.RVALID := true.B
			}.otherwise{
				core7.RVALID := true.B
			}
		}
		is(6.U){//RSP_ARBAR
			when((core_id_pbuf_3 === "b000".U(3.W)) && core0.RREADY){
				stateReg_12 := 7.U
				core0.RVALID := true.B
			}.elsewhen((core_id_pbuf_3 === "b001".U(3.W)) && core1.RREADY){
				stateReg_12 := 7.U
				core1.RVALID := true.B
			}.elsewhen((core_id_pbuf_3 === "b010".U(3.W)) && core2.RREADY){
				stateReg_12 := 7.U
				core2.RVALID := true.B
			}.elsewhen((core_id_pbuf_3 === "b011".U(3.W)) && core3.RREADY){
				stateReg_12 := 7.U
				core3.RVALID := true.B
			}.elsewhen((core_id_pbuf_3 === "b100".U(3.W)) && core4.RREADY){
				stateReg_12 := 7.U
				core4.RVALID := true.B
			}.elsewhen((core_id_pbuf_3 === "b101".U(3.W)) && core5.RREADY){
				stateReg_12 := 7.U
				core5.RVALID := true.B
			}.elsewhen((core_id_pbuf_3 === "b110".U(3.W)) && core6.RREADY){
				stateReg_12 := 7.U
				core6.RVALID := true.B
			}.elsewhen((core_id_pbuf_3 === "b111".U(3.W)) && core7.RREADY){
				stateReg_12 := 7.U
				core7.RVALID := true.B
			}.otherwise{
				stateReg_12 := 6.U
			}

			core0.RRESP := "b0000".U(4.W)
			core1.RRESP := "b0000".U(4.W)
			core2.RRESP := "b0000".U(4.W)
			core3.RRESP := "b0000".U(4.W)
			core4.RRESP := "b0000".U(4.W)
			core5.RRESP := "b0000".U(4.W)
			core6.RRESP := "b0000".U(4.W)
			core7.RRESP := "b0000".U(4.W)
		}
		is(7.U){
			stateReg_12 := 0.U
			last_buff := false.B
		}
	}
	/**

	//debug signal connecetion
	debug.stateReg_1 := stateReg_1
	debug.stateReg_2 := stateReg_2
	debug.stateReg_3 := stateReg_3
	debug.stateReg_4 := stateReg_4
	debug.stateReg_5 := stateReg_5
	debug.stateReg_6 := stateReg_6
	debug.stateReg_7 := stateReg_7
	debug.stateReg_8 := stateReg_8
	debug.stateReg_9 := stateReg_5
	debug.stateReg_10 := stateReg_6
	debug.stateReg_11 := stateReg_7
	debug.stateReg_12 := stateReg_8
	*/
}


