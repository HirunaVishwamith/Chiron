package DataCache
//Use namespaces if required

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import DataCache.constantsDCache._
import chisel3.util.random.LFSR

class cacheLookup extends Module{
  val cacheRequest = IO(new cacheRequest)
  val coherencyRequest = IO(Flipped(new coherencyRequest))
  val loadData = IO(Flipped(new loadData(dataWidth = lineSize * 8)))
  val memoryRequest = IO(Flipped(new requestACE(dataWidth = lineSize*8, addrWidth = addrWidth)))
  val responseOut = IO(new loadData(dataWidth = dataWidth))
  val storeDataIn = IO(new storeDataIn())
  val branchFail = IO(Input(Bool()))
  val loadCommit = IO(new loadCommit)
  val coherencyReceived = IO(Input(Bool()))
  val isAtomicsInOperation = IO(Output(Bool()))

  cacheRequest.ready := false.B
  cacheRequest.accepted := false.B
  
  loadData.ready := false.B

  memoryRequest.valid := false.B
  memoryRequest.writeEn := false.B
  memoryRequest.address := 0.U
  memoryRequest.data := 0.U
  memoryRequest.isUnique := false.B
  memoryRequest.isClean := false.B
  memoryRequest.instruction := 0.U
  
  responseOut.valid := false.B
  responseOut.data := 0.U
  responseOut.response := 0.U

  storeDataIn.ready := false.B

  coherencyRequest.ready := false.B
  coherencyRequest.accepted := false.B

  loadCommit.valid := false.B
  loadCommit.state := false.B  

  //-----------------------Intermediate signals--------------------//
  val isLoadWire = WireDefault(false.B)
  val isStoreWire = WireDefault(false.B)
  val isCoherentWire = WireDefault(false.B)

  val isAtomicsWire = WireDefault(false.B)
  val isLRWire = WireDefault(false.B)
  val isSCWire = WireDefault(false.B)

  val freeInputBuffer = WireDefault(false.B)
  val commitFIFOFull = WireDefault(false.B)

  val atomicsInOperationWire = WireInit(false.B)

  //-----------------------Input buffer----------------------------//
  val inputBuffer = RegInit(new Bundle{
    val address = UInt(addrWidth.W)
    val writeEn = Bool()
    val instruction = UInt(insWidth.W)
    val dataReq = Bool()
    val invalidateReq = Bool()
  }.Lit(
    _.address -> 0.U,
    _.writeEn -> false.B,
    _.instruction -> 0.U,
    _.dataReq -> false.B,
    _.invalidateReq -> false.B
  ))
  
  val empty :: memory :: coherency :: Nil = Enum(3)
  val inputBufferState = RegInit(empty)

  switch(inputBufferState){
    is(empty){
      cacheRequest.ready := !commitFIFOFull
      coherencyRequest.ready := true.B && !atomicsInOperationWire
      when(cacheRequest.valid && atomicsInOperationWire){
        inputBuffer.address := cacheRequest.address
        inputBuffer.writeEn := cacheRequest.writeEn
        inputBuffer.instruction := cacheRequest.instruction
      } .elsewhen(coherencyRequest.valid){
        inputBuffer.address := coherencyRequest.address
        inputBuffer.dataReq := coherencyRequest.dataReq
        inputBuffer.invalidateReq := coherencyRequest.invalidateReq
      } .elsewhen(cacheRequest.valid && !commitFIFOFull && !coherencyReceived){
        inputBuffer.address := cacheRequest.address
        inputBuffer.writeEn := cacheRequest.writeEn
        inputBuffer.instruction := cacheRequest.instruction
      }
      when(cacheRequest.valid && atomicsInOperationWire){
        inputBufferState := memory
      } .elsewhen(coherencyRequest.valid){
        inputBufferState := coherency
      } .elsewhen(cacheRequest.valid  && !coherencyRequest.valid && !coherencyReceived){//&& !branchFail) {
        inputBufferState := Mux(!commitFIFOFull, memory, empty)
      } .otherwise {
        inputBufferState := empty
      }
    }
    is(memory){
      cacheRequest.accepted := !freeInputBuffer
      inputBufferState := Mux(freeInputBuffer, empty, memory)
    }
    is(coherency){
      coherencyRequest.accepted := !freeInputBuffer
      inputBufferState := Mux(freeInputBuffer, empty, coherency)
    }
  }

  when(inputBufferState =/= empty){
    //The instruction(1,0) is additional measure
    isLoadWire := (!inputBuffer.writeEn && inputBuffer.instruction(1,0) === "b11".U )
    isStoreWire := (inputBuffer.writeEn && inputBuffer.instruction(1,0) === "b11".U )
    isAtomicsWire := (inputBuffer.instruction(6,0) === "b0101111".U)
    isLRWire := inputBuffer.instruction(31,27) === "b00010".U && isAtomicsWire
    isSCWire := inputBuffer.instruction(31,27) === "b00011".U && isAtomicsWire
  }

  isCoherentWire := (inputBufferState === coherency)

  //-----------------------Branch Ops-------------------------------//
  val branchFailReg = RegInit(false.B)
  when(branchFail){
    branchFailReg := branchFail
  }
  when(inputBufferState === empty && branchFailReg){
    branchFailReg := false.B
  }
  val isBranchFailWire = branchFail || branchFailReg

  //-----------------------Store data buffer----------------------//
  val storeDataBuffer = RegInit(new Bundle{
    val valid = Bool()
    val data = UInt(dataWidth.W)
  }.Lit(
    _.valid -> false.B,
    _.data -> 0.U
  ))
  val storeDataBufferState = RegInit(empty)
  switch(storeDataBufferState){
    is(empty){
      when(storeDataIn.valid){
        storeDataBuffer.data := storeDataIn.data
      }
      storeDataBufferState := Mux(storeDataIn.valid, memory, empty)
    }
    is(memory){
      storeDataBufferState := Mux(freeInputBuffer, empty, memory)
    }
  }

  //---------------------Reservation register---------------------//
  val reservationRegister = RegInit(new Bundle {
    val address = UInt(addrWidth.W)
    val reserved = Bool()
    //To say if a word - 0.U, or a double word - 1.U
    val size = UInt(1.W)  
  }.Lit(
    _.address -> 0.U,
    _.reserved -> false.B,
    _.size -> 0.U
  ))

  //-------------------------Atomics reg--------------------------//
  val atomicsInOperationReg = RegInit(false.B)
  when(!atomicsInOperationReg){
    atomicsInOperationReg := isAtomicsWire && isLoadWire && !isLRWire
  } .otherwise {
    atomicsInOperationReg := Mux(freeInputBuffer && (isAtomicsWire && isStoreWire && !isSCWire), false.B, atomicsInOperationReg)
  }
  atomicsInOperationWire := atomicsInOperationReg
  isAtomicsInOperation := atomicsInOperationWire

  //-----------------------Data BRAM------------------------------//
  val blockCount = nway                                   //No.of blocks
  val dataDepth = (cacheSize * 1024) / (lineSize * nway)
  val dataAddrWidth = log2Ceil(dataDepth)
  val dataDataWidth = lineSize * 8

  val dataBRAM = Seq.fill(nway)(Module(new CacheGenModule(
    addrWidth = dataAddrWidth, 
    dataWidth = dataDataWidth, 
    depth = dataDepth
  )))
  dataBRAM.foreach { bram =>
    bram.requestAddr := 0.U
    bram.writeData := 0.U
    bram.writeEn  := false.B
  }

  val dataBRAMVec = VecInit(dataBRAM.map { bram =>
    val bundle = Wire(new Bundle {
      val readData = UInt(dataDataWidth.W)
      val writeEn = Bool()
      val writeData = UInt(dataDataWidth.W)
      val requestAddr = UInt(dataAddrWidth.W)
    })
    bundle.readData := 0.U
    bundle.writeEn := false.B
    bundle.writeData := 0.U
    bundle.requestAddr := 0.U
    bundle
  })

  // Iterate over dataBRAMVec and connect them to dataBRAM
  dataBRAM.zip(dataBRAMVec).foreach { case (bram, vec) =>
    vec.readData := bram.readData
    bram.writeEn := vec.writeEn
    bram.writeData := vec.writeData
    bram.requestAddr := vec.requestAddr
  }

  //-----------------------Tag BRAM--------------------------------//
  //------Tag structure-------//
  // PLRU bit | Shared bit | Modified bit | Validity bit | Tag bits

  val tagSize = addrWidth - dataAddrWidth - log2Ceil(lineSize)    //Per cache line
  val tagDepth = dataDepth
  val tagAddrWidth = log2Ceil(tagDepth)
  val tagSection = (4 + tagSize + 7) >> 3     // nway no.of tag sections will be kept in one BRAM entry
  val tagDataWidth = (nway * tagSection) * 8  //4 bits for flags

  val tagBRAM = Module(new CacheGenModule(
    addrWidth = tagAddrWidth, 
    dataWidth = tagDataWidth, 
    depth = tagDepth
  ))
  tagBRAM.requestAddr := 0.U
  tagBRAM.writeData := 0.U
  tagBRAM.writeEn  := false.B

  //-----------------------Cache Cycle-----------------------------//
  val idle :: service :: dataOut :: modify :: request :: await :: resultOut :: writeToMem :: Nil = Enum(8)
  val cacheState = RegInit(idle)

  val requestValidWire = (inputBufferState =/= empty) && !freeInputBuffer

  val delayCounter = Module(new CounterModule(delay))
  delayCounter.incrm := false.B
  delayCounter.reset := false.B

  //Assigning addresses to the BRAMs
  val addrBeg = log2Ceil(lineSize)
  val addrEnd = dataAddrWidth - 1 + addrBeg
  dataBRAMVec.foreach { bram =>
    bram.requestAddr := inputBuffer.address(addrEnd, addrBeg)
  }
  tagBRAM.requestAddr := inputBuffer.address(addrEnd, addrBeg)

  val matchFoundReg = RegInit(VecInit(Seq.fill(nway)(false.B)))

  val resultWire = WireDefault(0.U(dataWidth.W))

  val receivedCacheLine = RegInit(0.U(dataDataWidth.W))
  val receivedResponse = RegInit(0.U(2.W))

  val writeBackCacheLine = RegInit(0.U(dataDataWidth.W))

  //Using a random value till PLRU policy is implemented
  val lfsr = LFSR(log2Ceil(nway))
  val randomValue = lfsr % (nway).U  // Restrict the random value to 0 to nway

  switch(cacheState){
    is(idle){
      delayCounter.reset := true.B
      cacheState := Mux(requestValidWire && !(isBranchFailWire) && !commitFIFOFull, service, idle)
    }
    is(service){
      val countCompleteWire = delayCounter.count === delay.U - 1.U
      delayCounter.incrm := !countCompleteWire

      when(countCompleteWire) {
        //Comparison for hit
        val tagChunks = VecInit(Seq.tabulate(nway) { i =>
          tagBRAM.readData(((i + 1) * (tagSection * 8)) - 1, i * (tagSection * 8))
        })
        // Compare each chunk with the size of tagSection for the request address
        val matchFoundVec = WireDefault(VecInit(Seq.fill(nway)(false.B)))
        for (i <- 0 until nway) {
          matchFoundVec(i) := (tagChunks(i)(tagSize - 1, 0) 
                            === inputBuffer.address(addrWidth - 1, dataAddrWidth + log2Ceil(lineSize)))
        }
        val hitTagWire = WireInit(PriorityEncoder(matchFoundVec))
        val validBitWire = WireInit(tagChunks(hitTagWire)(tagSize))
        val shareBitWire = WireInit(tagChunks(hitTagWire)(tagSize + 2))
        val isMissWire = !(matchFoundVec.reduce(_ | _) && validBitWire)

        matchFoundReg := matchFoundVec

        when(isCoherentWire){
          cacheState := resultOut
        } .otherwise{
          when(isMissWire){
            cacheState := request
          } .otherwise {
            when(isLoadWire){
              cacheState := dataOut
            } .otherwise { //A store
              when(shareBitWire){
                cacheState := request
              } .otherwise{
                cacheState := Mux(isSCWire, dataOut, modify)
              }
            }
          }
        }
      }
    }
    is(dataOut){
      //Comparison for hit
      val tagChunks = VecInit(Seq.tabulate(nway) { i =>
        tagBRAM.readData(((i + 1) * (tagSection * 8)) - 1, i * (tagSection * 8))
      })
      val hitTagWire = WireInit(PriorityEncoder(matchFoundReg))
      val validBitWire = WireInit(tagChunks(hitTagWire)(tagSize))
      val isMissWire = !(matchFoundReg.reduce(_ | _) && validBitWire)

      when(isLRWire){
        reservationRegister.address := inputBuffer.address
        reservationRegister.reserved := true.B
        reservationRegister.size := inputBuffer.instruction(12)
      }

      //Setting the dataOut
      val cacheLineChoosen = Mux(isMissWire, receivedCacheLine, dataBRAMVec(PriorityEncoder(matchFoundReg)).readData )
      val doubleWordSize = 64
      val numChunks = lineSize * 8 / doubleWordSize
      val doubleWordChunks = VecInit(Seq.tabulate(numChunks) { i =>
        cacheLineChoosen((i + 1) * doubleWordSize - 1, i * doubleWordSize)
      })
      val doubleWordChoosen = doubleWordChunks(inputBuffer.address(log2Ceil(lineSize) - 1, 3))
      val shiftAmount = (1.U << inputBuffer.instruction(13,12).asUInt)
      val section = (1.U << (8.U*shiftAmount)) - 1.U 
      val byteChunks = VecInit(Seq.tabulate(8) { i =>
        doubleWordChoosen((i + 1) * 8 - 1, i * 8) // 8-bit slices
      })
      val byteChoosed     = byteChunks(inputBuffer.address(2,0))
      val halfwordChoosed = Cat(byteChunks(2.U * inputBuffer.address(2,1) + 1.U),byteChunks(2.U * inputBuffer.address(2,1)))
      val wordChoosed     = Cat(byteChunks(4.U * inputBuffer.address(2) + 3.U),byteChunks(4.U * inputBuffer.address(2) + 2.U), 
                                byteChunks(4.U * inputBuffer.address(2) + 1.U),byteChunks(4.U * inputBuffer.address(2)))
      
      switch(inputBuffer.instruction(13, 12)){
        is("b00".U){resultWire := Mux(inputBuffer.instruction(14),byteChoosed,
                                      Cat(Fill((dataWidth-1*8),byteChoosed(7)),byteChoosed))}
        is("b01".U){resultWire := Mux(inputBuffer.instruction(14),halfwordChoosed,
                                      Cat(Fill((dataWidth-2*8),halfwordChoosed(15)),halfwordChoosed))}
        is("b10".U){resultWire := Mux(inputBuffer.instruction(14),wordChoosed,
                                      Cat(Fill((dataWidth-4*8),wordChoosed(31)),wordChoosed))}
        is("b11".U){resultWire := Mux(inputBuffer.instruction(14),"x0".U,
                                      doubleWordChoosen)}
      }
      when(isSCWire){
        resultWire := Mux(reservationRegister.reserved, 0.U, 1.U)
      } 

      when(responseOut.ready){
        cacheState := Mux(isSCWire && !reservationRegister.reserved, idle, modify)
      } .otherwise{
        cacheState := dataOut
      }
    }
    is(modify){
      //Tag BRAMs
      val tagChunks = VecInit(Seq.tabulate(nway) { i =>
        tagBRAM.readData((i + 1) * (tagSection * 8) - 1, i * (tagSection * 8))
      })
      val newtagChunks = VecInit(Seq.tabulate(nway) { i =>
        tagBRAM.readData((i + 1) * (tagSection * 8) - 1, i * (tagSection * 8))
      })
      val hitTagWire = WireInit(PriorityEncoder(matchFoundReg))

      val addrWire = WireInit(tagChunks(hitTagWire)(tagSize - 1,0))
      val validBitWire = WireInit(tagChunks(hitTagWire)(tagSize))
      val dirtyBitWire = WireInit(tagChunks(hitTagWire)(tagSize + 1))
      val shareBitWire = WireInit(tagChunks(hitTagWire)(tagSize + 2))
      val PLRUBitWire = WireInit(tagChunks(hitTagWire)(tagSize + 3))

      val isMissWire = !(matchFoundReg.reduce(_ | _) && validBitWire)
      val permissionMiss = !isMissWire && shareBitWire
      //For now PLRU is not updated properly, kept 0

      val newAddrWire =  WireInit(tagChunks(hitTagWire)(tagSize - 1,0))
      val newValidBitWire =  WireInit(tagChunks(hitTagWire)(tagSize))
      val newDirtyBitWire =  WireInit(tagChunks(hitTagWire)(tagSize + 1))
      val newShareBitWire =  WireInit(tagChunks(hitTagWire)(tagSize + 2))
      val newPLRUBitWire =  WireInit(tagChunks(hitTagWire)(tagSize + 3))

      //DataBRAMs
      val cacheLineChoosen = Mux(isMissWire, receivedCacheLine, dataBRAMVec(PriorityEncoder(matchFoundReg)).readData )
      val storeChunks = VecInit(Seq.tabulate(lineSize * 8 * 2 / dataWidth) { i =>
        cacheLineChoosen((i + 1) * (32) - 1, i * (32))
      })
      val newstoreChunks = VecInit(Seq.tabulate(lineSize * 8 * 2 / dataWidth) { i =>
        cacheLineChoosen((i + 1) * (32) - 1, i * (32))
      })
      val wordStore = storeChunks(inputBuffer.address(5,2))
      val doubleWordStore = Cat(storeChunks(inputBuffer.address(5,2)+ 1.U),storeChunks(inputBuffer.address(5,2)))
      val storeByteChunks = VecInit.tabulate(8)(i => doubleWordStore(8 * (i + 1) - 1, 8 * i))

      val result32 = WireDefault(0.U(32.W))
      val result64 = WireDefault(0.U(64.W))

      //PLRU logic
      val PLRUSetWire = WireInit(VecInit(tagChunks.map(chunk => chunk(tagSize + 3))))
      val flippedPLRUSetWire = ~PLRUSetWire.asUInt//WireInit(VecInit(PLRUSetWire.map(bit => ~bit)))
      val replacingset = PriorityEncoder(flippedPLRUSetWire)
      
      when(isLoadWire){
        newPLRUBitWire := Mux(PLRUSetWire.reduce(_ & _), 0.U, 1.U)
        when(isMissWire){
          newValidBitWire := 1.U
          newShareBitWire := receivedResponse(1)
          newDirtyBitWire := receivedResponse(0)
          newAddrWire := inputBuffer.address(addrWidth - 1, dataAddrWidth + log2Ceil(lineSize))
          for (i <- 0 until storeChunks.length) {
            storeChunks(i) := receivedCacheLine((i + 1) * 32 - 1, i * 32)
          }
        }
      }
      when(isStoreWire){
        newDirtyBitWire := 1.U
        newPLRUBitWire := Mux(PLRUSetWire.reduce(_ & _), 0.U, 1.U)
        when(isMissWire){
          newValidBitWire := 1.U
          newShareBitWire := receivedResponse(1)       
          newAddrWire := inputBuffer.address(addrWidth - 1, dataAddrWidth + log2Ceil(lineSize))
          for (i <- 0 until storeChunks.length) {
            storeChunks(i) := receivedCacheLine((i + 1) * 32 - 1, i * 32)
          }
        } .otherwise{
          newValidBitWire := 1.U
          newShareBitWire := receivedResponse(1)       
          newAddrWire := inputBuffer.address(addrWidth - 1, dataAddrWidth + log2Ceil(lineSize))
        }
        when(isAtomicsWire){
          when(inputBuffer.address(14,12) === "b010".U){
            switch(inputBuffer.instruction(31,27)){
              is("b00001".U){result32 := storeDataBuffer.data(31,0)}  //SWAP
              is("b00000".U){result32 := wordStore + storeDataBuffer.data(31,0)}  //ADD
              is("b00100".U){result32 := wordStore ^ storeDataBuffer.data(31,0)}  //XOR
              is("b01100".U){result32 := wordStore & storeDataBuffer.data(31,0)}  //AND
              is("b01000".U){result32 := wordStore | storeDataBuffer.data(31,0)}  //OR
              is("b10000".U){result32 := Mux(wordStore.asSInt < storeDataBuffer.data(31,0).asSInt, wordStore, storeDataBuffer.data(31,0))}  //MIN
              is("b10100".U){result32 := Mux(wordStore.asSInt > storeDataBuffer.data(31,0).asSInt, wordStore, storeDataBuffer.data(31,0))}  //MAX
              is("b11000".U){result32 := Mux(wordStore < storeDataBuffer.data(31,0), wordStore, storeDataBuffer.data(31,0))}  //MINU
              is("b11100".U){result32 := Mux(wordStore < storeDataBuffer.data(31,0), wordStore, storeDataBuffer.data(31,0))}  //MAXU
            }
            newstoreChunks(inputBuffer.address(5,2)) := result32
          }
          when(inputBuffer.address(14,12) === "b011".U){
            switch(inputBuffer.instruction(31,27)){
              is("b00001".U){result64 := storeDataBuffer.data}  //SWAP
              is("b00000".U){result64 := doubleWordStore + storeDataBuffer.data}  //ADD
              is("b00100".U){result64 := doubleWordStore ^ storeDataBuffer.data}  //XOR
              is("b01100".U){result64 := doubleWordStore & storeDataBuffer.data}  //AND
              is("b01000".U){result64 := doubleWordStore | storeDataBuffer.data}  //OR
              is("b10000".U){result64 := Mux(doubleWordStore.asSInt < storeDataBuffer.data.asSInt, doubleWordStore, storeDataBuffer.data)}  //MIN
              is("b10100".U){result64 := Mux(doubleWordStore.asSInt > storeDataBuffer.data.asSInt, doubleWordStore, storeDataBuffer.data)}  //MAX
              is("b11000".U){result64 := Mux(doubleWordStore < storeDataBuffer.data, doubleWordStore, storeDataBuffer.data)}  //MINU
              is("b11100".U){result64 := Mux(doubleWordStore < storeDataBuffer.data, doubleWordStore, storeDataBuffer.data)}  //MAXU
            }
            newstoreChunks(inputBuffer.address(5,2)) := result64(31,0)
            newstoreChunks(inputBuffer.address(5,2) + 1.U) := result64(63,32)
          }
        } .otherwise {
          when(!permissionMiss){
            switch(inputBuffer.instruction(13,12)){
              is("b00".U){for (i <- 0 until 1) {storeByteChunks(i.U) := storeDataBuffer.data(8 * (i + 1) - 1, 8 * i)}}
              is("b01".U){for (i <- 0 until 2) {storeByteChunks(i.U) := storeDataBuffer.data(8 * (i + 1) - 1, 8 * i)}}
              is("b10".U){for (i <- 0 until 4) {storeByteChunks(i.U) := storeDataBuffer.data(8 * (i + 1) - 1, 8 * i)}}
              is("b11".U){for (i <- 0 until 8) {storeByteChunks(i.U) := storeDataBuffer.data(8 * (i + 1) - 1, 8 * i)}}
            }
            newstoreChunks(inputBuffer.address(5, 2)) := Cat(storeByteChunks.slice(0, 4).reverse)
            newstoreChunks(inputBuffer.address(5, 2) + 1.U) := Cat(storeByteChunks.slice(4, 8).reverse)
          }
          //If permission miss, CleanUnique
        }
        //If SC, the reservation is already removed, so no need to check
        when(reservationRegister.reserved){
          switch(reservationRegister.size){
            is(0.U){reservationRegister.reserved := !((reservationRegister.address((addrWidth-1),2)) === (inputBuffer.address((addrWidth-1),2)))}
            is(1.U){reservationRegister.reserved := !((reservationRegister.address((addrWidth-1),3)) === (inputBuffer.address((addrWidth-1),3)))}
          }
        }
        when(isSCWire){
          reservationRegister.reserved := false.B
        }
      }
      when(isCoherentWire){
        when(inputBuffer.invalidateReq){
          newValidBitWire := 0.U
          newPLRUBitWire := 0.U
          newShareBitWire := 0.U
          newDirtyBitWire := 0.U
        } .otherwise{
          newShareBitWire := Mux(inputBuffer.dataReq, 1.U, tagChunks(hitTagWire)(tagSize + 2))
        }
        when(reservationRegister.reserved){
          switch(reservationRegister.size){
            is(0.U){reservationRegister.reserved := !((reservationRegister.address((addrWidth-1),2)) === (inputBuffer.address((addrWidth-1),2)))}
            is(1.U){reservationRegister.reserved := !((reservationRegister.address((addrWidth-1),3)) === (inputBuffer.address((addrWidth-1),3)))}
          }
        }
      }
      
      val updatingSet = Mux(isMissWire, replacingset, hitTagWire)
      val nextWriteEn = true.B
      tagBRAM.writeEn := RegNext(nextWriteEn)
      when(PLRUSetWire.reduce(_ & _)){
        for (i <- 0 until nway) {
          newtagChunks(i) := tagChunks(i) & ~(1.U << (tagSize + 3))//tagChunks(i).bitSet(tagSize + 3, false.B) // Set the bit to 1
        }
      }
      newtagChunks(updatingSet) := Cat(tagChunks(updatingSet)(tagSection * 8 - 1,tagSize + 4), newPLRUBitWire, newShareBitWire, newDirtyBitWire, newValidBitWire, 
                                      newAddrWire)
      tagBRAM.writeData := newtagChunks.reverse.reduce(Cat(_, _))

      dataBRAMVec(updatingSet).writeEn := isMissWire || isStoreWire
      dataBRAMVec(updatingSet).writeData := newstoreChunks.reverse.reduce(Cat(_, _))

      writeBackCacheLine := dataBRAMVec(hitTagWire).readData//Mux(!tagBRAM.writeEn, dataBRAMVec(hitTagWire).readData,writeBackCacheLine)

      when(!isBranchFailWire){
        //One cycle delay for result registers to setup, next cycle delay for BRAMs to get written
        when(!isCoherentWire){
          //Is miss and dirty
          cacheState := Mux(isMissWire && dirtyBitWire, writeToMem, idle)
        } .otherwise{
          cacheState := idle
        }
      } .otherwise {
        cacheState := idle
      }
    }
    is(request){
      val tagChunks = VecInit(Seq.tabulate(nway) { i =>
        tagBRAM.readData((i + 1) * (tagSection * 8) - 1, i * (tagSection * 8))
      })

      val hitTagWire = WireInit(PriorityEncoder(matchFoundReg))
      val validBitWire = WireInit(tagChunks(hitTagWire)(tagSize))
      val isMissWire = !(matchFoundReg.reduce(_ | _) && validBitWire)

      memoryRequest.valid := true.B
      memoryRequest.address := inputBuffer.address
      memoryRequest.data := 0.U
      memoryRequest.writeEn := false.B
      when(isLoadWire){
        //Read Shared
        memoryRequest.isUnique := 0.U
        memoryRequest.isClean := 0.U
      }
      when(isStoreWire){
        when(isMissWire){
          //ReadUnique
          memoryRequest.isUnique := 1.U
          memoryRequest.isClean := 0.U
        } .otherwise{
          //CleanUnique
          memoryRequest.isUnique := 1.U
          memoryRequest.isClean := 1.U
        }
      }

      when(!isBranchFailWire){
        cacheState := Mux(memoryRequest.ready, await, request)
      } .otherwise{
        cacheState := idle
      }    
    }
    is(await){
      loadData.ready := true.B
      when(loadData.valid){
        receivedCacheLine := loadData.data
        receivedResponse := loadData.response
      }
      
      when(coherencyReceived && !atomicsInOperationWire){
        cacheState := idle
      } .otherwise{
        when(loadData.valid){
          when(isStoreWire){
            cacheState := Mux(isBranchFailWire, idle, modify)
          } .otherwise{
            cacheState := Mux(isBranchFailWire, idle, dataOut)
          }
        } .otherwise {
          cacheState := await
        }
      }
    }
    is(resultOut) {
      val tagChunks = VecInit(Seq.tabulate(nway) { i =>
        tagBRAM.readData((i + 1) * (tagSection * 8) - 1, i * (tagSection * 8))
      })
      val hitTagWire = WireInit(PriorityEncoder(matchFoundReg))
      
      val newAddrWire = WireInit(tagChunks(hitTagWire)(tagSize - 1,0))
      val validBitWire = WireInit(tagChunks(hitTagWire)(tagSize))
      val dirtyBitWire = WireInit(tagChunks(hitTagWire)(tagSize + 1))
      val shareBitWire = WireInit(tagChunks(hitTagWire)(tagSize + 2))

      val isMissWire = !(matchFoundReg.reduce(_ | _) && validBitWire)
      
      memoryRequest.valid := true.B
      memoryRequest.address := 0.U

      when(isMissWire){
        memoryRequest.writeEn := false.B
        memoryRequest.data := 0.U
      } .otherwise{
        when(inputBuffer.dataReq){
          memoryRequest.writeEn := true.B
          memoryRequest.data := dataBRAMVec(hitTagWire).readData
          memoryRequest.isClean := Mux(inputBuffer.invalidateReq, !dirtyBitWire, 1.U)
          memoryRequest.isUnique := !shareBitWire
        } .otherwise {
          memoryRequest.data := 0.U //Line written for completeness
        }
      }

      when(memoryRequest.ready){
        when(isMissWire){
          cacheState:= idle
        } .otherwise {
          cacheState := modify
        }
      } .otherwise{
        cacheState := writeToMem
      }
    }
    is(writeToMem) {
      memoryRequest.valid := true.B
      memoryRequest.address := inputBuffer.address
      memoryRequest.writeEn := true.B
      val hitTagWire = WireInit(PriorityEncoder(matchFoundReg))
      memoryRequest.isUnique := 0.U
      //WriteBack
      memoryRequest.data := writeBackCacheLine
      memoryRequest.isClean := 0.U
     
      cacheState := Mux(memoryRequest.ready, idle, writeToMem)
    }
  }

  //--------------For input buffer state change--------------------//
  val priorCacheState = RegInit(idle)
  when(priorCacheState =/= cacheState){
    when(cacheState === idle){
      priorCacheState := idle
      freeInputBuffer := true.B
    } .otherwise{
      priorCacheState := cacheState
    }
  } .otherwise{
    when(cacheState === idle){
      freeInputBuffer := isBranchFailWire
    }
  }

  //-----------------------Results Out-----------------------------//
  responseOut.data := resultWire
  when(cacheState === dataOut){
    responseOut.valid := responseOut.ready && !isBranchFailWire
  }

  //-----------------------Commit FIFO-----------------------------//
  val commitFifo = Module(new commitFifo)

  commitFifo.dataIn.addr := 0.U
  commitFifo.dataIn.valid := false.B
  commitFifo.canAccept := false.B
  commitFifo.invalidateAddr := 0.U
  commitFifo.invalidateEnable := false.B
  commitFIFOFull := commitFifo.isFull

  when(isLoadWire && !isAtomicsWire && cacheState === modify){
    commitFifo.dataIn.addr := inputBuffer.address
    commitFifo.dataIn.valid := true.B
  }
  when(isCoherentWire && cacheState === idle){
    commitFifo.invalidateAddr := inputBuffer.address
    commitFifo.invalidateEnable := true.B
  }
  when(loadCommit.ready){
    commitFifo.canAccept := true.B
    loadCommit.state := commitFifo.dataOut.valid
    loadCommit.valid := !commitFifo.isEmpty
  }
  // when(isBranchFailWire){
  //   //Remove the most recent entry as that instruction has failed
  //   commitFifo.dequeueHead := true.B
  // }
}