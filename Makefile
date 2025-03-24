VPATH = ../scala:../cache:../common:../decode:../exec:../fetch:./memAccess:../rob:../testbench

.PHONY: vvadd
vvadd: .stamp.vvadd

.PHONY: matmul
matmul: .stamp.matmul

.PHONY: filter
filter: .stamp.filter

.PHONY: test_all_images
test_all_images: .stamp.test_all_images

.PHONY: runLockStep
runLockStep: .stamp.runLockStep

.PHONY: runLockStepTest
runLockStepTest: .stamp.runLockStepTest

.PHONY: sim
sim: .stamp.sim

.stamp.vvadd: .stamp.sim
	cp benchmark/mt-vvadd.bin fyp18-riscv-emulator/src/Image 
	$(MAKE) runLockStep; 
	@touch .stamp.vvadd

.stamp.matmul: .stamp.matmul
	cp benchmark/mt-matmul.bin fyp18-riscv-emulator/src/Image 
	$(MAKE) runLockStep; 
	@touch .stamp.matmul

.stamp.filter: .stamp.filter
	cp benchmark/mt-mask-sfilter.bin fyp18-riscv-emulator/src/Image 
	$(MAKE) runLockStep; 
	@touch .stamp.filter

.stamp.test_all_images: .stamp.sim
	@rm -f test_results.txt
	@for img in fyp18-riscv-emulator/riscv-tests/images/*; do \
		echo "Processing $$img..."; \
		cp $$img fyp18-riscv-emulator/src/Image; \
		$(MAKE) runLockStepTest; \
		STATUS=$$?; \
		if [ $$STATUS -eq 0 ]; then \
			echo "$$img: test pass" >> test_results.txt; \
		else \
			echo "$$img: test fail" >> test_results.txt; \
		fi; \
		rm fyp18-riscv-emulator/src/Image; \
	done

VERILATOR_INCLUDE = /home/mcrparadox/verilator/include

.stamp.runLockStep: .stamp.lock_step_run.out fyp18-riscv-emulator/src/Image
	./lock_step_run.out

.stamp.lock_step_run.out: lock_step_run.cpp fyp18-riscv-emulator/src/emulator.h fyp18-riscv-emulator/src/constants.h simulator/src/simulator.h simulator/src/obj_dir
	g++ -O3 -I $(VERILATOR_INCLUDE) -I simulator/src/obj_dir \
		$(VERILATOR_INCLUDE)/verilated.cpp $(VERILATOR_INCLUDE)/verilated_vcd_c.cpp \
		lock_step_run.cpp simulator/src/obj_dir/Vsystem__ALL.a -o lock_step_run.out
		@touch .stamp.lock_step_run.out

.stamp.runLockStepTest: .stamp.lock_step_run_test.out fyp18-riscv-emulator/src/Image
	./lock_step_run_test.out

.stamp.lock_step_run_test.out: lock_step_run_test.cpp fyp18-riscv-emulator/src/emulator.h fyp18-riscv-emulator/src/constants.h simulator/src/simulator.h simulator/src/obj_dir
	g++ -O3 -I $(VERILATOR_INCLUDE) -I simulator/src/obj_dir \
		$(VERILATOR_INCLUDE)/verilated.cpp $(VERILATOR_INCLUDE)/verilated_vcd_c.cpp \
		lock_step_run_test.cpp simulator/src/obj_dir/Vsystem__ALL.a -o lock_step_run_test.out
		@touch .stamp.lock_step_run_test.out

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
	# Change instructionBase in configuration file
	mv src/main/scala/common/configuration.scala configuration.txt
	sed 's/instructionBase/instructionBase = 0x0000000010000000L\/\//' configuration.txt > src/main/scala/common/configuration.scala
	# sbt "clean; compile; runMain system"
	sbt "runMain system"
	# Restoring the original configuration
	mv configuration.txt src/main/scala/common/configuration.scala
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
	g++ -O3 -I /usr/share/verilator/include -I obj_dir /usr/share/verilator/include/verilated.cpp /usr/share/verilator/include/verilated_vcd_c.cpp bench.cpp obj_dir/Vsystem__ALL.a -o bench.out

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
	# Change instructionBase in configuration file
	mv src/main/scala/common/configuration.scala configuration.txt
	sed 's/instructionBase/instructionBase = 0x0000000040000000L\/\//' configuration.txt > src/main/scala/common/configuration.scala
	sbt "runMain core"
	# Restoring the original configuration
	mv configuration.txt src/main/scala/common/configuration.scala
	# Contains the program that is run until the PS sets up RAM
	sbt "runMain bootROM"
	# Contains the clint, and the register to signal to the core that the image is loaded to RAM
	sbt "runMain testbench.psClint"
	cp src/main/resources/zynq/vivado.tcl .
	make prog.h

fix_inotify_instances_reached:
	# java.io.IOException: User limit of inotify instances reached or too many open files
	echo 512 | sudo tee /proc/sys/fs/inotify/max_user_instances

.PHONY: clean
clean:
	rm -rf .stamp.*;
	rm -rf ./simulator/src/obj_dir
