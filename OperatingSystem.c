#include "OperatingSystem.h"
#include "OperatingSystemBase.h"
#include "MMU.h"
#include "Processor.h"
#include "Buses.h"
#include "Heap.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>

// Functions prototypes
void OperatingSystem_PrepareDaemons();
void OperatingSystem_PCBInitialization(int, int, int, int, int);
void OperatingSystem_MoveToTheREADYState(int);
void OperatingSystem_Dispatch(int);
void OperatingSystem_RestoreContext(int);
void OperatingSystem_SaveContext(int);
void OperatingSystem_TerminateProcess();
int OperatingSystem_LongTermScheduler();
void OperatingSystem_PreemptRunningProcess();
int OperatingSystem_CreateProcess(int);
int OperatingSystem_ObtainMainMemory(int, int);
int OperatingSystem_ShortTermScheduler();
int OperatingSystem_ExtractFromReadyToRun();
void OperatingSystem_HandleException();
void OperatingSystem_HandleSystemCall();

void OperatingSystem_PrintReadyToRunQueue();
void OperatingSystem_HandleClockInterrupt();
int OperatingSystem_ExtractFromBlocked();
int OperatingSystem_CheckQueues();
int OperatingSystem_CompareToContextSwitch(int);

int OperatingSystem_GetExecutingProcessID();

int Processor_GetRegisterB();

char * statesNames[5]={"NEW","READY","EXECUTING","BLOCKED","EXIT"};

// The process table
PCB processTable[PROCESSTABLEMAXSIZE];

// Address base for OS code in this version
int OS_address_base = PROCESSTABLEMAXSIZE * MAINMEMORYSECTIONSIZE;

// Identifier of the current executing process
int executingProcessID=NOPROCESS;

// Identifier of the System Idle Process
int sipID;

// Initial PID for assignation
int initialPID = PROCESSTABLEMAXSIZE-1;

// Begin indes for daemons in programList
int baseDaemonsInProgramList; 

// Array that contains the identifiers of the READY processes
heapItem readyToRunQueue[NUMBEROFQUEUES][PROCESSTABLEMAXSIZE];
int numberOfReadyToRunProcesses[NUMBEROFQUEUES]={0,0};
char * queueNames [NUMBEROFQUEUES]={"USER","DAEMONS"}; 

// Variable containing the number of not terminated user processes
int numberOfNotTerminatedUserProcesses=0;

// VAriable containing the number of clock interrupts
int numberOfClockInterrupts=0;

// In OperatingSystem.c Exercise 5-b of V2
// Heap with blocked processes sorted by when to wakeup
heapItem sleepingProcessesQueue[PROCESSTABLEMAXSIZE];
int numberOfSleepingProcesses=0;

// Initial set of tasks of the OS
void OperatingSystem_Initialize(int daemonsIndex) {
	
	int i, selectedProcess;
	FILE *programFile; // For load Operating System Code

	// Obtain the memory requirements of the program
	int processSize=OperatingSystem_ObtainProgramSize(&programFile, "OperatingSystemCode");

	// Load Operating System Code
	OperatingSystem_LoadProgram(programFile, OS_address_base, processSize);
	
	// Process table initialization (all entries are free)
	for (i=0; i<PROCESSTABLEMAXSIZE;i++){
		processTable[i].busy=0;
	}
	// Initialization of the interrupt vector table of the processor
	Processor_InitializeInterruptVectorTable(OS_address_base+2);
		
	// Include in program list  all system daemon processes
	OperatingSystem_PrepareDaemons(daemonsIndex);
	
	//Exercise 3-1
	
	ComputerSystem_FillInArrivalTimeQueue();
	OperatingSystem_PrintStatus();
	
	// Create all user processes from the information given in the command line
	OperatingSystem_LongTermScheduler();
	
	if (strcmp(programList[processTable[sipID].programListIndex]->executableName,"SystemIdleProcess")) {
		// Show red message "FATAL ERROR: Missing SIP program!\n"
		OperatingSystem_ShowTime(SHUTDOWN);
		ComputerSystem_DebugMessage(99,SHUTDOWN,"FATAL ERROR: Missing SIP program!\n");
		exit(1);		
	}
	
	//int numberOfCreatedProcesses = 0;
	//numberOfCreatedProcesses = OperatingSystem_LongTermScheduler();
	if (numberOfNotTerminatedUserProcesses == 0 && OperatingSystem_IsThereANewProgram() == EMPTYQUEUE) {
		OperatingSystem_ReadyToShutdown();
	}

	// At least, one user process has been created
	// Select the first process that is going to use the processor
	selectedProcess=OperatingSystem_ShortTermScheduler();

	// Assign the processor to the selected process
	OperatingSystem_Dispatch(selectedProcess);

	// Initial operation for Operating System
	Processor_SetPC(OS_address_base);
}

// Daemon processes are system processes, that is, they work together with the OS.
// The System Idle Process uses the CPU whenever a user process is able to use it
void OperatingSystem_PrepareDaemons(int programListDaemonsBase) {
  
	// Include a entry for SystemIdleProcess at 0 position
	programList[0]=(PROGRAMS_DATA *) malloc(sizeof(PROGRAMS_DATA));

	programList[0]->executableName="SystemIdleProcess";
	programList[0]->arrivalTime=0;
	programList[0]->type=DAEMONPROGRAM; // daemon program

	sipID=initialPID%PROCESSTABLEMAXSIZE; // first PID for sipID

	// Prepare aditionals daemons here
	// index for aditionals daemons program in programList
	baseDaemonsInProgramList=programListDaemonsBase;

}


// The LTS is responsible of the admission of new processes in the system.
// Initially, it creates a process from each program specified in the 
// 			command lineand daemons programs
int OperatingSystem_LongTermScheduler() {
  
	int PID, i,
		numberOfSuccessfullyCreatedProcesses=0;
		
	while(OperatingSystem_IsThereANewProgram() == YES) {
		i = Heap_poll(arrivalTimeQueue, QUEUE_ARRIVAL, &numberOfProgramsInArrivalTimeQueue);
		PID = OperatingSystem_CreateProcess(i);
		if (PID == NOFREEENTRY) {
			OperatingSystem_ShowTime(ERROR);
			ComputerSystem_DebugMessage(103, ERROR, programList[i] -> executableName);
		}
		else if (PID == PROGRAMDOESNOTEXIST) {
			OperatingSystem_ShowTime(ERROR);
			ComputerSystem_DebugMessage(104, ERROR, programList[i] -> executableName, "it does not exist");
		}
		else if (PID == PROGRAMNOTVALID) {
			OperatingSystem_ShowTime(ERROR);
			ComputerSystem_DebugMessage(104, ERROR, programList[i] -> executableName, "invalid priority or size");
		}
		else if (PID == TOOBIGPROCESS) {
			OperatingSystem_ShowTime(ERROR);
			ComputerSystem_DebugMessage(105, ERROR, programList[i] -> executableName);
		}
		else {
			numberOfSuccessfullyCreatedProcesses++;
			if (programList[i]->type==USERPROGRAM) {
				numberOfNotTerminatedUserProcesses++;
			}
			// Move process to the ready state
			OperatingSystem_MoveToTheREADYState(PID);
		}
	}
	if (numberOfSuccessfullyCreatedProcesses > 1) {
		OperatingSystem_PrintStatus();
	}
	// Return the number of succesfully created processes
	return numberOfSuccessfullyCreatedProcesses;
}


// This function creates a process from an executable program
int OperatingSystem_CreateProcess(int indexOfExecutableProgram) {
  
	int PID;
	int processSize;
	int loadingPhysicalAddress;
	int priority;
	FILE *programFile;
	PROGRAMS_DATA *executableProgram=programList[indexOfExecutableProgram];

	// Obtain a process ID
	PID=OperatingSystem_ObtainAnEntryInTheProcessTable();
	
	if (PID == NOFREEENTRY) {
		return PID; 
	}

	// Obtain the memory requirements of the program
	processSize=OperatingSystem_ObtainProgramSize(&programFile, executableProgram->executableName);	
	
	if (processSize == PROGRAMDOESNOTEXIST) {
		return processSize; 
	}
	if (processSize == PROGRAMNOTVALID) {
		return processSize; 
	}
	// Obtain the priority for the process
	priority=OperatingSystem_ObtainPriority(programFile);
	
	// Obtain enough memory space
	if (processSize > MAINMEMORYSECTIONSIZE) {
		return TOOBIGPROCESS;
	}
	else {
		loadingPhysicalAddress=OperatingSystem_ObtainMainMemory(processSize, PID);
	}

	// Load program in the allocated memory
	if (OperatingSystem_LoadProgram(programFile, loadingPhysicalAddress, processSize) == TOOBIGPROCESS) {
		return TOOBIGPROCESS;
	}
	
	// PCB initialization
	OperatingSystem_PCBInitialization(PID, loadingPhysicalAddress, processSize, priority, indexOfExecutableProgram);
	
	// Show message "Process [PID] created from program [executableName]\n"
	OperatingSystem_ShowTime(INIT);
	ComputerSystem_DebugMessage(70,INIT,PID,executableProgram->executableName);
	
	return PID;
}


// Main memory is assigned in chunks. All chunks are the same size. A process
// always obtains the chunk whose position in memory is equal to the processor identifier
int OperatingSystem_ObtainMainMemory(int processSize, int PID) {

 	if (processSize>MAINMEMORYSECTIONSIZE)
		return TOOBIGPROCESS;
	
 	return PID*MAINMEMORYSECTIONSIZE;
}


// Assign initial values to all fields inside the PCB
void OperatingSystem_PCBInitialization(int PID, int initialPhysicalAddress, int processSize, int priority, int processPLIndex) {

	processTable[PID].busy=1;
	processTable[PID].initialPhysicalAddress=initialPhysicalAddress;
	processTable[PID].processSize=processSize;
	processTable[PID].state=NEW;
	OperatingSystem_ShowTime(SYSPROC);
	ComputerSystem_DebugMessage(111, SYSPROC,PID ,programList[processTable[PID].programListIndex]-> executableName);
	processTable[PID].priority=priority;
	processTable[PID].programListIndex=processPLIndex;
	// Daemons run in protected mode and MMU use real address
	if (programList[processPLIndex]->type == DAEMONPROGRAM) {
		processTable[PID].copyOfPCRegister=initialPhysicalAddress;
		processTable[PID].copyOfPSWRegister= ((unsigned int) 1) << EXECUTION_MODE_BIT;
		processTable[PID].queueID = DAEMONSQUEUE;
	} 
	else {
		processTable[PID].copyOfPCRegister=0;
		processTable[PID].copyOfPSWRegister=0;
		processTable[PID].queueID = USERPROCESSQUEUE;
	}

}


// Move a process to the READY state: it will be inserted, depending on its priority, in
// a queue of identifiers of READY processes
// statesNames[previousState], statesNames[processTable[PID].state]
void OperatingSystem_MoveToTheREADYState(int PID) {
	if (Heap_add(PID, readyToRunQueue[processTable[PID].queueID],QUEUE_PRIORITY ,&numberOfReadyToRunProcesses[processTable[PID].queueID],PROCESSTABLEMAXSIZE)>=0) {
		OperatingSystem_ShowTime(SYSPROC);
		ComputerSystem_DebugMessage(110,SYSPROC,PID,programList[processTable[PID].programListIndex]->executableName, statesNames[processTable[PID].state],"READY");
		processTable[PID].state=READY;
	}
	//OperatingSystem_PrintReadyToRunQueue(); 
}


// The STS is responsible of deciding which process to execute when specific events occur.
// It uses processes priorities to make the decission. Given that the READY queue is ordered
// depending on processes priority, the STS just selects the process in front of the READY queue
int OperatingSystem_ShortTermScheduler() {
	
	int selectedProcess;

	selectedProcess=OperatingSystem_ExtractFromReadyToRun();
	
	return selectedProcess;
}


// Return PID of more priority process in the READY queue
int OperatingSystem_ExtractFromReadyToRun() {
  
	int selectedProcess=NOPROCESS;

	if ( numberOfReadyToRunProcesses[USERPROCESSQUEUE] > 0 ){
		selectedProcess=Heap_poll(readyToRunQueue[USERPROCESSQUEUE],QUEUE_PRIORITY ,&numberOfReadyToRunProcesses[USERPROCESSQUEUE]);
	}
	else if ( numberOfReadyToRunProcesses[DAEMONSQUEUE] > 0 ){
		selectedProcess=Heap_poll(readyToRunQueue[DAEMONSQUEUE],QUEUE_PRIORITY ,&numberOfReadyToRunProcesses[DAEMONSQUEUE]);
	}
	
	// Return most priority process or NOPROCESS if empty queue
	return selectedProcess; 
}


// Function that assigns the processor to a process
void OperatingSystem_Dispatch(int PID) {

	// The process identified by PID becomes the current executing process
	executingProcessID=PID;
	// Change the process' state
	OperatingSystem_ShowTime(SYSPROC);
	ComputerSystem_DebugMessage(110,SYSPROC,PID,programList[processTable[PID].programListIndex]->executableName, statesNames[processTable[PID].state],"EXECUTING");
	processTable[PID].state=EXECUTING;	
	// Modify hardware registers with appropriate values for the process identified by PID
	OperatingSystem_RestoreContext(PID);
}


// Modify hardware registers with appropriate values for the process identified by PID
void OperatingSystem_RestoreContext(int PID) {
  
	// New values for the CPU registers are obtained from the PCB
	Processor_CopyInSystemStack(MAINMEMORYSIZE-1,processTable[PID].copyOfPCRegister);
	Processor_CopyInSystemStack(MAINMEMORYSIZE-2,processTable[PID].copyOfPSWRegister);
	Processor_SetAccumulator(processTable[PID].copyOfAccumulator);
	
	// Same thing for the MMU registers
	MMU_SetBase(processTable[PID].initialPhysicalAddress);
	MMU_SetLimit(processTable[PID].processSize);
}


// Function invoked when the executing process leaves the CPU 
void OperatingSystem_PreemptRunningProcess() {

	// Save in the process' PCB essential values stored in hardware registers and the system stack
	OperatingSystem_SaveContext(executingProcessID);
	// Change the process' state
	OperatingSystem_MoveToTheREADYState(executingProcessID);
	// The processor is not assigned until the OS selects another process
	executingProcessID=NOPROCESS;
}


// Save in the process' PCB essential values stored in hardware registers and the system stack
void OperatingSystem_SaveContext(int PID) {
	
	// Load PC saved for interrupt manager
	processTable[PID].copyOfPCRegister=Processor_CopyFromSystemStack(MAINMEMORYSIZE-1);
	
	// Load PSW saved for interrupt manager
	processTable[PID].copyOfPSWRegister=Processor_CopyFromSystemStack(MAINMEMORYSIZE-2);
	
	processTable[PID].copyOfAccumulator=Processor_GetAccumulator();
	
}


// Exception management routine
void OperatingSystem_OldHandleException() {
  
	// Show message "Process [executingProcessID] has generated an exception and is terminating\n"
	OperatingSystem_ShowTime(SYSPROC);
	ComputerSystem_DebugMessage(71,SYSPROC,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName);
	
	OperatingSystem_TerminateProcess();
	OperatingSystem_PrintStatus();
}

// Exception management routine
void OperatingSystem_HandleException() {
	OperatingSystem_ShowTime(INTERRUPT);
	int typeOfInterrupt = Processor_GetRegisterB();
	switch (typeOfInterrupt) {
		case DIVISIONBYZERO:
			ComputerSystem_DebugMessage(140,INTERRUPT,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName, "division by zero");
			break;
		case INVALIDPROCESSORMODE:
			ComputerSystem_DebugMessage(140,INTERRUPT,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName, "invalid processor mode");
			break;
		case INVALIDADDRESS:
			ComputerSystem_DebugMessage(140,INTERRUPT,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName, "invalid address");			
			break;
		case INVALIDINSTRUCTION:
			ComputerSystem_DebugMessage(140,INTERRUPT,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName, "invalid instruction");			
			break;
	}
	OperatingSystem_TerminateProcess();
	OperatingSystem_PrintStatus();
}


// All tasks regarding the removal of the process
void OperatingSystem_TerminateProcess() {
  
	int selectedProcess;
	OperatingSystem_ShowTime(SYSPROC);
  	ComputerSystem_DebugMessage(110,SYSPROC,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName, statesNames[processTable[executingProcessID].state],"EXIT");
	processTable[executingProcessID].state=EXIT;
	
	if (programList[processTable[executingProcessID].programListIndex]->type==USERPROGRAM) 
		// One more user process that has terminated
		numberOfNotTerminatedUserProcesses--;
	
	if (numberOfNotTerminatedUserProcesses==0 && OperatingSystem_IsThereANewProgram() == EMPTYQUEUE) {
		if (executingProcessID==sipID) {
			// finishing sipID, change PC to address of OS HALT instruction
			OperatingSystem_TerminatingSIP();
			OperatingSystem_ShowTime(SHUTDOWN);
			ComputerSystem_DebugMessage(99,SHUTDOWN,"The system will shut down now...\n");
			return; // Don't dispatch any process
		}
		// Simulation must finish, telling sipID to finish
		OperatingSystem_ReadyToShutdown();
	}
	
	// Select the next process to execute (sipID if no more user processes)
	selectedProcess=OperatingSystem_ShortTermScheduler();

	// Assign the processor to that process
	OperatingSystem_Dispatch(selectedProcess);
}

// System call management routine
void OperatingSystem_HandleSystemCall() {
  
	int systemCallID, queueID;

	// Register A contains the identifier of the issued system call
	systemCallID=Processor_GetRegisterA();
	
	switch (systemCallID) {
		case SYSCALL_PRINTEXECPID:
			// Show message: "Process [executingProcessID] has the processor assigned\n"
			OperatingSystem_ShowTime(SYSPROC);
			ComputerSystem_DebugMessage(72,SYSPROC,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName);
			break;

		case SYSCALL_END:
			// Show message: "Process [executingProcessID] has requested to terminate\n"
			OperatingSystem_ShowTime(SYSPROC);
			ComputerSystem_DebugMessage(73,SYSPROC,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName);
			OperatingSystem_TerminateProcess();
			OperatingSystem_PrintStatus();
			break;
		case SYSCALL_YIELD:
			queueID = processTable[executingProcessID].queueID;
			if (numberOfReadyToRunProcesses[queueID] > 0) {
				int candidate = readyToRunQueue[queueID][0].info;
				if (processTable[candidate].priority ==  processTable[executingProcessID].priority) {
					int selectedProcess = OperatingSystem_ExtractFromReadyToRun(queueID);
					OperatingSystem_ShowTime(SHORTTERMSCHEDULE);
					ComputerSystem_DebugMessage(115,SHORTTERMSCHEDULE,executingProcessID, programList[processTable[executingProcessID].programListIndex]->executableName, readyToRunQueue[queueID][0], programList[processTable[selectedProcess].programListIndex]->executableName);
					OperatingSystem_PreemptRunningProcess();
					OperatingSystem_Dispatch(selectedProcess);
					OperatingSystem_PrintStatus();
				}
			}		
			break;
		case SYSCALL_SLEEP:	
			OperatingSystem_ShowTime(SYSPROC);
			OperatingSystem_SaveContext(executingProcessID);
			processTable[executingProcessID].whenToWakeUp = abs(Processor_GetAccumulator())+ numberOfClockInterrupts + 1;
			if (Heap_add(executingProcessID, sleepingProcessesQueue,QUEUE_WAKEUP,&numberOfSleepingProcesses,PROCESSTABLEMAXSIZE)>=0) {
				ComputerSystem_DebugMessage(110,SYSPROC,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName, statesNames[processTable[executingProcessID].state],"BLOCKED");
				processTable[executingProcessID].state=BLOCKED;
			} 
			int selectedProcess = OperatingSystem_ShortTermScheduler();
			OperatingSystem_Dispatch(selectedProcess);
			OperatingSystem_PrintStatus();
			break;
		default:
			OperatingSystem_ShowTime(INTERRUPT);
			ComputerSystem_DebugMessage(141,INTERRUPT,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName, systemCallID);
			OperatingSystem_TerminateProcess();
			OperatingSystem_PrintStatus();
	}
}
	
//	Implement interrupt logic calling appropriate interrupt handle
void OperatingSystem_InterruptLogic(int entryPoint){
	switch (entryPoint){
		case SYSCALL_BIT: // SYSCALL_BIT=2
			OperatingSystem_HandleSystemCall();
			break;
		case EXCEPTION_BIT: // EXCEPTION_BIT=6
			OperatingSystem_HandleException();
			break;
		case CLOCKINT_BIT: // CLOCKINT_BIT = 9
			OperatingSystem_HandleClockInterrupt();
			break;
	}

}

// Prints the information about the procces that are about to run
//void OperatingSystem_PrintReadyToRunQueueOld(){
	//ComputerSystem_DebugMessage(106, SHORTTERMSCHEDULE);
	//int i;
	//for (i = 0; i < numberOfReadyToRunProcesses; i++) {
		//if (i == (numberOfReadyToRunProcesses-1)) {
			//ComputerSystem_DebugMessage(108, SHORTTERMSCHEDULE, readyToRunQueue[i],processTable[readyToRunQueue[i].info].priority);
		//}
		//else {
			//ComputerSystem_DebugMessage(107, SHORTTERMSCHEDULE, readyToRunQueue[i],processTable[readyToRunQueue[i].info].priority);
		//}
	//}
//}

// Prints the information about the procces that are about to run
void OperatingSystem_PrintReadyToRunQueue(){
	OperatingSystem_ShowTime(SHORTTERMSCHEDULE);
	ComputerSystem_DebugMessage(106, SHORTTERMSCHEDULE);
	int i;
	for (i = 0; i < numberOfReadyToRunProcesses[0]; i++) { //USERS
		ComputerSystem_DebugMessage(112, SHORTTERMSCHEDULE, queueNames[0]);
		if (i == (numberOfReadyToRunProcesses[0] - 1)) {
			OperatingSystem_ShowTime(SHORTTERMSCHEDULE);
			ComputerSystem_DebugMessage(108, SHORTTERMSCHEDULE, readyToRunQueue[0][i],processTable[readyToRunQueue[0][i].info].priority);
		}
		else {
			OperatingSystem_ShowTime(SHORTTERMSCHEDULE);
			ComputerSystem_DebugMessage(107, SHORTTERMSCHEDULE, readyToRunQueue[0][i],processTable[readyToRunQueue[0][i].info].priority);
		}
	}
	int j;
	for (j = 0; j < numberOfReadyToRunProcesses[1]; j++) { //DAEMONS
		ComputerSystem_DebugMessage(112, SHORTTERMSCHEDULE, queueNames[1]);
		if (j == (numberOfReadyToRunProcesses[1] - 1)) {
			OperatingSystem_ShowTime(SHORTTERMSCHEDULE);
			ComputerSystem_DebugMessage(108, SHORTTERMSCHEDULE, readyToRunQueue[1][j],processTable[readyToRunQueue[1][j].info].priority);
		}
		else {
			OperatingSystem_ShowTime(SHORTTERMSCHEDULE);
			ComputerSystem_DebugMessage(107, SHORTTERMSCHEDULE, readyToRunQueue[1][j],processTable[readyToRunQueue[1][j].info].priority);
		}
	}
}

void OperatingSystem_HandleClockInterrupt(){ 
	OperatingSystem_ShowTime(INTERRUPT);
	ComputerSystem_DebugMessage(120,INTERRUPT,numberOfClockInterrupts++);
	
	//6a
	
	int process = Heap_getFirst(sleepingProcessesQueue, numberOfSleepingProcesses);
	int processUnlocked = 0; //Activated when a process is unlocked
	while (processTable[process].whenToWakeUp == numberOfClockInterrupts) {
		OperatingSystem_ExtractFromBlocked(); //Unblocked
		OperatingSystem_MoveToTheREADYState(process); //Ready
		process=Heap_getFirst(sleepingProcessesQueue,numberOfSleepingProcesses); //Keep going through the elements in the queue
		processUnlocked = 1;
	}
	if (OperatingSystem_LongTermScheduler() > 0) {
		processUnlocked = 1;
	}
	
	//4-3
	if (numberOfNotTerminatedUserProcesses < 1 && OperatingSystem_IsThereANewProgram() == EMPTYQUEUE) {
		OperatingSystem_ReadyToShutdown();
	}
	
	if (processUnlocked) { //A process has been unlocked
		OperatingSystem_PrintStatus();
		int queueToCheck = OperatingSystem_CheckQueues(); //If it's -1 after the if and else if it implies that both queues are empty
		if (queueToCheck != -1) {
			int changeOfQueue = Heap_getFirst(readyToRunQueue[queueToCheck],numberOfReadyToRunProcesses[queueToCheck]); //We change the queue we operate with
			int comparison = OperatingSystem_CompareToContextSwitch(changeOfQueue);
			if (comparison != -1) {//Checks the priorities of the queue
				OperatingSystem_ShowTime(SHORTTERMSCHEDULE);
				ComputerSystem_DebugMessage(121,SHORTTERMSCHEDULE,executingProcessID, programList[processTable[executingProcessID].programListIndex]->executableName, readyToRunQueue[queueToCheck][0], programList[processTable[changeOfQueue].programListIndex]->executableName);
				OperatingSystem_PreemptRunningProcess();
				OperatingSystem_Dispatch(OperatingSystem_ShortTermScheduler());
				OperatingSystem_PrintStatus();  
			} 
		}
	}	
}
int OperatingSystem_CompareToContextSwitch(int process2) {
	if (processTable[executingProcessID].queueID > processTable[process2].queueID) {
		return 0;
	} else if (processTable[executingProcessID].queueID == processTable[process2].queueID) {
		if (processTable[process2].priority < processTable[executingProcessID].priority) {
			return 0;
		}
	}
	return -1;
}
	
int OperatingSystem_CheckQueues() {
	if (numberOfReadyToRunProcesses[USERPROCESSQUEUE] > 0) {
		return USERPROCESSQUEUE;
	} else if (numberOfReadyToRunProcesses[DAEMONSQUEUE] > 0) {
		return DAEMONSQUEUE;
	}
		return -1;
}
	
int OperatingSystem_ExtractFromBlocked() {
	int selectedProcess=NOPROCESS;
	selectedProcess=Heap_poll(sleepingProcessesQueue,QUEUE_WAKEUP ,&numberOfSleepingProcesses);
	return selectedProcess; 
}

int OperatingSystem_GetExecutingProcessID() {
	return executingProcessID;
}




