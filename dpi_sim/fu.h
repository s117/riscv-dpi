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
#ifndef FU_H
#define FU_H

typedef enum {
	FU_BR,		// function unit for branches
	FU_LS,		// function unit for integer loads/stores
	FU_ALU_S,	// function unit for simple integer ALU operations
	FU_ALU_C,	// function unit for complex integer ALU operations
	FU_LS_FP,	// function unit for floating-point loads/stores
	FU_ALU_FP,	// function unit for floating-point ALU operations
	FU_MTF,		// function unit for move-to/move-from floating-point registers
	NUMBER_FU_TYPES
} fu_type;

#endif //FU_H
