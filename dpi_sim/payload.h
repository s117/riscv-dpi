/*****************************************************************************
#                       NORTH CAROLINA STATE UNIVERSITY
#                              AnyCore Project
# 
# AnyCore written by NCSU authors Rangeen Basu Roy Chowdhury and Eric Rotenberg.
# 
# AnyCore is based on FabScalar which was written by NCSU authors Niket K. 
# Choudhary, Brandon H. Dwiel, and Eric Rotenberg.
# 
# AnyCore also includes contributions by NCSU authors Elliott Forbes, Jayneel 
# Gandhi, Anil Kumar Kannepalli, Sungkwan Ku, Hiran Mayukh, Hashem Hashemi 
# Najaf-abadi, Sandeep Navada, Tanmay Shah, Ashlesha Shastri, Vinesh Srinivasan, 
# and Salil Wadhavkar.
# 
# AnyCore is distributed under the BSD license.
******************************************************************************/
#ifndef PAYLOAD_H
#define PAYLOAD_H

//#include "debug.h"
#include "decode.h"
#include "fu.h"

#define PAYLOAD_BUFFER_SIZE  2048

typedef
enum {
	SEL_IQ_INT,		// Select integer IQ.
	SEL_IQ_NONE,		// Skip IQ: mark completed right away.
	SEL_IQ_NONE_EXCEPTION	// Skip IQ with exception: mark completed and exception right away.
} sel_iq;

union union64_t {
  reg_t dw;
  sreg_t sdw;
	word_t w[2];
	sword_t sw[2];
	float f[2];
	double d;
};

typedef unsigned int debug_index_t;

typedef struct {

	////////////////////////
	// Set by Fetch Stage.
	////////////////////////

	insn_t inst;           // The simplescalar instruction.
	reg_t pc;		// The instruction's PC.

  uint64_t sequence;

  // The next instruction's PC. (I.e., the PC of
	// the instruction fetched after this one.)
	reg_t next_pc;	
  
  // If the instruction is a branch, this is its
	// index into the Branch Predictor's
	// branch queue.
	unsigned int pred_tag;       

  // If 'true', this instruction has a
	// corresponding instruction in the
	// functional simulator. This implies the
	// instruction is on the correct control-flow
	// path.
	bool good_instruction;	

  //Fetch exception information
  bool fetch_exception;
  bool fetch_exception_cause;

  // Index of corresponding instruction in the
	// functional simulator
	// (if good_instruction == 'true').
	// Having this index is useful for obtaining
	// oracle information about the instruction,
	// for various oracle modes of the simulator.
	debug_index_t db_index;	

	////////////////////////
	// Set by Decode Stage.
	////////////////////////

  // Operation flags: can be used for quickly
	// deciphering the type of instruction.
	unsigned int flags;          

	fu_type fu;           // Operation function unit type.
	cycle_t latency; // Operation latency (ignore: not currently used).
	bool split;			// Instruction is split into two.

  // If 'true': this instruction is the upper
	// half of a split instruction.
	// If 'false': this instruction is the lower
	// half of a split instruction.
	bool upper;			

  // If 'true', this instruction is a branch
	// that needs a checkpoint.
	bool checkpoint;		

  // Floating-point stores (S_S and S_D) are
	// implemented as split-stores because they
	// use both int and fp regs.
	bool split_store;		

	// Source register A.
  // If 'true', the instruction has a
	// first source register.
	bool A_valid;		
  // If 'true', the source register is an
	// integer register, else it is a
	// floating-point register.
	bool A_int;			
  // The logical register specifier of the
	// source register.
	unsigned int A_log_reg;	

	// Source register B.
  // If 'true', the instruction has a
	// second source register.
	bool B_valid;		
  // If 'true', the source register is an
	// integer register, else it is a
	// floating-point register.
	bool B_int;			
  // The logical register specifier of the
	// source register.
	unsigned int B_log_reg;	

	// Destination register C.
  // If 'true', the instruction has a
	// destination register.
	bool C_valid;		
  // If 'true', the destination register is an
	// integer register, else it is a
	// floating-point register.
	bool C_int;			
  // The logical register specifier of the
	// destination register.
	unsigned int C_log_reg;	

	// Source register D.
  // If 'true', the instruction has a
	// second source register.
	bool D_valid;		
  // If 'true', the source register is an
	// integer register, else it is a
	// floating-point register.
	bool D_int;			
  // The logical register specifier of the
	// source register.
	unsigned int D_log_reg;	

	// IQ selection.
  // The value of this enumerated type indicates
	// whether to place the instruction in the
	// integer issue queue, floating-point issue
	// queue, or neither issue queue.
	// (The 'sel_iq' enumerated type is also
	// defined in this file.)
	sel_iq iq;			

  // CSR address
  uint64_t CSR_addr;

	// Details about loads and stores.
	unsigned int size;		// Size of load or store (1, 2, 4, or 8 bytes).
	bool is_signed;		// If 'true', the loaded value is signed,
	// else it is unsigned.
	bool left;			// LWL or SWL instruction.
	bool right;			// LWR or SWR instruction.

	////////////////////////
	// Set by Rename Stage.
	////////////////////////

	// Physical registers.
	unsigned int A_phys_reg;	// If there exists a first source register (A),
	// this is the physical register specifier to
	// which it is renamed.
	unsigned int B_phys_reg;	// If there exists a second source register (B),
	// this is the physical register specifier to
	// which it is renamed.
	unsigned int C_phys_reg;	// If there exists a destination register (C),
	// this is the physical register specifier to
	// which it is renamed.
	unsigned int D_phys_reg;	// If there exists a destination register (C),
	// this is the physical register specifier to
	// which it is renamed.
  
	// Branch ID, for checkpointed branches only.
	unsigned int branch_ID;	// When a checkpoint is created for a branch,
	// this is the branch's ID (its bit position
	// in the Global Branch Mask).

	////////////////////////
	// Set by Dispatch Stage.
	////////////////////////

	unsigned int AL_index_int;	// Index into integer Active List.
	unsigned int AL_index_fp;	// Index into floating-point Active List.
	unsigned int LQ_index;	// Indices into LSU. Only used by loads, stores, and branches.
	bool LQ_phase;
	unsigned int SQ_index;
	bool SQ_phase;

	unsigned int lane_id;	// Execution lane chosen for the instruction.

	////////////////////////
	// Set by Reg. Read Stage.
	////////////////////////

	// Source values.
	union64_t A_value;		// If there exists a first source register (A),
	// this is its value. To reference the value as
	// an unsigned long long, use "A_value.dw".
	union64_t B_value;		// If there exists a second source register (B),
	// this is its value. To reference the value as
	// an unsigned long long, use "B_value.dw".

  // FMAC  RS3
	union64_t D_value;		

	////////////////////////
	// Set by Execute Stage.
	////////////////////////

	// Load/store address calculated by AGEN unit.
	reg_t addr;

	// Resolved branch target. (c_next_pc: computed next program counter)
	reg_t c_next_pc;

	// Destination value.
  // If there exists a destination register (C),
	// this is its value. To reference the value as
	// a 64 bit unsigned integer, use "C_value.dw".
	union64_t C_value;		
  
  // CSR old value - Needed for atomicity check
  uint64_t CSR_old_value;

  // CSR new value
  uint64_t CSR_new_value;


} payload_t;

//Forward declaring dpisim_t class as pointer is passed to the dump function
class dpisim_t;

class payload {
public:
	////////////////////////////////////////////////////////////////////////
	//
	// The new 721sim explicitly models all processor queues and
	// pipeline registers so that it is structurally the same as
	// a real pipeline. To maintain good simulation efficiency,
	// however, the simulator holds all "payload" information about
	// an instruction in a centralized data structure and only
	// pointers (indices) into this data structure are actually
	// moved through the pipeline. It is not a real hardware
	// structure but each entry collectively represents an instruction's
	// payload bits distributed throughout the pipeline.
	//
	// Each instruction is allocated two consecutive entries,
	// even and odd, in case the instruction is split into two.
	//
	////////////////////////////////////////////////////////////////////////
	payload_t    buf[PAYLOAD_BUFFER_SIZE];
	unsigned int head;
	unsigned int tail;
	int          length;

	payload();		// constructor
	unsigned int push();
	void pop();
	void clear();
	void split(unsigned int index);
	void map_to_actual(dpisim_t* proc,unsigned int index, unsigned int Tid);
	void rollback(unsigned int index);
  void dump(dpisim_t* proc,unsigned int index, FILE* file=stderr);
};

#endif //PAYLOAD_H
