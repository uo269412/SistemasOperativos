#ifndef PROCESSOR_H
#define PROCESSOR_H

#include "MainMemory.h"
#include "ProcessorBase.h"

#define INTERRUPTTYPES 10
#define CPU_SUCCESS 1
#define CPU_FAIL 0
#define MULTIPLE_EXCEPTIONS

// Enumerated type that connects bit positions in the PSW register with
// processor events and status
enum PSW_BITS {POWEROFF_BIT=0, ZERO_BIT=1, NEGATIVE_BIT=2, OVERFLOW_BIT=3, EXECUTION_MODE_BIT=7,  INTERRUPT_MASKED_BIT=15};
enum EXCEPTIONS {DIVISIONBYZERO = 0, INVALIDPROCESSORMODE = 1, INVALIDADDRESS = 2, INVALIDINSTRUCTION = 3};

// Enumerated type that connects bit positions in the interruptLines with
// interrupt types 
enum INT_BITS {SYSCALL_BIT=2, EXCEPTION_BIT=6, CLOCKINT_BIT = 9};

// Functions prototypes
void Processor_InitializeInterruptVectorTable();
void Processor_InstructionCycleLoop();
void Processor_RaiseInterrupt(const unsigned int);

char * Processor_ShowPSW();
int Processor_GetCTRL();
void Processor_SetCTRL(int);

#endif
