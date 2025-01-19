#define MEM_SIZE 28

#define MASK32 0xFFFFFFFFllu
#define MASK64 0xFFFFFFFFFFFFFFFFllu

#define CAUSE_USER_ECALL            0x8
#define CAUSE_SUPERVISOR_ECALL      0x9
#define CAUSE_HYPERVISOR_ECALL      0xa
#define CAUSE_MACHINE_ECALL         0xb

#define CAUSE_USER_SOFT_INT         0x0
#define CAUSE_SUPERVISOR_SOFT_INT   0x1
#define CAUSE_MACHINE_SOFT_INT      0x3

#define CAUSE_USER_TIMER_INT        0x4
#define CAUSE_SUPERVISOR_TIMER_INT  0x5
#define CAUSE_MACHINE_TIMER_INT     0x7

#define CAUSE_USER_EXT_INT          0x8
#define CAUSE_SUPERVISOR_EXT_INT    0x9
#define CAUSE_MACHINE_EXT_INT       0xb

#define CAUSE_MISALIGNED_FETCH      0x0
#define CAUSE_ILLEGAL_INSTRUCTION   0x2
#define CAUSE_MISALIGNED_LOAD       0x4
#define CAUSE_MISALIGNED_STORE      0x6
#define CAUSE_BREAKPOINT            0x3
#define CAUSE_FETCH_ACCESS          0x1
#define CAUSE_LOAD_ACCESS           0x5

#define FIFO_ADDR_TX 0xe0001030
#define FIFO_ADDR_RX 0xe000102c

#define CLINT_BASE 0x2000000
#define CLINT_SIZE 0x00c0000

#define MTIME_ADDR (CLINT_BASE+0xbff8)
#define MTIMECMP_ADDR (CLINT_BASE+0x4000)

std::string reg_file_names[32] = {"zero","ra","sp","gp","tp","t0","t1","t2","s0","s1","a0","a1","a2","a3","a4","a5","a6","a7","s2","s3","s4","s5","s6","s7","s8","s9","s10","s11","t3","t4","t5","t6"};
//* Line 40: Register names for F registers
std::string freg_file_names[32] = {"ft0","ft1","ft2","ft3","ft4","ft5","ft6","ft7","fs0","fs1","fa0","fa1","fa2","fa3","fa4","fa5","fa6","fa7","fs2","fs3","fs4","fs5","fs6","fs7","fs8","fs9","fs10","fs11","ft8","ft9","ft10","ft11"};

// type defs
enum opcode_t {
	lui    = 0b0110111,
	auipc  = 0b0010111,
	jump   = 0b1101111,
	jumpr  = 0b1100111,
	cjump  = 0b1100011,
	load   = 0b0000011,
	store  = 0b0100011,
	iops   = 0b0010011,
	iops64 = 0b0011011,
	rops   = 0b0110011,
	rops64 = 0b0111011,
	amo    = 0b0101111,
	fence  = 0b0001111,
	systm  = 0b1110011,

	//*Line 43 Define new instruction opcodes
	fload  = 0b0000111,																						//
	fstore = 0b0100111,																						//
	fmadd  = 0b1000011,																						//
	fmsub  = 0b1000111,																						// for all S,D and Q
	fnmsub = 0b1001011,																						//
	fnmadd = 0b1001111,																						//
	fcomp  = 0b1010011,				//add,sub,mul,div,sqrt,sgnj,sgnjn,sgnjx,min,max,cvt,mv,eq,lt,le,class	//

};

enum plevel_t {
	MMODE = 0b11,
	HMODE = 0b10,
	SMODE = 0b01,
	UMODE = 0b00
};

#define USTATUS			0x000
#define UTVEC			0x005
#define UEPC			0x041
#define UCAUSE			0x042 
#define SCOUNTEREN		0x106
#define SATP			0x180
#define MSTATUS			0x300
#define MISA			0x301
#define MEDELEG			0x302
#define MIDELEG			0x303
#define MIE				0x304
#define MTVEC			0x305
#define MCOUNTEREN		0x306
#define MSCRATCH		0x340
#define MEPC			0x341
#define MCAUSE			0x342
#define MTVAL			0x343
#define MIP				0x344
#define PMPCFG0			0x3A0	
#define PMPADDR0		0x3B0 	
#define MVENDORID		0xF11 	
#define MARCHID			0xF12
#define MIMPID			0xF13
#define MHARTID			0xF14

//* Line 6: Add definitons for new Fcsr addresses
#define FFLAGS			0x001		//floating point accrued exceptions
#define FRM				0x002		//floating point dynamic rounding mode
#define FCSR			0x003		//floating point control and status register(frm + fflags)