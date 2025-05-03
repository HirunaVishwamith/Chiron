package cache_phase3

import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import cache_phase3._
import cache_phase3.constants._
import cache_phase3.ChiselUtils._
import os.size

//? After compiling
//TODO : In replay requests data not required field assertted then should not update cacheline
//TODO : Add cacheLine required track fifo
//TODO : -Enque if no match
//TODO : -Deque if match found

class cacheLookupUnit extends Module{
  val request = IO(new Bundle {
    val ready = Output(Bool())
    val holdInOrder = Output(Bool())
    val requestType = Input(UInt(2.W))
    val request = Input(new requestPipelineWire)
  })
  val toReplay = IO(new Bundle {
    val ready = Input(Bool())
    val request = Output(new requestPipelineWire)
  })
  val toWriteBack = IO(new Bundle {
    val ready = Input(Bool())
    val request = Output(new writeBackWire)
  })
  val toCoherency = IO(new Bundle {
    val ready = Input(Bool())
    val request = Output(new coherencyResponseWire)
  })
  val toResponse = IO(new Bundle {
    val request = Output(new requestPipelineWire)
  })
  val writeInstructionCommit = IO(new composableInterface)
  val branchOps = IO(new branchOps)
  //! Debug only
  val debug = IO(new debug)

  request.ready := false.B
  request.holdInOrder := false.B
  zeroInit(toResponse.request)
  zeroInit(toCoherency.request)
  zeroInit(toWriteBack.request)
  zeroInit(toReplay.request)

  //-------------------Operation Valid register-------------------//
  val operationValid = RegNext(request.ready && request.request.valid && request.request.branch.valid)

  //-----------------------Data BRAM------------------------------//
  val blockCount = nway                                   //No.of blocks
  val dataDepth = (cacheSize * 1024) / (lineSize * nway)
  val dataAddrWidth = log2Ceil(dataDepth)
  val dataDataWidth = lineSize * 8

  val dataBRAM = Seq.fill(nway)(Module(new moduleForwardingMemory(
    addrWidth = dataAddrWidth, 
    dataWidth = dataDataWidth, 
    depth = dataDepth
  )))

  // Initialize all BRAM instances
  dataBRAM.foreach { bram =>
    bram.rdAddr := 0.U
    bram.wrAddr := 0.U
    bram.wrData := 0.U
    bram.wrEna := false.B
  }

  // Create vector of forwarding memory interfaces
  val dataBRAMVec = VecInit(dataBRAM.map { bram =>
    val bundle = Wire(new Bundle {
      val rdData = UInt(dataDataWidth.W)
      val wrEna = Bool()
      val wrData = UInt(dataDataWidth.W)
      val rdAddr = UInt(dataAddrWidth.W)
      val wrAddr = UInt(dataAddrWidth.W)
    })
    bundle.rdData := 0.U
    bundle.wrEna := false.B
    bundle.wrData := 0.U
    bundle.rdAddr := 0.U
    bundle.wrAddr := 0.U
    bundle
  })

  // Connect forwarding memory interfaces to BRAM instances
  dataBRAM.zip(dataBRAMVec).foreach { case (bram, vec) =>
    vec.rdData := bram.rdData
    bram.rdAddr := vec.rdAddr
    bram.wrAddr := vec.wrAddr
    bram.wrData := vec.wrData
    bram.wrEna := vec.wrEna
  }

  //-----------------------Tag BRAM--------------------------------//
  //------Tag structure-------//
  // PLRU bit | Shared bit | Modified bit | Validity bit | Tag bits

  val tagSize = addrWidth - dataAddrWidth - log2Ceil(lineSize)    //Per cache line
  val tagDepth = dataDepth
  val tagAddrWidth = log2Ceil(tagDepth)
  val tagSection = 4 + tagSize    // nway no.of tag sections will be kept in one BRAM
  val tagDataWidth = (nway * tagSection)  //4 bits for flags

  val tagBRAM = Module(new moduleForwardingMemory(
    addrWidth = tagAddrWidth, 
    dataWidth = tagDataWidth, 
    depth = tagDepth
  ))
  tagBRAM.rdAddr := 0.U
  tagBRAM.wrData := 0.U
  tagBRAM.wrEna  := false.B
  tagBRAM.wrAddr := false.B
  
  //---------------------Reservation register---------------------//
  val reservationRegister = RegInit(0.U.asTypeOf(new Bundle {
    val address = UInt(addrWidth.W)
    val reserved = Bool()
    //To say if a word - 0.U, or a double word - 1.U
    val size = UInt(1.W)  
  }))
  val toReservationRegisterWire = WireDefault(false.B)

  //-----------------------Last Miss record-----------------------//
  val lastInorderMissRecordRegister = RegInit(0.U.asTypeOf(new requestPipelineWire))

  val toLastInorderMissRecordRegisterWire = WireDefault(false.B)  
  when(request.ready && request.request.valid && request.request.branch.valid && lastInorderMissRecordRegister.valid && lastInorderMissRecordRegister.branch.valid){
    val replayMatch = WireDefault(
      (request.request.address === lastInorderMissRecordRegister.address) &&
      (request.request.core.instruction === lastInorderMissRecordRegister.core.instruction) &&
      (request.request.core.robAddr === lastInorderMissRecordRegister.core.robAddr) &&
      (request.request.core.prfDest === lastInorderMissRecordRegister.core.prfDest) 
    )
    when(replayMatch){
      lastInorderMissRecordRegister.valid := false.B
    }
  }

  val lastSpeculativeMissRecordRegister = RegInit(0.U.asTypeOf(new requestPipelineWire))

  val toLastSpeculativetMissRecordRegisterWire = WireDefault(false.B)  
  when(request.ready && request.request.valid && request.request.branch.valid && lastSpeculativeMissRecordRegister.valid && lastSpeculativeMissRecordRegister.branch.valid){
    val replayMatch = WireDefault(
      (request.request.address === lastSpeculativeMissRecordRegister.address) &&
      (request.request.core.instruction === lastSpeculativeMissRecordRegister.core.instruction) &&
      (request.request.core.robAddr === lastSpeculativeMissRecordRegister.core.robAddr) &&
      (request.request.core.prfDest === lastSpeculativeMissRecordRegister.core.prfDest) 
    )
    when(replayMatch){
      lastSpeculativeMissRecordRegister.valid := false.B
    }
  }

  //Read Buffer
  val readBuffer = RegInit(0.U.asTypeOf(new requestPipelineWire))
  val requestType = RegInit(0.U(2.W))

  //* For output buffers need to only update the memory response buffer
  //* Other outputs with instructions (replay Buffer) goes to an FIFO

  //To replay buffer
  val replayBuffer = RegInit(0.U.asTypeOf(new requestPipelineWire))
  val toReplayValidWire = WireDefault(false.B)
  when(toReplay.ready){
    toReplay.request := replayBuffer
    replayBuffer.valid := false.B
  }

  val toMemoryResponseValidWire = WireDefault(false.B)

  val coherencyResponseBuffer = RegInit(0.U.asTypeOf(new coherencyResponseWire))
  val toCoherencyResponseValidWire = WireDefault(false.B)
  when(toCoherency.ready){
    toCoherency.request := coherencyResponseBuffer
    when(coherencyResponseBuffer.valid){
      coherencyResponseBuffer.valid := false.B
    }
  }

  val writeBackBuffer = RegInit(0.U.asTypeOf(new writeBackWire) )
  val toWriteBackValidWire = WireDefault(false.B)
  when(toWriteBack.ready){
    toWriteBack.request := writeBackBuffer 
    writeBackBuffer.valid := false.B
  }

  val writeCommitInstructionBuffer = RegInit(false.B)
  writeInstructionCommit.ready := writeCommitInstructionBuffer
  when(writeInstructionCommit.fired){
    writeCommitInstructionBuffer := false.B
  }

  //____________________Functional description_________________________//
  //Response out is always release in one clock cycle, so no ready signal
  request.ready := toReplay.ready && toWriteBack.ready && toCoherency.ready
  val islastInorderMissRecordRegisterValid = toLastInorderMissRecordRegisterWire || lastInorderMissRecordRegister.valid && lastInorderMissRecordRegister.branch.valid
  val islastSpeculativeMissRecordRegisterValid = toLastSpeculativetMissRecordRegisterWire || lastSpeculativeMissRecordRegister.valid && lastSpeculativeMissRecordRegister.branch.valid
  request.holdInOrder := (islastInorderMissRecordRegisterValid || islastSpeculativeMissRecordRegisterValid) && !writeCommitInstructionBuffer

  //Assigning addresses to the BRAMs
  val addrBeg = log2Ceil(lineSize)
  val addrEnd = dataAddrWidth - 1 + addrBeg
  dataBRAMVec.foreach { bram => bram.rdAddr := 0.U}
  tagBRAM.rdAddr := 0.U

  //Connecting correct addresses to the BRAMS as per request type
  when(request.request.valid && request.request.branch.valid){
    dataBRAMVec.foreach {bram => bram.rdAddr := request.request.address(addrEnd, addrBeg)}
    tagBRAM.rdAddr := request.request.address(addrEnd, addrBeg)

    readBuffer := request.request
    regWriteUpdate(readBuffer.branch, branchOps, request.request.branch)
    requestType := request.requestType
  } .otherwise {
    readBuffer.valid := false.B
  }

  //BranchOps of valid registers
    //BranchOps for valid registers
  when(replayBuffer.valid && replayBuffer.branch.valid){
    regRecordUpdate(replayBuffer.branch, branchOps)
  }
  //Input buffers
  when(readBuffer.valid && readBuffer.branch.valid && !operationValid){
    regRecordUpdate(readBuffer.branch, branchOps)
  }
  when(lastInorderMissRecordRegister.valid && lastInorderMissRecordRegister.branch.valid){
    regRecordUpdate(lastInorderMissRecordRegister.branch, branchOps)
  }
  when(lastSpeculativeMissRecordRegister.valid && lastSpeculativeMissRecordRegister.branch.valid){
    regRecordUpdate(lastSpeculativeMissRecordRegister.branch, branchOps)
  }

  //Main functions
  when(operationValid){
    //Setting control wires for request types
    val isReadWire = WireDefault(readBuffer.core.instruction(6,0) === "b0000011".U)
    val isWriteWire = WireDefault(readBuffer.core.instruction(6,0) === "b0100011".U)
    val isCoherentWire = WireDefault(requestType === "b11".U)
    val isAtomicsWire = WireDefault((readBuffer.core.instruction(6,0) === "b0101111".U))
    val isLRWire = WireDefault(readBuffer.core.instruction(31,27) === "b00010".U && isAtomicsWire)
    val isSCWire = WireDefault(readBuffer.core.instruction(31,27) === "b00011".U && isAtomicsWire)
    val isAtmoicReadWire = WireDefault(isAtomicsWire && !readBuffer.writeData.valid && !(isSCWire || isLRWire))
    val isAtmoicWriteWire = WireDefault(isAtomicsWire && readBuffer.writeData.valid && !(isSCWire || isLRWire))
    val isLRReadWire = WireDefault(isLRWire && !readBuffer.writeData.valid)
    val isLRWriteWire = WireDefault(isLRWire && readBuffer.writeData.valid)
    val isSCReadWire = WireDefault(isSCWire && !readBuffer.writeData.valid)
    val isSCWriteWire = WireDefault(isSCWire && readBuffer.writeData.valid)
    val requiredResponseWire = WireInit(0.U(2.W))

    //Getting tagBRAM results
    val tagChunks = VecInit(Seq.tabulate(nway) { i =>
      tagBRAM.rdData(((i + 1) * (tagSection)) - 1, i * (tagSection))
    })
    // Compare each chunk with the size of tagSection for the request address
    val matchFoundVec = WireDefault(VecInit(Seq.fill(nway)(false.B)))
    for (i <- 0 until nway) {
      matchFoundVec(i) := (tagChunks(i)(tagSize - 1, 0) 
                        === readBuffer.address(addrWidth - 1, dataAddrWidth + log2Ceil(lineSize)))
    }
    val hitTagWire = WireDefault(PriorityEncoder(matchFoundVec))
    val validBitWire = WireDefault(tagChunks(hitTagWire)(tagSize))
    val shareBitWire = WireDefault(tagChunks(hitTagWire)(tagSize + 2))
    val dirtyBitWire = WireDefault(tagChunks(hitTagWire)(tagSize + 1))
    val PLRUBitWire = WireDefault(tagChunks(hitTagWire)(tagSize + 3))
    
    val isDirtyWire = WireDefault(dirtyBitWire && validBitWire)
    val isSharedWire = WireDefault(shareBitWire && validBitWire)
    val isDataMissWire = WireDefault(!(matchFoundVec.reduce(_ | _) && validBitWire))
    val isPermissionMiss = WireDefault(!isDataMissWire && isSharedWire)
    val isReplayValidWire = WireDefault(requestType === "b10".U) //&& !readBuffer.cacheLine.invalidated)

    //Updating wires
    val newtagChunks = VecInit(Seq.tabulate(nway) { i =>
      tagBRAM.rdData((i + 1) * (tagSection) - 1, i * (tagSection))
    })
    val newAddrWire =  WireDefault(tagChunks(hitTagWire)(tagSize - 1,0))
    val newValidBitWire =  WireDefault(tagChunks(hitTagWire)(tagSize))
    val newDirtyBitWire =  WireDefault(tagChunks(hitTagWire)(tagSize + 1))
    val newShareBitWire =  WireDefault(tagChunks(hitTagWire)(tagSize + 2))
    val newPLRUBitWire =  WireDefault(tagChunks(hitTagWire)(tagSize + 3))

    //CacheLineUpdate wire
    val readCacheLineUpdate = WireDefault(isReplayValidWire && isDataMissWire && readBuffer.cacheLine.valid)
    val writeCacheLineUpdate = WireDefault(isReplayValidWire && readBuffer.cacheLine.valid)
    //DataBRAMs
    val cacheLineChoosen = WireDefault(dataBRAMVec(PriorityEncoder(matchFoundVec)).rdData)
    when(isReadWire){
      cacheLineChoosen := Mux(readCacheLineUpdate, readBuffer.cacheLine.cacheLine, dataBRAMVec(PriorityEncoder(matchFoundVec)).rdData)
    }
    when(isLRReadWire || isAtmoicReadWire || isWriteWire || isAtmoicWriteWire){
      cacheLineChoosen := Mux(writeCacheLineUpdate, readBuffer.cacheLine.cacheLine, dataBRAMVec(PriorityEncoder(matchFoundVec)).rdData)
    }
    val writeChunks = VecInit(Seq.tabulate(lineSize * 8 * 2 / dataWidth) { i =>
      cacheLineChoosen((i + 1) * (32) - 1, i * (32))
    })
    val newWriteChunks = VecInit(Seq.tabulate(lineSize * 8 * 2 / dataWidth) { i =>
      cacheLineChoosen((i + 1) * (32) - 1, i * (32))
    })
    val wordWrite = writeChunks(readBuffer.address(5,2))
    val doubleWordWrite = Cat(writeChunks((readBuffer.address(5,3) ## 1.U)),(writeChunks(readBuffer.address(5,3) ## 0.U)))
    val writeByteChunks = VecInit.tabulate(8)(i => doubleWordWrite(8 * (i + 1) - 1, 8 * i))

    //PLRU logic
    val PLRUSetWire = WireDefault(VecInit(tagChunks.map(chunk => chunk(tagSize + 3))))
    val flippedPLRUSetWire = WireDefault(VecInit(PLRUSetWire.map(bit => ~bit)))
    val replacingset = PriorityEncoder(flippedPLRUSetWire)

    val isUpdateValidWire = WireDefault(tagChunks(replacingset)(tagSize))
    val isUpdateDirtyWire = WireDefault(tagChunks(replacingset)(tagSize + 1))

    //read 
    when(isReadWire){
      when(!isDataMissWire){//Hit
        newPLRUBitWire := Mux(PLRUSetWire.reduce(_ & _), 0.U, 1.U)
      } .elsewhen(isReplayValidWire){
        newPLRUBitWire := Mux(PLRUSetWire.reduce(_ & _), 0.U, 1.U)
        newValidBitWire := 1.U
        newShareBitWire := readBuffer.cacheLine.response(1)
        newDirtyBitWire := readBuffer.cacheLine.response(0)
        newAddrWire := readBuffer.address(addrWidth - 1, dataAddrWidth + log2Ceil(lineSize))
        for (i <- 0 until writeChunks.length) {
          writeChunks(i) := readBuffer.cacheLine.cacheLine((i + 1) * 32 - 1, i * 32)
        }
      }
    }  
    when(isLRReadWire || isAtmoicReadWire){
      when(!isPermissionMiss && !isDataMissWire){//Hit
        newPLRUBitWire := Mux(PLRUSetWire.reduce(_ & _), 0.U, 1.U)
      } .elsewhen(isReplayValidWire){
        newPLRUBitWire := Mux(PLRUSetWire.reduce(_ & _), 0.U, 1.U)
        newValidBitWire := 1.U
        newShareBitWire := 0.U //readBuffer.cacheLine.response(1)
        newDirtyBitWire := readBuffer.cacheLine.response(0)
        newAddrWire := readBuffer.address(addrWidth - 1, dataAddrWidth + log2Ceil(lineSize))
        for (i <- 0 until writeChunks.length) {
          writeChunks(i) := readBuffer.cacheLine.cacheLine((i + 1) * 32 - 1, i * 32)
        }
      }
    }   

    //Write related
    val result32 = WireDefault(0.U(32.W))
    val result64 = WireDefault(0.U(64.W)) 
    when(isWriteWire ||  isAtmoicWriteWire || isSCWriteWire){
      when(!isPermissionMiss && !isDataMissWire){
        newDirtyBitWire := 1.U
        newPLRUBitWire := Mux(PLRUSetWire.reduce(_ & _), 0.U, 1.U)
      } .elsewhen(isReplayValidWire && isDataMissWire){
        newValidBitWire := 1.U
        newDirtyBitWire := 1.U
        newShareBitWire := 0.U //readBuffer.cacheLine.response(1)//Can put to 0.U       
        newPLRUBitWire := Mux(PLRUSetWire.reduce(_ & _), 0.U, 1.U)
        newAddrWire := readBuffer.address(addrWidth - 1, dataAddrWidth + log2Ceil(lineSize))
        for (i <- 0 until writeChunks.length) {
          writeChunks(i) := readBuffer.cacheLine.cacheLine((i + 1) * 32 - 1, i * 32)
        }
        //The data available but permission miss situation
      } .elsewhen(isReplayValidWire && isPermissionMiss){
        newValidBitWire := 1.U
        newShareBitWire := 0.U //readBuffer.cacheLine.response(1)         
        newAddrWire := readBuffer.address(addrWidth - 1, dataAddrWidth + log2Ceil(lineSize))
      }
      when(isAtmoicWriteWire){
        when(readBuffer.core.instruction(14,12) === "b010".U){
          switch(readBuffer.core.instruction(31,27)){
            is("b00001".U){result32 := readBuffer.writeData.data(31,0)}  //SWAP
            is("b00000".U){result32 := wordWrite + readBuffer.writeData.data(31,0)}  //ADD
            is("b00100".U){result32 := wordWrite ^ readBuffer.writeData.data(31,0)}  //XOR
            is("b01100".U){result32 := wordWrite & readBuffer.writeData.data(31,0)}  //AND
            is("b01000".U){result32 := wordWrite | readBuffer.writeData.data(31,0)}  //OR
            is("b10000".U){result32 := Mux(wordWrite.asSInt < readBuffer.writeData.data(31,0).asSInt, wordWrite, readBuffer.writeData.data(31,0))}  //MIN
            is("b10100".U){result32 := Mux(wordWrite.asSInt > readBuffer.writeData.data(31,0).asSInt, wordWrite, readBuffer.writeData.data(31,0))}  //MAX
            is("b11000".U){result32 := Mux(wordWrite.asUInt < readBuffer.writeData.data(31,0).asUInt, wordWrite, readBuffer.writeData.data(31,0))}  //MINU
            is("b11100".U){result32 := Mux(wordWrite.asUInt > readBuffer.writeData.data(31,0).asUInt, wordWrite, readBuffer.writeData.data(31,0))}  //MAXU
          }
          newWriteChunks(readBuffer.address(5,2)) := result32
        }
        when(readBuffer.core.instruction(14,12) === "b011".U){
          switch(readBuffer.core.instruction(31,27)){
            is("b00001".U){result64 := readBuffer.writeData.data}  //SWAP
            is("b00000".U){result64 := doubleWordWrite + readBuffer.writeData.data}  //ADD
            is("b00100".U){result64 := doubleWordWrite ^ readBuffer.writeData.data}  //XOR
            is("b01100".U){result64 := doubleWordWrite & readBuffer.writeData.data}  //AND
            is("b01000".U){result64 := doubleWordWrite | readBuffer.writeData.data}  //OR
            is("b10000".U){result64 := Mux(doubleWordWrite.asSInt < readBuffer.writeData.data.asSInt, doubleWordWrite, readBuffer.writeData.data)}  //MIN
            is("b10100".U){result64 := Mux(doubleWordWrite.asSInt > readBuffer.writeData.data.asSInt, doubleWordWrite, readBuffer.writeData.data)}  //MAX
            is("b11000".U){result64 := Mux(doubleWordWrite.asUInt < readBuffer.writeData.data.asUInt, doubleWordWrite, readBuffer.writeData.data)}  //MINU
            is("b11100".U){result64 := Mux(doubleWordWrite.asUInt > readBuffer.writeData.data.asUInt, doubleWordWrite, readBuffer.writeData.data)}  //MAXU
          }
          newWriteChunks(readBuffer.address(5,2)) := result64(31,0)
          newWriteChunks(readBuffer.address(5,2) + 1.U) := result64(63,32)
        }
      } .otherwise {
          switch(readBuffer.core.instruction(13,12)){
            is("b00".U){for (i <- 0 until 1) {writeByteChunks(readBuffer.address(2, 0) + i.U) := readBuffer.writeData.data(8 * (i + 1) - 1, 8 * i)}}
            is("b01".U){for (i <- 0 until 2) {writeByteChunks(readBuffer.address(2, 1)*2.U + i.U) := readBuffer.writeData.data(8 * (i + 1) - 1, 8 * i)}}
            is("b10".U){for (i <- 0 until 4) {writeByteChunks(readBuffer.address(2)*4.U + i.U) := readBuffer.writeData.data(8 * (i + 1) - 1, 8 * i)}}
            is("b11".U){for (i <- 0 until 8) {writeByteChunks(i.U) := readBuffer.writeData.data(8 * (i + 1) - 1, 8 * i)}}
          }
          newWriteChunks(readBuffer.address(5, 3)*2.U) := Cat(writeByteChunks.slice(0, 4).reverse)
          newWriteChunks(readBuffer.address(5, 3)*2.U + 1.U) := Cat(writeByteChunks.slice(4, 8).reverse)
      }  
    }
    when(isCoherentWire){
      when(readBuffer.cacheLine.response(1)){
        newValidBitWire := 0.U
        newPLRUBitWire := 0.U
        newShareBitWire := 0.U
        newDirtyBitWire := 0.U
      } .otherwise{
        newShareBitWire := Mux(readBuffer.cacheLine.response(0) && !isDataMissWire, 1.U, shareBitWire)
      }
    }
    val isReservationMatch32 = WireDefault((reservationRegister.address((addrWidth-1),2)) === (readBuffer.address((addrWidth-1),2)))
    val isReservationMatch64 = WireDefault((reservationRegister.address((addrWidth-1),3)) === (readBuffer.address((addrWidth-1),3)))
    val isReservationMatch = Mux(reservationRegister.size.asBool, isReservationMatch64, isReservationMatch32)
    when(reservationRegister.reserved && (isCoherentWire || isWriteWire ||  isAtmoicWriteWire)){
      switch(reservationRegister.size){
        is(0.U){reservationRegister.reserved := !isReservationMatch32}  
        is(1.U){reservationRegister.reserved := !isReservationMatch64}     
      }
    }

    //BRAM update
    val updatingSet = Mux(isDataMissWire, replacingset, hitTagWire)
    when(PLRUSetWire.reduce(_ & _)){
      for (i <- 0 until nway) {
        newtagChunks(i) := tagChunks(i) & ~(1.U << (tagSize + 3))
      }
    }
    newtagChunks(updatingSet) := Cat(newPLRUBitWire, newShareBitWire, newDirtyBitWire, newValidBitWire, newAddrWire)
    val dataBRAMUpdateWire = WireDefault(false.B)
    val tagBRAMUpdateWire = WireDefault(false.B)
    
    tagBRAM.wrEna := tagBRAMUpdateWire
    tagBRAM.wrData := newtagChunks.reverse.reduce(Cat(_, _))
    tagBRAM.wrAddr := readBuffer.address(addrEnd, addrBeg)

    dataBRAMVec(updatingSet).wrEna := dataBRAMUpdateWire
    dataBRAMVec(updatingSet).wrData := newWriteChunks.reverse.reduce(Cat(_, _))
    dataBRAMVec(updatingSet).wrAddr := readBuffer.address(addrEnd, addrBeg)

    //Setting control signals on deciding which buffer should data flow
    when(isReadWire){
      when(isReplayValidWire && isDataMissWire || !isDataMissWire){ //Hit
        toMemoryResponseValidWire := true.B
        tagBRAMUpdateWire:= true.B
        toWriteBackValidWire := (isUpdateDirtyWire && isUpdateValidWire) && isReplayValidWire && isDataMissWire
        dataBRAMUpdateWire := readCacheLineUpdate
      } .otherwise {
        toReplayValidWire := true.B
        requiredResponseWire := "b00".U
        toLastSpeculativetMissRecordRegisterWire := true.B
      }
    }
    when(isLRReadWire || isAtmoicReadWire){
      when(isReplayValidWire || (!isPermissionMiss && !isDataMissWire)){ //Hit
        toMemoryResponseValidWire := true.B
        tagBRAMUpdateWire:= true.B
        toReservationRegisterWire := true.B
        toWriteBackValidWire := (isUpdateDirtyWire && isUpdateValidWire) && isReplayValidWire && !isPermissionMiss
        dataBRAMUpdateWire := writeCacheLineUpdate
      } .otherwise {
        toReplayValidWire := true.B
        requiredResponseWire := Mux(isPermissionMiss && !isDataMissWire, "b11".U, "b01".U)
        toLastInorderMissRecordRegisterWire := true.B
      }
    }
    when(isLRWriteWire){writeCommitInstructionBuffer := true.B}
    when(isCoherentWire){
      toCoherencyResponseValidWire := true.B
      tagBRAMUpdateWire:= !isDataMissWire
    }
    when(isWriteWire || isAtmoicWriteWire){
      when(isReplayValidWire || (!isPermissionMiss && !isDataMissWire)){
        toWriteBackValidWire := (isUpdateDirtyWire && isUpdateValidWire) && isReplayValidWire && !isPermissionMiss
        tagBRAMUpdateWire:= true.B
        dataBRAMUpdateWire := true.B
        writeCommitInstructionBuffer := true.B
      } .otherwise {
        toReplayValidWire := true.B
        requiredResponseWire := Mux(isPermissionMiss && !isDataMissWire, "b11".U, "b01".U)
        toLastInorderMissRecordRegisterWire := true.B
      }
    }
    when(isSCReadWire){toMemoryResponseValidWire := true.B}
    when(isSCWriteWire){
      reservationRegister.reserved := false.B
      when(reservationRegister.reserved && isReservationMatch){
        toWriteBackValidWire := isDirtyWire  &&  isReplayValidWire && isDataMissWire && !isPermissionMiss
        tagBRAMUpdateWire:= true.B
        dataBRAMUpdateWire := true.B
      }
      writeCommitInstructionBuffer := true.B
    }

    //Setting the dataOut
    val responseResultWire = WireDefault(0.U(dataWidth.W))
    val doubleWordSize = 64
    val numChunks = lineSize * 8 / doubleWordSize
    val doubleWordChunks = VecInit(Seq.tabulate(numChunks) { i =>
      cacheLineChoosen((i + 1) * doubleWordSize - 1, i * doubleWordSize)
    })
    val doubleWordChoosen = doubleWordChunks(readBuffer.address(log2Ceil(lineSize) - 1, 3))
    val shiftAmount = (1.U << readBuffer.core.instruction(13,12).asUInt)
    val section = (1.U << (8.U*shiftAmount)) - 1.U 
    val byteChunks = VecInit(Seq.tabulate(8) { i =>
      doubleWordChoosen((i + 1) * 8 - 1, i * 8) // 8-bit slices
    })
    val byteChoosed     = byteChunks(readBuffer.address(2,0))
    val halfwordChoosed = Cat(byteChunks(2.U * readBuffer.address(2,1) + 1.U),byteChunks(2.U * readBuffer.address(2,1)))
    val wordChoosed     = Cat(byteChunks(4.U * readBuffer.address(2) + 3.U),byteChunks(4.U * readBuffer.address(2) + 2.U), 
                              byteChunks(4.U * readBuffer.address(2) + 1.U),byteChunks(4.U * readBuffer.address(2)))
    
    switch(readBuffer.core.instruction(13, 12)){
      is("b00".U){responseResultWire := Mux(readBuffer.core.instruction(14),byteChoosed,
                                    Cat(Fill((dataWidth-1*8),byteChoosed(7)),byteChoosed))}
      is("b01".U){responseResultWire := Mux(readBuffer.core.instruction(14),halfwordChoosed,
                                    Cat(Fill((dataWidth-2*8),halfwordChoosed(15)),halfwordChoosed))}
      is("b10".U){responseResultWire := Mux(readBuffer.core.instruction(14),wordChoosed,
                                    Cat(Fill((dataWidth-4*8),wordChoosed(31)),wordChoosed))}
      is("b11".U){responseResultWire := Mux(readBuffer.core.instruction(14),"x0".U,
                                    doubleWordChoosen)}
    }
    when(isSCReadWire){
      responseResultWire := Mux(reservationRegister.reserved && isReservationMatch, 0.U, 1.U)
    } 

    //____________________Output Buffer update___________________//
    //Replay
    when(toReplayValidWire && readBuffer.branch.valid){
      replayBuffer := readBuffer
      replayBuffer.cacheLine.response := requiredResponseWire
      regWriteUpdate(replayBuffer.branch, branchOps, readBuffer.branch)
    }

    //Response
    when(toMemoryResponseValidWire && readBuffer.branch.valid){
      toResponse.request := readBuffer
      toResponse.request.writeData.data := responseResultWire
      regWriteUpdate(toResponse.request.branch, branchOps, readBuffer.branch)
    }

    //Coherency
    when(toCoherencyResponseValidWire){
      coherencyResponseBuffer.valid := toCoherencyResponseValidWire
      when(readBuffer.cacheLine.response(0)){
        coherencyResponseBuffer.cacheLine := Mux(!isDataMissWire, dataBRAMVec(hitTagWire).rdData, 0.U)
        coherencyResponseBuffer.dataValid := !isDataMissWire
        coherencyResponseBuffer.response := newShareBitWire ## Mux(readBuffer.cacheLine.response(1), isDirtyWire, 0.U)
      } .otherwise{
        coherencyResponseBuffer.cacheLine := 0.U
        coherencyResponseBuffer.dataValid := false.B
      }
    }

    //WriteBack
    when(toWriteBackValidWire){
      writeBackBuffer.valid := toWriteBackValidWire
      writeBackBuffer.address := Cat(tagChunks(updatingSet), readBuffer.address(addrEnd, addrBeg), 0.U(log2Ceil(lineSize).W))
      writeBackBuffer.data := dataBRAMVec(updatingSet).rdData
    }
    
    //Last Miss Memory Record
    when(toLastInorderMissRecordRegisterWire && readBuffer.branch.valid){  
      lastInorderMissRecordRegister := readBuffer
      regWriteUpdate(lastInorderMissRecordRegister.branch, branchOps, readBuffer.branch)
    }
    when(toLastSpeculativetMissRecordRegisterWire && readBuffer.branch.valid){  
      lastSpeculativeMissRecordRegister := readBuffer
      regWriteUpdate(lastSpeculativeMissRecordRegister.branch, branchOps, readBuffer.branch)
    }

    //Reservation register
    when(toReservationRegisterWire){
      reservationRegister.reserved := toReservationRegisterWire
      reservationRegister.address := readBuffer.address
      reservationRegister.size := readBuffer.core.instruction(12)
    }
  }

  //! Debug only
  debug.request := readBuffer
  debug.isServicing := operationValid
  debug.rdAddr := tagBRAM.rdAddr
  debug.tagData := tagBRAM.rdData
  debug.dataBRAM0 := dataBRAMVec(0).rdData
  debug.dataBRAM1 := dataBRAMVec(1).rdData
  debug.dataBRAM2 := dataBRAMVec(2).rdData
  debug.dataBRAM3 := dataBRAMVec(3).rdData
}
