package cache

import chisel3._
import chisel3.util._
import chisel3.util.HasBlackBoxResource
import chisel3.experimental.BundleLiterals._
import chisel3.experimental.IO
import common.composableInterface
import common.configuration

class writeHandler extends Module {

  val itWasPeripheral = IO(Input(Bool()))

  val notAllocated :: allocated :: waitOnAWREADY :: waitOnBVALID :: Nil = Enum(4) 

  val addressQueue = RegInit(VecInit(Seq.fill(configuration.cache.writeDepth)(new Bundle {
    val status = notAllocated.cloneType
    val address = UInt(32.W)
    val len = UInt(1.W) // AWLEN
    val size = UInt(3.W) // AWSIZE
    val becausePeripheral = Bool()
  } Lit(_.status -> notAllocated, _.becausePeripheral -> false.B))))

  // allocatePointer -> points to an empty addressQueue entry
  // queryInsertPointer -> Address is given to entry to send to write address channel
  // queryIniatePointer -> Points to entry waiting for a response in write response channel
  // deallocatePointer -> Deallocates pointer when write response is received
  val allocatePointer, queryInsertPointer, queryIniatePointer, deallocatePointer = RegInit(0.U(log2Ceil(addressQueue.length).W))

  val writeCommit = IO(new composableInterface)
  writeCommit.ready := (addressQueue(allocatePointer).status === notAllocated)

  when(writeCommit.fired) { 
    addressQueue(allocatePointer).status := allocated
    addressQueue(allocatePointer + 1.U).becausePeripheral := false.B
    allocatePointer := allocatePointer + 1.U
  }

  val axi = IO(new AXI)

  val request = IO(Input(new pipelineMemoryRequest {
    val alignedData = UInt(64.W)
    val mask = UInt(8.W)
  }))

  when(request.valid) {
    addressQueue(queryInsertPointer).status := waitOnAWREADY
    addressQueue(queryInsertPointer).address := request.address
    addressQueue(queryInsertPointer).len := (request.instruction(13, 12) === 3.U).asUInt
    addressQueue(queryInsertPointer).size := Mux((request.instruction(13, 12) === 3.U), "b010".U, request.instruction(14, 12))
    queryInsertPointer := queryInsertPointer + 1.U
  }
  when(itWasPeripheral) {
    addressQueue(queryInsertPointer).status := notAllocated
    addressQueue(queryInsertPointer).becausePeripheral := true.B
    queryInsertPointer := queryInsertPointer + 1.U
  }

  when(axi.AWVALID && axi.AWREADY) {
    addressQueue(queryIniatePointer).status := waitOnBVALID
    queryIniatePointer := queryIniatePointer + 1.U
  }
  when((addressQueue(queryIniatePointer).status === notAllocated) && (addressQueue(queryIniatePointer).becausePeripheral)) {
    queryIniatePointer := queryIniatePointer + 1.U
  }

  when(axi.BVALID && axi.BREADY) {
    addressQueue(deallocatePointer).status := notAllocated
    deallocatePointer := deallocatePointer + 1.U
  }
  when((addressQueue(deallocatePointer).status === notAllocated) && (addressQueue(deallocatePointer).becausePeripheral)) {
    deallocatePointer := deallocatePointer + 1.U
  }

  val dataQueue = RegInit(VecInit(Seq.fill(addressQueue.length*2)(new Bundle {
    val wvalid = Bool()
    val wlast = Bool()
    val wdata = UInt(32.W)
    val wstrb = UInt(4.W)
  } Lit(_.wvalid -> false.B))))

  val dataAllocatePointer, dataDeallocatePointer = RegInit(0.U(log2Ceil(dataQueue.length).W))

  when(request.valid) {
    when(request.instruction(13, 12) === 3.U) {
      dataQueue(dataAllocatePointer).wdata := request.alignedData(31, 0)
      dataQueue(dataAllocatePointer).wlast := false.B
      dataQueue(dataAllocatePointer).wstrb := "b1111".U
      dataQueue(dataAllocatePointer).wvalid := true.B
      dataQueue(dataAllocatePointer+1.U).wdata := request.alignedData(63, 32)
      dataQueue(dataAllocatePointer+1.U).wlast := true.B
      dataQueue(dataAllocatePointer+1.U).wstrb := "b1111".U
      dataQueue(dataAllocatePointer+1.U).wvalid := true.B
      dataAllocatePointer := dataAllocatePointer + 2.U
    }.otherwise {
      dataQueue(dataAllocatePointer).wlast := true.B
      dataQueue(dataAllocatePointer).wvalid := true.B
      dataAllocatePointer := dataAllocatePointer + 1.U
      when(request.mask(3, 0).orR) { // data in the lower bytes
        dataQueue(dataAllocatePointer).wdata := request.alignedData(31, 0)
        dataQueue(dataAllocatePointer).wstrb := request.mask(3, 0)
      }.otherwise  { // data in the upper bytes
        dataQueue(dataAllocatePointer).wdata := request.alignedData(63, 32)
        dataQueue(dataAllocatePointer).wstrb := request.mask(7, 4)
      }
    }
  }

  when(axi.WVALID && axi.WREADY) {
    dataQueue(dataDeallocatePointer).wvalid := false.B
    dataDeallocatePointer := dataDeallocatePointer + 1.U
  }

  val dependencyCheck = IO(new Bundle {
		val requset = Input(new Bundle {
			val valid = Bool()
			val address = UInt(32.W)
		})
		val free = Output(Bool())
	})

  dependencyCheck.free := RegNext(Mux(dependencyCheck.requset.valid, 
  !addressQueue.map(i => (i.address(31, configuration.cache.wordOffsetWidth + 3) === dependencyCheck.requset.address(31, configuration.cache.wordOffsetWidth + 3)) && (i.status > allocated)).reduce(_ || _), false.B), false.B)

  axi.AWADDR := addressQueue(queryIniatePointer).address
  axi.AWBURST := 1.U
  axi.AWCACHE := 2.U
  axi.AWID := 0.U
  axi.AWLEN := addressQueue(queryIniatePointer).len
  axi.AWLOCK := 0.U
  axi.AWPROT := 0.U
  axi.AWQOS := 0.U
  axi.AWSIZE := addressQueue(queryIniatePointer).size
  axi.AWVALID := (addressQueue(queryIniatePointer).status === waitOnAWREADY)

  axi.WDATA := dataQueue(dataDeallocatePointer).wdata
  axi.WLAST := dataQueue(dataDeallocatePointer).wlast
  axi.WSTRB := dataQueue(dataDeallocatePointer).wstrb
  axi.WVALID := dataQueue(dataDeallocatePointer).wvalid

  axi.BREADY := (addressQueue(deallocatePointer).status === waitOnBVALID)

  Seq(
    axi.ARADDR,
    axi.ARBURST,
    axi.ARCACHE,
    axi.ARID,
    axi.ARLOCK,
    axi.ARPROT,
    axi.ARQOS,
    axi.ARSIZE,
    axi.ARLEN
  ).foreach { _ := false.B }

  axi.ARVALID := false.B
  axi.RREADY := false.B

  val clean = IO(Output(Bool()))
  clean := addressQueue.map(_.status === notAllocated).reduce(_ && _)
}

object writeHandler extends App {
  emitVerilog(new writeHandler)
}
