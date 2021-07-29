#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include "fileIO.h"
#include "dataStructures.h"


int debug = 0;

char* traceFileName;
FILE* traceptr;

char* outputFileName;

struct SystemParameters* sysParam;

gll_t *processList, *readyProcess, *runningProcess, *blockedProcess, *diskQueue;

struct TotalStats resultStats;

uint64_t current_time, nextDiskReadTime, nextQuanta, nextDiskInt;

uint64_t OSTime = 0;
uint64_t userTime = 0;

int numberContextSwitch = 0;

uint64_t contextSwitchTime = 1000;

uint64_t quantum = 10000;

/*Helper print functions*/
void printPCB(void* p);
void printStats(void* p);
/* Prints the order of execution of the processes */
void printExecOrder(void* c);

void init();
void finishAll();

/*Simulates the process simulator mechanism*/
void simulate();
/*Simulates the running of one process until it has to stop. Stops when there is a timer interrupt(1), disk interrupt(4), page fault(0), process finishes(-1) and if no running process to run(2). returns why it stopped (return values in braces)*/
int processSimulator();
/*Reads one instruction/address from the trace file. Has to check TLB/page table. Has to call page replacement or report page fault(-1). returns 1 on success, 0 if trace ends, -1 if page fault*/
int readPage(struct PCB* p, uint64_t stopTime);
/* Moves page from disk to memory after a disk interrupt occurs. Should take care of multiple if happens at the same time */
void diskToMemory();
/* Schedules next ready process to run in a round robin fashion */
void schedulingRR(int pauseCause);
/* Process done. Update statistics and close files. */
void cleanUpProcess(struct PCB* p);

/*Statistics related functions*/
void statsinit();
void statsUpdate();
diskq* MMU(struct PCB* proc, char * VA);
int updateTLB(long PPN);
void diskSim();
int trackNum();
