module MultiUart_Anon(
  input         clock,
  input         reset,
  input         client_AWID,
  input  [31:0] client_AWADDR,
  input         client_AWVALID,
  output        client_AWREADY,
  input  [31:0] client_WDATA,
  input         client_WLAST,
  input         client_WVALID,
  output        client_WREADY,
  output        client_BID,
  output        client_BVALID,
  input         client_ARID,
  input  [31:0] client_ARADDR,
  input  [7:0]  client_ARLEN,
  input         client_ARVALID,
  output        client_ARREADY,
  output        client_RID,
  output [31:0] client_RDATA,
  output        client_RLAST,
  output        client_RVALID,
  input         client_RREADY,
  output        MTIP,
  output        putCharOut0_valid,
  output [7:0]  putCharOut0_byte
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
  reg [31:0] _RAND_3;
  reg [31:0] _RAND_4;
  reg [31:0] _RAND_5;
  reg [31:0] _RAND_6;
  reg [31:0] _RAND_7;
  reg [31:0] _RAND_8;
  reg [31:0] _RAND_9;
  reg [63:0] _RAND_10;
  reg [63:0] _RAND_11;
  reg [31:0] _RAND_12;
  reg [31:0] _RAND_13;
  reg [63:0] _RAND_14;
`endif // RANDOMIZE_REG_INIT
  reg  readRequestBuffer_valid; // @[multi_uart.scala 18:34]
  reg [31:0] readRequestBuffer_address; // @[multi_uart.scala 18:34]
  reg [7:0] readRequestBuffer_len; // @[multi_uart.scala 18:34]
  reg  readRequestBuffer_id; // @[multi_uart.scala 18:34]
  reg  writeRequestBuffer_address_valid; // @[multi_uart.scala 26:35]
  reg [31:0] writeRequestBuffer_address_offset; // @[multi_uart.scala 26:35]
  reg  writeRequestBuffer_address_id; // @[multi_uart.scala 26:35]
  reg  writeRequestBuffer_data_valid; // @[multi_uart.scala 26:35]
  reg [31:0] writeRequestBuffer_data_data; // @[multi_uart.scala 26:35]
  reg  writeRequestBuffer_data_last; // @[multi_uart.scala 26:35]
  wire  _T = client_ARREADY & client_ARVALID; // @[multi_uart.scala 42:23]
  wire  _GEN_0 = client_ARREADY & client_ARVALID | readRequestBuffer_valid; // @[multi_uart.scala 42:42 43:29 18:34]
  wire [7:0] _readRequestBuffer_len_T_1 = readRequestBuffer_len - 8'h1; // @[multi_uart.scala 51:52]
  wire  _T_2 = |readRequestBuffer_len; // @[multi_uart.scala 52:33]
  reg [63:0] mtime; // @[multi_uart.scala 54:22]
  reg [63:0] mtimecmp; // @[multi_uart.scala 55:25]
  reg [31:0] mtimecmplowtemp; // @[multi_uart.scala 56:28]
  reg [3:0] couter_wrap; // @[multi_uart.scala 57:28]
  wire [3:0] _couter_wrap_T_1 = couter_wrap + 4'h1; // @[multi_uart.scala 58:30]
  wire [63:0] _GEN_44 = {{63'd0}, &couter_wrap}; // @[multi_uart.scala 59:18]
  wire [63:0] _mtime_T_2 = mtime + _GEN_44; // @[multi_uart.scala 59:18]
  reg [63:0] mtimeRead; // @[multi_uart.scala 60:22]
  wire [31:0] _client_RDATA_T_3 = _T_2 ? mtimeRead[31:0] : mtimeRead[63:32]; // @[multi_uart.scala 74:44]
  wire [31:0] _GEN_9 = 32'h4000000 == readRequestBuffer_address ? 32'h0 : 32'h8; // @[multi_uart.scala 70:16 71:37 75:38]
  wire [31:0] _GEN_10 = 32'h200bff8 == readRequestBuffer_address ? _client_RDATA_T_3 : _GEN_9; // @[multi_uart.scala 71:37 74:38]
  wire [31:0] _GEN_11 = 32'he000102c == readRequestBuffer_address ? 32'h2 : _GEN_10; // @[multi_uart.scala 71:37 73:38]
  wire [31:0] _putChar_valid_T = writeRequestBuffer_address_offset & 32'hff; // @[multi_uart.scala 86:58]
  wire  _T_9 = writeRequestBuffer_address_valid & writeRequestBuffer_data_valid; // @[multi_uart.scala 151:41]
  wire  _GEN_30 = writeRequestBuffer_data_last ? 1'h0 : writeRequestBuffer_address_valid; // @[multi_uart.scala 153:40 154:40 26:35]
  wire  _GEN_31 = writeRequestBuffer_address_valid & writeRequestBuffer_data_valid ? 1'h0 :
    writeRequestBuffer_data_valid; // @[multi_uart.scala 151:75 152:35 26:35]
  wire  _GEN_32 = writeRequestBuffer_address_valid & writeRequestBuffer_data_valid ? _GEN_30 :
    writeRequestBuffer_address_valid; // @[multi_uart.scala 151:75 26:35]
  wire  _GEN_33 = client_AWREADY & client_AWVALID | _GEN_32; // @[multi_uart.scala 158:42 159:38]
  wire  _GEN_38 = client_WREADY & client_WVALID | _GEN_31; // @[multi_uart.scala 166:40 167:35]
  wire  _T_14 = writeRequestBuffer_address_offset == 32'h2004000; // @[multi_uart.scala 176:40]
  wire  _T_15 = writeRequestBuffer_address_valid & _T_14; // @[multi_uart.scala 175:38]
  wire  _T_16 = _T_15 & writeRequestBuffer_data_valid; // @[multi_uart.scala 176:59]
  wire  _T_17 = _T_16 & writeRequestBuffer_data_last; // @[multi_uart.scala 177:35]
  wire [63:0] _mtimecmp_T = {writeRequestBuffer_data_data,mtimecmplowtemp}; // @[Cat.scala 33:92]
  assign client_AWREADY = ~writeRequestBuffer_address_valid; // @[multi_uart.scala 185:21]
  assign client_WREADY = ~writeRequestBuffer_data_valid | writeRequestBuffer_address_valid; // @[multi_uart.scala 186:51]
  assign client_BID = writeRequestBuffer_address_id; // @[multi_uart.scala 188:14]
  assign client_BVALID = _T_9 & writeRequestBuffer_data_last; // @[multi_uart.scala 190:86]
  assign client_ARREADY = ~readRequestBuffer_valid; // @[multi_uart.scala 183:21]
  assign client_RID = readRequestBuffer_id; // @[multi_uart.scala 77:14]
  assign client_RDATA = 32'he000002c == readRequestBuffer_address ? 32'h2 : _GEN_11; // @[multi_uart.scala 71:37 72:38]
  assign client_RLAST = ~_T_2; // @[multi_uart.scala 78:19]
  assign client_RVALID = readRequestBuffer_valid; // @[multi_uart.scala 80:17]
  assign MTIP = mtime > mtimecmp; // @[multi_uart.scala 193:18]
  assign putCharOut0_valid = _putChar_valid_T == 32'h30 & writeRequestBuffer_address_valid &
    writeRequestBuffer_data_valid; // @[multi_uart.scala 86:159]
  assign putCharOut0_byte = writeRequestBuffer_data_data[7:0]; // @[multi_uart.scala 87:47]
  always @(posedge clock) begin
    if (reset) begin // @[multi_uart.scala 18:34]
      readRequestBuffer_valid <= 1'h0; // @[multi_uart.scala 18:34]
    end else if (readRequestBuffer_valid & client_RREADY) begin // @[multi_uart.scala 50:50]
      if (~(|readRequestBuffer_len)) begin // @[multi_uart.scala 52:38]
        readRequestBuffer_valid <= 1'h0; // @[multi_uart.scala 52:64]
      end else begin
        readRequestBuffer_valid <= _GEN_0;
      end
    end else begin
      readRequestBuffer_valid <= _GEN_0;
    end
    if (client_ARREADY & client_ARVALID) begin // @[multi_uart.scala 42:42]
      readRequestBuffer_address <= client_ARADDR; // @[multi_uart.scala 44:31]
    end
    if (readRequestBuffer_valid & client_RREADY) begin // @[multi_uart.scala 50:50]
      readRequestBuffer_len <= _readRequestBuffer_len_T_1; // @[multi_uart.scala 51:27]
    end else if (client_ARREADY & client_ARVALID) begin // @[multi_uart.scala 42:42]
      readRequestBuffer_len <= client_ARLEN; // @[multi_uart.scala 45:27]
    end
    if (client_ARREADY & client_ARVALID) begin // @[multi_uart.scala 42:42]
      readRequestBuffer_id <= client_ARID; // @[multi_uart.scala 47:26]
    end
    if (reset) begin // @[multi_uart.scala 26:35]
      writeRequestBuffer_address_valid <= 1'h0; // @[multi_uart.scala 26:35]
    end else begin
      writeRequestBuffer_address_valid <= _GEN_33;
    end
    if (client_AWREADY & client_AWVALID) begin // @[multi_uart.scala 158:42]
      writeRequestBuffer_address_offset <= client_AWADDR; // @[multi_uart.scala 160:39]
    end
    if (client_AWREADY & client_AWVALID) begin // @[multi_uart.scala 158:42]
      writeRequestBuffer_address_id <= client_AWID; // @[multi_uart.scala 161:35]
    end
    if (reset) begin // @[multi_uart.scala 26:35]
      writeRequestBuffer_data_valid <= 1'h0; // @[multi_uart.scala 26:35]
    end else begin
      writeRequestBuffer_data_valid <= _GEN_38;
    end
    if (client_WREADY & client_WVALID) begin // @[multi_uart.scala 166:40]
      writeRequestBuffer_data_data <= client_WDATA; // @[multi_uart.scala 168:34]
    end
    if (client_WREADY & client_WVALID) begin // @[multi_uart.scala 166:40]
      writeRequestBuffer_data_last <= client_WLAST; // @[multi_uart.scala 169:34]
    end
    if (reset) begin // @[multi_uart.scala 54:22]
      mtime <= 64'h0; // @[multi_uart.scala 54:22]
    end else begin
      mtime <= _mtime_T_2; // @[multi_uart.scala 59:9]
    end
    if (reset) begin // @[multi_uart.scala 55:25]
      mtimecmp <= 64'h0; // @[multi_uart.scala 55:25]
    end else if (_T_17) begin // @[multi_uart.scala 179:5]
      mtimecmp <= _mtimecmp_T; // @[multi_uart.scala 180:14]
    end
    if (writeRequestBuffer_data_valid & ~writeRequestBuffer_data_last) begin // @[multi_uart.scala 173:72]
      mtimecmplowtemp <= writeRequestBuffer_data_data; // @[multi_uart.scala 173:90]
    end
    if (reset) begin // @[multi_uart.scala 57:28]
      couter_wrap <= 4'h0; // @[multi_uart.scala 57:28]
    end else begin
      couter_wrap <= _couter_wrap_T_1; // @[multi_uart.scala 58:15]
    end
    if (_T) begin // @[multi_uart.scala 61:42]
      mtimeRead <= mtime; // @[multi_uart.scala 62:15]
    end
  end
// Register and memory initialization
`ifdef RANDOMIZE_GARBAGE_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_INVALID_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_REG_INIT
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_MEM_INIT
`define RANDOMIZE
`endif
`ifndef RANDOM
`define RANDOM $random
`endif
`ifdef RANDOMIZE_MEM_INIT
  integer initvar;
`endif
`ifndef SYNTHESIS
`ifdef FIRRTL_BEFORE_INITIAL
`FIRRTL_BEFORE_INITIAL
`endif
initial begin
  `ifdef RANDOMIZE
    `ifdef INIT_RANDOM
      `INIT_RANDOM
    `endif
    `ifndef VERILATOR
      `ifdef RANDOMIZE_DELAY
        #`RANDOMIZE_DELAY begin end
      `else
        #0.002 begin end
      `endif
    `endif
`ifdef RANDOMIZE_REG_INIT
  _RAND_0 = {1{`RANDOM}};
  readRequestBuffer_valid = _RAND_0[0:0];
  _RAND_1 = {1{`RANDOM}};
  readRequestBuffer_address = _RAND_1[31:0];
  _RAND_2 = {1{`RANDOM}};
  readRequestBuffer_len = _RAND_2[7:0];
  _RAND_3 = {1{`RANDOM}};
  readRequestBuffer_id = _RAND_3[0:0];
  _RAND_4 = {1{`RANDOM}};
  writeRequestBuffer_address_valid = _RAND_4[0:0];
  _RAND_5 = {1{`RANDOM}};
  writeRequestBuffer_address_offset = _RAND_5[31:0];
  _RAND_6 = {1{`RANDOM}};
  writeRequestBuffer_address_id = _RAND_6[0:0];
  _RAND_7 = {1{`RANDOM}};
  writeRequestBuffer_data_valid = _RAND_7[0:0];
  _RAND_8 = {1{`RANDOM}};
  writeRequestBuffer_data_data = _RAND_8[31:0];
  _RAND_9 = {1{`RANDOM}};
  writeRequestBuffer_data_last = _RAND_9[0:0];
  _RAND_10 = {2{`RANDOM}};
  mtime = _RAND_10[63:0];
  _RAND_11 = {2{`RANDOM}};
  mtimecmp = _RAND_11[63:0];
  _RAND_12 = {1{`RANDOM}};
  mtimecmplowtemp = _RAND_12[31:0];
  _RAND_13 = {1{`RANDOM}};
  couter_wrap = _RAND_13[3:0];
  _RAND_14 = {2{`RANDOM}};
  mtimeRead = _RAND_14[63:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
module MultiUart_Anon_1(
  input         clock,
  input         reset,
  input         client_AWID,
  input  [31:0] client_AWADDR,
  input         client_AWVALID,
  output        client_AWREADY,
  input  [31:0] client_WDATA,
  input         client_WLAST,
  input         client_WVALID,
  output        client_WREADY,
  output        client_BID,
  output        client_BVALID,
  input         client_ARID,
  input  [31:0] client_ARADDR,
  input  [7:0]  client_ARLEN,
  input         client_ARVALID,
  output        client_ARREADY,
  output        client_RID,
  output [31:0] client_RDATA,
  output        client_RLAST,
  output        client_RVALID,
  input         client_RREADY,
  output        MTIP,
  output        putCharOut1_valid,
  output [7:0]  putCharOut1_byte
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
  reg [31:0] _RAND_3;
  reg [31:0] _RAND_4;
  reg [31:0] _RAND_5;
  reg [31:0] _RAND_6;
  reg [31:0] _RAND_7;
  reg [31:0] _RAND_8;
  reg [31:0] _RAND_9;
  reg [63:0] _RAND_10;
  reg [63:0] _RAND_11;
  reg [31:0] _RAND_12;
  reg [31:0] _RAND_13;
  reg [63:0] _RAND_14;
`endif // RANDOMIZE_REG_INIT
  reg  readRequestBuffer_valid; // @[multi_uart.scala 18:34]
  reg [31:0] readRequestBuffer_address; // @[multi_uart.scala 18:34]
  reg [7:0] readRequestBuffer_len; // @[multi_uart.scala 18:34]
  reg  readRequestBuffer_id; // @[multi_uart.scala 18:34]
  reg  writeRequestBuffer_address_valid; // @[multi_uart.scala 26:35]
  reg [31:0] writeRequestBuffer_address_offset; // @[multi_uart.scala 26:35]
  reg  writeRequestBuffer_address_id; // @[multi_uart.scala 26:35]
  reg  writeRequestBuffer_data_valid; // @[multi_uart.scala 26:35]
  reg [31:0] writeRequestBuffer_data_data; // @[multi_uart.scala 26:35]
  reg  writeRequestBuffer_data_last; // @[multi_uart.scala 26:35]
  wire  _T = client_ARREADY & client_ARVALID; // @[multi_uart.scala 42:23]
  wire  _GEN_0 = client_ARREADY & client_ARVALID | readRequestBuffer_valid; // @[multi_uart.scala 42:42 43:29 18:34]
  wire [7:0] _readRequestBuffer_len_T_1 = readRequestBuffer_len - 8'h1; // @[multi_uart.scala 51:52]
  wire  _T_2 = |readRequestBuffer_len; // @[multi_uart.scala 52:33]
  reg [63:0] mtime; // @[multi_uart.scala 54:22]
  reg [63:0] mtimecmp; // @[multi_uart.scala 55:25]
  reg [31:0] mtimecmplowtemp; // @[multi_uart.scala 56:28]
  reg [3:0] couter_wrap; // @[multi_uart.scala 57:28]
  wire [3:0] _couter_wrap_T_1 = couter_wrap + 4'h1; // @[multi_uart.scala 58:30]
  wire [63:0] _GEN_44 = {{63'd0}, &couter_wrap}; // @[multi_uart.scala 59:18]
  wire [63:0] _mtime_T_2 = mtime + _GEN_44; // @[multi_uart.scala 59:18]
  reg [63:0] mtimeRead; // @[multi_uart.scala 60:22]
  wire [31:0] _client_RDATA_T_3 = _T_2 ? mtimeRead[31:0] : mtimeRead[63:32]; // @[multi_uart.scala 74:44]
  wire [31:0] _GEN_9 = 32'h4000000 == readRequestBuffer_address ? 32'h0 : 32'h8; // @[multi_uart.scala 70:16 71:37 75:38]
  wire [31:0] _GEN_10 = 32'h200bff8 == readRequestBuffer_address ? _client_RDATA_T_3 : _GEN_9; // @[multi_uart.scala 71:37 74:38]
  wire [31:0] _GEN_11 = 32'he000102c == readRequestBuffer_address ? 32'h2 : _GEN_10; // @[multi_uart.scala 71:37 73:38]
  wire [31:0] _putChar_valid_T = writeRequestBuffer_address_offset & 32'hff; // @[multi_uart.scala 86:58]
  wire  _T_9 = writeRequestBuffer_address_valid & writeRequestBuffer_data_valid; // @[multi_uart.scala 151:41]
  wire  _GEN_30 = writeRequestBuffer_data_last ? 1'h0 : writeRequestBuffer_address_valid; // @[multi_uart.scala 153:40 154:40 26:35]
  wire  _GEN_31 = writeRequestBuffer_address_valid & writeRequestBuffer_data_valid ? 1'h0 :
    writeRequestBuffer_data_valid; // @[multi_uart.scala 151:75 152:35 26:35]
  wire  _GEN_32 = writeRequestBuffer_address_valid & writeRequestBuffer_data_valid ? _GEN_30 :
    writeRequestBuffer_address_valid; // @[multi_uart.scala 151:75 26:35]
  wire  _GEN_33 = client_AWREADY & client_AWVALID | _GEN_32; // @[multi_uart.scala 158:42 159:38]
  wire  _GEN_38 = client_WREADY & client_WVALID | _GEN_31; // @[multi_uart.scala 166:40 167:35]
  wire  _T_14 = writeRequestBuffer_address_offset == 32'h2004000; // @[multi_uart.scala 176:40]
  wire  _T_15 = writeRequestBuffer_address_valid & _T_14; // @[multi_uart.scala 175:38]
  wire  _T_16 = _T_15 & writeRequestBuffer_data_valid; // @[multi_uart.scala 176:59]
  wire  _T_17 = _T_16 & writeRequestBuffer_data_last; // @[multi_uart.scala 177:35]
  wire [63:0] _mtimecmp_T = {writeRequestBuffer_data_data,mtimecmplowtemp}; // @[Cat.scala 33:92]
  assign client_AWREADY = ~writeRequestBuffer_address_valid; // @[multi_uart.scala 185:21]
  assign client_WREADY = ~writeRequestBuffer_data_valid | writeRequestBuffer_address_valid; // @[multi_uart.scala 186:51]
  assign client_BID = writeRequestBuffer_address_id; // @[multi_uart.scala 188:14]
  assign client_BVALID = _T_9 & writeRequestBuffer_data_last; // @[multi_uart.scala 190:86]
  assign client_ARREADY = ~readRequestBuffer_valid; // @[multi_uart.scala 183:21]
  assign client_RID = readRequestBuffer_id; // @[multi_uart.scala 77:14]
  assign client_RDATA = 32'he000002c == readRequestBuffer_address ? 32'h2 : _GEN_11; // @[multi_uart.scala 71:37 72:38]
  assign client_RLAST = ~_T_2; // @[multi_uart.scala 78:19]
  assign client_RVALID = readRequestBuffer_valid; // @[multi_uart.scala 80:17]
  assign MTIP = mtime > mtimecmp; // @[multi_uart.scala 193:18]
  assign putCharOut1_valid = _putChar_valid_T == 32'h30 & writeRequestBuffer_address_valid &
    writeRequestBuffer_data_valid; // @[multi_uart.scala 86:159]
  assign putCharOut1_byte = writeRequestBuffer_data_data[7:0]; // @[multi_uart.scala 87:47]
  always @(posedge clock) begin
    if (reset) begin // @[multi_uart.scala 18:34]
      readRequestBuffer_valid <= 1'h0; // @[multi_uart.scala 18:34]
    end else if (readRequestBuffer_valid & client_RREADY) begin // @[multi_uart.scala 50:50]
      if (~(|readRequestBuffer_len)) begin // @[multi_uart.scala 52:38]
        readRequestBuffer_valid <= 1'h0; // @[multi_uart.scala 52:64]
      end else begin
        readRequestBuffer_valid <= _GEN_0;
      end
    end else begin
      readRequestBuffer_valid <= _GEN_0;
    end
    if (client_ARREADY & client_ARVALID) begin // @[multi_uart.scala 42:42]
      readRequestBuffer_address <= client_ARADDR; // @[multi_uart.scala 44:31]
    end
    if (readRequestBuffer_valid & client_RREADY) begin // @[multi_uart.scala 50:50]
      readRequestBuffer_len <= _readRequestBuffer_len_T_1; // @[multi_uart.scala 51:27]
    end else if (client_ARREADY & client_ARVALID) begin // @[multi_uart.scala 42:42]
      readRequestBuffer_len <= client_ARLEN; // @[multi_uart.scala 45:27]
    end
    if (client_ARREADY & client_ARVALID) begin // @[multi_uart.scala 42:42]
      readRequestBuffer_id <= client_ARID; // @[multi_uart.scala 47:26]
    end
    if (reset) begin // @[multi_uart.scala 26:35]
      writeRequestBuffer_address_valid <= 1'h0; // @[multi_uart.scala 26:35]
    end else begin
      writeRequestBuffer_address_valid <= _GEN_33;
    end
    if (client_AWREADY & client_AWVALID) begin // @[multi_uart.scala 158:42]
      writeRequestBuffer_address_offset <= client_AWADDR; // @[multi_uart.scala 160:39]
    end
    if (client_AWREADY & client_AWVALID) begin // @[multi_uart.scala 158:42]
      writeRequestBuffer_address_id <= client_AWID; // @[multi_uart.scala 161:35]
    end
    if (reset) begin // @[multi_uart.scala 26:35]
      writeRequestBuffer_data_valid <= 1'h0; // @[multi_uart.scala 26:35]
    end else begin
      writeRequestBuffer_data_valid <= _GEN_38;
    end
    if (client_WREADY & client_WVALID) begin // @[multi_uart.scala 166:40]
      writeRequestBuffer_data_data <= client_WDATA; // @[multi_uart.scala 168:34]
    end
    if (client_WREADY & client_WVALID) begin // @[multi_uart.scala 166:40]
      writeRequestBuffer_data_last <= client_WLAST; // @[multi_uart.scala 169:34]
    end
    if (reset) begin // @[multi_uart.scala 54:22]
      mtime <= 64'h0; // @[multi_uart.scala 54:22]
    end else begin
      mtime <= _mtime_T_2; // @[multi_uart.scala 59:9]
    end
    if (reset) begin // @[multi_uart.scala 55:25]
      mtimecmp <= 64'h0; // @[multi_uart.scala 55:25]
    end else if (_T_17) begin // @[multi_uart.scala 179:5]
      mtimecmp <= _mtimecmp_T; // @[multi_uart.scala 180:14]
    end
    if (writeRequestBuffer_data_valid & ~writeRequestBuffer_data_last) begin // @[multi_uart.scala 173:72]
      mtimecmplowtemp <= writeRequestBuffer_data_data; // @[multi_uart.scala 173:90]
    end
    if (reset) begin // @[multi_uart.scala 57:28]
      couter_wrap <= 4'h0; // @[multi_uart.scala 57:28]
    end else begin
      couter_wrap <= _couter_wrap_T_1; // @[multi_uart.scala 58:15]
    end
    if (_T) begin // @[multi_uart.scala 61:42]
      mtimeRead <= mtime; // @[multi_uart.scala 62:15]
    end
  end
// Register and memory initialization
`ifdef RANDOMIZE_GARBAGE_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_INVALID_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_REG_INIT
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_MEM_INIT
`define RANDOMIZE
`endif
`ifndef RANDOM
`define RANDOM $random
`endif
`ifdef RANDOMIZE_MEM_INIT
  integer initvar;
`endif
`ifndef SYNTHESIS
`ifdef FIRRTL_BEFORE_INITIAL
`FIRRTL_BEFORE_INITIAL
`endif
initial begin
  `ifdef RANDOMIZE
    `ifdef INIT_RANDOM
      `INIT_RANDOM
    `endif
    `ifndef VERILATOR
      `ifdef RANDOMIZE_DELAY
        #`RANDOMIZE_DELAY begin end
      `else
        #0.002 begin end
      `endif
    `endif
`ifdef RANDOMIZE_REG_INIT
  _RAND_0 = {1{`RANDOM}};
  readRequestBuffer_valid = _RAND_0[0:0];
  _RAND_1 = {1{`RANDOM}};
  readRequestBuffer_address = _RAND_1[31:0];
  _RAND_2 = {1{`RANDOM}};
  readRequestBuffer_len = _RAND_2[7:0];
  _RAND_3 = {1{`RANDOM}};
  readRequestBuffer_id = _RAND_3[0:0];
  _RAND_4 = {1{`RANDOM}};
  writeRequestBuffer_address_valid = _RAND_4[0:0];
  _RAND_5 = {1{`RANDOM}};
  writeRequestBuffer_address_offset = _RAND_5[31:0];
  _RAND_6 = {1{`RANDOM}};
  writeRequestBuffer_address_id = _RAND_6[0:0];
  _RAND_7 = {1{`RANDOM}};
  writeRequestBuffer_data_valid = _RAND_7[0:0];
  _RAND_8 = {1{`RANDOM}};
  writeRequestBuffer_data_data = _RAND_8[31:0];
  _RAND_9 = {1{`RANDOM}};
  writeRequestBuffer_data_last = _RAND_9[0:0];
  _RAND_10 = {2{`RANDOM}};
  mtime = _RAND_10[63:0];
  _RAND_11 = {2{`RANDOM}};
  mtimecmp = _RAND_11[63:0];
  _RAND_12 = {1{`RANDOM}};
  mtimecmplowtemp = _RAND_12[31:0];
  _RAND_13 = {1{`RANDOM}};
  couter_wrap = _RAND_13[3:0];
  _RAND_14 = {2{`RANDOM}};
  mtimeRead = _RAND_14[63:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
module MultiUart(
  input         clock,
  input         reset,
  input         client0_AWID,
  input  [31:0] client0_AWADDR,
  input  [7:0]  client0_AWLEN,
  input  [2:0]  client0_AWSIZE,
  input  [1:0]  client0_AWBURST,
  input         client0_AWLOCK,
  input  [3:0]  client0_AWCACHE,
  input  [2:0]  client0_AWPROT,
  input  [3:0]  client0_AWQOS,
  input         client0_AWVALID,
  output        client0_AWREADY,
  input  [31:0] client0_WDATA,
  input  [3:0]  client0_WSTRB,
  input         client0_WLAST,
  input         client0_WVALID,
  output        client0_WREADY,
  output        client0_BID,
  output [1:0]  client0_BRESP,
  output        client0_BVALID,
  input         client0_BREADY,
  input         client0_ARID,
  input  [31:0] client0_ARADDR,
  input  [7:0]  client0_ARLEN,
  input  [2:0]  client0_ARSIZE,
  input  [1:0]  client0_ARBURST,
  input         client0_ARLOCK,
  input  [3:0]  client0_ARCACHE,
  input  [2:0]  client0_ARPROT,
  input  [3:0]  client0_ARQOS,
  input         client0_ARVALID,
  output        client0_ARREADY,
  output        client0_RID,
  output [31:0] client0_RDATA,
  output [1:0]  client0_RRESP,
  output        client0_RLAST,
  output        client0_RVALID,
  input         client0_RREADY,
  input         client1_AWID,
  input  [31:0] client1_AWADDR,
  input  [7:0]  client1_AWLEN,
  input  [2:0]  client1_AWSIZE,
  input  [1:0]  client1_AWBURST,
  input         client1_AWLOCK,
  input  [3:0]  client1_AWCACHE,
  input  [2:0]  client1_AWPROT,
  input  [3:0]  client1_AWQOS,
  input         client1_AWVALID,
  output        client1_AWREADY,
  input  [31:0] client1_WDATA,
  input  [3:0]  client1_WSTRB,
  input         client1_WLAST,
  input         client1_WVALID,
  output        client1_WREADY,
  output        client1_BID,
  output [1:0]  client1_BRESP,
  output        client1_BVALID,
  input         client1_BREADY,
  input         client1_ARID,
  input  [31:0] client1_ARADDR,
  input  [7:0]  client1_ARLEN,
  input  [2:0]  client1_ARSIZE,
  input  [1:0]  client1_ARBURST,
  input         client1_ARLOCK,
  input  [3:0]  client1_ARCACHE,
  input  [2:0]  client1_ARPROT,
  input  [3:0]  client1_ARQOS,
  input         client1_ARVALID,
  output        client1_ARREADY,
  output        client1_RID,
  output [31:0] client1_RDATA,
  output [1:0]  client1_RRESP,
  output        client1_RLAST,
  output        client1_RVALID,
  input         client1_RREADY,
  output        putChar0_valid,
  output [7:0]  putChar0_byte,
  output        putChar1_valid,
  output [7:0]  putChar1_byte,
  output        MTIP0,
  output        MTIP1
);
  wire  uart0_clock; // @[multi_uart.scala 201:21]
  wire  uart0_reset; // @[multi_uart.scala 201:21]
  wire  uart0_client_AWID; // @[multi_uart.scala 201:21]
  wire [31:0] uart0_client_AWADDR; // @[multi_uart.scala 201:21]
  wire  uart0_client_AWVALID; // @[multi_uart.scala 201:21]
  wire  uart0_client_AWREADY; // @[multi_uart.scala 201:21]
  wire [31:0] uart0_client_WDATA; // @[multi_uart.scala 201:21]
  wire  uart0_client_WLAST; // @[multi_uart.scala 201:21]
  wire  uart0_client_WVALID; // @[multi_uart.scala 201:21]
  wire  uart0_client_WREADY; // @[multi_uart.scala 201:21]
  wire  uart0_client_BID; // @[multi_uart.scala 201:21]
  wire  uart0_client_BVALID; // @[multi_uart.scala 201:21]
  wire  uart0_client_ARID; // @[multi_uart.scala 201:21]
  wire [31:0] uart0_client_ARADDR; // @[multi_uart.scala 201:21]
  wire [7:0] uart0_client_ARLEN; // @[multi_uart.scala 201:21]
  wire  uart0_client_ARVALID; // @[multi_uart.scala 201:21]
  wire  uart0_client_ARREADY; // @[multi_uart.scala 201:21]
  wire  uart0_client_RID; // @[multi_uart.scala 201:21]
  wire [31:0] uart0_client_RDATA; // @[multi_uart.scala 201:21]
  wire  uart0_client_RLAST; // @[multi_uart.scala 201:21]
  wire  uart0_client_RVALID; // @[multi_uart.scala 201:21]
  wire  uart0_client_RREADY; // @[multi_uart.scala 201:21]
  wire  uart0_MTIP; // @[multi_uart.scala 201:21]
  wire  uart0_putCharOut0_valid; // @[multi_uart.scala 201:21]
  wire [7:0] uart0_putCharOut0_byte; // @[multi_uart.scala 201:21]
  wire  uart1_clock; // @[multi_uart.scala 205:21]
  wire  uart1_reset; // @[multi_uart.scala 205:21]
  wire  uart1_client_AWID; // @[multi_uart.scala 205:21]
  wire [31:0] uart1_client_AWADDR; // @[multi_uart.scala 205:21]
  wire  uart1_client_AWVALID; // @[multi_uart.scala 205:21]
  wire  uart1_client_AWREADY; // @[multi_uart.scala 205:21]
  wire [31:0] uart1_client_WDATA; // @[multi_uart.scala 205:21]
  wire  uart1_client_WLAST; // @[multi_uart.scala 205:21]
  wire  uart1_client_WVALID; // @[multi_uart.scala 205:21]
  wire  uart1_client_WREADY; // @[multi_uart.scala 205:21]
  wire  uart1_client_BID; // @[multi_uart.scala 205:21]
  wire  uart1_client_BVALID; // @[multi_uart.scala 205:21]
  wire  uart1_client_ARID; // @[multi_uart.scala 205:21]
  wire [31:0] uart1_client_ARADDR; // @[multi_uart.scala 205:21]
  wire [7:0] uart1_client_ARLEN; // @[multi_uart.scala 205:21]
  wire  uart1_client_ARVALID; // @[multi_uart.scala 205:21]
  wire  uart1_client_ARREADY; // @[multi_uart.scala 205:21]
  wire  uart1_client_RID; // @[multi_uart.scala 205:21]
  wire [31:0] uart1_client_RDATA; // @[multi_uart.scala 205:21]
  wire  uart1_client_RLAST; // @[multi_uart.scala 205:21]
  wire  uart1_client_RVALID; // @[multi_uart.scala 205:21]
  wire  uart1_client_RREADY; // @[multi_uart.scala 205:21]
  wire  uart1_MTIP; // @[multi_uart.scala 205:21]
  wire  uart1_putCharOut1_valid; // @[multi_uart.scala 205:21]
  wire [7:0] uart1_putCharOut1_byte; // @[multi_uart.scala 205:21]
  MultiUart_Anon uart0 ( // @[multi_uart.scala 201:21]
    .clock(uart0_clock),
    .reset(uart0_reset),
    .client_AWID(uart0_client_AWID),
    .client_AWADDR(uart0_client_AWADDR),
    .client_AWVALID(uart0_client_AWVALID),
    .client_AWREADY(uart0_client_AWREADY),
    .client_WDATA(uart0_client_WDATA),
    .client_WLAST(uart0_client_WLAST),
    .client_WVALID(uart0_client_WVALID),
    .client_WREADY(uart0_client_WREADY),
    .client_BID(uart0_client_BID),
    .client_BVALID(uart0_client_BVALID),
    .client_ARID(uart0_client_ARID),
    .client_ARADDR(uart0_client_ARADDR),
    .client_ARLEN(uart0_client_ARLEN),
    .client_ARVALID(uart0_client_ARVALID),
    .client_ARREADY(uart0_client_ARREADY),
    .client_RID(uart0_client_RID),
    .client_RDATA(uart0_client_RDATA),
    .client_RLAST(uart0_client_RLAST),
    .client_RVALID(uart0_client_RVALID),
    .client_RREADY(uart0_client_RREADY),
    .MTIP(uart0_MTIP),
    .putCharOut0_valid(uart0_putCharOut0_valid),
    .putCharOut0_byte(uart0_putCharOut0_byte)
  );
  MultiUart_Anon_1 uart1 ( // @[multi_uart.scala 205:21]
    .clock(uart1_clock),
    .reset(uart1_reset),
    .client_AWID(uart1_client_AWID),
    .client_AWADDR(uart1_client_AWADDR),
    .client_AWVALID(uart1_client_AWVALID),
    .client_AWREADY(uart1_client_AWREADY),
    .client_WDATA(uart1_client_WDATA),
    .client_WLAST(uart1_client_WLAST),
    .client_WVALID(uart1_client_WVALID),
    .client_WREADY(uart1_client_WREADY),
    .client_BID(uart1_client_BID),
    .client_BVALID(uart1_client_BVALID),
    .client_ARID(uart1_client_ARID),
    .client_ARADDR(uart1_client_ARADDR),
    .client_ARLEN(uart1_client_ARLEN),
    .client_ARVALID(uart1_client_ARVALID),
    .client_ARREADY(uart1_client_ARREADY),
    .client_RID(uart1_client_RID),
    .client_RDATA(uart1_client_RDATA),
    .client_RLAST(uart1_client_RLAST),
    .client_RVALID(uart1_client_RVALID),
    .client_RREADY(uart1_client_RREADY),
    .MTIP(uart1_MTIP),
    .putCharOut1_valid(uart1_putCharOut1_valid),
    .putCharOut1_byte(uart1_putCharOut1_byte)
  );
  assign client0_AWREADY = uart0_client_AWREADY; // @[multi_uart.scala 210:16]
  assign client0_WREADY = uart0_client_WREADY; // @[multi_uart.scala 210:16]
  assign client0_BID = uart0_client_BID; // @[multi_uart.scala 210:16]
  assign client0_BRESP = 2'h0; // @[multi_uart.scala 210:16]
  assign client0_BVALID = uart0_client_BVALID; // @[multi_uart.scala 210:16]
  assign client0_ARREADY = uart0_client_ARREADY; // @[multi_uart.scala 210:16]
  assign client0_RID = uart0_client_RID; // @[multi_uart.scala 210:16]
  assign client0_RDATA = uart0_client_RDATA; // @[multi_uart.scala 210:16]
  assign client0_RRESP = 2'h0; // @[multi_uart.scala 210:16]
  assign client0_RLAST = uart0_client_RLAST; // @[multi_uart.scala 210:16]
  assign client0_RVALID = uart0_client_RVALID; // @[multi_uart.scala 210:16]
  assign client1_AWREADY = uart1_client_AWREADY; // @[multi_uart.scala 211:16]
  assign client1_WREADY = uart1_client_WREADY; // @[multi_uart.scala 211:16]
  assign client1_BID = uart1_client_BID; // @[multi_uart.scala 211:16]
  assign client1_BRESP = 2'h0; // @[multi_uart.scala 211:16]
  assign client1_BVALID = uart1_client_BVALID; // @[multi_uart.scala 211:16]
  assign client1_ARREADY = uart1_client_ARREADY; // @[multi_uart.scala 211:16]
  assign client1_RID = uart1_client_RID; // @[multi_uart.scala 211:16]
  assign client1_RDATA = uart1_client_RDATA; // @[multi_uart.scala 211:16]
  assign client1_RRESP = 2'h0; // @[multi_uart.scala 211:16]
  assign client1_RLAST = uart1_client_RLAST; // @[multi_uart.scala 211:16]
  assign client1_RVALID = uart1_client_RVALID; // @[multi_uart.scala 211:16]
  assign putChar0_valid = uart0_putCharOut0_valid; // @[multi_uart.scala 215:12]
  assign putChar0_byte = uart0_putCharOut0_byte; // @[multi_uart.scala 215:12]
  assign putChar1_valid = uart1_putCharOut1_valid; // @[multi_uart.scala 218:12]
  assign putChar1_byte = uart1_putCharOut1_byte; // @[multi_uart.scala 218:12]
  assign MTIP0 = uart0_MTIP; // @[multi_uart.scala 223:9]
  assign MTIP1 = uart1_MTIP; // @[multi_uart.scala 224:9]
  assign uart0_clock = clock;
  assign uart0_reset = reset;
  assign uart0_client_AWID = client0_AWID; // @[multi_uart.scala 210:16]
  assign uart0_client_AWADDR = client0_AWADDR; // @[multi_uart.scala 210:16]
  assign uart0_client_AWVALID = client0_AWVALID; // @[multi_uart.scala 210:16]
  assign uart0_client_WDATA = client0_WDATA; // @[multi_uart.scala 210:16]
  assign uart0_client_WLAST = client0_WLAST; // @[multi_uart.scala 210:16]
  assign uart0_client_WVALID = client0_WVALID; // @[multi_uart.scala 210:16]
  assign uart0_client_ARID = client0_ARID; // @[multi_uart.scala 210:16]
  assign uart0_client_ARADDR = client0_ARADDR; // @[multi_uart.scala 210:16]
  assign uart0_client_ARLEN = client0_ARLEN; // @[multi_uart.scala 210:16]
  assign uart0_client_ARVALID = client0_ARVALID; // @[multi_uart.scala 210:16]
  assign uart0_client_RREADY = client0_RREADY; // @[multi_uart.scala 210:16]
  assign uart1_clock = clock;
  assign uart1_reset = reset;
  assign uart1_client_AWID = client1_AWID; // @[multi_uart.scala 211:16]
  assign uart1_client_AWADDR = client1_AWADDR; // @[multi_uart.scala 211:16]
  assign uart1_client_AWVALID = client1_AWVALID; // @[multi_uart.scala 211:16]
  assign uart1_client_WDATA = client1_WDATA; // @[multi_uart.scala 211:16]
  assign uart1_client_WLAST = client1_WLAST; // @[multi_uart.scala 211:16]
  assign uart1_client_WVALID = client1_WVALID; // @[multi_uart.scala 211:16]
  assign uart1_client_ARID = client1_ARID; // @[multi_uart.scala 211:16]
  assign uart1_client_ARADDR = client1_ARADDR; // @[multi_uart.scala 211:16]
  assign uart1_client_ARLEN = client1_ARLEN; // @[multi_uart.scala 211:16]
  assign uart1_client_ARVALID = client1_ARVALID; // @[multi_uart.scala 211:16]
  assign uart1_client_RREADY = client1_RREADY; // @[multi_uart.scala 211:16]
endmodule
