VPATH = ../scala:../cache:../common:../decode:../exec:../fetch:./memAccess:../rob:../testbench

# VADD_BINS := 1000 2000 5000 7000 10000
VADD_BINS := 1 2 3 4 5

# MATMUL_BINS := 32 64 128 256 512
MATMUL_BINS := 1 2 #3 4 5

# FILTER_BINS := 64 128 256 512 1024
FILTER_BINS := 1 2 #3 4 5

# CSAXPY_BINS := 1000 2000 5000 7000 10000
CSAXPY_BINS := 1 2 3 4 5

# HISTO_BINS := 10000 20000 50000 70000 100000
HISTO_BINS := 1 2 3 4 5

.PHONY: vvadd
vvadd: $(patsubst %, .stamp.vvadd_%, $(VADD_BINS))

.PHONY: matmul
matmul: $(patsubst %, .stamp.matmul_%, $(MATMUL_BINS))

.PHONY: filter
filter: $(patsubst %, .stamp.filter_%, $(FILTER_BINS))

.PHONY: csaxpy
csaxpy: $(patsubst %, .stamp.csaxpy_%, $(CSAXPY_BINS))

.PHONY: histo
histo: $(patsubst %, .stamp.histo_%, $(HISTO_BINS))

.PHONY: linux
linux: .stamp.linux

.PHONY: demo
demo: .stamp.demo

.PHONY: fire
fire: .stamp.fire

.PHONY: test_all_images
test_all_images: .stamp.test_all_images

.PHONY : bulk_test
bulk_test : .stamp.bulk_test

.PHONY: runLockStep
runLockStep: .stamp.runLockStep

.PHONY: sim
sim: .stamp.sim

.stamp.vvadd_%: .stamp.sim
	cp lock_step_files/lock_step_run_vvadd.cpp lock_step_run.cpp
	cp benchmark/mt-vvadd-s$*.bin fyp18-riscv-emulator/src/Image
	$(MAKE) runLockStep; \
	STATUS=$$?; \
	if [ $$STATUS -eq 0 ]; then \
		echo "vvadd-s$*: test pass" >> test_results.txt; \
	else \
		echo "vvadd-s$*: test fail" >> test_results.txt; \
	fi;
	@touch $@


.stamp.matmul_%: .stamp.sim
	cp lock_step_files/lock_step_run_matmul.cpp lock_step_run.cpp
	cp benchmark/mt-matmul-s$*.bin fyp18-riscv-emulator/src/Image
	$(MAKE) runLockStep; \
	STATUS=$$?; \
	if [ $$STATUS -eq 0 ]; then \
		echo "matmul-s$*: test pass" >> test_results.txt; \
	else \
		echo "matmul-s$*: test fail" >> test_results.txt; \
	fi;
	@touch $@


.stamp.filter_%: .stamp.sim
	cp lock_step_files/lock_step_run_filter.cpp lock_step_run.cpp
	cp benchmark/mt-mask-sfilter-s$*.bin fyp18-riscv-emulator/src/Image
	$(MAKE) runLockStep; \
	STATUS=$$?; \
	if [ $$STATUS -eq 0 ]; then \
		echo "filter-s$*: test pass" >> test_results.txt; \
	else \
		echo "filter-s$*: test fail" >> test_results.txt; \
	fi;
	@touch $@


.stamp.csaxpy_%: .stamp.sim
	cp lock_step_files/lock_step_run_csaxpy.cpp lock_step_run.cpp
	cp benchmark/mt-csaxpy-s$*.bin fyp18-riscv-emulator/src/Image
	$(MAKE) runLockStep; \
	STATUS=$$?; \
	if [ $$STATUS -eq 0 ]; then \
		echo "csaxpy-s$*: test pass" >> test_results.txt; \
	else \
		echo "csaxpy-s$*: test fail" >> test_results.txt; \
	fi;
	@touch $@

.stamp.histo_%: .stamp.sim
	cp lock_step_files/lock_step_run_histo.cpp lock_step_run.cpp
	cp benchmark/mt-histo-s$*.bin fyp18-riscv-emulator/src/Image
	$(MAKE) runLockStep; \
	STATUS=$$?; \
	if [ $$STATUS -eq 0 ]; then \
		echo "histo-s$*: test pass" >> test_results.txt; \
	else \
		echo "histo-s$*: test fail" >> test_results.txt; \
	fi;
	@touch $@

.stamp.linux: .stamp.linux .stamp.sim
	cp lock_step_files/lock_step_run.cpp lock_step_run.cpp
	cp Image fyp18-riscv-emulator/src/Image
	$(MAKE) runLockStep;
	@touch .stamp.linux

.stamp.demo: .stamp.demo .stamp.sim
	cp lock_step_files/lock_step_run.cpp lock_step_run.cpp
	cp benchmark/mt-image.bin fyp18-riscv-emulator/src/Image
	$(MAKE) runLockStep; \
	STATUS=$$?; \
	$(MAKE) python_decode; \
	@touch .stamp.demo 

.stamp.fire: .stamp.fire .stamp.sim
	cp lock_step_files/lock_step_run.cpp lock_step_run.cpp
	cp benchmark/mt-fire.bin fyp18-riscv-emulator/src/Image
	$(MAKE) runLockStep; \
	STATUS=$$?; \
	$(MAKE) python_decode; \
	@touch .stamp.fire 

.stamp.test_all_images: .stamp.sim
	cp lock_step_files/lock_step_run_test.cpp lock_step_run.cpp
	@rm -f test_results.txt
	@for img in fyp18-riscv-emulator/riscv-tests/images/*; do \
		echo "Processing $$img..."; \
		cp $$img fyp18-riscv-emulator/src/Image; \
		$(MAKE) runLockStep; \
		STATUS=$$?; \
		if [ $$STATUS -eq 0 ]; then \
			echo "$$img: test pass" >> test_results.txt; \
		else \
			echo "$$img: test fail" >> test_results.txt; \
		fi; \
		rm fyp18-riscv-emulator/src/Image; \
	done
	@touch .stamp.test_all_images

.stamp.bulk_test : .stamp.sim
	@rm -f test_results.txt
	$(MAKE) test_all_images;

	@date +"Time : %b %_d %Y %H:%M:%S" >> test_results.txt
	$(MAKE) vvadd; \

	@date +"Time : %b %_d %Y %H:%M:%S" >> test_results.txt
	$(MAKE) matmul; \

	@date +"Time : %b %_d %Y %H:%M:%S" >> test_results.txt
	$(MAKE) filter; \

	@date +"Time : %b %_d %Y %H:%M:%S" >> test_results.txt
	$(MAKE) csaxpy; \

	@date +"Time : %b %_d %Y %H:%M:%S" >> test_results.txt
	$(MAKE) histo; \

	@date +"Time : %b %_d %Y %H:%M:%S" >> test_results.txt

# VERILATOR_INCLUDE = /usr/share/verilator/share/verilator/include
VERILATOR_INCLUDE = /usr/share/verilator/include

.stamp.runLockStep: .stamp.lock_step_run.out fyp18-riscv-emulator/src/Image
	./lock_step_run.out

.stamp.lock_step_run.out: lock_step_run.cpp fyp18-riscv-emulator/src/emulator.h fyp18-riscv-emulator/src/constants.h simulator/src/simulator.h simulator/src/obj_dir
	g++ -O3 -I $(VERILATOR_INCLUDE) -I simulator/src/obj_dir \
		$(VERILATOR_INCLUDE)/verilated.cpp $(VERILATOR_INCLUDE)/verilated_vcd_c.cpp \
		lock_step_run.cpp simulator/src/obj_dir/Vsystem__ALL.a -o lock_step_run.out
		@touch .stamp.lock_step_run.out

simulator/src/obj_dir: .stamp.sim simulator/src/system.v simulator/src/iCacheRegisters.v
	cd simulator/src/; \
	echo '/* verilator lint_off UNUSED */' | cat - system.v > temp && mv temp system.v; \
	echo '/* verilator lint_off DECLFILENAME */' | cat - system.v > temp && mv temp system.v; \
	echo '/* verilator lint_off UNUSED */' | cat - iCacheRegisters.v > temp && mv temp iCacheRegisters.v; \
	echo '/* verilator lint_off DECLFILENAME */' | cat - iCacheRegisters.v > temp && mv temp iCacheRegisters.v; \
	echo '/* verilator lint_off VARHIDDEN */' | cat - iCacheRegisters.v > temp && mv temp iCacheRegisters.v; \
	echo '/* verilator lint_off VARHIDDEN */' | cat - system.v > temp && mv temp system.v; \
	echo '/* verilator lint_off WIDTH */' | cat - system.v > temp && mv temp system.v; \
	verilator -Wall --trace -cc system.v; \
	cd obj_dir/; \
	make -f Vsystem.mk; \

.stamp.sim:$(shell find src/main/scala/ -type f -name '*.scala')
	# Patch instructionBase, run sbt, then unconditionally restore original
	mv src/main/scala/common/configuration.scala configuration.txt && \
	sed 's/instructionBase/instructionBase = 0x0000000080000000L\/\//' configuration.txt > src/main/scala/common/configuration.scala && \
	(sbt "runMain system"; mv configuration.txt src/main/scala/common/configuration.scala)
	cp system.v simulator/src/
	cd simulator/src/; \
	cp ../../iCacheRegisters.v .; \
	echo '/* verilator lint_off UNUSED */' | cat - system.v > temp && mv temp system.v; \
	echo '/* verilator lint_off DECLFILENAME */' | cat - system.v > temp && mv temp system.v; \
	echo '/* verilator lint_off UNUSED */' | cat - iCacheRegisters.v > temp && mv temp iCacheRegisters.v; \
	echo '/* verilator lint_off DECLFILENAME */' | cat - iCacheRegisters.v > temp && mv temp iCacheRegisters.v; \
	echo '/* verilator lint_off VARHIDDEN */' | cat - iCacheRegisters.v > temp && mv temp iCacheRegisters.v; \
	echo '/* verilator lint_off VARHIDDEN */' | cat - system.v > temp && mv temp system.v; \
	echo '/* verilator lint_off WIDTH */' | cat - system.v > temp && mv temp system.v; \
	verilator -Wall --trace -cc system.v; \
	cd obj_dir/; \
	make -f Vsystem.mk;
	@touch .stamp.sim

simulator/src/bench.out: simulator/src/obj_dir simulator/src/simulator.h simulator/src/bench.cpp
	cd simulator/src; \
	g++ -O3 -I $(VERILATOR_INCLUDE) -I obj_dir $(VERILATOR_INCLUDE)/verilated.cpp $(VERILATOR_INCLUDE)/verilated_vcd_c.cpp bench.cpp obj_dir/Vsystem__ALL.a -o bench.out

runSim: simulator/src/bench.out
	cd simulator/src/; \
	./bench.out

bintoh :
	echo "#include <stdio.h>" > bintoh.c
	echo "int main(int argc,char ** argv) {if(argc==1) return -1; int c, p=0; printf( \"static const unsigned char %s[] = {\", argv[1] ); while( ( c = getchar() ) != EOF ) printf( \"0x%02x,%c\", c, (((p++)&15)==15)?10:' '); printf( \"};\" ); return 0; }" >> bintoh.c
	gcc bintoh.c -o bintoh

prog.h : Image1 bintoh
	./bintoh program < $< > $@
	# WARNING: sixtyfourmb.dtb MUST hvave at least 16 bytes of buffer room AND be 16-byte aligned.
	#  dtc -I dts -O dtb -o sixtyfourmb.dtb sixtyfourmb.dts -S 1536

make zynq:
	# Patch instructionBase for FPGA base address, run sbt, then unconditionally restore original
	mv src/main/scala/common/configuration.scala configuration.txt && \
	sed 's/instructionBase/instructionBase = 0x0000000040000000L\/\//' configuration.txt > src/main/scala/common/configuration.scala && \
	(sbt "runMain core"; mv configuration.txt src/main/scala/common/configuration.scala)
	# Contains the program that is run until the PS sets up RAM
	sbt "runMain bootROM"
	# Contains the clint, and the register to signal to the core that the image is loaded to RAM
	sbt "runMain testbench.psClint"
	cp src/main/resources/zynq/vivado.tcl .
	make prog.h

fix_inotify_instances_reached:
	# java.io.IOException: User limit of inotify instances reached or too many open files
	echo 512 | sudo tee /proc/sys/fs/inotify/max_user_instances


python_decode:
	if [ $$STATUS -eq 0 ]; then \
		echo "$$No errors yet"; \
	else \
    	. ~/.venv/bin/activate; \
      	python decoder.py run.log -o run_decoded.log; \
      	deactivate;\
	fi; \

# ── Profiling targets ────────────────────────────────────────────────────────
.PHONY: profile_build profile_isa profile_benchmarks profile_all

profile_build: .stamp.sim
	g++ -O3 -I $(VERILATOR_INCLUDE) -I simulator/src/obj_dir -I simulator/src \
		$(VERILATOR_INCLUDE)/verilated.cpp \
		lock_step_files/profile_run.cpp \
		simulator/src/obj_dir/Vsystem__ALL.a \
		-o profile_run.out

profile_isa: profile_build
	mkdir -p profile_results
	@for img in fyp18-riscv-emulator/riscv-tests/images/*.bin; do \
		name=$$(basename $$img .bin); \
		cp $$img fyp18-riscv-emulator/src/Image; \
		./profile_run.out --image fyp18-riscv-emulator/src/Image \
			--name isa_$$name \
			--output profile_results/isa_$$name.json \
			--timeout 50000000 || true; \
	done
	python3 scripts/profile_visualize.py profile_results/

profile_benchmarks: profile_build
	mkdir -p profile_results
	@for s in 1 2 3 4 5; do \
		if [ -f benchmark/mt-vvadd-s$$s.bin ]; then \
			cp benchmark/mt-vvadd-s$$s.bin fyp18-riscv-emulator/src/Image; \
			./profile_run.out --image fyp18-riscv-emulator/src/Image \
				--name vvadd-s$$s --output profile_results/vvadd-s$$s.json \
				--done-pc 0x800009a0 --done-pc 0x800009ac --done-a0 2 \
				--timeout 500000000 || true; \
		fi; \
	done
	@for s in 1 2 3 4 5; do \
		if [ -f benchmark/mt-matmul-s$$s.bin ]; then \
			cp benchmark/mt-matmul-s$$s.bin fyp18-riscv-emulator/src/Image; \
			./profile_run.out --image fyp18-riscv-emulator/src/Image \
				--name matmul-s$$s --output profile_results/matmul-s$$s.json \
				--done-pc 0x80000a04 \
				--timeout 500000000 || true; \
		fi; \
	done
	@for s in 1 2 3 4 5; do \
		if [ -f benchmark/mt-mask-sfilter-s$$s.bin ]; then \
			cp benchmark/mt-mask-sfilter-s$$s.bin fyp18-riscv-emulator/src/Image; \
			./profile_run.out --image fyp18-riscv-emulator/src/Image \
				--name filter-s$$s --output profile_results/filter-s$$s.json \
				--done-pc 0x80000bc8 \
				--timeout 500000000 || true; \
		fi; \
	done
	@for s in 1 2 3 4 5; do \
		if [ -f benchmark/mt-csaxpy-s$$s.bin ]; then \
			cp benchmark/mt-csaxpy-s$$s.bin fyp18-riscv-emulator/src/Image; \
			./profile_run.out --image fyp18-riscv-emulator/src/Image \
				--name csaxpy-s$$s --output profile_results/csaxpy-s$$s.json \
				--done-pc 0x800009a4 --done-pc 0x80000998 --done-a0 0 \
				--timeout 500000000 || true; \
		fi; \
	done
	@for s in 1 2 3 4 5; do \
		if [ -f benchmark/mt-histo-s$$s.bin ]; then \
			cp benchmark/mt-histo-s$$s.bin fyp18-riscv-emulator/src/Image; \
			./profile_run.out --image fyp18-riscv-emulator/src/Image \
				--name histo-s$$s --output profile_results/histo-s$$s.json \
				--done-pc 0x80000a40 \
				--timeout 500000000 || true; \
		fi; \
	done
	python3 scripts/profile_visualize.py profile_results/

profile_all: profile_isa profile_benchmarks

# ── Cleanup ──────────────────────────────────────────────────────────────────
.PHONY: clean
clean:
	rm -rf .stamp.*;
	rm -rf ./simulator/src/obj_dir
