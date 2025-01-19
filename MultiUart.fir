circuit MultiUart :
  module MultiUart_Anon :
    input clock : Clock
    input reset : Reset
    input client : { AWID : UInt<1>, AWADDR : UInt<32>, AWLEN : UInt<8>, AWSIZE : UInt<3>, AWBURST : UInt<2>, AWLOCK : UInt<1>, AWCACHE : UInt<4>, AWPROT : UInt<3>, AWQOS : UInt<4>, AWVALID : UInt<1>, flip AWREADY : UInt<1>, WDATA : UInt<32>, WSTRB : UInt<4>, WLAST : UInt<1>, WVALID : UInt<1>, flip WREADY : UInt<1>, flip BID : UInt<1>, flip BRESP : UInt<2>, flip BVALID : UInt<1>, BREADY : UInt<1>, ARID : UInt<1>, ARADDR : UInt<32>, ARLEN : UInt<8>, ARSIZE : UInt<3>, ARBURST : UInt<2>, ARLOCK : UInt<1>, ARCACHE : UInt<4>, ARPROT : UInt<3>, ARQOS : UInt<4>, ARVALID : UInt<1>, flip ARREADY : UInt<1>, flip RID : UInt<1>, flip RDATA : UInt<32>, flip RRESP : UInt<2>, flip RLAST : UInt<1>, flip RVALID : UInt<1>, RREADY : UInt<1>}
    output MTIP : UInt<1>
    output putCharOut0 : { valid : UInt<1>, byte : UInt<8>}

    wire _readRequestBuffer_WIRE : { valid : UInt<1>, address : UInt<32>, size : UInt<3>, len : UInt<8>, id : UInt<1>}
    _readRequestBuffer_WIRE.id is invalid
    _readRequestBuffer_WIRE.len is invalid
    _readRequestBuffer_WIRE.size is invalid
    _readRequestBuffer_WIRE.address is invalid
    _readRequestBuffer_WIRE.valid <= UInt<1>("h0")
    reg readRequestBuffer : { valid : UInt<1>, address : UInt<32>, size : UInt<3>, len : UInt<8>, id : UInt<1>}, clock with :
      reset => (reset, _readRequestBuffer_WIRE) @[multi_uart.scala 18:34]
    wire _writeRequestBuffer_WIRE : { address : { valid : UInt<1>, offset : UInt<32>, size : UInt<3>, len : UInt<8>, id : UInt<1>}, data : { valid : UInt<1>, data : UInt<32>, last : UInt<1>, strb : UInt<4>}}
    _writeRequestBuffer_WIRE.data.strb is invalid
    _writeRequestBuffer_WIRE.data.last is invalid
    _writeRequestBuffer_WIRE.data.data is invalid
    _writeRequestBuffer_WIRE.data.valid <= UInt<1>("h0")
    _writeRequestBuffer_WIRE.address.id is invalid
    _writeRequestBuffer_WIRE.address.len is invalid
    _writeRequestBuffer_WIRE.address.size is invalid
    _writeRequestBuffer_WIRE.address.offset is invalid
    _writeRequestBuffer_WIRE.address.valid <= UInt<1>("h0")
    reg writeRequestBuffer : { address : { valid : UInt<1>, offset : UInt<32>, size : UInt<3>, len : UInt<8>, id : UInt<1>}, data : { valid : UInt<1>, data : UInt<32>, last : UInt<1>, strb : UInt<4>}}, clock with :
      reset => (reset, _writeRequestBuffer_WIRE) @[multi_uart.scala 26:35]
    node _T = and(client.ARREADY, client.ARVALID) @[multi_uart.scala 42:23]
    when _T : @[multi_uart.scala 42:42]
      readRequestBuffer.valid <= UInt<1>("h1") @[multi_uart.scala 43:29]
      readRequestBuffer.address <= client.ARADDR @[multi_uart.scala 44:31]
      readRequestBuffer.len <= client.ARLEN @[multi_uart.scala 45:27]
      readRequestBuffer.size <= client.ARSIZE @[multi_uart.scala 46:28]
      readRequestBuffer.id <= client.ARID @[multi_uart.scala 47:26]
    node _T_1 = and(readRequestBuffer.valid, client.RREADY) @[multi_uart.scala 50:32]
    when _T_1 : @[multi_uart.scala 50:50]
      node _readRequestBuffer_len_T = sub(readRequestBuffer.len, UInt<1>("h1")) @[multi_uart.scala 51:52]
      node _readRequestBuffer_len_T_1 = tail(_readRequestBuffer_len_T, 1) @[multi_uart.scala 51:52]
      readRequestBuffer.len <= _readRequestBuffer_len_T_1 @[multi_uart.scala 51:27]
      node _T_2 = orr(readRequestBuffer.len) @[multi_uart.scala 52:33]
      node _T_3 = eq(_T_2, UInt<1>("h0")) @[multi_uart.scala 52:10]
      when _T_3 : @[multi_uart.scala 52:38]
        readRequestBuffer.valid <= UInt<1>("h0") @[multi_uart.scala 52:64]
    reg mtime : UInt<64>, clock with :
      reset => (reset, UInt<64>("h0")) @[multi_uart.scala 54:22]
    reg mtimecmp : UInt<64>, clock with :
      reset => (reset, UInt<64>("h0")) @[multi_uart.scala 55:25]
    reg mtimecmplowtemp : UInt<32>, clock with :
      reset => (UInt<1>("h0"), mtimecmplowtemp) @[multi_uart.scala 56:28]
    reg couter_wrap : UInt<4>, clock with :
      reset => (reset, UInt<4>("h0")) @[multi_uart.scala 57:28]
    node _couter_wrap_T = add(couter_wrap, UInt<1>("h1")) @[multi_uart.scala 58:30]
    node _couter_wrap_T_1 = tail(_couter_wrap_T, 1) @[multi_uart.scala 58:30]
    couter_wrap <= _couter_wrap_T_1 @[multi_uart.scala 58:15]
    node _mtime_T = andr(couter_wrap) @[multi_uart.scala 59:32]
    node _mtime_T_1 = add(mtime, _mtime_T) @[multi_uart.scala 59:18]
    node _mtime_T_2 = tail(_mtime_T_1, 1) @[multi_uart.scala 59:18]
    mtime <= _mtime_T_2 @[multi_uart.scala 59:9]
    reg mtimeRead : UInt<64>, clock with :
      reset => (UInt<1>("h0"), mtimeRead) @[multi_uart.scala 60:22]
    node _T_4 = and(client.ARREADY, client.ARVALID) @[multi_uart.scala 61:23]
    when _T_4 : @[multi_uart.scala 61:42]
      mtimeRead <= mtime @[multi_uart.scala 62:15]
    reg writeData : UInt<64>, clock with :
      reset => (UInt<1>("h0"), writeData) @[multi_uart.scala 66:22]
    reg ps_stat : UInt<32>, clock with :
      reset => (reset, UInt<32>("h0")) @[multi_uart.scala 69:24]
    client.RDATA <= UInt<4>("h8") @[multi_uart.scala 70:16]
    node _T_5 = eq(UInt<32>("he000002c"), readRequestBuffer.address) @[multi_uart.scala 71:37]
    when _T_5 : @[multi_uart.scala 71:37]
      client.RDATA <= UInt<2>("h2") @[multi_uart.scala 72:38]
    else :
      node _T_6 = eq(UInt<32>("he000102c"), readRequestBuffer.address) @[multi_uart.scala 71:37]
      when _T_6 : @[multi_uart.scala 71:37]
        client.RDATA <= UInt<2>("h2") @[multi_uart.scala 73:38]
      else :
        node _T_7 = eq(UInt<26>("h200bff8"), readRequestBuffer.address) @[multi_uart.scala 71:37]
        when _T_7 : @[multi_uart.scala 71:37]
          node _client_RDATA_T = orr(readRequestBuffer.len) @[multi_uart.scala 74:67]
          node _client_RDATA_T_1 = bits(mtimeRead, 31, 0) @[multi_uart.scala 74:81]
          node _client_RDATA_T_2 = bits(mtimeRead, 63, 32) @[multi_uart.scala 74:99]
          node _client_RDATA_T_3 = mux(_client_RDATA_T, _client_RDATA_T_1, _client_RDATA_T_2) @[multi_uart.scala 74:44]
          client.RDATA <= _client_RDATA_T_3 @[multi_uart.scala 74:38]
        else :
          node _T_8 = eq(UInt<27>("h4000000"), readRequestBuffer.address) @[multi_uart.scala 71:37]
          when _T_8 : @[multi_uart.scala 71:37]
            client.RDATA <= ps_stat @[multi_uart.scala 75:38]
    client.RID <= readRequestBuffer.id @[multi_uart.scala 77:14]
    node _client_RLAST_T = orr(readRequestBuffer.len) @[multi_uart.scala 78:42]
    node _client_RLAST_T_1 = eq(_client_RLAST_T, UInt<1>("h0")) @[multi_uart.scala 78:19]
    client.RLAST <= _client_RLAST_T_1 @[multi_uart.scala 78:16]
    client.RRESP <= UInt<1>("h0") @[multi_uart.scala 79:16]
    client.RVALID <= readRequestBuffer.valid @[multi_uart.scala 80:17]
    wire putChar : { valid : UInt<1>, byte : UInt<8>} @[multi_uart.scala 82:21]
    node _putChar_valid_T = and(writeRequestBuffer.address.offset, UInt<8>("hff")) @[multi_uart.scala 86:58]
    node _putChar_valid_T_1 = eq(_putChar_valid_T, UInt<6>("h30")) @[multi_uart.scala 86:70]
    node _putChar_valid_T_2 = and(_putChar_valid_T_1, writeRequestBuffer.address.valid) @[multi_uart.scala 86:159]
    node _putChar_valid_T_3 = and(_putChar_valid_T_2, writeRequestBuffer.data.valid) @[multi_uart.scala 86:159]
    putChar.valid <= _putChar_valid_T_3 @[multi_uart.scala 86:17]
    node _putChar_byte_T = bits(writeRequestBuffer.data.data, 7, 0) @[multi_uart.scala 87:47]
    putChar.byte <= _putChar_byte_T @[multi_uart.scala 87:16]
    wire _lastUartChars_WIRE : UInt<8>[17] @[multi_uart.scala 89:38]
    _lastUartChars_WIRE[0] <= UInt<8>("h0") @[multi_uart.scala 89:38]
    _lastUartChars_WIRE[1] <= UInt<8>("h0") @[multi_uart.scala 89:38]
    _lastUartChars_WIRE[2] <= UInt<8>("h0") @[multi_uart.scala 89:38]
    _lastUartChars_WIRE[3] <= UInt<8>("h0") @[multi_uart.scala 89:38]
    _lastUartChars_WIRE[4] <= UInt<8>("h0") @[multi_uart.scala 89:38]
    _lastUartChars_WIRE[5] <= UInt<8>("h0") @[multi_uart.scala 89:38]
    _lastUartChars_WIRE[6] <= UInt<8>("h0") @[multi_uart.scala 89:38]
    _lastUartChars_WIRE[7] <= UInt<8>("h0") @[multi_uart.scala 89:38]
    _lastUartChars_WIRE[8] <= UInt<8>("h0") @[multi_uart.scala 89:38]
    _lastUartChars_WIRE[9] <= UInt<8>("h0") @[multi_uart.scala 89:38]
    _lastUartChars_WIRE[10] <= UInt<8>("h0") @[multi_uart.scala 89:38]
    _lastUartChars_WIRE[11] <= UInt<8>("h0") @[multi_uart.scala 89:38]
    _lastUartChars_WIRE[12] <= UInt<8>("h0") @[multi_uart.scala 89:38]
    _lastUartChars_WIRE[13] <= UInt<8>("h0") @[multi_uart.scala 89:38]
    _lastUartChars_WIRE[14] <= UInt<8>("h0") @[multi_uart.scala 89:38]
    _lastUartChars_WIRE[15] <= UInt<8>("h0") @[multi_uart.scala 89:38]
    _lastUartChars_WIRE[16] <= UInt<8>("h0") @[multi_uart.scala 89:38]
    reg lastUartChars : UInt<8>[17], clock with :
      reset => (reset, _lastUartChars_WIRE) @[multi_uart.scala 89:30]
    when putChar.valid : @[multi_uart.scala 90:23]
      lastUartChars[0] <= putChar.byte @[multi_uart.scala 92:47]
      lastUartChars[1] <= lastUartChars[0] @[multi_uart.scala 92:47]
      lastUartChars[2] <= lastUartChars[1] @[multi_uart.scala 92:47]
      lastUartChars[3] <= lastUartChars[2] @[multi_uart.scala 92:47]
      lastUartChars[4] <= lastUartChars[3] @[multi_uart.scala 92:47]
      lastUartChars[5] <= lastUartChars[4] @[multi_uart.scala 92:47]
      lastUartChars[6] <= lastUartChars[5] @[multi_uart.scala 92:47]
      lastUartChars[7] <= lastUartChars[6] @[multi_uart.scala 92:47]
      lastUartChars[8] <= lastUartChars[7] @[multi_uart.scala 92:47]
      lastUartChars[9] <= lastUartChars[8] @[multi_uart.scala 92:47]
      lastUartChars[10] <= lastUartChars[9] @[multi_uart.scala 92:47]
      lastUartChars[11] <= lastUartChars[10] @[multi_uart.scala 92:47]
      lastUartChars[12] <= lastUartChars[11] @[multi_uart.scala 92:47]
      lastUartChars[13] <= lastUartChars[12] @[multi_uart.scala 92:47]
      lastUartChars[14] <= lastUartChars[13] @[multi_uart.scala 92:47]
      lastUartChars[15] <= lastUartChars[14] @[multi_uart.scala 92:47]
      lastUartChars[16] <= lastUartChars[15] @[multi_uart.scala 92:47]
    node _T_9 = and(writeRequestBuffer.address.valid, writeRequestBuffer.data.valid) @[multi_uart.scala 151:41]
    when _T_9 : @[multi_uart.scala 151:75]
      writeRequestBuffer.data.valid <= UInt<1>("h0") @[multi_uart.scala 152:35]
      when writeRequestBuffer.data.last : @[multi_uart.scala 153:40]
        writeRequestBuffer.address.valid <= UInt<1>("h0") @[multi_uart.scala 154:40]
    node _T_10 = and(client.AWREADY, client.AWVALID) @[multi_uart.scala 158:23]
    when _T_10 : @[multi_uart.scala 158:42]
      writeRequestBuffer.address.valid <= UInt<1>("h1") @[multi_uart.scala 159:38]
      writeRequestBuffer.address.offset <= client.AWADDR @[multi_uart.scala 160:39]
      writeRequestBuffer.address.id <= client.AWID @[multi_uart.scala 161:35]
      writeRequestBuffer.address.len <= client.AWLEN @[multi_uart.scala 162:36]
      writeRequestBuffer.address.size <= client.AWSIZE @[multi_uart.scala 163:37]
    node _T_11 = and(client.WREADY, client.WVALID) @[multi_uart.scala 166:22]
    when _T_11 : @[multi_uart.scala 166:40]
      writeRequestBuffer.data.valid <= UInt<1>("h1") @[multi_uart.scala 167:35]
      writeRequestBuffer.data.data <= client.WDATA @[multi_uart.scala 168:34]
      writeRequestBuffer.data.last <= client.WLAST @[multi_uart.scala 169:34]
      writeRequestBuffer.data.strb <= client.WSTRB @[multi_uart.scala 170:34]
    node _T_12 = eq(writeRequestBuffer.data.last, UInt<1>("h0")) @[multi_uart.scala 173:41]
    node _T_13 = and(writeRequestBuffer.data.valid, _T_12) @[multi_uart.scala 173:38]
    when _T_13 : @[multi_uart.scala 173:72]
      mtimecmplowtemp <= writeRequestBuffer.data.data @[multi_uart.scala 173:90]
    node _T_14 = eq(writeRequestBuffer.address.offset, UInt<26>("h2004000")) @[multi_uart.scala 176:40]
    node _T_15 = and(writeRequestBuffer.address.valid, _T_14) @[multi_uart.scala 175:38]
    node _T_16 = and(_T_15, writeRequestBuffer.data.valid) @[multi_uart.scala 176:59]
    node _T_17 = and(_T_16, writeRequestBuffer.data.last) @[multi_uart.scala 177:35]
    when _T_17 : @[multi_uart.scala 179:5]
      node _mtimecmp_T = cat(writeRequestBuffer.data.data, mtimecmplowtemp) @[Cat.scala 33:92]
      mtimecmp <= _mtimecmp_T @[multi_uart.scala 180:14]
    node _client_ARREADY_T = eq(readRequestBuffer.valid, UInt<1>("h0")) @[multi_uart.scala 183:21]
    client.ARREADY <= _client_ARREADY_T @[multi_uart.scala 183:18]
    node _client_AWREADY_T = eq(writeRequestBuffer.address.valid, UInt<1>("h0")) @[multi_uart.scala 185:21]
    client.AWREADY <= _client_AWREADY_T @[multi_uart.scala 185:18]
    node _client_WREADY_T = eq(writeRequestBuffer.data.valid, UInt<1>("h0")) @[multi_uart.scala 186:20]
    node _client_WREADY_T_1 = or(_client_WREADY_T, writeRequestBuffer.address.valid) @[multi_uart.scala 186:51]
    client.WREADY <= _client_WREADY_T_1 @[multi_uart.scala 186:17]
    client.BID <= writeRequestBuffer.address.id @[multi_uart.scala 188:14]
    client.BRESP <= UInt<1>("h0") @[multi_uart.scala 189:16]
    node _client_BVALID_T = and(writeRequestBuffer.address.valid, writeRequestBuffer.data.valid) @[multi_uart.scala 190:53]
    node _client_BVALID_T_1 = and(_client_BVALID_T, writeRequestBuffer.data.last) @[multi_uart.scala 190:86]
    client.BVALID <= _client_BVALID_T_1 @[multi_uart.scala 190:17]
    node _MTIP_T = gt(mtime, mtimecmp) @[multi_uart.scala 193:18]
    MTIP <= _MTIP_T @[multi_uart.scala 193:8]
    putCharOut0 <= putChar @[multi_uart.scala 203:17]

  module MultiUart_Anon_1 :
    input clock : Clock
    input reset : Reset
    input client : { AWID : UInt<1>, AWADDR : UInt<32>, AWLEN : UInt<8>, AWSIZE : UInt<3>, AWBURST : UInt<2>, AWLOCK : UInt<1>, AWCACHE : UInt<4>, AWPROT : UInt<3>, AWQOS : UInt<4>, AWVALID : UInt<1>, flip AWREADY : UInt<1>, WDATA : UInt<32>, WSTRB : UInt<4>, WLAST : UInt<1>, WVALID : UInt<1>, flip WREADY : UInt<1>, flip BID : UInt<1>, flip BRESP : UInt<2>, flip BVALID : UInt<1>, BREADY : UInt<1>, ARID : UInt<1>, ARADDR : UInt<32>, ARLEN : UInt<8>, ARSIZE : UInt<3>, ARBURST : UInt<2>, ARLOCK : UInt<1>, ARCACHE : UInt<4>, ARPROT : UInt<3>, ARQOS : UInt<4>, ARVALID : UInt<1>, flip ARREADY : UInt<1>, flip RID : UInt<1>, flip RDATA : UInt<32>, flip RRESP : UInt<2>, flip RLAST : UInt<1>, flip RVALID : UInt<1>, RREADY : UInt<1>}
    output MTIP : UInt<1>
    output putCharOut1 : { valid : UInt<1>, byte : UInt<8>}

    wire _readRequestBuffer_WIRE : { valid : UInt<1>, address : UInt<32>, size : UInt<3>, len : UInt<8>, id : UInt<1>}
    _readRequestBuffer_WIRE.id is invalid
    _readRequestBuffer_WIRE.len is invalid
    _readRequestBuffer_WIRE.size is invalid
    _readRequestBuffer_WIRE.address is invalid
    _readRequestBuffer_WIRE.valid <= UInt<1>("h0")
    reg readRequestBuffer : { valid : UInt<1>, address : UInt<32>, size : UInt<3>, len : UInt<8>, id : UInt<1>}, clock with :
      reset => (reset, _readRequestBuffer_WIRE) @[multi_uart.scala 18:34]
    wire _writeRequestBuffer_WIRE : { address : { valid : UInt<1>, offset : UInt<32>, size : UInt<3>, len : UInt<8>, id : UInt<1>}, data : { valid : UInt<1>, data : UInt<32>, last : UInt<1>, strb : UInt<4>}}
    _writeRequestBuffer_WIRE.data.strb is invalid
    _writeRequestBuffer_WIRE.data.last is invalid
    _writeRequestBuffer_WIRE.data.data is invalid
    _writeRequestBuffer_WIRE.data.valid <= UInt<1>("h0")
    _writeRequestBuffer_WIRE.address.id is invalid
    _writeRequestBuffer_WIRE.address.len is invalid
    _writeRequestBuffer_WIRE.address.size is invalid
    _writeRequestBuffer_WIRE.address.offset is invalid
    _writeRequestBuffer_WIRE.address.valid <= UInt<1>("h0")
    reg writeRequestBuffer : { address : { valid : UInt<1>, offset : UInt<32>, size : UInt<3>, len : UInt<8>, id : UInt<1>}, data : { valid : UInt<1>, data : UInt<32>, last : UInt<1>, strb : UInt<4>}}, clock with :
      reset => (reset, _writeRequestBuffer_WIRE) @[multi_uart.scala 26:35]
    node _T = and(client.ARREADY, client.ARVALID) @[multi_uart.scala 42:23]
    when _T : @[multi_uart.scala 42:42]
      readRequestBuffer.valid <= UInt<1>("h1") @[multi_uart.scala 43:29]
      readRequestBuffer.address <= client.ARADDR @[multi_uart.scala 44:31]
      readRequestBuffer.len <= client.ARLEN @[multi_uart.scala 45:27]
      readRequestBuffer.size <= client.ARSIZE @[multi_uart.scala 46:28]
      readRequestBuffer.id <= client.ARID @[multi_uart.scala 47:26]
    node _T_1 = and(readRequestBuffer.valid, client.RREADY) @[multi_uart.scala 50:32]
    when _T_1 : @[multi_uart.scala 50:50]
      node _readRequestBuffer_len_T = sub(readRequestBuffer.len, UInt<1>("h1")) @[multi_uart.scala 51:52]
      node _readRequestBuffer_len_T_1 = tail(_readRequestBuffer_len_T, 1) @[multi_uart.scala 51:52]
      readRequestBuffer.len <= _readRequestBuffer_len_T_1 @[multi_uart.scala 51:27]
      node _T_2 = orr(readRequestBuffer.len) @[multi_uart.scala 52:33]
      node _T_3 = eq(_T_2, UInt<1>("h0")) @[multi_uart.scala 52:10]
      when _T_3 : @[multi_uart.scala 52:38]
        readRequestBuffer.valid <= UInt<1>("h0") @[multi_uart.scala 52:64]
    reg mtime : UInt<64>, clock with :
      reset => (reset, UInt<64>("h0")) @[multi_uart.scala 54:22]
    reg mtimecmp : UInt<64>, clock with :
      reset => (reset, UInt<64>("h0")) @[multi_uart.scala 55:25]
    reg mtimecmplowtemp : UInt<32>, clock with :
      reset => (UInt<1>("h0"), mtimecmplowtemp) @[multi_uart.scala 56:28]
    reg couter_wrap : UInt<4>, clock with :
      reset => (reset, UInt<4>("h0")) @[multi_uart.scala 57:28]
    node _couter_wrap_T = add(couter_wrap, UInt<1>("h1")) @[multi_uart.scala 58:30]
    node _couter_wrap_T_1 = tail(_couter_wrap_T, 1) @[multi_uart.scala 58:30]
    couter_wrap <= _couter_wrap_T_1 @[multi_uart.scala 58:15]
    node _mtime_T = andr(couter_wrap) @[multi_uart.scala 59:32]
    node _mtime_T_1 = add(mtime, _mtime_T) @[multi_uart.scala 59:18]
    node _mtime_T_2 = tail(_mtime_T_1, 1) @[multi_uart.scala 59:18]
    mtime <= _mtime_T_2 @[multi_uart.scala 59:9]
    reg mtimeRead : UInt<64>, clock with :
      reset => (UInt<1>("h0"), mtimeRead) @[multi_uart.scala 60:22]
    node _T_4 = and(client.ARREADY, client.ARVALID) @[multi_uart.scala 61:23]
    when _T_4 : @[multi_uart.scala 61:42]
      mtimeRead <= mtime @[multi_uart.scala 62:15]
    reg writeData : UInt<64>, clock with :
      reset => (UInt<1>("h0"), writeData) @[multi_uart.scala 66:22]
    reg ps_stat : UInt<32>, clock with :
      reset => (reset, UInt<32>("h0")) @[multi_uart.scala 69:24]
    client.RDATA <= UInt<4>("h8") @[multi_uart.scala 70:16]
    node _T_5 = eq(UInt<32>("he000002c"), readRequestBuffer.address) @[multi_uart.scala 71:37]
    when _T_5 : @[multi_uart.scala 71:37]
      client.RDATA <= UInt<2>("h2") @[multi_uart.scala 72:38]
    else :
      node _T_6 = eq(UInt<32>("he000102c"), readRequestBuffer.address) @[multi_uart.scala 71:37]
      when _T_6 : @[multi_uart.scala 71:37]
        client.RDATA <= UInt<2>("h2") @[multi_uart.scala 73:38]
      else :
        node _T_7 = eq(UInt<26>("h200bff8"), readRequestBuffer.address) @[multi_uart.scala 71:37]
        when _T_7 : @[multi_uart.scala 71:37]
          node _client_RDATA_T = orr(readRequestBuffer.len) @[multi_uart.scala 74:67]
          node _client_RDATA_T_1 = bits(mtimeRead, 31, 0) @[multi_uart.scala 74:81]
          node _client_RDATA_T_2 = bits(mtimeRead, 63, 32) @[multi_uart.scala 74:99]
          node _client_RDATA_T_3 = mux(_client_RDATA_T, _client_RDATA_T_1, _client_RDATA_T_2) @[multi_uart.scala 74:44]
          client.RDATA <= _client_RDATA_T_3 @[multi_uart.scala 74:38]
        else :
          node _T_8 = eq(UInt<27>("h4000000"), readRequestBuffer.address) @[multi_uart.scala 71:37]
          when _T_8 : @[multi_uart.scala 71:37]
            client.RDATA <= ps_stat @[multi_uart.scala 75:38]
    client.RID <= readRequestBuffer.id @[multi_uart.scala 77:14]
    node _client_RLAST_T = orr(readRequestBuffer.len) @[multi_uart.scala 78:42]
    node _client_RLAST_T_1 = eq(_client_RLAST_T, UInt<1>("h0")) @[multi_uart.scala 78:19]
    client.RLAST <= _client_RLAST_T_1 @[multi_uart.scala 78:16]
    client.RRESP <= UInt<1>("h0") @[multi_uart.scala 79:16]
    client.RVALID <= readRequestBuffer.valid @[multi_uart.scala 80:17]
    wire putChar : { valid : UInt<1>, byte : UInt<8>} @[multi_uart.scala 82:21]
    node _putChar_valid_T = and(writeRequestBuffer.address.offset, UInt<8>("hff")) @[multi_uart.scala 86:58]
    node _putChar_valid_T_1 = eq(_putChar_valid_T, UInt<6>("h30")) @[multi_uart.scala 86:70]
    node _putChar_valid_T_2 = and(_putChar_valid_T_1, writeRequestBuffer.address.valid) @[multi_uart.scala 86:159]
    node _putChar_valid_T_3 = and(_putChar_valid_T_2, writeRequestBuffer.data.valid) @[multi_uart.scala 86:159]
    putChar.valid <= _putChar_valid_T_3 @[multi_uart.scala 86:17]
    node _putChar_byte_T = bits(writeRequestBuffer.data.data, 7, 0) @[multi_uart.scala 87:47]
    putChar.byte <= _putChar_byte_T @[multi_uart.scala 87:16]
    wire _lastUartChars_WIRE : UInt<8>[17] @[multi_uart.scala 89:38]
    _lastUartChars_WIRE[0] <= UInt<8>("h0") @[multi_uart.scala 89:38]
    _lastUartChars_WIRE[1] <= UInt<8>("h0") @[multi_uart.scala 89:38]
    _lastUartChars_WIRE[2] <= UInt<8>("h0") @[multi_uart.scala 89:38]
    _lastUartChars_WIRE[3] <= UInt<8>("h0") @[multi_uart.scala 89:38]
    _lastUartChars_WIRE[4] <= UInt<8>("h0") @[multi_uart.scala 89:38]
    _lastUartChars_WIRE[5] <= UInt<8>("h0") @[multi_uart.scala 89:38]
    _lastUartChars_WIRE[6] <= UInt<8>("h0") @[multi_uart.scala 89:38]
    _lastUartChars_WIRE[7] <= UInt<8>("h0") @[multi_uart.scala 89:38]
    _lastUartChars_WIRE[8] <= UInt<8>("h0") @[multi_uart.scala 89:38]
    _lastUartChars_WIRE[9] <= UInt<8>("h0") @[multi_uart.scala 89:38]
    _lastUartChars_WIRE[10] <= UInt<8>("h0") @[multi_uart.scala 89:38]
    _lastUartChars_WIRE[11] <= UInt<8>("h0") @[multi_uart.scala 89:38]
    _lastUartChars_WIRE[12] <= UInt<8>("h0") @[multi_uart.scala 89:38]
    _lastUartChars_WIRE[13] <= UInt<8>("h0") @[multi_uart.scala 89:38]
    _lastUartChars_WIRE[14] <= UInt<8>("h0") @[multi_uart.scala 89:38]
    _lastUartChars_WIRE[15] <= UInt<8>("h0") @[multi_uart.scala 89:38]
    _lastUartChars_WIRE[16] <= UInt<8>("h0") @[multi_uart.scala 89:38]
    reg lastUartChars : UInt<8>[17], clock with :
      reset => (reset, _lastUartChars_WIRE) @[multi_uart.scala 89:30]
    when putChar.valid : @[multi_uart.scala 90:23]
      lastUartChars[0] <= putChar.byte @[multi_uart.scala 92:47]
      lastUartChars[1] <= lastUartChars[0] @[multi_uart.scala 92:47]
      lastUartChars[2] <= lastUartChars[1] @[multi_uart.scala 92:47]
      lastUartChars[3] <= lastUartChars[2] @[multi_uart.scala 92:47]
      lastUartChars[4] <= lastUartChars[3] @[multi_uart.scala 92:47]
      lastUartChars[5] <= lastUartChars[4] @[multi_uart.scala 92:47]
      lastUartChars[6] <= lastUartChars[5] @[multi_uart.scala 92:47]
      lastUartChars[7] <= lastUartChars[6] @[multi_uart.scala 92:47]
      lastUartChars[8] <= lastUartChars[7] @[multi_uart.scala 92:47]
      lastUartChars[9] <= lastUartChars[8] @[multi_uart.scala 92:47]
      lastUartChars[10] <= lastUartChars[9] @[multi_uart.scala 92:47]
      lastUartChars[11] <= lastUartChars[10] @[multi_uart.scala 92:47]
      lastUartChars[12] <= lastUartChars[11] @[multi_uart.scala 92:47]
      lastUartChars[13] <= lastUartChars[12] @[multi_uart.scala 92:47]
      lastUartChars[14] <= lastUartChars[13] @[multi_uart.scala 92:47]
      lastUartChars[15] <= lastUartChars[14] @[multi_uart.scala 92:47]
      lastUartChars[16] <= lastUartChars[15] @[multi_uart.scala 92:47]
    node _T_9 = and(writeRequestBuffer.address.valid, writeRequestBuffer.data.valid) @[multi_uart.scala 151:41]
    when _T_9 : @[multi_uart.scala 151:75]
      writeRequestBuffer.data.valid <= UInt<1>("h0") @[multi_uart.scala 152:35]
      when writeRequestBuffer.data.last : @[multi_uart.scala 153:40]
        writeRequestBuffer.address.valid <= UInt<1>("h0") @[multi_uart.scala 154:40]
    node _T_10 = and(client.AWREADY, client.AWVALID) @[multi_uart.scala 158:23]
    when _T_10 : @[multi_uart.scala 158:42]
      writeRequestBuffer.address.valid <= UInt<1>("h1") @[multi_uart.scala 159:38]
      writeRequestBuffer.address.offset <= client.AWADDR @[multi_uart.scala 160:39]
      writeRequestBuffer.address.id <= client.AWID @[multi_uart.scala 161:35]
      writeRequestBuffer.address.len <= client.AWLEN @[multi_uart.scala 162:36]
      writeRequestBuffer.address.size <= client.AWSIZE @[multi_uart.scala 163:37]
    node _T_11 = and(client.WREADY, client.WVALID) @[multi_uart.scala 166:22]
    when _T_11 : @[multi_uart.scala 166:40]
      writeRequestBuffer.data.valid <= UInt<1>("h1") @[multi_uart.scala 167:35]
      writeRequestBuffer.data.data <= client.WDATA @[multi_uart.scala 168:34]
      writeRequestBuffer.data.last <= client.WLAST @[multi_uart.scala 169:34]
      writeRequestBuffer.data.strb <= client.WSTRB @[multi_uart.scala 170:34]
    node _T_12 = eq(writeRequestBuffer.data.last, UInt<1>("h0")) @[multi_uart.scala 173:41]
    node _T_13 = and(writeRequestBuffer.data.valid, _T_12) @[multi_uart.scala 173:38]
    when _T_13 : @[multi_uart.scala 173:72]
      mtimecmplowtemp <= writeRequestBuffer.data.data @[multi_uart.scala 173:90]
    node _T_14 = eq(writeRequestBuffer.address.offset, UInt<26>("h2004000")) @[multi_uart.scala 176:40]
    node _T_15 = and(writeRequestBuffer.address.valid, _T_14) @[multi_uart.scala 175:38]
    node _T_16 = and(_T_15, writeRequestBuffer.data.valid) @[multi_uart.scala 176:59]
    node _T_17 = and(_T_16, writeRequestBuffer.data.last) @[multi_uart.scala 177:35]
    when _T_17 : @[multi_uart.scala 179:5]
      node _mtimecmp_T = cat(writeRequestBuffer.data.data, mtimecmplowtemp) @[Cat.scala 33:92]
      mtimecmp <= _mtimecmp_T @[multi_uart.scala 180:14]
    node _client_ARREADY_T = eq(readRequestBuffer.valid, UInt<1>("h0")) @[multi_uart.scala 183:21]
    client.ARREADY <= _client_ARREADY_T @[multi_uart.scala 183:18]
    node _client_AWREADY_T = eq(writeRequestBuffer.address.valid, UInt<1>("h0")) @[multi_uart.scala 185:21]
    client.AWREADY <= _client_AWREADY_T @[multi_uart.scala 185:18]
    node _client_WREADY_T = eq(writeRequestBuffer.data.valid, UInt<1>("h0")) @[multi_uart.scala 186:20]
    node _client_WREADY_T_1 = or(_client_WREADY_T, writeRequestBuffer.address.valid) @[multi_uart.scala 186:51]
    client.WREADY <= _client_WREADY_T_1 @[multi_uart.scala 186:17]
    client.BID <= writeRequestBuffer.address.id @[multi_uart.scala 188:14]
    client.BRESP <= UInt<1>("h0") @[multi_uart.scala 189:16]
    node _client_BVALID_T = and(writeRequestBuffer.address.valid, writeRequestBuffer.data.valid) @[multi_uart.scala 190:53]
    node _client_BVALID_T_1 = and(_client_BVALID_T, writeRequestBuffer.data.last) @[multi_uart.scala 190:86]
    client.BVALID <= _client_BVALID_T_1 @[multi_uart.scala 190:17]
    node _MTIP_T = gt(mtime, mtimecmp) @[multi_uart.scala 193:18]
    MTIP <= _MTIP_T @[multi_uart.scala 193:8]
    putCharOut1 <= putChar @[multi_uart.scala 207:17]

  module MultiUart :
    input clock : Clock
    input reset : UInt<1>
    input client0 : { AWID : UInt<1>, AWADDR : UInt<32>, AWLEN : UInt<8>, AWSIZE : UInt<3>, AWBURST : UInt<2>, AWLOCK : UInt<1>, AWCACHE : UInt<4>, AWPROT : UInt<3>, AWQOS : UInt<4>, AWVALID : UInt<1>, flip AWREADY : UInt<1>, WDATA : UInt<32>, WSTRB : UInt<4>, WLAST : UInt<1>, WVALID : UInt<1>, flip WREADY : UInt<1>, flip BID : UInt<1>, flip BRESP : UInt<2>, flip BVALID : UInt<1>, BREADY : UInt<1>, ARID : UInt<1>, ARADDR : UInt<32>, ARLEN : UInt<8>, ARSIZE : UInt<3>, ARBURST : UInt<2>, ARLOCK : UInt<1>, ARCACHE : UInt<4>, ARPROT : UInt<3>, ARQOS : UInt<4>, ARVALID : UInt<1>, flip ARREADY : UInt<1>, flip RID : UInt<1>, flip RDATA : UInt<32>, flip RRESP : UInt<2>, flip RLAST : UInt<1>, flip RVALID : UInt<1>, RREADY : UInt<1>}
    input client1 : { AWID : UInt<1>, AWADDR : UInt<32>, AWLEN : UInt<8>, AWSIZE : UInt<3>, AWBURST : UInt<2>, AWLOCK : UInt<1>, AWCACHE : UInt<4>, AWPROT : UInt<3>, AWQOS : UInt<4>, AWVALID : UInt<1>, flip AWREADY : UInt<1>, WDATA : UInt<32>, WSTRB : UInt<4>, WLAST : UInt<1>, WVALID : UInt<1>, flip WREADY : UInt<1>, flip BID : UInt<1>, flip BRESP : UInt<2>, flip BVALID : UInt<1>, BREADY : UInt<1>, ARID : UInt<1>, ARADDR : UInt<32>, ARLEN : UInt<8>, ARSIZE : UInt<3>, ARBURST : UInt<2>, ARLOCK : UInt<1>, ARCACHE : UInt<4>, ARPROT : UInt<3>, ARQOS : UInt<4>, ARVALID : UInt<1>, flip ARREADY : UInt<1>, flip RID : UInt<1>, flip RDATA : UInt<32>, flip RRESP : UInt<2>, flip RLAST : UInt<1>, flip RVALID : UInt<1>, RREADY : UInt<1>}
    output putChar0 : { valid : UInt<1>, byte : UInt<8>}
    output putChar1 : { valid : UInt<1>, byte : UInt<8>}
    output MTIP0 : UInt<1>
    output MTIP1 : UInt<1>

    inst uart0 of MultiUart_Anon @[multi_uart.scala 201:21]
    uart0.clock <= clock
    uart0.reset <= reset
    inst uart1 of MultiUart_Anon_1 @[multi_uart.scala 205:21]
    uart1.clock <= clock
    uart1.reset <= reset
    uart0.client <= client0 @[multi_uart.scala 210:16]
    uart1.client <= client1 @[multi_uart.scala 211:16]
    putChar0 <= uart0.putCharOut0 @[multi_uart.scala 215:12]
    putChar1 <= uart1.putCharOut1 @[multi_uart.scala 218:12]
    MTIP0 <= uart0.MTIP @[multi_uart.scala 223:9]
    MTIP1 <= uart1.MTIP @[multi_uart.scala 224:9]

