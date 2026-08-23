// Set-associative instruction cache storage.
//
// Geometry is entirely parametric: 2^line_width sets x 2^way_width ways x
// 2^offset_width instructions. The defaults below are overridden from
// common/configuration.scala (coreConfiguration.iCache*), which is the single
// source of truth -- 7/2/4 there means 128 sets x 4 ways x 16 words = 32 KB.
//
// Ways are resolved INSIDE this module so the Chisel wrapper's hit test is
// unchanged: on a hit we drive `tag` with the probed address's own tag and
// raise tag_valid, so ICache.scala's existing `tag === address >> shift`
// comparison still decides hit/miss. A miss simply drops tag_valid.
//
// The direct-mapped predecessor also exported a `block_out` port for a planned
// block-fetch path (F2). It was never declared in the Chisel BlackBox, so
// it was dead-stripped, and its one-line-per-index shape has no meaning once
// a set holds several ways. Dropped rather than left to mislead.
module iCacheRegisters #(
  parameter offset_width = 4,  // 2^offset_width instructions per line
  parameter line_width   = 7,  // 2^line_width sets
  parameter way_width    = 2,  // 2^way_width ways per set
  localparam tag_width   = 32 - offset_width - line_width - 2,
  localparam n_sets      = 1 << line_width,
  localparam n_ways      = 1 << way_width,
  localparam n_lines     = n_sets * n_ways,
  localparam block_size  = 1 << offset_width
) (
  input      [31:0]                address,
  output reg [31:0]                instruction,
  output reg [tag_width-1: 0]      tag,
  output reg                       tag_valid,
  input      [line_width-1:0]      write_line_index,
  input      [32*block_size - 1:0] write_block,
  input      [tag_width-1: 0]      write_tag,
  input                            reset, write_in, clock, invalidate_all
);

  // Flattened as set*n_ways + way so the valid vector and the victim pointers
  // stay packed: reset/invalidate is then a single NBA each. An unpacked loop
  // of `<=` hits Verilator's BLKLOOPINIT once the loop exceeds 64 iterations,
  // which 128 sets would.
  reg [31:0]              cache [n_lines-1:0][block_size-1:0];
  reg [tag_width-1:0]     tags  [n_lines-1:0];
  reg [n_lines-1:0]       validBits;
  reg [n_sets*way_width-1:0] victim;   // round-robin replacement, one per set

  // ---- probe -----------------------------------------------------------
  wire [line_width-1:0]   rd_set  = address[line_width+offset_width+2-1 : offset_width+2];
  wire [tag_width-1:0]    rd_tag  = address[31 : line_width+offset_width+2];
  wire [offset_width-1:0] rd_word = address[offset_width+2-1 : 2];

  reg [way_width-1:0] rd_way;
  reg                 rd_hit;
  integer r;
  always @* begin
    rd_hit = 1'b0;
    rd_way = {way_width{1'b0}};
    for (r = 0; r < n_ways; r = r + 1) begin
      if (validBits[rd_set*n_ways + r] && tags[rd_set*n_ways + r] == rd_tag) begin
        rd_hit = 1'b1;
        rd_way = r[way_width-1:0];
      end
    end
  end

  // Synchronous read, matching the one-cycle latency the wrapper pipelines on.
  always @(posedge clock) begin
    instruction <= cache[rd_set*n_ways + rd_way][rd_word];
    tag         <= rd_tag;
    tag_valid   <= rd_hit;
  end

  // ---- fill ------------------------------------------------------------
  // Refilling a line that is somehow still resident reuses its way instead of
  // allocating a second one: two ways holding the same tag would halve the set
  // and make the probe result depend on way order.
  reg [way_width-1:0] wr_way;
  reg                 wr_hit;
  integer q;
  always @* begin
    wr_hit = 1'b0;
    wr_way = victim[write_line_index*way_width +: way_width];
    for (q = 0; q < n_ways; q = q + 1) begin
      if (validBits[write_line_index*n_ways + q] && tags[write_line_index*n_ways + q] == write_tag) begin
        wr_hit = 1'b1;
        wr_way = q[way_width-1:0];
      end
    end
  end

  integer j;
  always @(posedge clock) begin
    if (reset || invalidate_all) begin
      validBits <= {n_lines{1'b0}};
      victim    <= {(n_sets*way_width){1'b0}};
    end else if (write_in) begin
      for (j = 0; j < block_size; j = j + 1) begin
        cache[write_line_index*n_ways + wr_way][j] <= write_block[32*j +: 32];
      end
      tags[write_line_index*n_ways + wr_way]      <= write_tag;
      validBits[write_line_index*n_ways + wr_way] <= 1'b1;
      if (!wr_hit) begin
        victim[write_line_index*way_width +: way_width]
          <= victim[write_line_index*way_width +: way_width] + 1'b1;
      end
    end
  end

endmodule
