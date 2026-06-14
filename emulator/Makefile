rv_test: src/rv_tests.cpp $(wildcard src/*.h)
	g++ -O3 -o rv_test src/rv_tests.cpp

emulator: src/emulator_linux.cpp $(wildcard src/*.h)
	g++ -O3 -o emulator src/emulator_linux.cpp

linux: emulator
	./emulator Image

run_rv_tests: rv_test
	./rv_test riscv-tests/images/rv64ua-p-amoadd_d.bin riscv-tests/images/rv64ua-p-amoadd_w.bin riscv-tests/images/rv64ua-p-amoand_d.bin riscv-tests/images/rv64ua-p-amoand_w.bin \
riscv-tests/images/rv64ua-p-amomax_d.bin riscv-tests/images/rv64ua-p-amomax_w.bin riscv-tests/images/rv64ua-p-amomaxu_d.bin riscv-tests/images/rv64ua-p-amomaxu_w.bin \
riscv-tests/images/rv64ua-p-amomin_d.bin riscv-tests/images/rv64ua-p-amomin_w.bin riscv-tests/images/rv64ua-p-amominu_d.bin riscv-tests/images/rv64ua-p-amominu_w.bin \
riscv-tests/images/rv64ua-p-amoor_d.bin riscv-tests/images/rv64ua-p-amoor_w.bin riscv-tests/images/rv64ua-p-amoswap_d.bin riscv-tests/images/rv64ua-p-amoswap_w.bin \
riscv-tests/images/rv64ua-p-amoxor_d.bin riscv-tests/images/rv64ua-p-amoxor_w.bin riscv-tests/images/rv64ua-p-lrsc.bin riscv-tests/images/rv64uf-p-fadd.bin \
riscv-tests/images/rv64uf-p-fclass.bin riscv-tests/images/rv64uf-p-fcmp.bin riscv-tests/images/rv64uf-p-fcvt.bin riscv-tests/images/rv64uf-p-fcvt_w.bin \
riscv-tests/images/rv64uf-p-fdiv.bin riscv-tests/images/rv64uf-p-fmadd.bin riscv-tests/images/rv64uf-p-fmin.bin riscv-tests/images/rv64uf-p-ldst.bin \
riscv-tests/images/rv64uf-p-move.bin riscv-tests/images/rv64uf-p-recoding.bin riscv-tests/images/rv64ui-p-add.bin riscv-tests/images/rv64ui-p-addi.bin \
riscv-tests/images/rv64ui-p-addiw.bin riscv-tests/images/rv64ui-p-addw.bin riscv-tests/images/rv64ui-p-and.bin riscv-tests/images/rv64ui-p-andi.bin \
riscv-tests/images/rv64ui-p-auipc.bin riscv-tests/images/rv64ui-p-beq.bin riscv-tests/images/rv64ui-p-bge.bin riscv-tests/images/rv64ui-p-bgeu.bin \
riscv-tests/images/rv64ui-p-blt.bin riscv-tests/images/rv64ui-p-bltu.bin riscv-tests/images/rv64ui-p-bne.bin riscv-tests/images/rv64ui-p-fence_i.bin \
riscv-tests/images/rv64ui-p-jal.bin riscv-tests/images/rv64ui-p-jalr.bin riscv-tests/images/rv64ui-p-lb.bin riscv-tests/images/rv64ui-p-lbu.bin \
riscv-tests/images/rv64ui-p-ld.bin riscv-tests/images/rv64ui-p-lh.bin riscv-tests/images/rv64ui-p-lhu.bin riscv-tests/images/rv64ui-p-lui.bin \
riscv-tests/images/rv64ui-p-lw.bin riscv-tests/images/rv64ui-p-lwu.bin riscv-tests/images/rv64ui-p-or.bin riscv-tests/images/rv64ui-p-ori.bin \
riscv-tests/images/rv64ui-p-sb.bin riscv-tests/images/rv64ui-p-sd.bin riscv-tests/images/rv64ui-p-sh.bin riscv-tests/images/rv64ui-p-simple.bin \
riscv-tests/images/rv64ui-p-sll.bin riscv-tests/images/rv64ui-p-slli.bin riscv-tests/images/rv64ui-p-slliw.bin riscv-tests/images/rv64ui-p-sllw.bin \
riscv-tests/images/rv64ui-p-slt.bin riscv-tests/images/rv64ui-p-slti.bin riscv-tests/images/rv64ui-p-sltiu.bin riscv-tests/images/rv64ui-p-sltu.bin \
riscv-tests/images/rv64ui-p-sra.bin riscv-tests/images/rv64ui-p-srai.bin riscv-tests/images/rv64ui-p-sraiw.bin riscv-tests/images/rv64ui-p-sraw.bin \
riscv-tests/images/rv64ui-p-srl.bin riscv-tests/images/rv64ui-p-srli.bin riscv-tests/images/rv64ui-p-srliw.bin riscv-tests/images/rv64ui-p-srlw.bin \
riscv-tests/images/rv64ui-p-sub.bin riscv-tests/images/rv64ui-p-subw.bin riscv-tests/images/rv64ui-p-sw.bin riscv-tests/images/rv64ui-p-xor.bin \
riscv-tests/images/rv64ui-p-xori.bin riscv-tests/images/rv64um-p-div.bin riscv-tests/images/rv64um-p-divu.bin riscv-tests/images/rv64um-p-divuw.bin \
riscv-tests/images/rv64um-p-divw.bin riscv-tests/images/rv64um-p-mul.bin riscv-tests/images/rv64um-p-mulh.bin riscv-tests/images/rv64um-p-mulhsu.bin \
riscv-tests/images/rv64um-p-mulhu.bin riscv-tests/images/rv64um-p-mulw.bin riscv-tests/images/rv64um-p-rem.bin riscv-tests/images/rv64um-p-remu.bin \
riscv-tests/images/rv64um-p-remuw.bin riscv-tests/images/rv64um-p-remw.bin riscv-tests/images/rv64mi-p-ld-misaligned.bin riscv-tests/images/rv64mi-p-lh-misaligned.bin \
riscv-tests/images/rv64mi-p-lw-misaligned.bin riscv-tests/images/rv64mi-p-mcsr.bin riscv-tests/images/rv64mi-p-scall.bin riscv-tests/images/rv64mi-p-sd-misaligned.bin \
riscv-tests/images/rv64mi-p-sh-misaligned.bin riscv-tests/images/rv64mi-p-sw-misaligned.bin
