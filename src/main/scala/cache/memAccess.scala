package cache

import chisel3._
import chisel3.util._
import chisel3.util.HasBlackBoxResource
import chisel3.experimental.BundleLiterals._
import common.configuration
import common.configuration._
import common.configuration.cache.{lineIndexWidth, wordOffsetWidth}
import common.composableInterface

class memAccess extends Module {
  val scheduler = Module(new queryScheduler)

  /* val newInstruction = IO(Input(scheduler.newInstruction.cloneType))
  scheduler.newInstruction := newInstruction */

  
  val peripheralHandler = Module(new peripheralHandler)

  val peripheral = IO(new AXI)
  peripheral <> peripheralHandler.axi

  scheduler.peripheral.ready := peripheralHandler.ready
  peripheralHandler.request := scheduler.peripheral.bits

  val branchOps = IO(Input(new Bundle {
    val valid = Bool()
    val branchMask = UInt(configuration.newBranchMaskWidth.W) //leon coherency
    val passed = Bool()
  }))

  val reservation = RegInit(new Bundle {
    val valid = Bool()
    val address = UInt(32.W)
    val waslr64 = Bool() // last lr was 64-bit
  } Lit(_.valid -> false.B))
  val reservation64 = RegInit(new Bundle {
    val valid = Bool()
    val address = UInt(32.W)
    val waslr64 = Bool() // last lr was 64-bit
  } Lit(_.valid -> false.B))

  scheduler.branchOps := branchOps

  val cache = Module(new zeroWriteLatencyCache)

  cache.readAddress := scheduler.toCache.queryWithData.query.address

  // write Handler will take care not to overflow
  // thats why this structure is dependent on cache.writeDepth
  val dataQueue = RegInit(VecInit(Seq.fill(configuration.cache.writeDepth)(new Bundle {
    val valid = Bool()
    val data = UInt(64.W)
  } Lit(_.valid -> false.B))))

  val dataToCache = Reg(UInt(64.W))

  val writeDataIn = IO(Input(dataQueue(0).cloneType))

  val servicing = RegInit(scheduler.toCache.queryWithData.cloneType Lit(_.query.valid -> false.B))

  servicing.query := scheduler.toCache.queryWithData.query
  when(scheduler.toCache.replaying) { servicing.data := scheduler.toCache.queryWithData.data }
  .otherwise { 
    servicing.data := 0.U
    when(scheduler.toCache.queryWithData.query.instruction(24, 20).orR) {
      servicing.data := dataToCache
    } 
  }
  
  peripheralHandler.writeIn.valid := false.B
  when(!inRangeRAM(scheduler.toCache.queryWithData.query.address)) {
    servicing.query.valid := false.B
    peripheralHandler.writeIn.valid := scheduler.toCache.queryWithData.query.valid && scheduler.toCache.queryWithData.query.instruction(5).asBool
  }
  peripheralHandler.writeIn.data := dataToCache

  val cacheLookUp = RegInit(new Bundle {
    val query = new pipelineMemoryRequest
    val write = new Bundle {
      val dataByteAligned = UInt(64.W)
      val aligedMask = UInt(8.W)
    }
    val hitVector = UInt(configuration.cache.associativity.W)
    val setFillVector = UInt(configuration.cache.associativity.W)// shows which sets are full
    val cacheDouble = UInt(64.W)
  } Lit(_.query.valid -> false.B))
  cacheLookUp.query := servicing.query
  cacheLookUp.hitVector := Cat(cache.readOut.map(i => i.valid && i.tag === servicing.query.address(31, lineIndexWidth + wordOffsetWidth + 3)).reverse)
  cacheLookUp.setFillVector := Cat(cache.readOut.map(_.valid).reverse)
  cacheLookUp.write.dataByteAligned := servicing.data << (VecInit.tabulate(8)(i => (i*8).U)(servicing.query.address(2, 0)))
  cacheLookUp.write.aligedMask := VecInit(1.U, 3.U, 15.U, 255.U)(servicing.query.instruction(13, 12)) << servicing.query.address(2, 0)

  val memoryResponseWriteDataByteAligned = WireInit(0.U(64.W))
  val memoryResponse = RegInit(new Bundle {
    val query = new pipelineMemoryRequest
    val write = new Bundle {
      val dataByteAligned = UInt(64.W)
      val aligedMask = UInt(8.W)
    }
    // when true memory reads are committed
    val hit = Bool()
    // the set that hit
    val hitVector = UInt(configuration.cache.associativity.W)
    val readData = UInt(64.W)
    val invalidate = cache.invalidateSet.cloneType
    // finishing filling cache
    val cacheSetFill = cache.cacheFillDone.cloneType
    val filling = Bool()
  } Lit(_.query.valid -> false.B, _.filling -> false.B))
  MuxCase(
    MuxCase(cache.readOut(0).data, cache.readOut.drop(1).map(i => ((i.valid  && i.tag === servicing.query.address(31, lineIndexWidth + wordOffsetWidth + 3))-> i.data))),
    Seq(
      (memoryResponse.query.valid && memoryResponse.query.instruction(5).asBool && memoryResponse.query.address(31, 3) === servicing.query.address(31, 3)) -> memoryResponse.write.dataByteAligned,
      (cacheLookUp.query.valid && cacheLookUp.query.instruction(5).asBool && cacheLookUp.query.address(31, 3) === servicing.query.address(31, 3)) -> memoryResponseWriteDataByteAligned
    )  
  )
  // cacheLookUp.cacheDouble := Mux( memoryResponse.query.valid && memoryResponse.query.instruction(5).asBool && memoryResponse.query.address(31, 3) === servicing.query.address(31, 3), memoryResponse.write.dataByteAligned, MuxCase(cache.readOut(0).data, cache.readOut.drop(1).map(i => ((i.valid  && i.tag === servicing.query.address(31, lineIndexWidth + wordOffsetWidth + 3))-> i.data))))
  cacheLookUp.cacheDouble := MuxCase(
    MuxCase(cache.readOut(0).data, cache.readOut.drop(1).map(i => ((i.valid  && i.tag === servicing.query.address(31, lineIndexWidth + wordOffsetWidth + 3))-> i.data))),
    Seq(
      (cacheLookUp.query.valid && cacheLookUp.query.instruction(5).asBool && cacheLookUp.query.address(31, 3) === servicing.query.address(31, 3)) -> memoryResponseWriteDataByteAligned,
      (memoryResponse.query.valid && memoryResponse.query.instruction(5).asBool && memoryResponse.query.address(31, 3) === servicing.query.address(31, 3)) -> memoryResponse.write.dataByteAligned
    )  
  )

  val missHandler = Module(new missHandler)
  missHandler.missedRequest := {
    val request = Wire(missHandler.missedRequest.cloneType)
    request.query := cacheLookUp.query
    request.data := cacheLookUp.write.dataByteAligned >> (VecInit.tabulate(8)(i => (i*8).U)(cacheLookUp.query.address(2, 0)))
    request.query.valid := cacheLookUp.query.valid
    when(!missHandler.handlerSaturated) {
      when(!missHandler.handlerBusy) {
        when((cacheLookUp.query.instruction(6, 2) === "b01000".U) || cacheLookUp.hitVector.orR) { request.query.valid := false.B }
      }.elsewhen(cacheLookUp.query.address(31, wordOffsetWidth + 3) =/= missHandler.dependencyCheck.requset.address(31, wordOffsetWidth + 3)) {
        when((cacheLookUp.query.instruction(6, 2) === "b01000".U) || cacheLookUp.hitVector.orR) { request.query.valid := false.B }
      }.otherwise {
        // when replaying overlaps with handler being busy
        when(cacheLookUp.hitVector.orR) { request.query.valid := false.B }
      }
    }
    when(missHandler.handlerBusy) {
      when(
        (missHandler.dependencyCheck.requset.address(31, wordOffsetWidth + 3) === cacheLookUp.query.address(31, wordOffsetWidth + 3)) &&
        cacheLookUp.hitVector.orR
      ) { request.query.valid := false.B }
    }
    when(branchOps.valid && (branchOps.branchMask & cacheLookUp.query.branchMask).orR) {
      when(branchOps.passed) { request.query.branchMask := branchOps.branchMask ^ cacheLookUp.query.branchMask }
      .otherwise { request.query.valid := false.B }
    }

    request
  }

  missHandler.cachePipelineEmpty := !Seq(
    scheduler.toCache.queryWithData.query.valid,
    servicing.query.valid,
    cacheLookUp.query.valid
  ).reduce(_ || _)

  missHandler.setFillStatus := cacheLookUp.setFillVector

  missHandler.branchOps := branchOps

  memoryResponse.query.valid := false.B
  memoryResponse.filling := false.B
  memoryResponse.invalidate.valid := false.B
  memoryResponse.cacheSetFill.valid := false.B

  peripheralHandler.readFinished.fired := false.B
  // filling the response
  when(cacheLookUp.query.valid) {
    memoryResponse.query := cacheLookUp.query
    memoryResponse.cacheSetFill.valid := false.B
    memoryResponse.filling := false.B
    memoryResponse.hit := cacheLookUp.hitVector.orR
    memoryResponse.filling := false.B
    // only invalidated if all sets are filled
    memoryResponse.invalidate.valid := cacheLookUp.setFillVector.andR && !missHandler.handlerBusy && cacheLookUp.query.valid && !cacheLookUp.hitVector.orR
    memoryResponse.invalidate.cacheIndex := cacheLookUp.query.address(lineIndexWidth + wordOffsetWidth + 3, wordOffsetWidth + 3)
    memoryResponse.invalidate.invalidateVector := VecInit.tabulate(configuration.cache.associativity)(i => missHandler.setInvalidateVector(i))
    memoryResponse.readData := {
      val msb = VecInit(
        VecInit(0 until 8 map(_*8 + 7) map(i => cacheLookUp.cacheDouble(i)))(cacheLookUp.query.address(2, 0)),
        VecInit(0 until 4 map(_*16 + 15) map(i => cacheLookUp.cacheDouble(i)))(cacheLookUp.query.address(2, 1)),
        VecInit(0 until 2 map(_*32 + 31) map(i => cacheLookUp.cacheDouble(i)))(cacheLookUp.query.address(2)),
        cacheLookUp.cacheDouble(63)
      )(cacheLookUp.query.instruction(13, 12))

      VecInit(
        Cat(Fill(56, Mux(!cacheLookUp.query.instruction(14).asBool, msb, 0.U(1.W))), VecInit.tabulate(8)(i => cacheLookUp.cacheDouble(8*i + 7, 8*i))(cacheLookUp.query.address(2,0))),
        Cat(Fill(48, Mux(!cacheLookUp.query.instruction(14).asBool, msb, 0.U(1.W))), VecInit.tabulate(4)(i => cacheLookUp.cacheDouble(16*i + 15, 16*i))(cacheLookUp.query.address(2,1))),
        Cat(Fill(32, Mux(!cacheLookUp.query.instruction(14).asBool, msb, 0.U(1.W))), VecInit.tabulate(2)(i => cacheLookUp.cacheDouble(32*i + 31, 32*i))(cacheLookUp.query.address(2))),
        cacheLookUp.cacheDouble
      )(cacheLookUp.query.instruction(13, 12))
    }
    when(cacheLookUp.query.instruction(3).asBool) {
      memoryResponse.readData := (!((reservation.valid || reservation64.valid) && Seq(
        reservation64.valid && cacheLookUp.query.instruction(12).asBool && (reservation64.address === cacheLookUp.query.address),
        reservation.valid && !cacheLookUp.query.instruction(12).asBool && (reservation.address === cacheLookUp.query.address)
      ).reduce(_ || _))).asUInt
    }
    memoryResponse.hitVector := cacheLookUp.hitVector
    memoryResponseWriteDataByteAligned := {
      val storeMask = cacheLookUp.write.aligedMask
      val storeData = Cat(Seq.tabulate(8)(i => Mux(storeMask(i).asBool, cacheLookUp.write.dataByteAligned, cacheLookUp.cacheDouble)(i*8+7, i*8)).reverse)

      val semaphore64 = VecInit(
        cacheLookUp.cacheDouble,
        Mux(reservation64.valid && reservation64.address === cacheLookUp.query.address,
        cacheLookUp.write.dataByteAligned,
        cacheLookUp.cacheDouble
        )
      )(cacheLookUp.query.instruction(27))

      val atomic64 = Mux(cacheLookUp.query.instruction(28).asBool, semaphore64, VecInit(
        Mux(cacheLookUp.query.instruction(27).asBool, cacheLookUp.write.dataByteAligned, cacheLookUp.cacheDouble + cacheLookUp.write.dataByteAligned),
        cacheLookUp.cacheDouble ^ cacheLookUp.write.dataByteAligned,
        cacheLookUp.cacheDouble | cacheLookUp.write.dataByteAligned,
        cacheLookUp.cacheDouble & cacheLookUp.write.dataByteAligned,
        Mux(cacheLookUp.cacheDouble.asSInt < cacheLookUp.write.dataByteAligned.asSInt, cacheLookUp.cacheDouble, cacheLookUp.write.dataByteAligned),
        Mux(cacheLookUp.cacheDouble.asSInt < cacheLookUp.write.dataByteAligned.asSInt, cacheLookUp.write.dataByteAligned, cacheLookUp.cacheDouble),
        Mux(cacheLookUp.cacheDouble < cacheLookUp.write.dataByteAligned, cacheLookUp.cacheDouble, cacheLookUp.write.dataByteAligned),
        Mux(cacheLookUp.cacheDouble < cacheLookUp.write.dataByteAligned, cacheLookUp.write.dataByteAligned, cacheLookUp.cacheDouble)
      )(cacheLookUp.query.instruction(31, 29)))

      val atomic32Src1 = Mux(cacheLookUp.query.address(2).asBool, cacheLookUp.cacheDouble(63, 32), cacheLookUp.cacheDouble(31, 0))
      val atomic32Src2 = Mux(cacheLookUp.query.address(2).asBool, cacheLookUp.write.dataByteAligned(63, 32), cacheLookUp.write.dataByteAligned(31, 0))

      val  semaphore32High = VecInit(
        cacheLookUp.cacheDouble(63, 32),
        Mux(reservation.valid &&
        (reservation.address === cacheLookUp.query.address),
        cacheLookUp.write.dataByteAligned(63, 32),
        cacheLookUp.cacheDouble(63, 32)
        )
      )(cacheLookUp.query.instruction(27))

      val  semaphore32Low = VecInit(
        cacheLookUp.cacheDouble(31, 0),
        Mux(reservation.valid &&
        (reservation.address === cacheLookUp.query.address),
        cacheLookUp.write.dataByteAligned(31, 0),
        cacheLookUp.cacheDouble(31, 0)
        )
      )(cacheLookUp.query.instruction(27))
      val semaphore32 = Mux(cacheLookUp.query.address(2).asBool, semaphore32High, semaphore32Low)

      val atomic32 = Mux(cacheLookUp.query.instruction(28).asBool, semaphore32, VecInit(
        Mux(cacheLookUp.query.instruction(27).asBool, atomic32Src2, atomic32Src1 + atomic32Src2),
        atomic32Src1 ^ atomic32Src2,
        atomic32Src1 | atomic32Src2,
        atomic32Src1 & atomic32Src2,
        Mux(atomic32Src1.asSInt < atomic32Src2.asSInt, atomic32Src1, atomic32Src2),
        Mux(atomic32Src1.asSInt < atomic32Src2.asSInt, atomic32Src2, atomic32Src1),
        Mux(atomic32Src1 < atomic32Src2, atomic32Src1, atomic32Src2),
        Mux(atomic32Src1 < atomic32Src2, atomic32Src2, atomic32Src1)
      )(cacheLookUp.query.instruction(31, 29)))
      val postAtomic32 = VecInit(Cat(cacheLookUp.cacheDouble(63, 32), atomic32), Cat(atomic32, cacheLookUp.cacheDouble(31, 0)))(cacheLookUp.query.address(2))

      val atomic = Mux(cacheLookUp.query.instruction(12).asBool, atomic64, postAtomic32)

      Mux(cacheLookUp.query.instruction(3).asBool, atomic, storeData)
    }
    memoryResponse.write.dataByteAligned := memoryResponseWriteDataByteAligned
    memoryResponse.write.aligedMask := cacheLookUp.write.aligedMask
    when(branchOps.valid && (branchOps.branchMask & cacheLookUp.query.branchMask).orR) {
      when(branchOps.passed) { memoryResponse.query.branchMask := branchOps.branchMask ^ cacheLookUp.query.branchMask }
      .otherwise { memoryResponse.query.valid := false.B }
    }
  }.elsewhen(missHandler.pushToCache.ready) {
    memoryResponse.query.valid := false.B
    memoryResponse.query.address := Cat(missHandler.dependencyCheck.requset.address(31, (lineIndexWidth + wordOffsetWidth + 3)),missHandler.pushToCache.cacheWriteOut.address,0.U(3.W))
    memoryResponse.filling := true.B
    memoryResponse.cacheSetFill.valid := missHandler.rlastToCache
    memoryResponse.cacheSetFill.cacheIndex := (missHandler.pushToCache.cacheWriteOut.address >> (wordOffsetWidth))
    memoryResponse.cacheSetFill.validateVector := VecInit(missHandler.pushToCache.cacheWriteOut.setSelVector.asBools)
    memoryResponse.hit := false.B
    memoryResponse.hitVector := missHandler.pushToCache.cacheWriteOut.setSelVector
    memoryResponse.invalidate.valid := false.B
    memoryResponse.write.dataByteAligned := missHandler.pushToCache.cacheWriteOut.data
    memoryResponse.cacheSetFill.tag := missHandler.dependencyCheck.requset.address >> (lineIndexWidth + wordOffsetWidth + 3)
  }.elsewhen(peripheralHandler.readFinished.ready) {
    // getting data from peripheral read
    memoryResponse.query := peripheralHandler.finishedRequest
    memoryResponse.filling := false.B
    memoryResponse.hit := true.B
    memoryResponse.hitVector := 1.U
    memoryResponse.invalidate.valid := false.B
    peripheralHandler.readFinished.fired := true.B
    memoryResponse.readData := peripheralHandler.readDataOut
  }
  //peripheralHandler.readFinished.fired := peripheralHandler.readFinished.ready && 

  when(cacheLookUp.query.valid && cacheLookUp.hitVector.orR) {
    when(Cat(Seq(28, 27, 3).map(i => cacheLookUp.query.instruction(i))) === "b101".U){
      when(cacheLookUp.query.instruction(12).asBool) { reservation64.valid := true.B } otherwise { reservation.valid := true.B }
      when(cacheLookUp.query.instruction(12).asBool) { 
        reservation64.address :=  cacheLookUp.query.address 
      } otherwise { reservation.address :=  cacheLookUp.query.address}
    }.elsewhen(Cat(Seq(28, 27,5,  3).map(i => cacheLookUp.query.instruction(i))) === "b1111".U) {
      when(cacheLookUp.query.instruction(12).asBool) { reservation64.valid := false.B } otherwise { reservation.valid := false.B }
    }
  }
  
  // TODO: Consider moving all places where memoryResponse.query.valid is updated to one place
  when(cacheLookUp.query.valid) {
    when(missHandler.handlerSaturated && !missHandler.nonSaturatedReplay ) {
      // no response goes through when miss handler is saturated
      memoryResponse.query.valid := false.B
    }.elsewhen(missHandler.handlerBusy && !missHandler.nonSaturatedReplay) {
      when(cacheLookUp.query.address(31, wordOffsetWidth + 3) === missHandler.dependencyCheck.requset.address(31, wordOffsetWidth + 3)){ 
        memoryResponse.query.valid := false.B  
      }
    }
    when(missHandler.missedRequest.query.valid) { memoryResponse.query.valid := false.B }
  }
  missHandler.pushToCache.fired := missHandler.pushToCache.ready && !cacheLookUp.query.valid

  // modifying cache
  Seq.tabulate(configuration.cache.associativity)(i => memoryResponse.hitVector(i).asBool)
  .zip(cache.writePorts)
  .foreach{ case(enable, writePort) => {
    writePort.enable := enable && ((memoryResponse.query.valid && memoryResponse.query.instruction(5).asBool) || memoryResponse.filling)
    writePort.cacheAddress := (memoryResponse.query.address)
    writePort.data := memoryResponse.write.dataByteAligned
  }}

  cache.cacheFillDone := memoryResponse.cacheSetFill

  cache.invalidateSet := memoryResponse.invalidate

  val responseOut = IO(Output(new Bundle {
    val valid = Bool()
    val prfDest = UInt(prfAddrWidth.W)
    val robAddr = UInt(robAddrWidth.W)
    val result = UInt(64.W)
    val instruction = UInt(32.W)
  }))

  responseOut.valid := memoryResponse.query.valid && memoryResponse.hit && (memoryResponse.query.instruction(6, 5) =/= "b01".U)
  responseOut.prfDest := memoryResponse.query.prfDest
  responseOut.robAddr := memoryResponse.query.robAddr
  responseOut.result := memoryResponse.readData
  responseOut.instruction := memoryResponse.query.instruction

  val dataQueueUpdate = Wire(dataQueue.cloneType)
  dataQueue.map(_.valid).scanLeft(true.B)(_ && _)
  .zip(dataQueue zip dataQueueUpdate)
  .foreach{ case(priorEntriesFull, (reg, update)) => {
    update := Mux(priorEntriesFull && !reg.valid, writeDataIn, reg)
  }}
  val dequeueData = scheduler.storeCommit.ready && dataQueue(0).valid && !missHandler.handlerBusy && !scheduler.cacheStalled && !scheduler.replaying 
  scheduler.storeCommit.fired := dequeueData

  dataQueue zip dataQueueUpdate foreach{ case(reg, update) => reg := update }

  when(dequeueData) {
    dataQueue zip dataQueueUpdate.drop(1) foreach{ case(reg, update) => reg := update }
    dataQueue(dataQueue.length-1).valid := false.B
    dataToCache := dataQueue(0).data
  }

  scheduler.cacheStalled := missHandler.handlerSaturated

  //scheduler.peripheral.ready := true.B

  scheduler.replaying := missHandler.replayingQuries

  scheduler.replayQueue := missHandler.replayOut

  val request = IO(Input(scheduler.newInstruction.cloneType))
  scheduler.newInstruction := request

  val writeHandler = Module(new writeHandler)

  writeHandler.request.address := memoryResponse.query.address
  writeHandler.request.branchMask := 0.U
  writeHandler.request.instruction := memoryResponse.query.instruction
  writeHandler.request.prfDest := 0.U
  writeHandler.request.robAddr := 0.U
  writeHandler.request.alignedData := memoryResponse.write.dataByteAligned
  writeHandler.request.mask := memoryResponse.write.aligedMask
  writeHandler.request.valid := memoryResponse.query.valid && memoryResponse.query.instruction(5).asBool

  writeHandler.dependencyCheck <> missHandler.dependencyCheck

  val dPort = IO(new AXI)

  Seq(
    missHandler.axi.ARADDR,
    missHandler.axi.ARBURST,
    missHandler.axi.ARCACHE,
    missHandler.axi.ARID,
    missHandler.axi.ARLEN,
    missHandler.axi.ARLOCK,
    missHandler.axi.ARPROT,
    missHandler.axi.ARQOS,
    missHandler.axi.ARREADY,
    missHandler.axi.ARSIZE,
    missHandler.axi.ARVALID,
    writeHandler.axi.AWADDR,
    writeHandler.axi.AWBURST,
    writeHandler.axi.AWCACHE,
    writeHandler.axi.AWID,
    writeHandler.axi.AWLEN,
    writeHandler.axi.AWLOCK,
    writeHandler.axi.AWPROT,
    writeHandler.axi.AWQOS,
    writeHandler.axi.AWREADY,
    writeHandler.axi.AWSIZE,
    writeHandler.axi.AWSIZE,
    writeHandler.axi.AWVALID,
    writeHandler.axi.BID,
    writeHandler.axi.BREADY,
    writeHandler.axi.BRESP,
    writeHandler.axi.BVALID,
    missHandler.axi.RDATA,
    missHandler.axi.RID,
    missHandler.axi.RLAST,
    missHandler.axi.RREADY,
    missHandler.axi.RRESP,
    missHandler.axi.RVALID,
    writeHandler.axi.WDATA,
    writeHandler.axi.WLAST,
    writeHandler.axi.WREADY,
    writeHandler.axi.WSTRB,
    writeHandler.axi.WVALID
  )
  .zip(Seq(
    dPort.ARADDR,
    dPort.ARBURST,
    dPort.ARCACHE,
    dPort.ARID,
    dPort.ARLEN,
    dPort.ARLOCK,
    dPort.ARPROT,
    dPort.ARQOS,
    dPort.ARREADY,
    dPort.ARSIZE,
    dPort.ARVALID,
    dPort.AWADDR,
    dPort.AWBURST,
    dPort.AWCACHE,
    dPort.AWID,
    dPort.AWLEN,
    dPort.AWLOCK,
    dPort.AWPROT,
    dPort.AWQOS,
    dPort.AWREADY,
    dPort.AWSIZE,
    dPort.AWSIZE,
    dPort.AWVALID,
    dPort.BID,
    dPort.BREADY,
    dPort.BRESP,
    dPort.BVALID,
    dPort.RDATA,
    dPort.RID,
    dPort.RLAST,
    dPort.RREADY,
    dPort.RRESP,
    dPort.RVALID,
    dPort.WDATA,
    dPort.WLAST,
    dPort.WREADY,
    dPort.WSTRB,
    dPort.WVALID
  ))
  .foreach{ case (internal, external) => internal <> external }

  missHandler.axi.AWREADY := false.B
  missHandler.axi.WREADY := false.B
  missHandler.axi.BID := 0.U
  missHandler.axi.BVALID := false.B
  missHandler.axi.BRESP := 0.U

  writeHandler.axi.ARREADY := false.B
  writeHandler.axi.RDATA := 0.U
  writeHandler.axi.RID := 0.U
  writeHandler.axi.RLAST := false.B
  writeHandler.axi.RRESP := 0.U
  writeHandler.axi.RVALID := false.B

  writeHandler.itWasPeripheral := RegNext(RegNext(RegNext(peripheralHandler.writeIn.valid, false.B), false.B), false.B)

  val writeCommit = IO(writeHandler.writeCommit.cloneType)
  writeCommit <> writeHandler.writeCommit
  writeCommit.ready := writeHandler.writeCommit.ready && !(scheduler.cacheStalled || scheduler.replaying)
  peripheralHandler.branchOps := branchOps

  val canAllocate = IO(Output(Bool()))
  canAllocate := scheduler.canAllocate

  when(branchOps.valid) {
    Seq(
      scheduler.toCache.queryWithData.query.branchMask,
      servicing.query.branchMask,
      cacheLookUp.query.branchMask
    ).zip(Seq(
      servicing.query.branchMask,
      cacheLookUp.query.branchMask,
      memoryResponse.query.branchMask
    )).foreach { case(oldMask, newMask) => when((oldMask & branchOps.branchMask).orR) {newMask := oldMask ^ branchOps.branchMask} }

    when(!branchOps.passed) {
      Seq(
      scheduler.toCache.queryWithData.query.branchMask,
      servicing.query.branchMask,
      cacheLookUp.query.branchMask
      ).zip(Seq(
        servicing.query.valid,
        cacheLookUp.query.valid,
        memoryResponse.query.valid
      )).foreach { case(oldMask, validNext) => when((oldMask & branchOps.branchMask).orR) {validNext := false.B}}
    }
  }

  val initiateFence = IO(Input(Bool()))
  val waitForFenceData = RegInit(false.B)
  val fenceInstructions = IO(new composableInterface)
  fenceInstructions.ready := false.B

  when(!waitForFenceData) { waitForFenceData := initiateFence }
  .elsewhen(!Seq(scheduler.toCache.queryWithData.query.valid, servicing.query.valid, cacheLookUp.query.valid, memoryResponse.query.valid).reduce(_ || _) && Seq(scheduler.clean, missHandler.clean, writeHandler.clean).reduce(_ && _)) {
    fenceInstructions.ready := true.B
    when(fenceInstructions.fired) { waitForFenceData := false.B } 
  }

  val pendingStoresOut = IO(Output(scheduler.pendingStoresOut.cloneType))
  pendingStoresOut := scheduler.pendingStoresOut
}

object memAccess extends App {
  emitVerilog(new memAccess)
}
