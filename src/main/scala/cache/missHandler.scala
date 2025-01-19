package cache

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import common.configuration
import common.composableInterface

class AXI(
  idWidth: Int = 1,
  addressWidth: Int = 32,
  dataWidth: Int = 32
)extends Bundle {
  val AWID = Output(UInt(idWidth.W))
	val AWADDR = Output(UInt(addressWidth.W))
	val AWLEN = Output(UInt(8.W))
	val AWSIZE = Output(UInt(3.W))
	val AWBURST = Output(UInt(2.W))
	val AWLOCK = Output(UInt(1.W))
	val AWCACHE = Output(UInt(4.W))
	val AWPROT = Output(UInt(3.W))
	val AWQOS = Output(UInt(4.W))
	val AWVALID = Output(Bool())
	val AWREADY = Input(Bool())

	val WDATA = Output(UInt(dataWidth.W))
	val WSTRB = Output(UInt((dataWidth/8).W))
	val WLAST = Output(Bool())
	val WVALID = Output(Bool())
	val WREADY = Input(Bool())

	val BID = Input(UInt(idWidth.W))
	val BRESP = Input(UInt(2.W))
	val BVALID = Input(Bool())
	val BREADY = Output(Bool())

	val ARID = Output(UInt(idWidth.W))
	val ARADDR = Output(UInt(addressWidth.W))
	val ARLEN = Output(UInt(8.W))
	val ARSIZE = Output(UInt(3.W))
	val ARBURST = Output(UInt(2.W))
	val ARLOCK = Output(UInt(1.W))
	val ARCACHE = Output(UInt(4.W))
	val ARPROT = Output(UInt(3.W))
	val ARQOS = Output(UInt(4.W))
	val ARVALID = Output(Bool())
	val ARREADY = Input(Bool())

	val RID = Input(UInt(idWidth.W))
	val RDATA = Input(UInt(dataWidth.W))
	val RRESP = Input(UInt(2.W))
	val RLAST = Input(Bool())
	val RVALID = Input(Bool())
	val RREADY = Output(Bool())
}

class missHandler extends Module {

	val offsetWidth = configuration.cache.wordOffsetWidth
	val lineWidth = configuration.cache.lineIndexWidth

  /**
    * Interface that sends the instructions to be replayed
    */
  val replayOut = IO(Output(new Bundle {
    val query = new pipelineMemoryRequest
    val data = UInt(64.W)
  }))

  /**
    * Sends out the saturated misses to be replayed. This does
    * not collect saturated misses. A separate structure is needed
    * because a cache miss can happen in saturated misses
    */
  val saturatedReplayVector = RegInit(VecInit(Seq.fill(configuration.cache.saturatedMissDepth)(replayOut.cloneType Lit(_.query.valid -> false.B))))

  /**
    * Cache is stalled iff 
    * (no of misses passes a threshold) || (miss target a separate cache block)
    */
  val saturatedCollectVector = RegInit(VecInit(Seq.fill(configuration.cache.saturatedMissDepth)(replayOut.cloneType Lit(_.query.valid -> false.B))))

  val nonSaturatedMisses = RegInit(VecInit(Seq.fill(configuration.cache.hitUnderMissDepth)(replayOut.cloneType Lit(_.query.valid -> false.B))))

  val idle :: servicing :: waitToReplay :: replaying :: Nil = Enum(4)
  val handlerStatus = RegInit(idle)

	val saturated = RegInit(false.B)

  val replayingQuries = IO(Output(Bool()))
  replayingQuries := saturatedReplayVector.map(_.query.valid).reduce(_ || _) || (handlerStatus === replaying) || (handlerStatus === waitToReplay)
  
  replayOut := Mux(handlerStatus === replaying, nonSaturatedMisses(0), saturatedReplayVector(0))
	when(handlerStatus === waitToReplay) {
		replayOut.query.valid := false.B
	}

	val branchOps = IO(Input(new Bundle {
    val valid = Bool()
    val branchMask = UInt(configuration.newBranchMaskWidth.W)  //leon coherency
    val passed = Bool()
  }))
	
	saturatedReplayVector zip saturatedReplayVector.drop(1) foreach { case (reg, next) => 
		reg := next
		when (branchOps.valid) {
			when((branchOps.branchMask & next.query.branchMask).orR) {reg.query.branchMask := branchOps.branchMask ^ next.query.branchMask}
			when(!branchOps.passed && (branchOps.branchMask & next.query.branchMask).orR) { reg.query.valid := false.B }
		}
	}
	// by default all entries are moved forward through the structure and no new ones are taken in
	saturatedReplayVector(saturatedReplayVector.length-1).query.valid := false.B 

	val cachePipelineEmpty = IO(Input(Bool()))
	when((handlerStatus === replaying) && !nonSaturatedMisses.map(_.query.valid).reduce(_ || _) && cachePipelineEmpty) {
		// moving query instructions to be replayed
		// there should not be any new requests in this cycle
		saturatedReplayVector zip saturatedCollectVector foreach { case (reg, next) => {
			reg := next
			when (branchOps.valid) {
				when((branchOps.branchMask & next.query.branchMask).orR) {reg.query.branchMask := branchOps.branchMask ^ next.query.branchMask}
				when(!branchOps.passed && (branchOps.branchMask & next.query.branchMask).orR) { reg.query.valid := false.B }
			}
		}}
	}

	val servicingBlock = Reg(UInt(32.W)) // address of the servicing block (for use outside and inside of missHandler)

	val missedRequest = IO(Input(replayOut.cloneType))
  
	when(!saturated) {
		// The first request that saturates the missHandler
		when(
			handlerStatus =/= idle && 
			missedRequest.query.valid && 
			((missedRequest.query.address(31, offsetWidth+3) =/= servicingBlock(31, offsetWidth+3)) || nonSaturatedMisses.map(_.query.valid).reduce(_ && _))
		) {
			saturatedCollectVector(0) := missedRequest
			saturated := true.B
			when(branchOps.valid) {
				when((missedRequest.query.branchMask & branchOps.branchMask).orR) {saturatedCollectVector(0).query.branchMask := missedRequest.query.branchMask ^ branchOps.branchMask}
				when(!branchOps.passed && (missedRequest.query.branchMask & branchOps.branchMask).orR) { saturatedCollectVector(0).query.valid := false.B }
			}
		}
	}.otherwise {
		val saturatedCollectVectorUpdate = Wire(Vec(saturatedCollectVector.length+1, saturatedCollectVector(0).cloneType))
		saturatedCollectVectorUpdate zip (saturatedCollectVector :+ missedRequest) foreach { case (update, curr) => 
			update := curr
			when(branchOps.valid) {
				when((curr.query.branchMask & branchOps.branchMask).orR) {update.query.branchMask := curr.query.branchMask ^ branchOps.branchMask}
				when(!branchOps.passed && (curr.query.branchMask & branchOps.branchMask).orR) { update.query.valid := false.B }
			}
		}

		// collecting new entries
		(saturatedCollectVector.map(_.query.valid).scanLeft(true.B)(_ && _) zip saturatedCollectVector)
		.zip(saturatedCollectVectorUpdate zip saturatedCollectVectorUpdate.drop(1))
		.foreach{ case((priorEntriesFull, reg), (old, next)) => when(!priorEntriesFull || !reg.query.valid) { reg := next } otherwise { reg := old } }

		when((handlerStatus === replaying) && !nonSaturatedMisses.map(_.query.valid).reduce(_ || _) && cachePipelineEmpty) {
			saturatedCollectVector.foreach(_.query.valid := false.B)
			saturated := false.B
		}
	}

	val cacheWrite = RegInit(new Bundle {
		val data = UInt(64.W)
		val valid = Bool()
		val address = UInt((lineWidth + offsetWidth).W) // when this is the last address of the block, FSM changes state
		val setSelVector = UInt(configuration.cache.associativity.W)
	} Lit(_.valid -> false.B))

	val fetched = RegInit(VecInit(Seq.fill(2)(new Bundle {
		val rdata = UInt(32.W)
		val valid = Bool()
	} Lit(_.valid -> false.B))))

	// pushes fetched doublewords to cache
	val pushToCache = IO(new composableInterface {
		val cacheWriteOut = (Output(cacheWrite.cloneType))
	})
	pushToCache.cacheWriteOut := cacheWrite

	// buiding doubleword writing to cache
	when(pushToCache.fired || !cacheWrite.valid) {
		cacheWrite.data := Cat(fetched(1).rdata, fetched(0).rdata)
		cacheWrite.valid := fetched.map(_.valid).reduce(_ && _)
	}

	switch(handlerStatus) {
		is(idle) {
			when(missedRequest.query.valid) {
				cacheWrite.address := Cat(missedRequest.query.address(lineWidth + offsetWidth + 3, offsetWidth + 3), 0.U(offsetWidth.W))
			}
		}
		is(servicing) {
			when(pushToCache.fired) {
				cacheWrite.address := cacheWrite.address + 1.U
			}
		}
	}

	val axi = IO(new AXI)

	when(axi.RREADY && axi.RVALID) {
		when((pushToCache.fired || !cacheWrite.valid) && fetched.map(_.valid).reduce(_ && _)) { 
			fetched(1).valid := false.B
			fetched(0).rdata := axi.RDATA
			fetched(0).valid := true.B
		}.elsewhen(!fetched(0).valid){
			fetched(0).rdata := axi.RDATA
			fetched(0).valid := true.B
		}.otherwise {
			fetched(1).rdata := axi.RDATA
			fetched(1).valid := true.B
		}
	}.elsewhen((pushToCache.fired || !cacheWrite.valid) && fetched.map(_.valid).reduce(_ && _)) {
		fetched(0).valid := false.B
		fetched(1).valid := false.B
	}
	

	// make sure there are no write dependencies with wrtieHandler
	val clearToFetch = RegInit(false.B)

	val dependencyCheck = IO(new Bundle {
		val requset = Output(new Bundle {
			val valid = Bool()
			val address = UInt(32.W)
		})
		val free = Input(Bool())
	})
	dependencyCheck.requset.address := servicingBlock
	dependencyCheck.requset.valid := (handlerStatus === servicing)

	// tracks whether or not we have requested a fetch
	val requested = RegInit(false.B)

	when(!requested) { requested := axi.ARVALID && axi.ARREADY }
	.otherwise { requested := (handlerStatus =/= idle) }

	when(!clearToFetch) {
		when(handlerStatus === servicing) { when (dependencyCheck.free) { clearToFetch := true.B }}
	}.otherwise { clearToFetch := (handlerStatus =/= idle) }

	val arvalid, rready = RegInit(false.B)
	when (!arvalid) { arvalid := !requested && clearToFetch }
	.otherwise { arvalid := !(axi.ARVALID && axi.ARREADY) }

	when (!rready) { rready := axi.ARVALID && axi.ARREADY }
	.otherwise { rready := !(axi.RVALID && axi.RREADY && axi.RLAST) }

	axi.ARVALID := arvalid
	axi.RREADY := rready && (pushToCache.fired || !fetched.map(_.valid).reduce(_ && _))

	switch(handlerStatus) {
		is(idle) {
			nonSaturatedMisses(0) := missedRequest
			when(branchOps.valid) {
				when((missedRequest.query.branchMask & branchOps.branchMask).orR) {nonSaturatedMisses(0).query.branchMask := missedRequest.query.branchMask ^ branchOps.branchMask}
				when(!branchOps.passed && (missedRequest.query.branchMask & branchOps.branchMask).orR) { nonSaturatedMisses(0).query.valid := false.B }
			}
		}
		is(servicing) {
			/* val nonSaturatedMissesUpdate = Wire(Vec(nonSaturatedMisses.length+1, nonSaturatedMisses(0).cloneType))
			nonSaturatedMissesUpdate zip (nonSaturatedMisses :+ missedRequest) foreach { case (update, curr) => {
				update := curr
				when(branchOps.valid) {
					when((curr.query.branchMask & branchOps.branchMask).orR) {update.query.branchMask := curr.query.branchMask ^ branchOps.branchMask}
					when(!branchOps.passed && (curr.query.branchMask & branchOps.branchMask).orR) { update.query.valid := false.B }
				}
			}}
			nonSaturatedMissesUpdate(nonSaturatedMissesUpdate.length-1).query.valid := missedRequest.query.valid && !(
				((missedRequest.query.address(31, offsetWidth+3) =/= servicingBlock(31, offsetWidth+3)) || nonSaturatedMisses.map(_.query.valid).reduce(_ && _))
			)
			// filling up misses
			(nonSaturatedMisses.map(_.query.valid).scanLeft(true.B)(_ && _) zip nonSaturatedMisses)
			.zip(nonSaturatedMissesUpdate zip nonSaturatedMissesUpdate.drop(1))
			.foreach { case ((priorEntriesFull, reg), (old, next)) => when(!priorEntriesFull || !reg.query.valid) { reg := next } otherwise { reg := old } } */
		}
	}
	when(handlerStatus =/= idle) {
		val nonSaturatedMissesUpdate = Wire(Vec(nonSaturatedMisses.length+1, nonSaturatedMisses(0).cloneType))
		nonSaturatedMissesUpdate zip (nonSaturatedMisses :+ missedRequest) foreach { case (update, curr) => {
			update := curr
			when(branchOps.valid) {
				when((curr.query.branchMask & branchOps.branchMask).orR) {update.query.branchMask := curr.query.branchMask ^ branchOps.branchMask}
				when(!branchOps.passed && (curr.query.branchMask & branchOps.branchMask).orR) { update.query.valid := false.B }
			}
		}}
		nonSaturatedMissesUpdate(nonSaturatedMissesUpdate.length-1).query.valid := missedRequest.query.valid && !saturated && !(
			((missedRequest.query.address(31, offsetWidth+3) =/= servicingBlock(31, offsetWidth+3)) || nonSaturatedMisses.map(_.query.valid).reduce(_ && _))
		)
		// filling up misses
		(nonSaturatedMisses.map(_.query.valid).scanLeft(true.B)(_ && _) zip nonSaturatedMisses)
		.zip(nonSaturatedMissesUpdate zip nonSaturatedMissesUpdate.drop(1))
		.foreach { case ((priorEntriesFull, reg), (old, next)) => when(!priorEntriesFull || !reg.query.valid) { reg := next } otherwise { reg := old } }
	}
	// stores the set number targeted by new cache block
	val nextSet = Reg(UInt(configuration.cache.associativity.W))
	cacheWrite.setSelVector := nextSet

	// random select function - sampled when in a cache miss all sets are filled
	val randSelect = RegInit(1.U(configuration.cache.associativity.W))
	randSelect := Mux(randSelect(0).asBool, (1 << (configuration.cache.associativity-1)).U, randSelect >> 1)	
	
	// indicates that this is the last doubleword in the cache block
	val rlastToCache = IO(Output(Bool()))
	rlastToCache := cacheWrite.address(offsetWidth-1, 0).andR

	// gives the set to invalidate, which will then be filled with new block
	val setInvalidateVector = IO(Output(UInt(configuration.cache.associativity.W)))	

	// gives the original occupation of sets at a line index
	val setFillStatus = IO(Input(UInt(configuration.cache.associativity.W)))
	setInvalidateVector := Mux(
		setFillStatus.andR, // all sets are full a random set is taken 
		randSelect, 
		MuxCase(0.U, (0 until configuration.cache.associativity).map(i => (!setFillStatus(i).asBool -> (1 << i).U))) // an empty set is taken
	)

	when(handlerStatus === idle) {
		when (missedRequest.query.valid) {
			nextSet := Mux(
				setFillStatus.andR, // all sets are full a random set is taken 
				randSelect, 
				MuxCase(0.U, (0 until configuration.cache.associativity).map(i => (!setFillStatus(i).asBool -> (1 << i).U))) // an empty set is taken
			)
		}
	}

	
	val handlerBusy = IO(Output(Bool()))
	handlerBusy := handlerStatus =/= idle

	val handlerSaturated = IO(Output(Bool()))
	handlerSaturated := saturated

	// FSM
	switch(handlerStatus) {
		is(idle) {
			when(missedRequest.query.valid) { 
				handlerStatus := servicing
				servicingBlock := Cat(missedRequest.query.address(31, offsetWidth+3), 0.U((offsetWidth+3).W)) 
			}
		}
		is(servicing) {
			when(cacheWrite.address(offsetWidth-1, 0).andR && pushToCache.fired) { handlerStatus := waitToReplay }
		}
		is(waitToReplay) {
			when (cachePipelineEmpty) { handlerStatus := replaying }
		}
		is(replaying) {
			when (!nonSaturatedMisses.map(_.query.valid).reduce(_ || _) && cachePipelineEmpty) { 
				handlerStatus := idle 
				
			}
			nonSaturatedMisses zip nonSaturatedMisses.drop(1) foreach{ case(reg, next) => reg := next }
			nonSaturatedMisses(nonSaturatedMisses.length-1).query.valid := false.B
			// Look into: what happens in case of branch miss prediction?
			// @Kaveesha-98 is that taken in to account? 
		}
	}

	pushToCache.ready := cacheWrite.valid && (cachePipelineEmpty || !rlastToCache)

	axi.ARADDR := servicingBlock
  axi.ARBURST := 1.U
  axi.ARCACHE := 2.U
  axi.ARID := 0.U
  axi.ARLEN := ((2 << offsetWidth)-1).U
  axi.ARLOCK := 0.U
  axi.ARPROT := 0.U
  axi.ARQOS := 0.U
  axi.ARSIZE := 2.U
  axi.ARVALID := arvalid

  axi.RREADY := rready && (!fetched.map(_.valid).reduce(_ && _) || pushToCache.fired)

  axi.AWADDR := 0.U
  axi.AWBURST := 1.U
  axi.AWCACHE := 2.U
  axi.AWID := 0.U
  axi.AWLEN := 0.U
  axi.AWLOCK := 0.U
  axi.AWPROT := 0.U
  axi.AWQOS := 0.U
  axi.AWSIZE := 2.U
  axi.AWVALID := false.B

  axi.WDATA := 0.U
  axi.WLAST := false.B
  axi.WSTRB := false.B
  axi.WVALID := false.B

  axi.BREADY := false.B

	val clean = IO(Output(Bool()))
	clean := !(nonSaturatedMisses.map(_.query.valid).reduce(_ || _) || saturatedCollectVector.map(_.query.valid).reduce(_ || _) || saturatedReplayVector.map(_.query.valid).reduce(_ || _) )

	val nonSaturatedReplay = IO(Output(Bool()))
	nonSaturatedReplay := (handlerStatus === replaying)
}

object missHandler extends App {
  emitVerilog(new missHandler)
}