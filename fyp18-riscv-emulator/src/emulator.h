/**
 * This header file contains a riscv-emulator implemented
 * as a class. This provides an easy way to use the same
 * emulator code for testing the two processors.
 */

/* EMULATOR INCLUDE HEADER FILES */
#include <cstdint>
#include <unistd.h>
#include <cstdlib>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <string>
#include <fstream>
#include <stdio.h>
//#include <bits/stdc++.h>
#include <iostream>
#include <sys/time.h>
#include <fcntl.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <bitset>
#include <fenv.h>
#include <iomanip>

#ifdef EMULATOR_LOGGING
	#define DEBUG_LOG(t, v) logfile << "[DEBUG]: " << t << " 0x" << setfill('0') << setw(16) << hex << v << endl;
#else 
	#define DEBUG_LOG(t, v)
#endif

#include "constants.h"

#pragma STDC FENV_ACCESS ON

#define debug(fmt,...) printf(fmt, ##__VA_ARGS__)
// #define debug(fmt,...)
#define FLOAT_TO_32BITS(x) (*reinterpret_cast<uint32_t*>(&x))

using namespace std;

#ifndef MISA_SPEC
	#define MISA_SPEC (0b100000001000100100001 | (0b1llu << 63))
#endif

// disables echo for terminal
void enable_raw_mode() {
	termios term;
	tcgetattr(0, &term);
	term.c_lflag &= ~(ICANON | ECHO); // Disable echo as well
	tcsetattr(0, TCSANOW, &term);
}

// return terminal back to original state before termination of the program
void disable_raw_mode() {
	termios term;
	tcgetattr(0, &term);
	term.c_lflag |= ICANON | ECHO;
	tcsetattr(0, TCSANOW, &term);
}

// detecting a key hit
int kbhit() {
	int byteswaiting;
	ioctl(0, FIONREAD, &byteswaiting);
	return byteswaiting > 0;
}

/**
 * Main emulator class, the following functions are defined.
 *
 * @fn step() - Steps through one architectural change of pc
 * which includes executing one legal instruction, moving to
 * exception/interrupt handler after a synchronous/asynchronous
 * exception
 * @fn init() - Initializes the emulator, i.e.
 *  1. Setting the pc to the first instruction to be executed
 *  2. Setting gprs and csrs
 *  3. Initializes the memory with the image of the program
 * @fn memory_read() - Performs a memory read at the given
 * physical address. The read is a 8 byte read at a given
 * address.
 * @fn memory_write() - Performs a memory write at the given
 * physical address, at the specified size
 * @fn set_interrupts() - Looks for interrupts. If any are set,
 * then in the next step() the pc moves to the exception handler
 * @fn csr_set() - Performs settings CSRs such as mcycle
 * @fn fetch_instruction() - Performs a memory read for an
 * instruction
 *
 * The above functions may be overwritten when adopting the same
 * code for co-simulation and emulation with hardware simulation
 * of a RISC-V core
 * e.g.: To match peripheral accesses between the emulator and
 * simulator are synchronous, memory_read() will change to provide
 * this functionality.
 */
class emulator {
private:
	/**
	 * Initiate all the variables needed to define the state of the
	 * emulator.
	 * e.g.: gprs, csrs, memory, etc...
	 */

	bool csr_read_success = false;
	plevel_t cp = (plevel_t)MMODE;
	bool LD_ADDR_MISSALIG = false;    // load address misalignment
	bool STORE_ADDR_MISSALIG = false; // store/amo address misalignment
	bool ILL_INS = false;             // illegal instruction
	bool EBREAK = false;              // break point
	bool INS_ADDR_MISSALIG = false;
	bool INS_ACC_FAULT = false; // instruction access fault
	bool LD_ACC_FAULT = false;  // load access fault

	int64_t DRAM_BASE = 0x10000000;

	static_assert(std::numeric_limits<double>::is_iec559,
									"This code requires IEEE-754 doubles");

	std::ofstream logfile;
	string line;
	uint64_t temp;
	uint64_t i = 0;

	enum opcode_t opcode;
	uint64_t rd = 0;
	uint64_t func3 = 0;
	uint64_t rs1 = 0;
	uint64_t rs2 = 0;
	uint64_t func7 = 0;
	uint64_t imm11_0 = 0;
	uint64_t imm31_12 = 0;
	uint64_t imm_j = 0;
	uint64_t imm_b = 0;
	uint64_t imm_s = 0;
	uint32_t imm = 0;
	uint64_t amo_op = 0;

	//* Line 106, field deceleration
	uint64_t rm = 0;
	uint64_t fmt = 0;
	uint64_t funt5 = 0;
	uint64_t rs3 = 0;

	bool amo_reserve_valid = false;
	bool amo_reserve_valid64 = false;
	uint64_t amo_reserve_addr = 0;
	uint64_t amo_reserve_addr64 = 0;

	uint64_t wb_data = 0;

	//* To store intermediate result in single precision operations
	float f_wb_data = 0;

	uint64_t load_addr = 0;
	uint64_t load_data = 0;
	bool ls_success = false;

	uint64_t store_addr = 0;
	uint64_t store_data = 0;
	uint64_t val = 0;

	//*To store f_register values
	float f_val = 0;

	bool branch = false;

	uint64_t csr_data = 0;
	bool csr_bool = false;

	uint64_t itr = 0;

	uint64_t cycle_count = 0;

	__uint128_t mult_temp = 0;

	uint64_t ret_data = 0;

	vector<uint64_t> memory = vector<uint64_t>(1 << MEM_SIZE); // main memory
	// vector<uint64_t> reg_file = vector<uint64_t>(32);          // register file

	uint64_t PC;
	uint64_t PC_phy;
	uint64_t instruction;

	//* Line 155 Change MISA to incorporate floating point.
	uint64_t misa = MISA_SPEC;     
	uint64_t mscratch = 0;
	uint64_t medeleg = 0;
	uint64_t mideleg = 0;
	uint64_t mepc = 0;
	uint64_t uepc = 0;
	uint64_t mtval = 0;
	uint64_t mcounteren = 0;
	uint64_t scounteren = 0;
	uint64_t pmpcfg0 = 0;
	uint64_t pmpaddr0 = 0;
	uint64_t mhartid = 0;
	uint64_t mvendorid = 0;
	uint64_t marchid = 0;
	uint64_t mimpid = 0;

	struct satp_t {
		uint64_t PPN;
		uint16_t ASID;
		uint8_t MODE = 0;
		satp_t() {
			PPN = 0;
			ASID = 0;
			MODE = 0;
		}
		uint64_t read_reg() {
			return (((uint64_t)(MODE & 0xF) << 60) + ((uint64_t)ASID << 44) + (PPN & 0xFFFFFFFFFFFull));
		}

		void write_reg(const uint64_t &val) {
			PPN = val & 0xFFFFFFFFFFFull;
			ASID = 0; //(val>>44) & 0xFFFF ;
			MODE = (val >> 60) & 0xF;
			if (MODE != 8)
			{
				MODE = 0;
			}
		}
	} satp; //

	struct mstatus_t {
		uint8_t uie, sie, mie, upie, spie, mpie, spp, mpp, fs, xs, mprv, sum, mxr, tvm, tw, tsr, uxl, sxl, sd;
		mstatus_t() {
			uie = 0;
			sie = 0;
			mie = 0;
			upie = 0;
			spie = 0;
			mpie = 0;
			spp = 0;
			mpp = 0b0;
			fs = 0;
			xs = 0;
			mprv = 0;
			sum = 0;
			mxr = 0;
			tvm = 0;
			tw = 0;
			tsr = 0;
			uxl = 2;
			sxl = 2;
			sd = 0;
		}
		uint64_t read_reg() {
			return (((uint64_t)sd << 63) + ((uint64_t)sxl << 34) + ((uint64_t)uxl << 32) + (tsr << 22) + (tw << 21) + (tvm << 20) + (mxr << 19) + (sum << 18) + (mprv << 17) + (xs << 15) + (fs << 13) + (mpp << 11) + (spp << 8) + (mpie << 7) + (spie << 5) + (upie << 4) + (mie << 3) + (sie << 1) + uie);
		}

		void write_reg(const uint64_t &val) {
			uie = (val & 0b1);
			sie = ((val >> 1) & 0b1);
			mie = ((val >> 3) & 0b1);
			upie = ((val >> 4) & 0b1);
			spie = ((val >> 5) & 0b1);
			mpie = ((val >> 7) & 0b1);
			spp = ((val >> 8) & 0b1);
			mpp = ((val >> 11) & 0b11); // 0b11; removed hard wire to 11
			fs = ((val >> 13) & 0b11);
			xs = ((val >> 15) & 0b11);
			mprv = ((val >> 17) & 0b1);
			sum = ((val >> 18) & 0b1);
			mxr = ((val >> 19) & 0b1);
			tvm = ((val >> 20) & 0b1);
			tw = ((val >> 21) & 0b1);
			tsr = ((val >> 22) & 0b1); /*uxl= ((val>>32)& 0b11); read only*/
			//sxl = ((val >> 34) & 0b11);
			sd = ((val >> 63) & 0b1);
		}
	} mstatus; //

	struct ustatus_t {
		// uint8_t uie, sie, upie, spie, spp, fs, xs, sum, mxr, uxl, sd;
		uint8_t uie = 0;
		uint8_t upie = 0;
		uint8_t fs = 0;
		uint8_t xs = 0;
		uint8_t sum = 0;
		uint8_t mxr = 0;
		uint8_t uxl = 2;
		uint8_t sd = 0;
		ustatus_t()
		{
		}
		uint64_t read_reg() {
			return (((uint64_t)sd << 63) + ((uint64_t)uxl << 32) + (mxr << 19) + (sum << 18) + (xs << 15) + (fs << 13) + (upie << 4) + uie);
		}
		void write_reg(const uint64_t &val) {
			uie = (val & 0b1);
			upie = ((val >> 5) & 0b1);
			fs = ((val >> 13) & 0b11);
			xs = ((val >> 15) & 0b11);
			sum = ((val >> 18) & 0b1);
			mxr = ((val >> 19) & 0b1);
			uxl = ((val >> 32) & 0b11);
			sd = ((val >> 63) & 0b1);
		}
	} ustatus;

	struct mie_t {
		uint8_t MEIE;
		uint8_t SEIE;
		uint8_t UEIE;
		uint8_t MTIE;
		uint8_t STIE;
		uint8_t UTIE;
		uint8_t MSIE;
		uint8_t SSIE;
		uint8_t USIE;
		mie_t() {
			MEIE = 0;
			SEIE = 0;
			UEIE = 0;
			MTIE = 0;
			STIE = 0;
			UTIE = 0;
			MSIE = 0;
			SSIE = 0;
			USIE = 0;
		}
		uint64_t read_reg() {
			return (((MEIE & 0b1) << 11) + ((SEIE & 0b1) << 9) + ((UEIE & 0b1) << 8) + ((MTIE & 0b1) << 7) + ((STIE & 0b1) << 5) + ((UTIE & 0b1) << 4) + ((MSIE & 0b1) << 3) + ((SSIE & 0b1) << 1) + (USIE & 0b1));
		}
		void write_reg(const uint64_t &val) {
			MEIE = (val >> 11) & 0b1;
			SEIE = (val >> 9) & 0b1;
			UEIE = (val >> 8) & 0b1;
			MTIE = (val >> 7) & 0b1;
			STIE = (val >> 5) & 0b1;
			UTIE = (val >> 4) & 0b1;
			MSIE = (val >> 3) & 0b1;
			SSIE = (val >> 1) & 0b1;
			USIE = val & 0b1;
		}
	} mie; //

	struct mip_t {
		uint8_t MEIP;
		uint8_t SEIP;
		uint8_t UEIP;
		uint8_t MTIP;
		uint8_t STIP;
		uint8_t UTIP;
		uint8_t MSIP;
		uint8_t SSIP;
		uint8_t USIP;
		mip_t() {
			MEIP = 0;
			SEIP = 0;
			UEIP = 0;
			MTIP = 0;
			STIP = 0;
			UTIP = 0;
			MSIP = 0;
			SSIP = 0;
			USIP = 0;
		}
		uint64_t read_reg() {
			return (((MEIP & 0b1) << 11) + ((SEIP & 0b1) << 9) + ((UEIP & 0b1) << 8) + ((MTIP & 0b1) << 7) + ((STIP & 0b1) << 5) + ((UTIP & 0b1) << 4) + ((MSIP & 0b1) << 3) + ((SSIP & 0b1) << 1) + (USIP & 0b1));
		}

		void write_reg(const uint64_t &val) {
			MEIP = (val >> 11) & 0b1;
			SEIP = (val >> 9) & 0b1;
			UEIP = (val >> 8) & 0b1;
			MTIP = (val >> 7) & 0b1;
			STIP = (val >> 5) & 0b1;
			UTIP = (val >> 4) & 0b1;
			MSIP = (val >> 3) & 0b1;
			SSIP = (val >> 1) & 0b1;
			USIP = val & 0b1;
		}
	} mip; //

	struct mtvec_t {
		uint8_t mode;
		uint64_t base;
		mtvec_t() {
			mode = 0;
			base = 0;
		}
		uint64_t read_reg() {
			return ((mode & 0b11) + (base & (MASK64 - 0b11)));
		}

		void write_reg(const uint64_t &val) {
			mode = val & 0b11;
			base = (val & (MASK64 - 0b11));
		}
	} mtvec; //

	struct utvec_t {
		uint8_t mode;
		uint64_t base;
		utvec_t() {
			mode = 0;
			base = 0;
		}
		uint64_t read_reg() {
			return ((mode & 0b11) + (base & (MASK64 - 0b11)));
		}
		void write_reg(const uint64_t &val) {
			mode = val & 0b11;
			base = (val & (MASK64 - 0b11));
		}
	} utvec;

	struct mcause_t {
		uint8_t interrupt;
		uint64_t ecode;
		mcause_t() {
			interrupt = 0;
			ecode = 0;
		}
		uint64_t read_reg() {
			// cout << "mcause: " << dec << (((1llu<<63)-1) & ecode)+((uint64_t)interrupt<<63);
			return ((((1llu << 63) - 1) & ecode) + ((uint64_t)interrupt << 63));
		}

		void write_reg(const uint64_t &val) {
			ecode = val & ((1llu << 63) - 1);
			interrupt = (val >> 63) & 0b1;
		}
	} mcause; //

	struct ucause_t {
		uint8_t interrupt;
		uint64_t ecode;
		ucause_t() {
			interrupt = 0;
			ecode = 0;
		}
		uint64_t read_reg() {
			return ((((1llu << 63) - 1) & ecode) + ((uint64_t)interrupt << 63));
		}

		void write_reg(const uint64_t &val) {
			ecode = val & ((1llu << 63) - 1);
			interrupt = (val >> 63) & 0b1;
		}
	} ucause;

	//* Line 449 FCSR Struct
	struct fcsr_t {
		uint8_t frm; 
		uint8_t nv; 
		uint8_t dz; 
		uint8_t of; 
		uint8_t uf; 
		uint8_t nx;
		fcsr_t() {
		frm = 0;
			nv = 0;
			dz = 0;
			of = 0;
			uf = 0;
			nx = 0; 
		}

		uint64_t read_reg() {
			return ((( frm & 0b111) << 5)+((nv & 0b1) << 4) + ((dz & 0b1) << 3) + ((of & 0b1) << 2) + ((uf & 0b1) << 1) + (nx & 0b1));
		}
		uint64_t read_frm() {
			return (frm & 0b111);
		}
		uint64_t read_fflags() {
			return (((nv & 0b1) << 4) + ((dz & 0b1) << 3) + ((of & 0b1) << 2) + ((uf & 0b1) << 1) + (nx & 0b1));
		}

		void write_reg(const uint64_t &val) {
			nx = val & 0b1;
			uf = (val >> 1) & 0b1;
			of = (val >> 2) & 0b1;
			dz = (val >> 3) & 0b1;
			nv = (val >> 4) & 0b1;
		frm = (val >> 5) & 0b111;
		}
		void write_frm(const uint64_t &val) {
		frm = val & 0b111;
		}
		void write_fflags(const uint64_t &val) {
			nx = val & 0b1;
			uf = (val >> 1) & 0b1;
			of = (val >> 2) & 0b1;
			dz = (val >> 3) & 0b1;
			nv = (val >> 4) & 0b1;
		}
	} fcsr;

	uint64_t mtime; // = memory.at(MTIME_ADDR / 8);
	uint64_t mtimecmp; // = memory.at(MTIMECMP_ADDR / 8);

	struct timeval tv;
	uint64_t time_in_micros;

	uint64_t csr_read(const uint64_t &csr_addr) {
		csr_read_success = true;
		switch (csr_addr) {
		case MSTATUS:
			if (cp == MMODE) {
				csr_read_success = true;
				return mstatus.read_reg();
			} else {
				csr_read_success = false;
				return 1;
			}
		case SATP: return satp.read_reg();
		case MIE: return mie.read_reg();
		case MIP: return mip.read_reg();
		case MTVEC: return mtvec.read_reg();
		//*Adding FCSR
		case FCSR: return fcsr.read_reg();
		case FRM: return fcsr.read_frm();
		case FFLAGS: return fcsr.read_fflags();
		case MEPC: return mepc;
		case MCAUSE: return mcause.read_reg();
		case MTVAL: return mtval;
		case PMPCFG0: return pmpcfg0;
		case PMPADDR0: return pmpaddr0;
		case MHARTID: return mhartid;
		case MVENDORID: return mvendorid; break;
		case MARCHID: return marchid; break;
		case MIMPID: return mimpid; break;
		case MSCRATCH: return mscratch;
		case MISA: return misa;
		case SCOUNTEREN: return scounteren;
		case MCOUNTEREN: return mcounteren;
		default:
			csr_read_success = false;
			return 1;
		}
	}

	bool csr_write(const uint64_t &csr_addr, const uint64_t &val) {
		switch (csr_addr) {
		case MSTATUS: mstatus.write_reg(val); return true;
		case SATP: satp.write_reg(val); return true;
		case MIE: mie.write_reg(val); return true;
		case MIP: mip.write_reg(val); return true;
		case MTVEC: mtvec.write_reg(val); return true;
		case FCSR: fcsr.write_reg(val); return true;
		case FRM: fcsr.write_frm(val); return true;
		case FFLAGS: fcsr.write_fflags(val); return true;	
		case MEPC: mepc = val; return true;
		case MCAUSE: mcause.write_reg(val); return true;
		case MTVAL: mtval = val; return true;
		case PMPCFG0: pmpcfg0 = val; return true;
		case PMPADDR0: pmpaddr0 = val; return true;
		case MHARTID: mhartid = val; return true;
		case MVENDORID: mvendorid = val; return true;
		case MARCHID: marchid = val; return true;
		case MIMPID: mimpid = val; return true;
		case MSCRATCH: mscratch = val; return true;
		case MISA: misa = val; return true;
		case SCOUNTEREN: scounteren = val; return true;
		case MCOUNTEREN: mcounteren = val; return true;
		default: return false;
		}
	}

	uint64_t excep_function(const uint64_t &PC, const uint64_t &mecode, const uint64_t &secode, const uint64_t &uecode, const plevel_t &current_privilage) {
		uint64_t ecode = 0;
		uint64_t new_PC = 0;

		if (current_privilage == UMODE) {
			ecode = uecode;
		} else if (current_privilage == MMODE) {
			ecode = mecode;
		}

		mstatus.mpp = (uint64_t)cp;
		cp = MMODE;
		mstatus.mpie = mstatus.mie;
		mstatus.mie = 0;
		mcause.interrupt = 0;
		mcause.ecode = ecode;
		mepc = PC - 4;
		new_PC = mtvec.base;

		return new_PC;
	}

	uint64_t interrupt_function(const uint64_t &PC, const uint64_t &mecode, const plevel_t &current_privilage) {
		uint64_t ecode = 0;
		uint64_t new_PC = 0;

		ecode = mecode;

		if (mstatus.mie == 1) {
			mstatus.mpp = (uint64_t)cp;
			cp = MMODE;
			mstatus.mpie = mstatus.mie;
			mstatus.mie = 0;
			mcause.interrupt = 1;
			mcause.ecode = ecode;
			mepc = PC;

			if (mtvec.mode == 0b0) {
				new_PC = mtvec.base;
			}
			else if (mtvec.mode == 0b1) {
				new_PC = mtvec.base + 4 * ecode;
			}
		} else {
			// cout << "Unrecognized mode for interrupt_function" << endl;
			return PC;
		}
		return new_PC;
	}

	uint64_t getINST(const uint64_t &PC, const vector<uint64_t> *memory) {
		// cout << "This is here" << endl;
		if (PC % 2 == 0)
			return ((MASK32) & (memory->at(PC / 2)));
		else
			return ((memory->at(PC / 2)) >> 32);
	}

	int64_t signed_value(const uint64_t &x) {
		if (((x >> 63) & 0b1) == 1)
			return (x ^ (1llu << 63)) - (1llu << 63);
		else
			return x;
	}

	int32_t signed_value32(const uint64_t &x) {
		uint32_t y = x & MASK32;
		if (((y >> 31) & 0b1) == 1)
			return (y ^ (1lu << 31)) - (1lu << 31);
		else
			return y;
	}

	//*Line 655
	int number_class(float num_check){
		switch(FLOAT_TO_32BITS(num_check)&0xff800000) {
		case 0x00000000: return FLOAT_TO_32BITS(num_check) ? 5 : 4;
		case 0x7f800000: return (FLOAT_TO_32BITS(num_check)&0x007fffff) ? ((FLOAT_TO_32BITS(num_check)&0x00400000)?9:8) : 7;
		case 0x80000000: return (FLOAT_TO_32BITS(num_check)<<1) ? 2 : 3;
		case 0xff800000: return (FLOAT_TO_32BITS(num_check)&0x007fffff) ? ((FLOAT_TO_32BITS(num_check)&0x00400000)?9:8) : 0;
		default: return (FLOAT_TO_32BITS(num_check)&0x80000000) ? 1 : 6;	
		}
	}

	template <class T>
	T sign_extend(const T &x, const int &bits) {
		T m = 1;
		m <<= bits - 1;
		return (x ^ m) - m;
	}

	string exec(const char *cmd) {
		char buffer[128];
		string result = "";
		FILE *pipe = popen(cmd, "r");
		if (!pipe)
			throw runtime_error("popen() failed!");
		try {
			while (fgets(buffer, sizeof buffer, pipe) != NULL) {
				result += buffer;
			}
		} catch (...) {
			pclose(pipe);
			throw;
		}
		pclose(pipe);
		return result;
	}

	/* int hex_to_dec(const int64_t &hex_num)
	{
		stringstream stream;
		int y;
		stream << hex_num;
		stream >> hex >> y;
		return y;
	} */

	void print_reg_file(const vector<uint64_t> &reg_file) {
		printf("\n");
		for (int i = 0; i < 32 - 7; i += 8) {
			printf("%s : %lu | %s : %lu | %s : %lu | %s : %lu | %s : %lu | %s : %lu | %s : %lu | %s : %lu\n",
				reg_file_names[i].c_str(), signed_value(reg_file[i]),
				reg_file_names[i + 1].c_str(), signed_value(reg_file[i + 1]),
				reg_file_names[i + 2].c_str(), signed_value(reg_file[i + 2]),
				reg_file_names[i + 3].c_str(), signed_value(reg_file[i + 3]),
				reg_file_names[i + 4].c_str(), signed_value(reg_file[i + 4]),
				reg_file_names[i + 5].c_str(), signed_value(reg_file[i + 5]),
				reg_file_names[i + 6].c_str(), signed_value(reg_file[i + 6]),
				reg_file_names[i + 7].c_str(), signed_value(reg_file[i + 7]));
		}
		printf("\n");
	}

	template <class T>
	T divi(T num1, T num2, int s) {
		if (num2 == 0) {
			switch (s) {
			case 0:
				return (T)(-1);
			case 1:
				return (T)MASK64;
			case 2:
				return num1;
			case 3:
				return num1;
			}
		}
		else if (num1 == (-pow(2ull, 63)) && num2 == -1) {
			if (s == 0 || s == 2) {
				switch (s) {
				case 0:
					return (T)(-pow(2ull, 63));
				case 2:
					return 0;
				}
			}
		}
		else {
			ldiv_t div_result;
			switch (s) {
			case 0:
				div_result = div((int64_t)num1, (int64_t)num2);
				return div_result.quot;
			case 1:
				return num1 / num2;
			case 2:
				div_result = div((int64_t)num1, (int64_t)num2);
				return div_result.rem;
			case 3:
				return num1 % num2;
			default:
				return (T)(-1);
			}
		}
		return (T)(-1);
	}

	template <class T>
	T divi32(T num1, T num2, int s) {
		if (num2 == 0) {
			switch (s) {
			case 0:
				return (T)(-1);
			case 1:
				return (T)MASK32;
			case 2:
				return num1;
			case 3:
				return num1;
			}
		}
		else if (num1 == (-pow(2, 31)) && num2 == -1) {
			if (s == 0 || s == 2) {
				switch (s) {
				case 0:
					return (T)(-pow(2ull, 31));
				case 2:
					return 0;
				}
			}
		} else {
			div_t div_result;
			switch (s) {
			case 0:
				div_result = div((int32_t)num1, (int32_t)num2);
				return div_result.quot;
			case 1:
				return num1 / num2;
			case 2:
				div_result = div((int32_t)num1, (int32_t)num2);
				return div_result.rem;
			case 3:
				return num1 % num2;
			default:
				return (T)(-1);
			}
		}
		return (T)(-1);
	}

	//*Line 775 Add rounding modes
	void roundingmode_change(const uint8_t RM, const float result_temp){
	//? const uint8_t &RM is this the way to get the rounding mode?
		switch (RM) {
		case (0b000): fesetround(FE_TONEAREST); break;
		case (0b001): fesetround(FE_TOWARDZERO); break;
		case (0b010): fesetround(FE_DOWNWARD); break;
		case (0b011): fesetround(FE_UPWARD); break;
		case (0b100):
			if (std::signbit(result_temp)) {
				fesetround(FE_DOWNWARD);
				break;
			} else {
				fesetround(FE_UPWARD);
				break;
			}
			break;
		case (0b101): break; // Invalid
		case (0b110): break;// Invalid
		case (0b111):
			//Read fcsr frm field
			//And call one of the first five
			break;
		default:
			break;

		//TODO Need to add Invalid options
		// ? Round to Nearest, ties to Max Magnitude need to be implemented in 
		// ? in arithmetic instruction location
		}
	}

	void roundingmode_revert(){
		//Default rounding mode
		fesetround(FE_TONEAREST);
	}

	//* Function is used to unary rounding
	float round_to_int(uint8_t RM,float num) {
	//this function is used to unary rounding
	//fcvt.w.s,fcvt.wu.s,fcvt.l.s,fcvt.lu.s
	//fcvt.s.w,fcvt.s.wu,fcvt.s.l,fcvt.s.lu
	switch(RM){ 
	case (0b000): return round(num); break; // rne
	case (0b001): return trunc(num); break; // rtz
	case (0b010): return floor(num); break; // rdn
	case (0b011): return ceil(num); break; // rup
	case (0b100): break; // rmm - need to complete
	case (0b101): break; // invalid
	case (0b110): break; // invalid
	case (0b111): 
		RM=static_cast<uint8_t>(fcsr.read_frm());
		round_to_int(rm,num);
		break; // dynamic
	default: break;
	}
	return num;
	}

	//* To access exceptions
	void setfflags() {
		//feclearexcept(FE_ALL_EXCEPT);
		int temp;
		if (fetestexcept(FE_INVALID)){
			temp = fcsr.read_fflags();
			fcsr.write_fflags(0b10000 | temp);
		}
		if (fetestexcept(FE_DIVBYZERO)){
			temp = fcsr.read_fflags();
			fcsr.write_fflags(0b01000 | temp);  
		}
		if (fetestexcept(FE_OVERFLOW)){
			temp = fcsr.read_fflags();
			fcsr.write_fflags(0b00100 | temp);  
		}
		if (fetestexcept(FE_UNDERFLOW)){
			temp = fcsr.read_fflags();
			fcsr.write_fflags(0b00010 | temp);   
		}
		if (fetestexcept(FE_INEXACT)){
			temp = fcsr.read_fflags();
			fcsr.write_fflags(0b00001 | temp);   
		}
	}

	//* To handle min-max instructions of floating point S
	float min_max_f(float operand_1, float operand_2, int type) {
		uint32_t canonical_nan_bits = 0x7fc00000;
		if ((number_class(operand_1) == 8) || (number_class(operand_2) == 8)) { fcsr.write_fflags(0b10000 | fcsr.read_fflags()); }
		if (isnan(operand_1)) { 
			return isnan(operand_2) ? *reinterpret_cast<float *>(&canonical_nan_bits) : operand_2; }
		else if (isnan(operand_2)) { fcsr.write_fflags(0b10000 | fcsr.read_fflags()); return operand_1; }
		else {
			switch (type) {
			case 0: return (operand_1 < operand_2) ? operand_1 : \
			(((FLOAT_TO_32BITS(operand_1) == 0x80000000) && (FLOAT_TO_32BITS(operand_2) == 0x0)) ? operand_1 : operand_2);
			
			case 1: return (operand_1 > operand_2) ? operand_1 : \
			(((FLOAT_TO_32BITS(operand_1) == 0x0) && (FLOAT_TO_32BITS(operand_2) == 0x80000000)) ? operand_1 : operand_2);
			}
			return 0.0; // should not happen
		}
	}

	bool load_word(const uint64_t &load_addr, const uint64_t &load_data, uint64_t &wb_data) {
		wb_data = static_cast<uint32_t>((load_data >> ((load_addr&7) << 3)) & 0xFFFFFFFF);
		return ((load_addr&3) == 0) ? true : false;
	}

	bool store_word(const uint64_t &store_addr, const uint64_t &load_data, const uint64_t &value, uint64_t &wb_data) {
		wb_data = (load_data & ~(0x00000000fffffffflu << ((store_addr&7) << 3))) + ((value & 0xFFFFFFFF) << ((store_addr&7) << 3));
		return ((store_addr&3) == 0) ? true : false;
	}

	//*Adding load_word_fp
	bool load_word_fp(const uint64_t &load_addr, const uint64_t &load_data, float &f_wb_data)
	{
		uint32_t chunk = static_cast<uint32_t>((load_data >> ((load_addr&7) << 3)) & 0xFFFFFFFF);
		f_wb_data = *reinterpret_cast<float*>(&chunk);
		return ((load_addr&3) == 0) ? true : false;
	}

	//*Adding store_word_fp
	bool store_word_fp(const uint64_t &store_addr, const uint64_t &load_data,  float &value, uint64_t &wb_data) {
		uint64_t value_int = *reinterpret_cast<uint64_t*>(&value);
		wb_data = (load_data & ~(0x00000000fffffffflu << ((store_addr&7) << 3))) + ((value_int & 0xFFFFFFFF) << ((store_addr&7) << 3));
		return ((store_addr&3) == 0) ? true : false;
	}

public:
	struct emulator_state
	{
		uint64_t pc;
		uint64_t gpr[32];
		uint32_t instruction;
	};
	
	struct emulator_state before_step_state;

	uint64_t get_mstatus() {
		DEBUG_LOG("Fethcing mstatus for cross checking", mstatus.read_reg()); 
		return mstatus.read_reg(); 
	}

	vector<uint64_t> reg_file = vector<uint64_t>(32);          // register file
	//*Line 1021: Add F registers
	vector<float> freg_file = vector<float>(32);        // F register file

	//*Line 1023 Add Fregs to show state
	void show_state() {
		printf("pc: %016lx mstatus: %016lx mie: %016lx mcause: %016lx mepc: %016lx rx_ready: %d\n\
		x00: %016lx x01: %016lx x02: %016lx x03: %016lx x04: %016lx x05: %016lx x06: %016lx x07: %016lx\n\
		x08: %016lx x09: %016lx x10: %016lx x11: %016lx x12: %016lx x13: %016lx x14: %016lx x15: %016lx\n\
		x16: %016lx x17: %016lx x18: %016lx x19: %016lx x20: %016lx x21: %016lx x22: %016lx x23: %016lx\n\
		x24: %016lx x25: %016lx x26: %016lx x27: %016lx x28: %016lx x29: %016lx x30: %016lx x31: %016lx\n\
		fcsr: %016lx\n\
		f00: %016lf f01: %016lf f02: %016lf f03: %016lf f04: %016lf f05: %016lf f06: %016lf f07: %016lf\n\
		f08: %016lf f09: %016lf f10: %016lf f11: %016lf f12: %016lf f13: %016lf f14: %016lf f15: %016lf\n\
		f16: %016lf f17: %016lf f18: %016lf f19: %016lf f20: %016lf f21: %016lf f22: %016lf f23: %016lf\n\
		f24: %016lf f25: %016lf f26: %016lf f27: %016lf f28: %016lf f29: %016lf f30: %016lf f31: %016lf\n",
		PC, mstatus.read_reg(), mie.read_reg(), mcause.read_reg(), mepc, 1,
		reg_file[0], reg_file[1], reg_file[2], reg_file[3], reg_file[4], reg_file[5], reg_file[6], reg_file[7], 
		reg_file[8], reg_file[9], reg_file[10], reg_file[11], reg_file[12], reg_file[13], reg_file[14], reg_file[15],
		reg_file[16], reg_file[17], reg_file[18], reg_file[19], reg_file[20], reg_file[21], reg_file[22], reg_file[23],
		reg_file[24], reg_file[25], reg_file[26], reg_file[27], reg_file[28], reg_file[29], reg_file[30], reg_file[31],
		fcsr.read_reg(),
		freg_file[0], freg_file[1], freg_file[2], freg_file[3], freg_file[4], freg_file[5], freg_file[6], freg_file[7], 
		freg_file[8], freg_file[9], freg_file[10], freg_file[11], freg_file[12], freg_file[13], freg_file[14], freg_file[15],
		freg_file[16], freg_file[17], freg_file[18], freg_file[19], freg_file[20], freg_file[21], freg_file[22], freg_file[23],
		freg_file[24], freg_file[25], freg_file[26], freg_file[27], freg_file[28], freg_file[29], freg_file[30], freg_file[31]);
	}

	__uint64_t get_pc() {
		DEBUG_LOG("fetching PC for cross checking:", PC); 
		return PC; 
	}

	__uint64_t fetch_long(__uint64_t offset) { return memory.at(offset / 8); }

	int is_peripheral_read() {
		__uint32_t instruction = fetch_instruction(PC);
		__uint64_t load_addr = reg_file[(instruction >> 15) & 0x1f] + ( (__uint64_t)( (__int64_t)((__int32_t) instruction) >> 20 ) );/* (((__uint64_t)((__int32_t) instruction)) >> 20) */;
		DEBUG_LOG("@ is_peripheral_read() load_addr", load_addr);
		if ((instruction & 0x7f) != 0b0000011) { return 0; } 
		if ((load_addr >= DRAM_BASE) && (load_addr <= (DRAM_BASE + 0x9000000))) { return 0; } else { return 1; }
	}

	__uint32_t read_address() {
		__uint32_t instruction = fetch_instruction(PC);
		return reg_file[(instruction >> 15) & 0x1f] + (((__uint64_t)((__int32_t) instruction)) >> 20);
	}

	__uint32_t get_instruction() {
		return fetch_instruction(PC);
	}

	void set_register_with_value(__uint8_t rd,__uint64_t value) {
		DEBUG_LOG("@ set_register_with_value    rd:", ((uint64_t) rd));
		DEBUG_LOG("@ set_register_with_value value:", value);
		DEBUG_LOG("@ set_register_with_value   x15:", reg_file[15]);
		reg_file[rd] = value;
		DEBUG_LOG("@ set_register_with_value   x15:", reg_file[15]);
	}

	/**
	 * Initializes the emulator, i.e.
	 * 1. Setting the pc to the first instruction to be executed
	 * 2. Setting gprs and csrs
	 * 3. Initializes the memory with the image of the program
	 * @param image_name filename of the kernel image to be loaded to
	 *  emulator memory
	 * return 0 - to signal an error
	 */
	int init(string image_name) {
#ifdef EMULATOR_LOGGING
		logfile.open("emulator.log");
#endif

		ifstream infile(image_name, ios::binary);
		printf("stepping\n");
		if (!infile.good()) {
			fprintf(stderr, "Could not open image file! \n");
			exit(0);
		}

		// Get the file size by seeking to the end and then getting the position
		infile.seekg(0, ios::end);
		streampos fileSize = infile.tellg();
		infile.seekg(0, ios::beg);

		// Create a vector to store the binary data
		vector<unsigned long> byte_memory(fileSize);

		// Read the binary data into the vector
		infile.read(reinterpret_cast<char *>(byte_memory.data()), fileSize);

		infile.close();
		unsigned long pointer_end = (fileSize / 8) - 1;
		unsigned long long_jump = 0;
		for (const uint64_t data : byte_memory) {
			memory.at(long_jump) = (static_cast<unsigned long>(data));
			// printf("%lx\n", static_cast<unsigned long>(address));
			if ((long_jump++) >= pointer_end) {
				break;
			}
		}

		memory.at(MTIME_ADDR / 8) = 0;
		memory.at(MTIMECMP_ADDR / 8) = -1;

		PC = DRAM_BASE;
		PC_phy = 0;
		instruction = 0;

		return 1;
	}

	/**
	 * Perform a memory read for an instruction
	 * @param PC pc of the instruction
	 */
	__uint64_t fetch_instruction(__uint64_t PC) {
		PC_phy = PC - DRAM_BASE;

		if (PC % 4 == 0) {
			instruction = getINST(PC_phy / 4, &memory);
			return instruction;
		} else {
			INS_ADDR_MISSALIG = true;
			PC -= PC % 4;
		}
		return 0UL;
	}

	/**
	 * Perform a memory read for a data fetch, always read 8 bytes.
	 * Both for peripherals and main memory
	 * @param address physical base address of the data to be accessed
	 */
	__uint64_t memory_read(__uint64_t address) {
		// TODO add assertions to make sure no out of bounds access
		return memory.at((address - DRAM_BASE) / 8);;
	}

	#ifndef LOCKSTEP
	/**
	 * Sets up interrupts to execute instructions next
	 * i.e.: sets up *TIP, *SIP, *EIP
	 */
	void set_interrupts() {
		mip.MTIP = (mtime >= mtimecmp);

		if (signed_value(PC) < 0) {
			INS_ACC_FAULT = true;
		}

		if (LD_ACC_FAULT) {
			LD_ACC_FAULT = false;
			PC = excep_function(PC, CAUSE_LOAD_ACCESS, CAUSE_LOAD_ACCESS, CAUSE_LOAD_ACCESS, cp);
		} else if (mie.MEIE && mip.MEIP) {
			PC = interrupt_function(PC, CAUSE_MACHINE_EXT_INT, cp);
		} else if (mie.MSIE & mip.MSIP) {
			PC = interrupt_function(PC, CAUSE_MACHINE_SOFT_INT, cp);
		} else if (mie.MTIE && mip.MTIP) {
			PC = interrupt_function(PC, CAUSE_MACHINE_TIMER_INT, cp);
		} else if (mie.UEIE & mip.UEIP) {
			PC = interrupt_function(PC, CAUSE_USER_EXT_INT, cp);
		} else if (mie.UTIE & mip.UTIP) {
			PC = interrupt_function(PC, CAUSE_USER_TIMER_INT, cp);
		} else if (mie.USIE & mip.USIP) {
			PC = interrupt_function(PC, CAUSE_USER_SOFT_INT, cp);
		} else if (INS_ACC_FAULT) {
			INS_ACC_FAULT = false;
			PC = excep_function(PC, CAUSE_FETCH_ACCESS, CAUSE_FETCH_ACCESS, CAUSE_FETCH_ACCESS, cp);
		} else if (ILL_INS) {
			ILL_INS = false;
			PC = excep_function(PC, CAUSE_ILLEGAL_INSTRUCTION, CAUSE_ILLEGAL_INSTRUCTION, CAUSE_ILLEGAL_INSTRUCTION, cp);
		} else if (INS_ADDR_MISSALIG) {
			INS_ADDR_MISSALIG = false;
			PC = excep_function(PC, CAUSE_MISALIGNED_FETCH, CAUSE_MISALIGNED_FETCH, CAUSE_MISALIGNED_FETCH, cp);
		} else if (EBREAK) {
			EBREAK = false;
			// PC = excep_function(PC,CAUSE_BREAKPOINT,CAUSE_BREAKPOINT,CAUSE_BREAKPOINT,cp);
		} else if (LD_ADDR_MISSALIG) {
			LD_ADDR_MISSALIG = false;
			PC = excep_function(PC, CAUSE_MISALIGNED_LOAD, CAUSE_MISALIGNED_LOAD, CAUSE_MISALIGNED_LOAD, cp);
		} else if (STORE_ADDR_MISSALIG) {
			STORE_ADDR_MISSALIG = false;
			PC = excep_function(PC, CAUSE_MISALIGNED_STORE, CAUSE_MISALIGNED_STORE, CAUSE_MISALIGNED_STORE, cp);
		}
	}
	#else
	// this sets up timer interrupt
	// returns a positive integer [error code] if its not possible to set up interrupt
	int set_interrupts(__uint64_t peripheral_read = 1, __uint64_t mepc = 0) {

#ifdef EMULATOR_LOGGING
		if (!logfile.is_open()) {
			std::cerr << "No log file opened!, undefine EMULATOR_LOGGING if logging not required" << std::endl;
			exit(0);
		}

		logfile << "set_interrupts(): " << std::endl; 

#endif
		// emulator-simulator state semantic mis matches
		// look at the pc definitions when comparing state
		// step();
		if (is_peripheral_read()) {
			// outFile << "peripheral read after interrupt at: " << hex << get_pc();
			__uint32_t p_instruction = get_instruction();
			step();
			set_register_with_value((p_instruction>>7)&0x1f, peripheral_read);
		} else{
			step();
		}
		while (
			((get_instruction() & 0x0000007f) == 0x73) && 
			(get_instruction() & 0x00007000) && (get_pc() != mepc)
		) {
			step();
		}

		// mip.MTIP = 1; //(mtime >= mtimecmp);
		if (!mie.MTIE || !mstatus.mie) { return 1; }

		PC = interrupt_function(PC, CAUSE_MACHINE_TIMER_INT, cp);
		DEBUG_LOG("x15 at set_interrupts():", reg_file[15]);
		return 0;
	}

	#endif

	/**
	 * Steps through one architectural change of pc
	 * which includes executing one legal instruction, moving to
	 * exception/interrupt handler after a synchronous/asynchronous
	 * exception
	 */
	void step() {
		reg_file[0] = 0;

		gettimeofday(&tv, NULL);
		time_in_micros = 1000000 * tv.tv_sec + tv.tv_usec;
		mtime = (uint64_t)(time_in_micros * 10);

		instruction = fetch_instruction(PC);
		// printf("pc: %016lx fcsr: %016lx\n", PC, fcsr.read_fflags());
		// show_state();
		//printf("wbdata: %08x\n", static_cast<int32_t>(freg_file[0]));

		//--------------Debugging---------------------
		//To check if all tests passed
		// if (instruction == 0x00000073 && reg_file[3] == 0x1 && reg_file[17] == 0x5d && reg_file[10] == 0x0) {
		//   std::cout<<"All tests passed"<<endl;
		//   disable_raw_mode();
		//   exit(0);
		// }
		// if (instruction == 0x00018513) {
		//   std::cout<<"Tests failed"<<endl;
		//   disable_raw_mode();
		//   exit(1);
		// }
		//--------------Debugging---------------------
#ifdef EMULATOR_LOGGING
		if (!logfile.is_open()) {
			std::cerr << "No log file opened!, undefine EMULATOR_LOGGING if logging not required" << std::endl;
			exit(0);
		}

		logfile << "step(): ";
		logfile << "0x" << setfill('0') << setw(16) << hex << PC << " " /*log pc*/
			<< "0x" << setfill('0') << setw(8) << hex << instruction << " " /*log instruction to be executed*/
			<< "0x" << setfill('0') << setw(16) << hex << memory.at((0x12004000 - DRAM_BASE) / 8) << " " /*log instruction to be executed*/
			<< "0x" << setfill('0') << setw(16) << hex << reg_file[10] << std::endl; /*log instruction to be executed*/

#endif
		// record state of emulator before executing step
		/* before_step_state.instruction = instruction;
		before_step_state.pc = PC;
		for (int32_t gpr_i = 0; gpr_i < 32; gpr_i++) {
			before_step_state.gpr[gpr_i] = reg_file[gpr_i];
		}
		uint64_t before_step_probe = memory.at((0x12004000 - DRAM_BASE) / 8); */
		if (!INS_ADDR_MISSALIG) {

			// show_state();
			PC = PC + 4;

			opcode = static_cast<opcode_t>((instruction) & 0b1111111);

			rd = ((instruction) >> 7) & 0b11111;
			func3 = ((instruction) >> 12) & 0b111;
			rs1 = ((instruction) >> 15) & 0b11111;
			rs2 = ((instruction) >> 20) & 0b11111;
			func7 = ((instruction) >> 25) & 0b1111111;

			imm11_0 = ((instruction) >> 20) & 0b111111111111;
			imm31_12 = ((instruction) >> 12) & 0xFFFFF; // extract 20 bits

			imm_j = ((((instruction) >> 31) & 0b1) << 20) + ((instruction) & (0b11111111 << 12)) + ((((instruction) >> 20) & 0b1) << 11) + ((((instruction) >> 21) & 0b1111111111) << 1); //((instruction>>31) & 0b1)<<20 + (instruction & (0b11111111<<12)) + ((instruction>>20) & 0b1)<<11 +
			imm_b = ((((instruction) >> 31) & 0b1) << 12) + ((((instruction) >> 7) & 0b1) << 11) + ((((instruction) >> 25) & 0b111111) << 5) + (((instruction) >> 7) & 0b11110);
			imm_s = ((((instruction) >> 25) & 0b1111111) << 5) + (((instruction) >> 7) & 0b11111);
			imm = rd | ((instruction >> (25 - 5)) & 0xfe0);
			imm = (imm << 20) >> 20;

			amo_op = ((instruction) >> 27) & 0b11111;

			//* Line 1262 Add new opcode fields
			rm = ((instruction) >> 12) & 0b111;
			fmt = ((instruction) >> 25) & 0b11;
			funt5 = ((instruction) >> 27) & 0b11111;
			rs3 = ((instruction) >> 27) & 0b11111;

			switch (opcode) {
			case lui:
				wb_data = sign_extend<uint64_t>((instruction & 0xFFFFF000), 32);
				reg_file[rd] = wb_data;
				break;
			case auipc:
				wb_data = (PC - 4) + sign_extend<uint64_t>(((imm31_12) << 12), 32);
				reg_file[rd] = wb_data;
				break;
			case jump:
				wb_data = PC;
				PC = (PC - 4) + sign_extend<uint64_t>(imm_j, 21); // 21 bits sign extend
				if (PC % 4 == 0) { reg_file[rd] = wb_data; /* PC + 4 to rd */ }
				break;
			case jumpr:
				wb_data = PC;
				PC = (reg_file[rs1] + sign_extend<uint64_t>(imm11_0, 12)) & 0xFFFFFFFFFFFFFFFE; // setting LSB to 0 as spec page 20
				if (PC % 4 == 0) { reg_file[rd] = wb_data; /* PC + 4 to rd */ }
				break;
			case cjump:
				switch (func3) {
				case 0b000: branch = reg_file[rs1] == reg_file[rs2]; break; // BEQ
				case 0b001: branch = (reg_file[rs1] != reg_file[rs2]); break; // BNE
				case 0b100: branch = (signed_value(reg_file[rs1]) < signed_value(reg_file[rs2])); break; // BLT
				case 0b101: branch = (signed_value(reg_file[rs1]) >= signed_value(reg_file[rs2])); break; // BGE
				case 0b110: branch = (reg_file[rs1] < reg_file[rs2]); break; // BLTU
				case 0b111: branch = (reg_file[rs1] >= reg_file[rs2]); break; // BGEU
				default:
					break;
				}
				if (branch) { PC = PC - 4 + sign_extend<uint64_t>(imm_b, 13); }
				break;
			case load:
				load_addr = (reg_file[rs1] + sign_extend<uint64_t>(imm11_0, 12)) & 0x00000000ffffffff;
				if (signed_value(load_addr) < 0) {
					LD_ACC_FAULT = true;
				} else {
					if (((load_addr >= DRAM_BASE) && (load_addr <= (DRAM_BASE + 0x9000000)))) {
						load_data = memory.at((load_addr - DRAM_BASE) / 8);
						DEBUG_LOG("load_addr", load_addr); DEBUG_LOG("load_data", load_data);
						// right justification and sign extension
						wb_data = (static_cast<int64_t>(load_data << ((64-((load_addr&7)<<3)) - (1 << (3 + (func3&3)))))) >> (64 - (1 << (3 + (func3&3))));
						DEBUG_LOG("wb_data", wb_data);
						wb_data = (func3&4) ? (wb_data & (0xfffffffffffffffflu >> (64 - (1 << (3 + (func3&3)))))) : wb_data;
						DEBUG_LOG("wb_data", wb_data);
						if ((load_addr & ((1 << (func3&3)) - 1))) { LD_ADDR_MISSALIG = true; } else { reg_file[rd] = wb_data; }
					} else {
						if ((load_addr >= CLINT_BASE) && (load_addr <= CLINT_BASE + CLINT_SIZE)) {
							uint64_t offset = load_addr - CLINT_BASE;
							switch (offset) {
							case 0xbff8: wb_data = mtime; break; // rtc time
							case 0x4000: wb_data = mtimecmp; break; // timecmp
							default:
								debug("[WARNING-EMULATOR] undefined load address load_addr: 0x%08lx\n", load_addr); 
								wb_data = 0;
								break;
							}
						} else if (load_addr == FIFO_ADDR_RX) {
							wb_data = kbhit() ? 0 : 2;
						}
						else if (load_addr == FIFO_ADDR_TX) {
							#ifndef LOCKSTEP
							wb_data = getchar();
							#endif
						} else {
							wb_data = 0x1000;
							debug("[WARNING-EMULATOR] undefined load address load_addr: 0x%08lx\n", load_addr); 
						}
						reg_file[rd] = wb_data;
					}
				}
				break;
			case store:
				store_addr = (reg_file[rs1] + sign_extend<uint64_t>(imm_s, 12)) & 0x00000000ffffffff; // address generation
				if ((store_addr >= DRAM_BASE) & (store_addr < (DRAM_BASE + 0x9000000))) { // main memory
					DEBUG_LOG("store_addr: ", store_addr);
					store_data = memory.at((store_addr - DRAM_BASE) / 8); // get original data
					DEBUG_LOG("original_data", store_data);
					wb_data = store_data & ~((0xfffffffffffffffflu >> (64 - (1 << (3 + (func3&3))))) << ((store_addr&7) << 3)); // make space for new data to be written
					DEBUG_LOG("after clearing out original data: ", wb_data);
					val = (reg_file[rs2] & (0xfffffffffffffffflu >> (64 - (1 << (3 + (func3&3)))))) << ((store_addr&7) << 3); // byte aligning write data
					DEBUG_LOG("After byte aligning data: ", val);
					wb_data |= val; // overwriting original data
					DEBUG_LOG("After modifying double, before writing: ", wb_data);
					ls_success = ((store_addr & ((1 << (func3&3)) - 1)) == 0); // looking for misalignment 
					DEBUG_LOG("ls_success: ", ls_success);
					if (!ls_success) { 
						PC = excep_function(PC, CAUSE_MISALIGNED_STORE, CAUSE_MISALIGNED_STORE, CAUSE_MISALIGNED_STORE, cp);
					} else {
						memory.at((store_addr - DRAM_BASE) / 8) = wb_data; // store data
					}
				} else { // peripherals
					if ((store_addr >= CLINT_BASE) & (store_addr <= (CLINT_BASE + CLINT_SIZE))) {
						switch (store_addr - CLINT_BASE) {
						case 0x4000:
							mtimecmp = reg_file[rs2];
							mip.STIP = 0;
							break;
						case 0x0000:
							// This is the MSIP, which doesn't do anything yet
							break;
						default:
							debug("[WARNING-EMULATOR] undefined store address store_addr: 0x%08lx\n", store_addr); 
							break;
						}
					} else if (store_addr == FIFO_ADDR_TX) {
						cout << (char)reg_file[rs2] << flush;
					} else { debug("[WARNING-EMULATOR] undefined store address store_addr: 0x%08lx\n", store_addr); }
				}
				break;
			case iops:
			case rops:
				store_data = (opcode == rops) ? (reg_file[rs2]) : (((func3&3)!=1)?sign_extend<uint64_t>(imm11_0, 12):(imm11_0&63)); // temp variable
				if ((opcode == rops) && (func7 == 0b0000001)) {
					switch (func3) {
					case 0b000: // MUL
						mult_temp = reg_file[rs1] * store_data;
						reg_file[rd] = (mult_temp)&MASK64;
						break;
					case 0b001: // MULH
						mult_temp = ((__uint128_t)signed_value(reg_file[rs1]) * (__uint128_t)signed_value(store_data));
						reg_file[rd] = ((mult_temp) >> 64) & MASK64;
						break;
					case 0b010: // MULHSU
						mult_temp = ((__uint128_t)signed_value(reg_file[rs1]) * (__uint128_t)store_data);
						reg_file[rd] = ((mult_temp) >> 64) & MASK64;
						break;
					case 0b011: // MULHU
						mult_temp = (__uint128_t)reg_file[rs1] * (__uint128_t)store_data;
						reg_file[rd] = ((mult_temp) >> 64) & MASK64;
						break;
					case 0b100: // DIV
						reg_file[rd] = (uint64_t)divi<int64_t>(signed_value(reg_file[rs1]), signed_value(store_data), 0);
						break;
					case 0b101: // DIVU
						reg_file[rd] = divi<uint64_t>(reg_file[rs1], store_data, 1);
						break;
					case 0b110: // REM
						reg_file[rd] = (uint64_t)divi<int64_t>(signed_value(reg_file[rs1]), signed_value(store_data), 2);
						break;
					case 0b111: // REMU
						reg_file[rd] = divi<uint64_t>(reg_file[rs1], store_data, 3);
						break;
					}
				}
				else {
					switch (func3) {
					case 0b000: wb_data = ((opcode != rops) || (func7 == 0)) ? (reg_file[rs1] + store_data) : (reg_file[rs1] - store_data); break; // ADD & SUB
					case 0b010: wb_data = (signed_value(reg_file[rs1]) < signed_value(store_data)) ? 1 : 0; break; // SLT
					case 0b011: wb_data = (reg_file[rs1] < store_data) ? 1 : 0; break; // SLTU
					case 0b111: wb_data = reg_file[rs1] & store_data; break; // AND
					case 0b110: wb_data = reg_file[rs1] | store_data; break; // OR
					case 0b100: wb_data = reg_file[rs1] ^ store_data; break; // XOR
					case 0b001: wb_data = ((reg_file[rs1]) << (store_data & 0b111111)); break; // SLL
					case 0b101: wb_data = ((func7 >> 1) == 0) ? (reg_file[rs1] >> (store_data & 63)) : (static_cast<int64_t>(reg_file[rs1]) >> (store_data&63)); break; // SRL
					default:
						break;
					}
					reg_file[rd] = wb_data;
				}
				break;
			case iops64: // 32 bit instructions exclusive for rv64i
			case rops64:
				store_data = (opcode == rops64) ? (reg_file[rs2]) : (((func3&3)!=1)?sign_extend<uint64_t>(imm11_0, 12):(imm11_0&31)); // temp variable
				if ((opcode == rops64) && (func7 == 0b0000001)) {
					switch (func3) {
					case 0b000: // MULW
						wb_data = sign_extend<uint64_t>(((reg_file[rs1] & MASK32) * (store_data & MASK32)) & MASK32, 32);
						break;
					case 0b100: // DIVW
						wb_data = sign_extend<uint64_t>(MASK32 & ((uint64_t)divi32<int32_t>(signed_value32(reg_file[rs1] & MASK32), signed_value32(store_data & MASK32), 0)), 32);
						break;
					case 0b101: // DIVUW
						wb_data = sign_extend<uint64_t>((uint64_t)divi32<uint32_t>(reg_file[rs1], store_data, 1), 32);
						break;
					case 0b110: // REMW
						wb_data = sign_extend<uint64_t>(((uint64_t)divi32<int32_t>(signed_value32(reg_file[rs1]), signed_value32(store_data), 2) & MASK32), 32);
						break;
					case 0b111: // REMUW
						wb_data = sign_extend<uint64_t>((uint64_t)divi32<uint32_t>(reg_file[rs1], store_data, 3), 32);
						break;
					}
					reg_file[rd] = wb_data;
				}
				else {
					switch (func3) {
					case 0b000: 
						wb_data = static_cast<int32_t>(((opcode != rops64) || (func7 == 0)) ? (reg_file[rs1] + store_data) : (reg_file[rs1] - store_data));
						break;
					case 0b001: // SLLW
						wb_data = static_cast<int32_t>((reg_file[rs1]) << ((store_data) & 0b11111));
						break;
					case 0b101: // SRLW
						wb_data = static_cast<int32_t>((func7 == 0) ? ((reg_file[rs1]&0xffffffffu) >> (store_data & 31)) : ((static_cast<int32_t>(reg_file[rs1])) >> (store_data & 31)));
						break;
					}
					reg_file[rd] = wb_data;
				}
				break;
			case amo:
				if (func3 == 0b010) { // AMO.W-32
					load_addr = reg_file[rs1] & 0x00000000ffffffff;
					if (load_addr > DRAM_BASE & load_addr < DRAM_BASE + 0x9000000) {
						load_addr = load_addr;
					} else {
						printf("illegal access\n");
						exit(0);
					}
					load_data = memory.at((load_addr - DRAM_BASE) / 8);
					switch (amo_op) {
					case 0b00010: // LR.W
						ls_success = load_word(load_addr, load_data, wb_data);
						if (!ls_success) {
							STORE_ADDR_MISSALIG = true;
							mtval = load_addr;
						} else {
							ret_data = sign_extend<uint64_t>(wb_data & MASK32, 32);
							amo_reserve_valid = true;
							amo_reserve_addr = load_addr;
						}
						break;
					case 0b00011: // SC.W
						if (amo_reserve_valid && (reg_file[rs1] == amo_reserve_addr)) {
							store_data = reg_file[rs2] & MASK32;
							ls_success = store_word(load_addr, load_data, store_data, wb_data);
							if (!ls_success) {
								STORE_ADDR_MISSALIG = true;
								mtval = load_addr;
								ret_data = 1;
							} else {
								memory.at((load_addr - DRAM_BASE) / 8) = wb_data;
								ret_data = 0;
							}
						}
						else {
							ret_data = 1;
						}
						amo_reserve_addr = 0;
						amo_reserve_valid = false;
						break;
					default:
						ls_success = load_word(load_addr, load_data, wb_data);
						if (!ls_success) {
							mtval = load_addr;
							STORE_ADDR_MISSALIG = true;
						} else {
							ret_data = sign_extend<uint64_t>((wb_data & MASK32), 32);
							switch (amo_op)
							{
							case 0b00001: wb_data = reg_file[rs2] & MASK32; break; // AMOSWAP.W
							case 0b00000: wb_data = ((wb_data & MASK32) + (reg_file[rs2] & MASK32)) & MASK32; break; // AMOADD.W
							case 0b00100: wb_data = ((wb_data & MASK32) ^ (reg_file[rs2] & MASK32)) & MASK32; break; // AMOXOR.W
							case 0b01100: wb_data = ((wb_data & MASK32) & (reg_file[rs2] & MASK32)) & MASK32; break; // AMOAND.W
							case 0b01000: wb_data = ((wb_data & MASK32) | (reg_file[rs2] & MASK32)) & MASK32; break; // AMOOR.W
							case 0b10000: wb_data = min(signed_value32(wb_data & MASK32), signed_value32(reg_file[rs2] & MASK32)) & MASK32; break; // AMOMIN.W
							case 0b10100: wb_data = max(signed_value32(wb_data & MASK32), signed_value32(reg_file[rs2] & MASK32)) & MASK32; break; // AMOMAX.W
							case 0b11000: wb_data = min((wb_data & MASK32), (reg_file[rs2] & MASK32)) & MASK32; break; // AMOMINU.W
							case 0b11100: wb_data = max((wb_data & MASK32), (reg_file[rs2] & MASK32)) & MASK32; break; // AMOMAXU.W
							default:
								break;
							}
							ls_success = store_word(load_addr, load_data, wb_data, store_data);
							memory.at((load_addr - DRAM_BASE) / 8) = store_data;
						}
						break;
					}
					reg_file[rd] = ret_data;
				}
				else if (func3 == 0b011) { // AMO.D-64
					load_addr = reg_file[rs1] & 0x00000000ffffffff;
					if (load_addr > DRAM_BASE & load_addr < DRAM_BASE + 0x9000000) {
						load_addr = load_addr;
					} else {
						printf("illegal access\n");
						exit(0);
					}
					wb_data = memory.at((load_addr - DRAM_BASE) / 8);
					switch (amo_op) {
					case 0b00010: // LR.D
						if ((load_addr % 8) != 0) {
							mtval = load_addr;
							STORE_ADDR_MISSALIG = true;
						} else {
							ret_data = wb_data;
							amo_reserve_valid64 = true;
							amo_reserve_addr64 = reg_file[rs1];
						}
						break;
					case 0b00011: // SC.D
						if (amo_reserve_valid64 && (reg_file[rs1] == amo_reserve_addr64)) {
							store_data = reg_file[rs2];
							if ((load_addr % 8) != 0) {
								mtval = load_addr;
								STORE_ADDR_MISSALIG = true;
							}
							else {
								memory.at((load_addr - DRAM_BASE) / 8) = store_data;
								ret_data = 0;
							}
						}
						else {
							ret_data = 1;
						}
						amo_reserve_addr64 = 0;
						amo_reserve_valid64 = false;
						break;
					default:
						if ((load_addr % 8) != 0) {
							mtval = load_addr;
							STORE_ADDR_MISSALIG = true;
						} else {
							ret_data = wb_data;
							switch (amo_op) {
							case 0b00001: wb_data = reg_file[rs2]; break; // AMOSWAP.W
							case 0b00000: wb_data = (wb_data + reg_file[rs2]); break; // AMOADD.D
							case 0b00100: wb_data = (wb_data ^ reg_file[rs2]); break; // AMOXOR.D
							case 0b01100: wb_data = (wb_data & reg_file[rs2]); break; // AMOAND.D
							case 0b01000: wb_data = (wb_data | reg_file[rs2]); break; // AMOOR.D
							case 0b10000: wb_data = min(signed_value(wb_data), signed_value(reg_file[rs2])); break; // AMOMIN.D
							case 0b10100: wb_data = max(signed_value(wb_data), signed_value(reg_file[rs2])); break; // AMOMAX.D
							case 0b11000: wb_data = min(wb_data, reg_file[rs2]); break; // AMOMINU.D
							case 0b11100: wb_data = max(wb_data, reg_file[rs2]); break; // AMOMAXU.D
							default: break;
							}
							memory.at((load_addr - DRAM_BASE) / 8) = wb_data;
						}
						break;
					}
					reg_file[rd] = ret_data;
				}
				break;
			case fence:
				break;
			case systm:
				switch (func3) {
				case 0b000:
					switch (imm11_0) {
					case 0: // ecall
						PC = excep_function(PC, CAUSE_MACHINE_ECALL, CAUSE_SUPERVISOR_ECALL, CAUSE_USER_ECALL, cp);
						break;
					case 1: // ebreak
						EBREAK = true;
						break;
					case 770: // mret
						PC = mepc;
						cp = (plevel_t)mstatus.mpp;
						mstatus.mie = mstatus.mpie; // 1; ?
						mstatus.mpp = 0b00;         // setting to umode (least privilage mode)
						mstatus.mpie = 1;           // 0; ?
						break;
					default:
						break;
					}
					break;
				default:
					DEBUG_LOG("Before xecuting CSR read x15, at this time this:", reg_file[15]);
					csr_data = csr_read(imm11_0);
					DEBUG_LOG("Executing CSR read x15, after this:", reg_file[15]);
					if (csr_read_success) {
						store_data =  func3&4 ? rs1 : reg_file[rs1];
						switch (func3 & 3) {
						case 0b01: csr_bool = csr_write(imm11_0, store_data); break; // CSRRW*
						case 0b10: csr_bool = csr_write(imm11_0, (store_data | csr_data)); break; // CSRRS*
						case 0b11: csr_bool = csr_write(imm11_0, ((~store_data) & csr_data)); break; // CSRRC*
						default: break; // illegal instruction
						}
						DEBUG_LOG("Executing CSR write, x15 after this:", reg_file[15]);
						if (!csr_bool) {
							mtval = instruction;
							ILL_INS = true;
						} else if (rd != 0) {
							reg_file[rd] = csr_data;
							DEBUG_LOG("Writing CSR value to gprs, x15 after this:", reg_file[15]);
						}
					} else {
						mtval = instruction;
						ILL_INS = true;
					}
					break;
				}
				break;
			//* Line 1043 Adding S instructions
			//wb_data and f_wb_data assigned as arrised to reduce confussion. 
			case fload:
				load_addr = (reg_file[rs1] + sign_extend<uint64_t>(imm11_0, 12)) & 0x00000000ffffffff;
				if ((load_addr >= DRAM_BASE) & (load_addr <= (DRAM_BASE + 0x9000000))) {
					load_data = memory.at((load_addr - DRAM_BASE) / 8);
					//For load word
					if (func3 == 0b010) {
						if (!load_word_fp(load_addr, load_data, f_wb_data)) {
							LD_ADDR_MISSALIG = true;
						} else {
							freg_file[rd] = f_wb_data;
						}
					}
				} else {
					// illegal access
				}
				break;
			case fstore:
				store_addr = (reg_file[rs1] + sign_extend<uint64_t>(imm_s, 12)) & 0x00000000ffffffff;
				if ((store_addr >= DRAM_BASE) & (store_addr < (DRAM_BASE + 0x9000000))) {
					store_data = memory.at((store_addr - DRAM_BASE) / 8);
					if (func3 == 0b010) {
						f_val = freg_file[rs2];
						ls_success = store_word_fp(store_addr, store_data, f_val, wb_data);
					}
					if (!ls_success) {
						PC = excep_function(PC, CAUSE_MISALIGNED_STORE, CAUSE_MISALIGNED_STORE, CAUSE_MISALIGNED_STORE, cp);
					} else {
						memory.at((store_addr - DRAM_BASE) / 8) = wb_data;
						load_data = memory.at((load_addr - DRAM_BASE) / 8);
					}
				} else {
					// illegal access
				}
				break;
			case fmadd:
			case fmsub:
			case fnmsub:
			case fnmadd:
				float temp_result;
				switch (opcode) {
				case fmadd: temp_result = (freg_file[rs1] * freg_file[rs2]) + freg_file[rs3]; break;
				case fmsub: temp_result = (freg_file[rs1] * freg_file[rs2]) - freg_file[rs3]; break;
				case fnmsub: temp_result = - (freg_file[rs1] * freg_file[rs2]) + freg_file[rs3]; break;
				case fnmadd: temp_result = - (freg_file[rs1] * freg_file[rs2]) - freg_file[rs3]; break;
				}

				roundingmode_change(rm,temp_result);

				feclearexcept(FE_ALL_EXCEPT);
				switch (opcode) {
					case fmadd: f_wb_data = (freg_file[rs1] * freg_file[rs2]) + freg_file[rs3]; break;
					case fmsub: f_wb_data = (freg_file[rs1] * freg_file[rs2]) - freg_file[rs3]; break;
					case fnmsub: f_wb_data = - (freg_file[rs1] * freg_file[rs2]) + freg_file[rs3]; break;
					case fnmadd: f_wb_data = - (freg_file[rs1] * freg_file[rs2]) - freg_file[rs3]; break;
				}
				setfflags();

				freg_file[rd] = f_wb_data;

				roundingmode_revert();
				break;
			case fcomp:
				switch (func7) {
				case 0b0000000: //FADD.S
				case 0b0000100: //FSUB.S
				case 0b0001000: //FMUL.S
				case 0b0001100: //FDIV.S
				case 0b0101100: //FSQRT.S
					float temp_result;
					switch (func7) {
					case 0b0000000: temp_result = freg_file[rs1] + freg_file[rs2]; break; //FADD.S
					case 0b0000100: temp_result = freg_file[rs1] - freg_file[rs2]; break; //FSUB.S
					case 0b0001000: temp_result = freg_file[rs1] * freg_file[rs2]; break; //FMUL.S
					case 0b0001100: temp_result = freg_file[rs1] / freg_file[rs2]; break; //FDIV.S
					case 0b0101100: temp_result = sqrt(freg_file[rs1]); break; //FSQRT.S
					}

					roundingmode_change(rm,temp_result);

					feclearexcept(FE_ALL_EXCEPT);

					switch (func7) {
					case 0b0000000: f_wb_data = freg_file[rs1] + freg_file[rs2]; break; //FADD.S
					case 0b0000100: f_wb_data = freg_file[rs1] - freg_file[rs2]; break; //FSUB.S
					case 0b0001000: f_wb_data = freg_file[rs1] * freg_file[rs2]; break; //FMUL.S
					case 0b0001100: f_wb_data = freg_file[rs1] / freg_file[rs2]; break; //FDIV.S
					case 0b0101100: f_wb_data = sqrt(freg_file[rs1]); break; //FSQRT.S
					}
					setfflags();
					if (((FLOAT_TO_32BITS(f_wb_data) & 0x7f800000) == 0x7f800000) && (FLOAT_TO_32BITS(f_wb_data) & 0x007fffff)) {
						int temp = 0x7FC00000;
						float* tempPtr = reinterpret_cast<float*>(&temp);
						f_wb_data = *tempPtr;
					}

					freg_file[rd] = f_wb_data;

					roundingmode_revert();

					break;
				case 0b0010000: //FSGNJ.S FSGNJN.S FSGNJX.S
					int32_t result_bits;
					switch (rm) {
					case 0b000: result_bits = (FLOAT_TO_32BITS(freg_file[rs1]) & 0x7fffffff) | (FLOAT_TO_32BITS(freg_file[rs2]) & 0x80000000); break;  //FSGNJ.S
					case 0b001: result_bits = (FLOAT_TO_32BITS(freg_file[rs1]) & 0x7fffffff) | ((~FLOAT_TO_32BITS(freg_file[rs2])) & 0x80000000); break;  //FSGNJN.S
					case 0b010: result_bits = (FLOAT_TO_32BITS(freg_file[rs1]) & 0x7fffffff) | ((FLOAT_TO_32BITS(freg_file[rs2]) ^ FLOAT_TO_32BITS(freg_file[rs1])) & 0x80000000); break;  //FSGNJX.S
					default:
						break;
					}
					freg_file[rd] = *reinterpret_cast<float*>(&result_bits);
					break;
				case 0b0010100: //FMIN.S FMAX.S 
					//Exception flags and results are handled by the min_max_f function
					freg_file[rd] = min_max_f(freg_file[rs1], freg_file[rs2], rm&1);
					break;

				case 0b1100000: //FCVT.W.S FCVTWU.S FCVT.L.S FCVT.LU.S
					roundingmode_change(rm, freg_file[rs1]);

					feclearexcept(FE_ALL_EXCEPT); 

					if (isnan(freg_file[rs1]) || (freg_file[rs1] == INFINITY) || \
					(freg_file[rs1] > static_cast<float>(0xffffffffffffffffllu >> (33 - (rs2&1) - ((rs2&2)*16)))) /* Output for out-of-range positive input */ ) {

						wb_data = 0xffffffffffffffffllu >> ((rs2 == 0) ? 33 : ((rs2 == 2) ? 1 : 0));  // most positive value representable by target value
						fcsr.write_fflags(0b10000 | fcsr.read_fflags()); // set invalid bit
					} else if ((freg_file[rs1] == -INFINITY) || \
					((rs2&1)?((FLOAT_TO_32BITS(freg_file[rs1]) >= 0x80000000u) && (static_cast<int32_t>(freg_file[rs1]) < 0)):\
					(freg_file[rs1] < (static_cast<float>(static_cast<int64_t>(0xffffffff80000000 << (32*(rs2&2))))))) /* Output for out-of-range negative input */) {

						wb_data = (rs2&1) ? 0 : (0xffffffff80000000 << (16*(rs2&2))); // most negative value representable by target value
						fcsr.write_fflags(0b10000 | fcsr.read_fflags()); // set invalid bit
					} else {
						if ( FLOAT_TO_32BITS(freg_file[rs1]) & \
						((((FLOAT_TO_32BITS(freg_file[rs1]) >> 23) & 0xff) < 150)?((1 << (150 - ((FLOAT_TO_32BITS(freg_file[rs1]) >> 23) & 0xff))) - 1):0) /*bits lost after conversion*/ ) {
							// inexact converstion
							fcsr.write_fflags(0b00001 | fcsr.read_fflags());
						}
						//no need to check for the lower limit of int32_t because it is handled accordingly
						switch (rs2) {
						case 0b00000: wb_data = static_cast<uint64_t>(static_cast<int32_t>(freg_file[rs1])); break; //FCVT.W.S
						case 0b00001: wb_data = static_cast<int32_t>(static_cast<uint32_t>(freg_file[rs1])); break; //FCVT.WU.S
						case 0b00010: wb_data = static_cast<uint64_t>(static_cast<int64_t>(freg_file[rs1])); break; //FCVT.L.S
						case 0b00011: wb_data = static_cast<uint64_t>(static_cast<uint64_t>(freg_file[rs1])); break; //FCVT.LU.S
						default: break; // illegal instruction
						}
					}

					reg_file[rd] = wb_data;
					roundingmode_revert();
					break;
				case 0b1010000: //FEQ.S FLT.S FLE.S  here rd is in integer register file

					feclearexcept(FE_ALL_EXCEPT);

					// Setting flags
					switch (func3) {
					case 0b010:
						if (number_class(freg_file[rs1]) != 8 && number_class(freg_file[rs2]) != 8) { break; }
					case 0b000:
					case 0b001: // func3 == 0b000 and func3 == 0b001
						if (!isnan(freg_file[rs1]) && !isnan(freg_file[rs2])) { break; }
						fcsr.write_fflags(0b10000 | fcsr.read_fflags()); //Set invalid operation flag high
					}
					// execution
					switch (func3) {
					case 0b000: wb_data = (freg_file[rs1] <= freg_file[rs2]) ? 1 : 0; break;
					case 0b001: wb_data = (freg_file[rs1] < freg_file[rs2]) ? 1 : 0; break;
					case 0b010: wb_data = (freg_file[rs1] == freg_file[rs2]) ? 1 : 0; break;
					default: break; // For illegal instructions
					}
					
					reg_file[rd] = wb_data;
					break;
				case 0b1101000: //FCVT.S.W FCVT.S.WU  FCVT.S.L FCVT.S.LU 
					roundingmode_change(rm, reg_file[rs1]);

					feclearexcept(FE_ALL_EXCEPT); 

					switch (rs2){
						case 0b00000: //FCVT.S.W
							f_wb_data=signed_value32(reg_file[rs1]);
							break;
						case 0b00001: //FCVT.S.WU
							f_wb_data=(reg_file[rs1] & MASK32);
							break;
						case 0b00010: //FCVT.S.L
							f_wb_data=signed_value(reg_file[rs1]);
							break;
						case 0b00011: //FCVT.S.LU 
							f_wb_data=reg_file[rs1];
							break;
						default:	
							break;	
					}
					setfflags();
					freg_file[rd] = f_wb_data;
					roundingmode_revert();
					break;
				case 0b1111000: {//FMV.W.X 
					f_wb_data = *reinterpret_cast<float *>(&reg_file[rs1]); 
					freg_file[rd] = f_wb_data;
					// if (rm != 0) illegal instruction
					break;  
				}
				case 0b1110000: //FCLASS & FMV.X.W
					switch (rm) {
					case 0b001: wb_data = (0b1 << (number_class(freg_file[rs1]))); break; // FCLASS // number_class(freg_file[rs1]) should not return -1
					case 0b000: wb_data = static_cast<uint64_t>(*reinterpret_cast<int32_t*>(&freg_file[rs1])); break; // FMV.X.W
					default: break; // Illegal instruction
					}
					reg_file[rd] = wb_data; // TODO: Should not happen in case of illegal instruction
					break;
				}
				default:
					break;
			}
		}
		DEBUG_LOG("x10 after executing instruction:", reg_file[10]);
		// get state of probe after step
		/* uint64_t probe_after_step = memory.at((0x12004000 - DRAM_BASE) / 8);
		if (probe_after_step != before_step_probe) {
			printf ("Noticed change in proble location at pc: 0x%016lx instruction: 0x%08lx\n", before_step_state.pc, before_step_state.instruction);
			printf ("Value at probe before: 0x%016lx and after: 0x%016lx \n", before_step_probe, probe_after_step);
			printf ("State of gprs\n");
			printf ("\t x00: %016lx x01: %016lx x02: %016lx x03: %016lx x04: %016lx x05: %016lx x06: %016lx x07: %016lx \n",
				before_step_state.gpr[0], before_step_state.gpr[1], before_step_state.gpr[2], before_step_state.gpr[3], 
				before_step_state.gpr[4], before_step_state.gpr[5], before_step_state.gpr[6], before_step_state.gpr[7]);
			printf ("\t x08: %016lx x09: %016lx x10: %016lx x11: %016lx x12: %016lx x13: %016lx x14: %016lx x15: %016lx \n",
				before_step_state.gpr[8], before_step_state.gpr[9], before_step_state.gpr[10], before_step_state.gpr[11], 
				before_step_state.gpr[12], before_step_state.gpr[13], before_step_state.gpr[14], before_step_state.gpr[15]);
			printf ("\t x16: %016lx x17: %016lx x18: %016lx x19: %016lx x20: %016lx x21: %016lx x22: %016lx x23: %016lx \n",
				before_step_state.gpr[16], before_step_state.gpr[17], before_step_state.gpr[18], before_step_state.gpr[19], 
				before_step_state.gpr[20], before_step_state.gpr[21], before_step_state.gpr[22], before_step_state.gpr[23]);
			printf ("\t x24: %016lx x25: %016lx x26: %016lx x27: %016lx x28: %016lx x29: %016lx x30: %016lx x31: %016lx \n",
				before_step_state.gpr[24], before_step_state.gpr[25], before_step_state.gpr[26], before_step_state.gpr[27], 
				before_step_state.gpr[28], before_step_state.gpr[29], before_step_state.gpr[30], before_step_state.gpr[31]);
			printf ("What to do now c: continue with cosimulation b: break and exit program: ");
			uint8_t keep_looping = 1;
			while (keep_looping) {
				switch (getchar()) {
				case 'c':
					printf ("\nContinuing cosimulation\n");
					keep_looping = 0;
					break;

				case 'b': 
					printf ("Exiting program.... ");
					exit(0);
				
				default:
					printf ("\nInvalid input try again: ");
					break;
				}
			}
		} */
	}
};
