#ifndef DATASTRUCTURES_H 
#define DATASTRUCTURES_H

#include <stdio.h>
#include <stdlib.h>
#include "gll.h"


typedef struct PCB ProcessControlBlock;

typedef struct {
    int valid;
    long PPN;
    int time;
} PTE;

typedef struct {
    int valid;
    long PPN;
    int time;
} TLBE;

typedef struct {
    long number;
    int time;
    PTE * pte;
} Page;

typedef struct {
    PTE * pte_t;
    int rvalue;
    uint64_t interrupt;
    long PPN;
    int track;
} diskq;

struct PCB
{
    char* name;
    uint64_t start_time;
    char* memoryFilename;
    FILE* memoryFile;
    gll_t* memReq;
    int numOfIns;
    
    int hitCount;
    int missCount;

    int TLBhitcount;
    int TLBmissCount;

    uint64_t fracLeft;

    uint64_t OS_time;
    uint64_t user_time;
    uint64_t blockedTIme;
    
    int diskInt;
    int contSwitch;
    int memcount;

    int numOfMEM;
    PTE* PTBR;
} PCBNode;


struct NextMem
{
    char* type;
    char* address;
};


typedef struct Stats{
    char* processName;
    int hitCount;
    int missCount;
    uint64_t duration;
    int numberOfPgFaults;
    int numberOfTLBmiss;
    int numberOfTLBhit;
    uint64_t blockedStateDuration;
    int diskInt;

    uint64_t OS_time;
    uint64_t user_time;
    uint64_t blockedTIme;
    
    int contSwitch;
    

} stats;

typedef struct TotalStats{
    uint64_t start_time;
    uint64_t end_time;
    gll_t* perProcessStats;
    int numberOfContextSwitch;
    int numberOfDiskInt;
    int totalPgFaults;
    int totalTLBmiss;
    uint64_t totalBlockedStateDuration;
    uint64_t OSModetime;
    uint64_t userModeTime;
    gll_t* executionOrder;

} totalstats;

typedef struct SystemParameters{
    uint64_t non_mem_inst_length;
    int virtual_addr_size_in_bits;
    uint64_t contextSwitchTime;

    uint64_t TLB_latency;
    uint64_t DRAM_latency;
    uint64_t Swap_latency;
    uint64_t Page_fault_trap_handling_time;
    uint64_t Swap_interrupt_handling_time; 

    uint64_t quantum;

    int DRAM_size_in_MB;
    int TLB_size_in_entries; 
    int P_in_bits;

    char* TLB_replacement_policy;
    char* TLB_type;

    double Frac_mem_inst;
    int Num_pagetable_levels;
    int N1_in_bits;
    int N2_in_bits;
    int N3_in_bits;
    char* Page_replacement_policy;
    int Num_procs;
} systemParameters;

#endif