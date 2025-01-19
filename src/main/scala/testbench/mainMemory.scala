package testbench

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import chisel3.experimental.IO

import pipeline.ports._
import common.coreConfiguration._
import cache.AXI
import os.write
import decode.constants
import os.read
import dataclass.data
import DataCache.ACE

class mainMemory(
  addressBitSize:Int = 28,
  latency:Int = 1 // this variable changes nothing for now!!!
  ) extends Module {
  // toggle ON when kernel Image has been loaded
  val programmed = RegInit(false.B)

  // single cycle logic high to indicate that the memory has been programmed by an external entity
  val finishedProgramming = IO(Input(Bool()))

  when(finishedProgramming) { programmed := true.B }

  val memory = SyncReadMem ((1 << addressBitSize) , UInt (8.W))

  // External programmer
  val programmer = IO(Input(new Bundle {
    val valid   = Bool()
    val byte    = UInt(64.W)
    val offset  = UInt(addressBitSize.W)
  }))

  when (!programmed && programmer.valid) { (0 to 7).foreach { i => memory.write(programmer.offset + i.U, programmer.byte(8*i + 7, 8*i)) } } // memory.write(programmer.offset, programmer.byte) }
  // (0 to 7).foreach { i => memory.write(programmer.offset + i.U, programmer.byte(8*i + 7, 8*i)) }

  // connection with core pipeline
  val clients = IO(Flipped(Vec(2, (new ACE(busWidth = 64)))))
  for (i <- 0 until 2) {
  clients(i).CRREADY := 0.U
  clients(i).ACPROT := 0.U
  clients(i).ACVALID := 0.U
  clients(i).ACSNOOP := 0.U
  clients(i).ACADDR := 0.U
  clients(i).CDREADY := 0.U
}

  val instruction :: data :: Nil = Enum(2)

  val servicing = RegInit(VecInit.fill(2)(new Bundle {
    val valid = Bool()
    val address = UInt(addressBitSize.W)
    val id = clients(0).ARID.cloneType
    val beatsRemaining = UInt(8.W)
  } Lit(_.valid -> false.B)))

  // accepting read requests
  (clients zip servicing)
  .foreach{ case(client, buffer) => 
    when(client.ARREADY && client.ARVALID) { 
      buffer.valid := true.B
      buffer.address := (client.ARADDR&(~(3.U(32.W)))) + 8.U //leon changed
      buffer.id := client.ARID
      buffer.beatsRemaining := client.ARLEN
    } 
  }

  // read response buffer
  val readBackBuffers = RegInit(VecInit.fill(2)(new Bundle {
    val valid = Bool()
    val id = clients(0).RID.cloneType
    val data = clients(0).RDATA.cloneType // should be 32-bits
    val last = Bool()
  } Lit(_.valid -> false.B)))

  // servicing a read request
  (clients zip (servicing zip readBackBuffers))
  .foreach{ case(client, (request, response)) =>
    when(client.RREADY || !response.valid) {
      response.valid := request.valid
      response.data := Cat(Seq.tabulate(8)(i => memory.read(i.U + Mux(request.valid, request.address, (client.ARADDR&(~(3.U(32.W))))))).reverse) //leon
      response.last := Mux(!response.last, request.valid && !(request.beatsRemaining.orR), !(client.RVALID && client.RREADY))
      response.id := request.id

      when(request.valid) {
        when(!request.beatsRemaining.orR) { request.valid := false.B }
        .otherwise { request.beatsRemaining := (request.beatsRemaining - 1.U) }

        request.address := request.address + 8.U //leon changed
      }
    }  
    client.RVALID := response.valid
    client.ARREADY := Seq(programmed, !response.valid, !request.valid).reduce(_ && _)

    client.RDATA := response.data
    client.RID := response.id
    client.RLAST := response.last
    client.RRESP := 0.U
  }

  // write buffers
  val writeBuffers = RegInit(new Bundle {
    val addressValid = Bool()
    val address   = UInt(addressBitSize.W)
    val dataValid = Bool()
    val dataLast = Bool()
    val dataMask = clients(1).WSTRB.cloneType
    val data = clients(1).WDATA.cloneType // should be 32-bit
    val id = clients(1).AWID.cloneType
  } Lit(_.addressValid -> false.B, _.dataValid -> false.B))

  // finish writing memory
  when(writeBuffers.addressValid && writeBuffers.dataValid) {
   writeBuffers.dataValid := false.B
   when(writeBuffers.dataLast) { writeBuffers.addressValid := false.B } 
  }

  // accepting a new write request
  when(clients(data).AWVALID && clients(data).AWREADY) {
    writeBuffers.addressValid := true.B
    writeBuffers.address := clients(data).AWADDR & (~(3.U(32.W)))
    writeBuffers.id := clients(data).AWID
  }

  // accepting new write data
  when(clients(data).WVALID && clients(data).WREADY) {
    writeBuffers.data := clients(data).WDATA
    writeBuffers.dataLast := clients(data).WLAST
    writeBuffers.dataValid := true.B
    writeBuffers.dataMask := clients(data).WSTRB
  }

  // writing to memory
  when(writeBuffers.addressValid && writeBuffers.dataValid) {
    Seq.tabulate(8)(i => (i, (writeBuffers.data >> (i*8))(7, 0), writeBuffers.dataMask(i))) //leon
    .foreach{ case(offset, data, maskBit) => when(maskBit.asBool) { memory.write(writeBuffers.address + offset.U, data) } }

    writeBuffers.address := writeBuffers.address + 8.U //leon changed
  }
  
  // write response buffer
  val writeFinished = RegInit(false.B)
  when(writeBuffers.addressValid && writeBuffers.dataLast && writeBuffers.dataValid) { writeFinished := true.B }
  .elsewhen(clients(data).BVALID && clients(data).BREADY) { writeFinished := false.B }

  clients(data).AWREADY := (!writeBuffers.addressValid && !writeFinished)
  clients(data).WREADY := (!writeBuffers.dataValid || writeBuffers.addressValid)
  clients(data).BVALID := writeFinished

  clients(data).BRESP := 0.U
  clients(data).BID := writeBuffers.id

  val writing :: reading :: Nil = Enum(2)
  val arbiter = RegInit(reading)
  switch(arbiter) {
    is(reading) {
      clients(data).AWREADY := false.B
      when(
        (!servicing(data).valid && !clients(data).ARVALID) ||
        Seq(clients(data).RREADY, clients(data).RVALID, clients(data).RLAST).reduce(_ && _) 
      ) {
        arbiter := writing
      }
    }
    is(writing) {
      clients(data).ARREADY := false.B
      when(
        (!writeBuffers.addressValid && !clients(data).AWVALID) ||
        (clients(data).WVALID && clients(data).WREADY) 
      ) {
        arbiter := reading
      }
    }
  }

  clients(instruction).BID := 0.U
  clients(instruction).BVALID := false.B
  clients(instruction).BRESP := 0.U
  clients(instruction).AWREADY := false.B
  clients(instruction).WREADY := false.B

  val externalProbe = IO(new Bundle {
    val offset = Input(UInt(addressBitSize.W))
    val accessLong = Output(UInt(64.W))
  })

  externalProbe.accessLong := Cat(Seq.tabulate(8)(i => memory.read(i.U + externalProbe.offset)).reverse)
}

object mainMemory extends App {
  emitVerilog(new mainMemory)
}

/**
  * Defines the parameters for write request prority
  * i.e. Whether address or data channel is accepted first
  */
sealed trait writeArbitrationParam
// Write address beat is accepted first
case class addressFirst() extends writeArbitrationParam
// All data beats are accepted before the address beat is accepted
case class allDataFirst() extends writeArbitrationParam
// One data beat is accepted, and then the write address is accepted,
// then the rest of the data is accepted
case class oneDataFirst() extends writeArbitrationParam
// The two channels will act independently from each other
case class noWritePriority() extends writeArbitrationParam

/**
  * A main memory for testbench use cases, with parameterized latencies
  *
  * @param totalConcurrent Maximum number of requests that can be 
  *   outstanding at anytime
  * @param totalReads Maximum number of read requests that can be
  *   outstanding at anytime
  * @param totalWrites Maximum number of write requests that can be
  *   outstanding at anytime
  * @param latency Number of cycles to present the first response
  *   after the request has been fired
  * @param readAddressWait Number of cycles a read address will remain valid
  *   until it is accepted
  * @param readResponseGap Number cycles between a read response beat being 
  *   fired and the next one being accepted
  * @param writeAddressWait Number of cycles a write address will remain valid
  *   until it is accepted
  * @param writeDataWait Number of cycles a write data will remain valid
  *   until it is accepted
  * @param writeOpArbitration Arbitration policy for write request channels
  * @param addressBitSize Total size of memory is 2^(addressBitSize)
  */
class simulatedMemory(
  val totalConcurrent: Int = 1,
  val totalReads: Int = 1,
  val totalWrites: Int = 1,
  val latency: Int = 1,
  val readAddressWait: Int = 1,
  val readResponseGap: Int = 1,
  val writeAddressWait: Int = 1,
  val writeDataWait: Int = 1,
  val writeOpArbitration: writeArbitrationParam = noWritePriority(),

  val addressBitSize: Int = 28
) extends Module {

  // This will be the RAM of the test bench. This will be programmed
  // through an external interface after reset
  val memory = SyncReadMem ((1 << addressBitSize) , UInt (8.W))

  // Once the kernel image has been loaded to memory, this bit will
  // be toggled on 
  val programmed = RegInit(false.B)

  // single cycle logic high to indicate that the memory has been programmed by the 
  // external programmer
  val finishedProgramming = IO(Input(Bool()))
  when(finishedProgramming) { programmed := true.B }

  // External programmer
  val programmer = IO(Input(new Bundle {
    // Programmed when this bit is high
    val valid   = Bool()
    // Data that will be writter (Name needs changing) 
    val byte    = UInt(64.W)
    // Location of programming
    val offset  = UInt(addressBitSize.W)
  }))

  when (!programmed && programmer.valid) {
    // The external program can only load image when 'programmed' is low
    // There are 8 bytes in the programmed data 
    (0 to 7).foreach { i => memory.write(programmer.offset + i.U, programmer.byte(8*i + 7, 8*i)) } 
  }

  // This will be used by an external observer to look at a particular
  // address during simulation
  val externalProbe = IO(new Bundle {
    val offset = Input(UInt(addressBitSize.W))
    val accessLong = Output(UInt(64.W))
  })
  externalProbe.accessLong := Cat(Seq.tabulate(8)(i => memory.read(i.U + externalProbe.offset)).reverse)

  // connection with core pipeline
  val clients = IO(Flipped(Vec(2, (new AXI))))
  val clientInstr :: clientData :: Nil = Enum(2)

  // How we account for latency is through a set registers to add latency
  // 'valid' indicates a valid entry
  // Other fields corresponds to a field in AXI4 read address channel
  val readPipes = Seq(8, latency).map(Seq.fill(_)(RegInit(new Bundle {
    val valid = Bool()
    val addr = UInt(32.W) // lets assume we won't need to simulate bigger memories
    val size = UInt(3.W)
    // All requests should be same burst type
    // The cache field means nothing here
    // Assume prot is not implemented
    val id = clients.head.ARID.cloneType
    val len = UInt(8.W)
    // Assume lock is not necessary
    // Assume no QoS needed
    // Assume REGION field not needed
  } Lit( _.valid -> false.B ))))

  val writeAddressPipe = Seq.fill(latency)(RegInit(new Bundle {
    val valid = Bool()
    val addr = UInt(32.W)
    val size = UInt(3.W)
    // burst field is ignored
    // cache field is ignored
    // prot field is ignored
    val id = clients.head.AWID.cloneType
    val len = UInt(8.W)
    // lock field is ignored
    // qos field is ignored
    // region field is ignored
  } Lit( _.valid -> false.B )))

  // Maximum beats possible for a burst is 256
  val writeDataPipe = Seq.fill(Seq(latency, 256).max)(RegInit(new Bundle {
    val valid = Bool()
    val last = Bool()
    val data = UInt(32.W)
    val strb = UInt(4.W)
    // id field is ignored
  } Lit( _.valid -> false.B )))

  // Moving entries ahead
  readPipes.foreach { pipe => {
    pipe.zip(pipe.drop(1)).foreach { case(curr, next) => {
      when(!curr.valid) { 
        curr := next
        next.valid := false.B 
      }
    }}
  } }
  writeAddressPipe.zip(writeAddressPipe.drop(1)).foreach { case(curr, next) => {
    when(!curr.valid) { 
      curr := next
      next.valid := false.B 
    }
  }}
  writeDataPipe.zip(writeDataPipe.drop(1)).foreach { case(curr, next) => {
    when(!curr.valid) { 
      curr := next
      next.valid := false.B 
    }
  }}

  val newInstrRead  = clients(clientInstr).ARVALID && clients(clientData).ARREADY
  val newDataRead   = clients(clientData).ARVALID && clients(clientData).ARREADY
  val newWriteAddr  = clients(clientData).AWVALID && clients(clientData).AWREADY
  val newWriteData  = clients(clientData).WVALID && clients(clientData).WREADY

  val writeInProgress = RegInit(false.B)

  val readResults = RegInit(VecInit.fill(clients.length)(RegInit(new Bundle {
    val valid = Bool()
    val last = Bool()
    val data = UInt(32.W)
    // rresp value will always be OKAY
    val id = clients.head.ARID.cloneType
    val getFromBuffer = Bool() // specify where to get data from
  } Lit(_.valid -> false.B))))

  val ramReadData = 
    readPipes.map(_.head.addr & ((1 << addressBitSize)-4).U(32.W)) // get the base offset of the word access
    .map(baseAddr => Cat(Seq.tabulate(4)(i => memory.read(baseAddr + i.U)).reverse)) // reading the word

  // connecting the results to the read response port
  clients.zip(readResults.zip(ramReadData))
  .foreach { case (axi, (readReg, ramRead)) => {
    axi.RVALID := readReg.valid
    axi.RDATA := Mux(readReg.getFromBuffer, readReg.data, ramRead)
    axi.RID := readReg.id
    axi.RLAST := readReg.last
    axi.RRESP := 0.U
  }}

  // performing reads
  clients.zip(readPipes)
  .zip(readResults.zip(ramReadData))
  .foreach { case((axi, pipe), (readReg, ramRead)) => {
    when(readReg.valid) {
      when(axi.RREADY) {
        // Read is finished
        when(readReg.last) { readReg.valid := false.B }
        .otherwise {
          // preparing next read beat
          readReg.getFromBuffer := false.B // ram data will be available sample in next cycle
          readReg.last := pipe.head.len === 0.U
          // getting ready for next beat after next
          pipe.head.addr := pipe.head.addr + 4.U
          pipe.head.len := pipe.head.len - 1.U
          when(pipe.head.len === 0.U) { pipe.head.valid := false.B }
        }
      }.otherwise {
        // sampling data to buffer after one cycle
        when(!readReg.getFromBuffer) { readReg.data := ramRead }
        readReg.getFromBuffer := true.B
      }
    }.otherwise {
      when(!writeInProgress) {
        // no new reads will be started while a write is in progress
        when(pipe.head.valid) {
          // starting a new read
          readReg.valid := true.B
          readReg.getFromBuffer := false.B
          readReg.last := pipe.head.len === 0.U

          pipe.head.addr := pipe.head.addr + 4.U
          pipe.head.len := pipe.head.len - 1.U
          when(pipe.head.len === 0.U) { pipe.head.valid := false.B }
        }
      }
    }
  }}

  clients.zip(readPipes.map(_.reverse.head))
  .foreach { case(axi, pipeEnd) => {
    // Taking a new read request
    when(!pipeEnd.valid) {
      pipeEnd.valid := axi.ARVALID && axi.ARREADY
      pipeEnd.addr := axi.ARADDR
      pipeEnd.id := axi.ARID
      pipeEnd.len := axi.ARLEN
      pipeEnd.size := axi.ARSIZE
    }
  }}
  when(!writeAddressPipe.reverse.head.valid) {
    writeAddressPipe.reverse.head := {
      val req = Wire(writeAddressPipe.head.cloneType)
      req.valid := clients(1).AWVALID && clients(1).AWREADY
      req.addr := clients(1).AWADDR & "hfffffffc".U(32.W)
      req.id := clients(1).AWID
      req.len := clients(1).AWLEN
      req.size := clients(1).AWSIZE
      req
    }
  }
  when(!writeDataPipe.reverse.head.valid) {
    writeDataPipe.reverse.head := {
      val req = Wire(writeDataPipe.head.cloneType)
      req.valid := clients(1).WVALID && clients(1).WREADY
      req.data := clients(1).WDATA
      req.last := clients(1).WLAST
      req.strb := clients(1).WSTRB
      req
    }
  }

  // ---- Raising the ready to accept a new request ---------
  // Keeping track of amount space left to service requests
  val acceptInstRead  = (clients(0).ARVALID && clients(0).ARREADY)
  val finishInstRead  = (clients(0).RVALID && clients(0).RREADY && clients(0).RLAST)
  val acceptDataRead  = (clients(1).ARVALID && clients(1).ARREADY)
  val finishDataRead  = (clients(1).RVALID && clients(1).RREADY && clients(1).RLAST)
  val finishDataWrite = (clients(1).BVALID && clients(1).BREADY)
  // Knowing when a write is accepted is tricky, it can happen through data or addr channel
  // Accepting the first beat of a data burst or the corresponding beat on address channel
  /**
    * How to detect a new write request
    * writeAcceptTracker
    * - (= 0): Either the next beat on address channel or next first beat of a data burst
    * - (< 0): next first beat of a data burst
    * - (> 0): next beat on address channel
    */
  val writeAcceptTracker = RegInit(0.U((log2Ceil(totalWrites) + 2).W))
  val nextDataIsFirst = RegInit(true.B)
  when(clients(clientData).WREADY && clients(clientData).WVALID) { 
    nextDataIsFirst := clients(clientData).WLAST 
  }
  writeAcceptTracker := writeAcceptTracker +& 
    (clients(1).AWVALID && clients(1).AWREADY).asUInt -&
    (clients(1).WVALID && clients(1).WREADY && nextDataIsFirst).asUInt
  val acceptDataWrite = Wire(Bool())
  acceptDataWrite := (
    (clients(1).AWREADY && clients(1).AWVALID) ||
    (clients(1).WVALID && clients(1).WREADY && nextDataIsFirst)
  )
  when(writeAcceptTracker.asSInt > 0.S) {
    acceptDataWrite := (clients(1).AWVALID && clients(1).AWREADY)
  }.elsewhen(writeAcceptTracker.asSInt < 0.S) {
    acceptDataWrite := (clients(1).WVALID && clients(1).WREADY && nextDataIsFirst)
  }

  // Counters that keep track of the available space
  val totalReadsAvailable = RegInit(totalReads.U((log2Ceil(totalReads+1)+4).W))
  val totalWriteAvailable = RegInit(totalWrites.U((log2Ceil(totalWrites+1)+2).W))
  val totalConcurrentAvailable = RegInit(totalConcurrent.U((log2Ceil(totalConcurrent+1)+6).W))

  // counting
  totalReadsAvailable := totalReadsAvailable +&
    finishInstRead.asUInt +& finishDataRead.asUInt -&
    acceptInstRead.asUInt -& acceptDataRead.asUInt
  // Write only available through data interface currently
  totalWriteAvailable := totalWriteAvailable +&
    finishDataWrite -& acceptDataWrite
  totalConcurrentAvailable := totalConcurrentAvailable +&
    finishInstRead.asUInt +& finishDataRead.asUInt +& finishDataWrite -&
    acceptInstRead.asUInt -& acceptDataRead.asUInt -& acceptDataWrite

  // Counters to count the wait time 
  val instrWaitAddr = RegInit(readAddressWait.U(log2Ceil(readAddressWait+1).W))
  val dataWaitReadAddr  = RegInit(readAddressWait.U(log2Ceil(readAddressWait+1).W))
  val dataWaitWriteAddr = RegInit(writeAddressWait.U(log2Ceil(writeAddressWait+1).W))
  val dataWaitWriteData = RegInit(writeDataWait.U(log2Ceil(writeDataWait+1).W))
  val instrReadRespWait = RegInit(readResponseGap.U(log2Ceil(readResponseGap+1).W))
  val dataReadRespWait = RegInit(readResponseGap.U(log2Ceil(readResponseGap+1).W))
  // Performing count down
  Seq(instrWaitAddr, dataWaitReadAddr, dataWaitWriteAddr, dataWaitWriteData, instrReadRespWait, dataReadRespWait)
  .foreach(counter => when(counter =/= 0.U) { counter := counter - 1.U })
  // Resetting after a request is accepted
  val acceptDataBeat = clients(1).WREADY && clients(1).WVALID
  val sentReadRespData = clients(1).RREADY && clients(1).RVALID
  val sentReadRespInst = clients(0).RREADY && clients(0).RVALID
  Seq(
    (instrWaitAddr, readAddressWait, acceptInstRead),
    (dataWaitReadAddr, readAddressWait, acceptDataRead),
    (dataWaitWriteAddr, writeAddressWait, acceptDataWrite),
    (dataWaitWriteData, writeDataWait, acceptDataBeat),
    (instrReadRespWait, readResponseGap, sentReadRespInst),
    (dataReadRespWait, readResponseGap, sentReadRespData)
  ).foreach { case(counter, resetValue, resetCond) => {
    when(counter === 0.U) {
      when(resetCond) { counter := resetValue.U }
    }
  }} 

  // write FSM
  // noWrite: Wait for a write(address and all data beats)
  // waitForReads: Finish up the remaining in progress reads
  // writing: Perform the writes
  // finishWrite: Send the write response to master
  val noWrite :: waitForReads :: writing :: finishWrite :: Nil = Enum(4)
  val writeFSM = RegInit(noWrite)
  val waitForReadyB = RegInit(new Bundle {
    val valid = Bool()
    val id = clients(0).BID.cloneType
  } Lit(_.valid -> false.B))
  switch(writeFSM) {
    is(noWrite) { 
      when(writeAddressPipe.head.valid && VecInit(writeDataPipe)(writeAddressPipe.head.len).last) {
        writeFSM := waitForReads
      } 
    }
    is(waitForReads) {
      // waiting for all the inprogress reads to finish
      // "inprogress" - at least one response beat is sent
      when(!readResults.map(_.valid).reduce(_ || _)) {
        writeFSM := writing
      }
    }
    is(writing) {
      when(writeDataPipe.head.valid && writeDataPipe.head.last) {
        writeFSM := finishWrite
      }
    }
    is(finishWrite) {
      when(clients(1).BVALID && clients(1).BREADY) {
        writeFSM := noWrite
        waitForReadyB.valid := false.B
      }
    }
  }

  clients(1).BVALID := waitForReadyB.valid
  clients(1).BID := waitForReadyB.id
  clients(1).BRESP := 0.U
  // doing the write
  when(writeFSM === writing) {
    when(writeDataPipe.head.valid) {
      Seq.tabulate(4)(i => (writeAddressPipe.head.addr + i.U, writeDataPipe.head.data(8*i + 7, 8*i), writeDataPipe.head.strb(i).asBool))
      .foreach { case(address, writeByte, en) => when(en) { memory.write(address&((1 << addressBitSize) - 1).U, writeByte) }}
      writeAddressPipe.head.addr := writeAddressPipe.head.addr + 4.U
      when(writeDataPipe.head.last) {
        // Preparing write response
        waitForReadyB.valid := true.B
        waitForReadyB.id := writeAddressPipe.head.id
        // Removing write request from pipeline
        writeAddressPipe.head.valid := false.B
      }
      // Must remove to get next data
      writeDataPipe.head.valid := false.B  
    }
  }
  // Signalling ready to master
  val readyInstrAR = WireDefault(false.B)
  val instrRead :: dataRead :: dataWrite :: dataReqBusy :: Nil = Enum(4)
  val arbiter = RegInit(instrRead)
  val writeCompleteTracker = RegInit(0.U((log2Ceil(totalWrites)+2).W))
  switch(arbiter) {
    is(instrRead) { arbiter := dataRead }
    is(dataRead) { arbiter := dataWrite }
    is(dataWrite) {
      when(!acceptDataWrite) { arbiter := instrRead }
      .elsewhen(clients(1).AWVALID && clients(1).AWREADY && clients(1).WREADY && clients(1).WVALID && clients(1).WLAST) { arbiter := instrRead }
      .otherwise { arbiter := dataReqBusy }
    }
    is(dataReqBusy) { when(writeCompleteTracker === 0.U && writeAcceptTracker === 0.U) { arbiter := instrRead } }
  }

  clients(0).ARREADY := false.B
  when(instrWaitAddr === 0.U && programmed) {
    when((totalReadsAvailable > 1.U) && (totalConcurrentAvailable > 2.U)) {
      clients(0).ARREADY := !readPipes(0).reverse.head.valid
    }.elsewhen(
      (totalReadsAvailable > 0.U) && (totalConcurrentAvailable > 0.U) &&
      (arbiter === instrRead)
    ) {
      clients(0).ARREADY := !readPipes(0).reverse.head.valid
    }
  }
  
  clients(1).ARREADY := false.B
  when(dataWaitReadAddr === 0.U && programmed) {
    when((totalReadsAvailable > 1.U) && (totalConcurrentAvailable > 2.U)) {
      clients(1).ARREADY := !readPipes(1).reverse.head.valid
    }.elsewhen(
      (totalReadsAvailable > 0.U) && (totalConcurrentAvailable > 0.U) &&
      (arbiter === dataRead)
    ) {
      clients(1).ARREADY := !readPipes(1).reverse.head.valid
    }
  }

  clients(1).AWREADY := false.B
  writeCompleteTracker := writeCompleteTracker +& (clients(1).AWVALID && clients(1).AWREADY).asUInt -&
    (clients(1).WVALID && clients(1).WREADY && clients(1).WLAST).asUInt
  val checkWritesReady = WireDefault(false.B)
  when(totalWriteAvailable > 0.U && totalConcurrentAvailable > 2.U) { checkWritesReady := true.B }
  .elsewhen(arbiter === dataReqBusy) { checkWritesReady := true.B }
  .elsewhen(arbiter === dataWrite && totalConcurrentAvailable === 1.U ) { checkWritesReady := true.B }
  when(dataWaitWriteAddr === 0.U && programmed) {
    when(
      (checkWritesReady) && (totalWriteAvailable > 0.U && totalConcurrentAvailable > 0.U)
      ) {
      clients(1).AWREADY := { writeOpArbitration match {
        case addressFirst() => !writeAddressPipe.reverse.head.valid
        case allDataFirst() => writeCompleteTracker.asSInt < 0.S && !writeAddressPipe.reverse.head.valid
        case oneDataFirst() => writeAcceptTracker.asSInt < 0.S && !writeAddressPipe.reverse.head.valid
        case noWritePriority() => !writeAddressPipe.reverse.head.valid
      } }
    }.elsewhen(totalWriteAvailable === 0.U || totalConcurrentAvailable === 0.U) {
      // Finish receiving any incomplete writes
      when(writeAcceptTracker.asSInt < 0.S) {
        clients(1).AWREADY := !writeAddressPipe.reverse.head.valid
      }
    }
  }
  clients(1).WREADY := false.B
  when(dataWaitWriteData === 0.U && programmed) {
    when(
      (checkWritesReady) && (totalWriteAvailable > 0.U && totalConcurrentAvailable > 0.U) 
      ) {
      clients(1).WREADY := { writeOpArbitration match {
        case addressFirst() => writeCompleteTracker.asSInt > 0.S && !writeDataPipe.reverse.head.valid
        case allDataFirst() => !writeDataPipe.reverse.head.valid
        case oneDataFirst() => !writeDataPipe.reverse.head.valid
        case noWritePriority() => !writeDataPipe.reverse.head.valid
      } }
    }.elsewhen(totalWriteAvailable === 0.U || totalConcurrentAvailable === 0.U) {
      when(Mux(nextDataIsFirst, writeCompleteTracker.asSInt > 0.S, true.B)) {
        clients(1).WREADY := !writeDataPipe.reverse.head.valid
      }
    }
  }

  clients(0).BVALID := false.B
  clients(0).BRESP := 0.U
  clients(0).WREADY := false.B
  clients(0).BID := 0.U
  clients(0).AWREADY := false.B
  /* when(dataWaitWriteAddr === 0.U) {
    when(totalWriteAvailable )
  } */
  /**
    * Todo, check how these are done
    * Accepting requests
    * 1. How is the ready asserted
    *   - Is it asserted after the valid is asserted?
    *   - How is arbitration done
    *   - How are write arbitrations done?
    *   - Are the write address and data channels properly arbitrated?
    * 2. Accepting a new request
    *   - Is the new requested correctly put in to the correct pipe?
    *   - Are the counters properly decremented
    * 3. Servicing the requests
    * 4. Sending the response
    *   - Do we wait long enough?
    */
}

object simulatedMemory extends App {
  emitVerilog(new simulatedMemory)
}
