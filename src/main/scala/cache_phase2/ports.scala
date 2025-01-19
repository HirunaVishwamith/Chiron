package DataCache

import chisel3._
import DataCache.constantsDCache._
import java.util.ResourceBundle

class branchOps extends Bundle {
  val valid = Input(Bool())
  val branchMask = Input(UInt(branchMaskWidth.W))
  val passed = Input(Bool())
}

class writeDataIn extends Bundle {
  val valid = Input(Bool())
  val data = Input(UInt(dataWidth.W))
}

class responseOut extends Bundle{
  val valid = Output(Bool())
  val prfDest = Output(UInt(prfAddrWidth.W))
  val robAddr = Output(UInt(robAddrWidth.W))
  val result = Output(UInt(dataWidth.W))
  val instruction = Output(UInt(insWidth.W))
}

class request extends Bundle {
  val valid = Input(Bool())
  val address = Input(UInt(addrWidth.W))
  val instruction = Input(UInt(insWidth.W))
  val branchMask = Input(UInt(branchMaskWidth.W))
  val robAddr = Input(UInt(robAddrWidth.W))
  val prfDest = Input(UInt(prfAddrWidth.W))
}

// Can allocate and initate Fence are just output an input ports with bool respectively
// val canAllocate = IO(Output(Bool()))
// val initiateFence = IO(Input(Bool()))

class composableInterface extends Bundle {
  val ready = Output(Bool())
  val fired = Input(Bool())
}

//For write commit and fenceInstruction just extend the composable interface
//val writeCommit = IO(new composableInterface)
//val fenceInstructions = IO(new composableInterface)

class AXI(
  idWidth: Int = 2,
  addressWidth: Int = 32,
  busWidth: Int //32
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

	val WDATA = Output(UInt(busWidth.W))
	val WSTRB = Output(UInt((busWidth/8).W))
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
	val RDATA = Input(UInt(busWidth.W))
	val RRESP = Input(UInt(2.W)) // 4.W
	val RLAST = Input(Bool())
	val RVALID = Input(Bool())
	val RREADY = Output(Bool())
}

class ACE(
  idWidth: Int = 2,
  addressWidth: Int = 32,
  busWidth: Int
) extends AXI(idWidth, addressWidth, busWidth){
	val AWDOMAIN = Output(UInt(2.W))
	val AWSNOOP = Output(UInt(3.W))
	val AWBAR = Output(UInt(2.W))

	val ARDOMAIN = Output(UInt(2.W))
	val ARSNOOP = Output(UInt(4.W))
	val ARBAR = Output(UInt(2.W))

	override val RRESP = Input(UInt(4.W)) // Override RRESP to 4 bits

	val ACVALID  = Input(Bool())
	val ACREADY = Output(Bool())
	val ACADDR = Input(UInt(addressWidth.W))
	val ACSNOOP = Input(UInt(4.W))
	val ACPROT = Input(UInt(3.W))

	val CRVALID = Output(Bool())
	val CRREADY = Input(Bool())
	val CRRESP = Output(UInt(5.W))

	val CDVALID = Output(Bool())
	val CDREADY = Input(Bool())
	val CDDATA = Output(UInt(busWidth.W))
	val CDLAST = Output(Bool())
}

//Written for AXIUnit
//The port to put out the memory read data
class loadData(
	dataWidth: Int 
) extends Bundle {
	val ready = Input(Bool())
	val valid = Output(Bool())
	val data = Output(UInt((dataWidth).W))
	val response = Output(UInt(2.W))
}

class storeData(
	dataWidth: Int 
) extends Bundle {
	val ready = Output(Bool())
	val valid = Input(Bool())
	val data = Input(UInt((dataWidth).W))
}

class requestAXI(		//Defined as seen from AXI unit
	dataWidth: Int, 
	addrWidth: Int,
	hasInstruction: Boolean = true // Parameter to control inclusion
) extends Bundle {
  val ready = Output(Bool())
  val valid = Input(Bool())
  val writeEn = Input(Bool())      // To signal a store (memory write)
  val address = Input(UInt(addrWidth.W))
  val data = Input(UInt(dataWidth.W)) // Configurable data width
	val instruction = Input(UInt(insWidth.W))
}

class requestACE(       //Defined as seen from ACE unit
  dataWidth: Int, 
  addrWidth: Int
) extends requestAXI(dataWidth, addrWidth) {  
  val isUnique = Input(Bool())     // ReadUnique or CleanUnique
  val isClean = Input(Bool())      // WriteClean or WriteBack
}

class cacheRequest extends Bundle {
  val instruction = Input(UInt(insWidth.W)) 
	val ready = Output(Bool())
  val valid = Input(Bool())
	val accepted = Output(Bool())
  val writeEn = Input(Bool())      // To signal a store (memory write)
  val address = Input(UInt(addrWidth.W))
}


//Written to cacheLookup
class storeDataIn extends writeDataIn {
  val ready = Output(Bool())
}

class coherencyRequest(
	addrWidth: Int = addrWidth,
) extends Bundle{ //Defined as seen from AXI unit
	val ready = Input(Bool())
	val valid = Output(Bool())
	val accepted = Input(Bool())
	val address = Output(UInt(addrWidth.W))
	val dataReq = Output(Bool())
	val invalidateReq = Output(Bool())
}

class loadCommit extends Bundle{
	val ready = Input(Bool())
	val valid = Output(Bool())
	val state = Output(Bool())
}