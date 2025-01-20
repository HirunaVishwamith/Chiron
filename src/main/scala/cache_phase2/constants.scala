package DataCache

//AXI port ID is hard-coded

object constantsDCache{
  val lineSize : Int = 64 //8   //in bytes
  val cacheSize : Int = 32    //in KB
  val delay : Int = 2         //BRAM delay
  val cacheAddrWidth : Int = 32
  val cacheDataWidth : Int = 64*8  //8*8 
  // val depth : Int = 1
  val nway : Int = 4 
  val idWidth: Int = 2
  val fifoDepth : Int = 8

  val addrWidth : Int = 32
  val dataWidth : Int = 64
  val insWidth : Int = 32

  val branchMaskWidth : Int = 5
  val robAddrWidth : Int = 4
  val prfAddrWidth : Int = 6

  val FIFO_ADDR_TX = "hE000_1030"
  val FIFO_ADDR_RX = "hE000_102C"

  // val dPort_ID : Int = 1
  val dPort_PROT : Int = 2
  val dPort_LEN : Int = 7     //= "b0000_0001"         //"b0000_0111"
  val dPort_SIZE : Int = 3       //= "b010"        //"b011"
  val dPort_WIDTH: Int = math.pow(2, dPort_SIZE).toInt * 8  // 64 //32

  // val peripheral_ID : Int = 1
  val peripheral_LEN : Int = 1        //= "b0000_0001"
  val peripheral_SIZE : Int = 2       //= "b010"
  val peripheral_WIDTH : Int = math.pow(2, peripheral_SIZE).toInt * 8//32      //64

  val schedulerDepth : Int = 8
}