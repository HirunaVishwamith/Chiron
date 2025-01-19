import pipeline.fifo._
import pipeline.ports._
import chisel3._
import chisel3.util._
import chisel3.experimental.BundleLiterals._
import chisel3.experimental.IO
import common.configuration


// definition of all ports can be found here


/**
 * Question: does the fetch unit need to communicate the predicted next address along with
 * instruction to the decode unit?
 * It will be 64-bit that will be rarely used(Only for branch instructions it will be used)
 */

/**
 * Functionality - decode unit communicates the pc of the instruction
 * its epecting through the decodeIssue.expeted port.
 *
 * Results of branch predictions are returned on branchRes port.
 *
 * cache is used to access instruction memory.
 *
 * additional information on ports can be found on common/ports.scala
 */
class predictor(val depth: Int) extends Module {
  val io = IO(new Bundle {
    val branchres = new branchResToFetch
    val curr_pc = Input(UInt(64.W))
    val next_pc = Output(UInt(64.W))
  })

  // Extract addresses and indexes
  val btb_addr = io.curr_pc(log2Down(depth) + 1, 2)
  val tag = io.curr_pc(63, log2Down(depth) + 2)
  val result_addr = io.branchres.pc(log2Down(depth) + 1, 2)
  val result_tag = io.branchres.pc(63, log2Down(depth) + 2)

  // Define tables and valid bits
  val btb = Mem(depth, UInt(64.W))
  val counters = Mem(depth, UInt(2.W))
  val valid_bits = RegInit(VecInit(Seq.fill(depth)(0.U(1.W))))
  val tag_store = Mem(depth, UInt((62-log2Down(depth)).W))

  // Update btb ,counters and valid bits
  when(io.branchres.fired){
    when(io.branchres.isBranch) {
      valid_bits(result_addr) := 1.U
      tag_store(result_addr) := result_tag
      btb(result_addr) := io.branchres.pcAfterBrnach
      // Update counters
      when(io.branchres.branchTaken){
        when(!(counters(result_addr) === 3.U)){
          counters(result_addr) := counters(result_addr) + 1.U
        }
      }.otherwise{
        when(!(counters(result_addr) === 0.U)) {
          counters(result_addr) := counters(result_addr) - 1.U
        }
      }
    }.otherwise{
      when(io.branchres.branchTaken){
        valid_bits(result_addr) := 0.U
      }
    }
  }

  val btb_hit = valid_bits(btb_addr)===1.U && tag_store(btb_addr) === tag

  io.next_pc := Mux(btb_hit && counters(btb_addr)(1) === 1.U, btb(btb_addr), io.curr_pc + 4.U)

  io.branchres.ready := 1.B

  //debug signals
  val btbhitOut = IO(Output(Bool()))
  btbhitOut := btb_hit
}



abstract class global_predictor(val depth:Int) extends Module{
  class shiftReg(depth: Int) extends Module {
    val in = IO(Input(Bool()))
    val en = IO(Input(Bool()))
    val output = IO(Vec(log2Ceil(depth)/2, Output(Bool())))

    val shiftregs = Seq.fill(log2Ceil(depth)/2)(RegInit(0.B))

    when(en) {
      shiftregs(0) := in
      for (i <- 1 until log2Ceil(depth)/2) {
        shiftregs(i) := shiftregs(i - 1)
      }
    }

    shiftregs.zipWithIndex.foreach(c => output(c._2) := c._1)

  }

  class updateShiftReg(depth: Int) extends shiftReg(depth){
    val updateVals = IO(Input(Vec(log2Ceil(depth)/2,Bool())))
    val update = IO(Input(Bool()))
    when(update){
      shiftregs.zipWithIndex.foreach(c => c._1 := updateVals(c._2))
    }
  }

}

class gshare_predictor(val counter_depth: Int, val btb_size:Int) extends global_predictor(counter_depth) {
  val io = IO(new Bundle {
    val branchres = new branchResToFetch
    val curr_pc = Input(UInt(64.W))
    val next_pc = Output(UInt(64.W))
  })

  // Global history registers
  val correct_history = Module(new shiftReg(counter_depth))
  val predicted_history = Module(new updateShiftReg(counter_depth))
  correct_history.en := io.branchres.fired && io.branchres.isBranch
  correct_history.in := io.branchres.branchTaken

  for (i<- 0 until log2Ceil(counter_depth)/2){
    predicted_history.updateVals(i) := correct_history.output(i)
  }

  // Hash history and pc to get counter index
  val counterIndex_pred = Cat(io.curr_pc(log2Ceil(counter_depth)-(log2Ceil(counter_depth)/2)+1,2),io.curr_pc((log2Ceil(counter_depth)/2)+1,2) ^ Reverse(predicted_history.output.asUInt))
  val counterIndex_train = Cat(io.branchres.pc(log2Ceil(counter_depth)-(log2Ceil(counter_depth)/2)+1,2),io.branchres.pc((log2Ceil(counter_depth)/2)+1,2) ^ Reverse(correct_history.output.asUInt))
  // Check when request leaves fetch
  val requestSent = IO(Input(Bool()))

  // Branch mispredicted
  val mispredicted = IO(Input(Bool()))

  // Extract addresses and indexes
  val btb_addr = io.curr_pc(log2Down(btb_size) + 1, 2)
  val tag = io.curr_pc(63, log2Down(btb_size) + 2)
  val result_addr = io.branchres.pc(log2Down(btb_size) + 1, 2)
  val result_tag = io.branchres.pc(63, log2Down(btb_size) + 2)

  // Define tables and valid bits
  val btb = Mem(btb_size, UInt(64.W))
  val counters = Mem(counter_depth, UInt(2.W))
  val valid_bits = RegInit(VecInit(Seq.fill(btb_size)(0.U(1.W))))
  val tag_store = Mem(btb_size, UInt((62-log2Down(btb_size)).W))

  // Update btb ,counters and valid bits
  when(io.branchres.fired){
    when(io.branchres.isBranch) {
      valid_bits(result_addr) := 1.U
      tag_store(result_addr) := result_tag
      btb(result_addr) := io.branchres.pcAfterBrnach
      // Update counters
      when(io.branchres.branchTaken){
        when(!(counters(counterIndex_train) === 3.U)){
          counters(counterIndex_train) := counters(counterIndex_train) + 1.U
        }
      }.otherwise{
        when(!(counters(counterIndex_train) === 0.U)) {
          counters(counterIndex_train) := counters(counterIndex_train) - 1.U
        }
      }
    }.otherwise{
      when(io.branchres.branchTaken){
        valid_bits(result_addr) := 0.U
      }
    }
  }

  val btb_hit = valid_bits(btb_addr)===1.U && tag_store(btb_addr) === tag
  val prediction = counters(counterIndex_pred)(1) === 1.U

  predicted_history.en := !mispredicted && btb_hit && requestSent
  predicted_history.in := prediction

  io.next_pc := Mux(btb_hit && prediction, btb(btb_addr), io.curr_pc + 4.U)

  // Handle misprediction
  when (mispredicted){
    predicted_history.update := 1.B
    when(correct_history.en){
      predicted_history.updateVals(0) := io.branchres.branchTaken
      for (i<-1 until log2Ceil(counter_depth)/2){
        predicted_history.updateVals(i) := correct_history.output(i)
      }
    }.otherwise{
      for (i <- 0 until log2Ceil(counter_depth) / 2) {
        predicted_history.updateVals(i) := correct_history.output(i)
      }
    }
  }.otherwise{
    predicted_history.update := 0.B
  }

  io.branchres.ready := 1.B

}

//class predictor extends Module {
//  val io = IO(new Bundle {
//    val branchres = new branchResToFetch
//    val curr_pc = Input(UInt(64.W))
//    val next_pc = Output(UInt(64.W))
//  })
//  io.next_pc := io.curr_pc + 4.U
//  io.branchres.ready := 1.B
//}


class fetch(val fifo_size: Int) extends Module {
  /**
   * Inputs and Outputs of the module
   */

  // interface with cache
  val cache = IO(new Bundle {
    val req   = DecoupledIO(UInt(64.W)) // address is 64 bits wide for 64-bit machine
    val resp  = Flipped(DecoupledIO(UInt(32.W))) // instructions are 32 bits wide
  })

  // issuing instructions to pc
  val toDecode = IO(new issueInstrFrmFetch)

  // receiving results of branches in order
  val branchRes   = IO(new branchResToFetch)

  // request to implement fence_i
  val carryOutFence = IO(new composableInterface)

  // request to update cache lines
  // once fired all pending requests to cache are invalidated
  // updateAllCachelines and cache.req **cannot** be ready at the same time
  val updateAllCachelines = IO(new composableInterface)

  // after updateAllCachelines is fired, this should be ready
  // will fire once all cachelines in I$ is updated
  val cachelinesUpdatesResp = IO(new composableInterface)

  /**
   * Internal of the module goes here
   */

  //register defs
  val PC = RegInit(configuration.instructionBase.U(64.W))
  val redirect_bit= RegInit(0.U(1.W))
  val handle_fenceI= RegInit(0.U(1.W))
  val clear_cache_req= RegInit(0.U(1.W))
  val cache_cleared= RegInit(0.U(1.W))
  val fence_pending= RegInit(0.U(1.W))


  // initialize BHT and fifo buffer
  val predictor = Module(new gshare_predictor(2048,256))
  predictor.io.branchres <> branchRes
  predictor.io.curr_pc := PC
  val PC_fifo = Module(new regFifo(UInt(128.W), fifo_size))

  //Connect PC fifo
  PC_fifo.io.enq.bits := PC
  PC_fifo.io.enq.valid := cache.req.valid & cache.req.ready
  PC_fifo.io.deq.ready := cache.resp.valid & cache.resp.ready
  toDecode.pc := PC_fifo.io.deq.bits

  //fence.I
  val is_fenceI = (toDecode.instruction(6,2) === "b00011".U) & (toDecode.instruction(14,13) === 0.U) & (toDecode.fired)
  when (handle_fenceI===1.U){
    PC_fifo.reset:=1.U
  }
  when (handle_fenceI === 0.U){
    handle_fenceI := is_fenceI
  }.otherwise{
    when (clear_cache_req===0.U & cache_cleared === 0.U & fence_pending===0.U){
      handle_fenceI := 0.U
    }
  }

  when (clear_cache_req===0.U & !handle_fenceI===1.U){
    clear_cache_req := is_fenceI
  }.elsewhen(updateAllCachelines.fired){
    clear_cache_req := 0.U
  }

  when(cache_cleared === 0.U & !handle_fenceI===1.U) {
    cache_cleared := is_fenceI
  }.elsewhen(cachelinesUpdatesResp.fired) {
    cache_cleared := 0.U
  }

  carryOutFence.ready := fence_pending

  when (fence_pending===0.U & !handle_fenceI===1.U){
    fence_pending:= is_fenceI
  }.elsewhen(carryOutFence.fired){
    fence_pending:=0.U
  }

  //redirect signal calc
  val redirect = Wire(Bool())
  val coherent = Wire(Bool()) //leon coherency

  redirect := !(toDecode.expected.pc === toDecode.pc) & toDecode.expected.valid
  coherent := toDecode.expected.coherency //leon coherency

  //redirect bit logic
  when(redirect_bit===0.U & PC_fifo.io.deq.valid){
    redirect_bit := redirect
  }.elsewhen (PC_fifo.io.deq.valid === 0.U){
    redirect_bit := 0.U
  }


  //PC update logic
  when(redirect_bit===1.U) {
    PC := toDecode.expected.pc
  }.elsewhen(is_fenceI) {
    PC := PC_fifo.io.deq.bits + 4.U
  }.elsewhen(cache.req.valid & cache.req.ready) {
    PC := predictor.io.next_pc
  }
  cache.req.bits := PC

  //ready valid signal logic
  cache.req.valid := redirect_bit === 0.U & PC_fifo.io.enq.ready & !is_fenceI & !(handle_fenceI===1.U)
  cache.resp.ready := (redirect_bit===1.U || toDecode.fired) & !(handle_fenceI)
  updateAllCachelines.ready := clear_cache_req
  cachelinesUpdatesResp.ready := cache_cleared

  when (redirect || redirect_bit===1.U || !cache.resp.valid || !PC_fifo.io.deq.valid || handle_fenceI===1.U){
    toDecode.ready := 0.B
  }.otherwise{
    toDecode.ready := 1.B
  }

  toDecode.instruction := cache.resp.bits

  predictor.requestSent := cache.req.valid && cache.req.ready

  // detect msprediction
  val misPredicted = RegInit(0.B)
  when (redirect_bit===0.U & PC_fifo.io.deq.valid){
    misPredicted := redirect && !coherent  //leon coherency
  }.otherwise{
    misPredicted := 0.B
  }

  predictor.mispredicted := misPredicted
  when(handle_fenceI.asBool) { cache.resp.ready := true.B }


  //  val branchOut = IO(Output(new Bundle() {
  //    val fired = Bool()
  //    val pc = UInt(64.W)
  //    val isBranch = Bool()
  //}))
  //  branchOut.pc := branchRes.pc
  //  branchOut.fired := branchRes.fired
  //  branchOut.isBranch := branchRes.isBranch
  //printf(p"${cache} ${updateAllCachelines} ${cachelinesUpdatesResp} ${carryOutFence} ${fence_pending}\n")
}


