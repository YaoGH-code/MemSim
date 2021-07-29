#include "simulator.h"
#include <string.h>
#include <math.h>

int startingPage = 1;

Page *DREM;
TLBE *TLB;
int DREM_ulti = 0;
int TLB_ulti = 0;
uint64_t DREM_time=0;
uint64_t TLB_time=0;
uint64_t diskInt=0;

void init()
{
    current_time = 0;
    nextQuanta = current_time + quantum;
    readyProcess = gll_init();
    runningProcess= gll_init();
    blockedProcess = gll_init();
    diskQueue = gll_init();

    processList = gll_init();
    traceptr = openTrace(traceFileName);

    sysParam = readSysParam(traceptr);

    //read traces from trace file and put them in the processList
    struct PCB* temp = readNextTrace(traceptr);

    if(temp == NULL)
    {
        printf("No data in file. Exit.\n");
        exit(1);
    }

    while(temp != NULL)
    {
        gll_pushBack(processList, temp);
        temp = readNextTrace(traceptr);
    }

    //transfer ready processes from processList to readyProcess list
    temp = gll_first(processList);
    
    while((temp!= NULL) && ( temp->start_time <= current_time))
    {
        struct NextMem* tempAddr;
        temp->memoryFile = openTrace(temp->memoryFilename);
        temp->numOfIns = readNumIns(temp->memoryFile);
        tempAddr = readNextMem(temp->memoryFile);
        while(tempAddr!= NULL)
        {
            gll_pushBack(temp->memReq, tempAddr);

            //count number of MEM
            if(strcmp(tempAddr->type, "MEM") == 0){
                temp->numOfMEM += 1;
            }

            tempAddr = readNextMem(temp->memoryFile);
            
        }
        gll_pushBack(readyProcess, temp);
        gll_pop(processList);

        temp = gll_first(processList);
    }

    //page table
    PTE* PTBR;
    int i;  
    struct PCB* procinReady = gll_first(readyProcess);
    if (procinReady->numOfMEM != 0){
        PTBR = (PTE*) malloc( (int) pow(2,sysParam->N1_in_bits) * sizeof(PTE));
        procinReady->PTBR = PTBR;
    }

    for (i=0; i<processList->size; i++){
        struct PCB* procinProc = (struct PCB*) gll_get(processList, i );
        PTBR = (PTE*) malloc( (int) pow(2,sysParam->N1_in_bits) * sizeof(PTE) );
        procinProc->PTBR = PTBR;
    }

    
    
    //init DREM
    int x = (sysParam->DRAM_size_in_MB)*1024*1024/pow(2,sysParam->P_in_bits);
    DREM = (Page*) malloc( x * sizeof(Page) );

    //init TLB
    TLB = (TLBE*) malloc( sysParam->TLB_size_in_entries * sizeof(TLBE));
    
}

void finishAll()
{
    if((gll_first(readyProcess)!= NULL) || (gll_first(runningProcess)!= NULL) || (gll_first(blockedProcess)!= NULL) || (gll_first(processList)!= NULL))
    {
        printf("Something is still pending\n");
    }
    gll_destroy(readyProcess);
    gll_destroy(runningProcess);
    gll_destroy(blockedProcess);
    gll_destroy(processList);
    gll_destroy(diskQueue);

//TODO: Anything else you want to destroy

    closeTrace(traceptr);
}

void statsinit()
{
    // statsList = gll_init();
    resultStats.perProcessStats = gll_init();
    resultStats.executionOrder = gll_init();
    resultStats.start_time = current_time;
    
}

void statsUpdate()
{
    resultStats.OSModetime = OSTime;
    resultStats.userModeTime  = userTime;   
    resultStats.numberOfContextSwitch = numberContextSwitch;
    resultStats.end_time = current_time;
}

/*
    update the TLB
*/
int updateTLB(long PPN){
    int flag=0;
    int i;

    // TLB hit
    for (i=0; i<sysParam->TLB_size_in_entries; i++){
        if ( (TLB+i)->PPN == PPN ){
            (TLB+i)->time=TLB_time;
            TLB_time++;
            flag=1;
            //printf("   [LOG] updateTLB: found in TLB: %d, PPN, %ld\n",i, PPN);
            return (-2);
            break;
        }
    }

    //update the TLB
    if (flag == 0){

        // Adding new entry
        if ( TLB_ulti < sysParam->TLB_size_in_entries ){
            (TLB+TLB_ulti)->time=TLB_time;
            (TLB+TLB_ulti)->PPN= PPN;
            (TLB+TLB_ulti)->valid=0;
            TLB_time++;
            TLB_ulti++;
            //printf("   [LOG] updateTLB: new in TLB: %d, PPN, %ld\n",TLB_ulti-1, PPN);
        } else {

            // Run LRU
            int i;

            TLBE* small=TLB;
            for (i=0; i<sysParam->TLB_size_in_entries; i++){
                if ( (TLB+i)->time < small->time){
                    small = TLB+i;
                }
            }
            //printf("   [LOG] updateTLB: replace in TLB: %ld, PPN, %ld\n",small->PPN, PPN);

            small->PPN=PPN;
            small->time=TLB_time;
            small->valid=0;
            TLB_time++;
            
        }
    }
    return (0);
}

/*
    MMU
*/
diskq* MMU(struct PCB* proc, char * VA)
{
    diskq* trans = (diskq *) malloc(sizeof(diskq));

    
    if (sysParam->Num_pagetable_levels == 2){ // 2 level page table
        char c1[sysParam->N1_in_bits/4];
        char c2[sysParam->N2_in_bits/4];

        memcpy(c1, VA+2, (sysParam->N1_in_bits/4));
        c1[sysParam->N1_in_bits/4]='\0';
        memcpy(c2, VA+2+(sysParam->N1_in_bits/4), (sysParam->N2_in_bits/4));
        c2[sysParam->N2_in_bits/4]='\0';
        
        int i1 = strtol( c1, NULL, 16 );
        int i2 = strtol( c2, NULL, 16 );

        PTE * l1 = proc->PTBR+i1;
        PTE * l2;

        // creating level 2
        if ( l1->valid ==0 && l1->PPN ==0 ){
            PTE* PTBA2 = (PTE*) malloc( (int) pow(2,sysParam->N2_in_bits) * sizeof(PTE) );
            l1->PPN = (long) PTBA2;
            (PTBA2+i2)->PPN = startingPage;
            startingPage = startingPage+1;
            proc->missCount++;
            proc->TLBmissCount++;

            trans->pte_t= (PTBA2+i2);
            trans->rvalue=-1;
            trans->PPN = (PTBA2+i2)->PPN;

            updateTLB(trans->PPN);
            return(trans);

        } else {
            l2 = ((PTE*) (l1->PPN))+i2;
            if (l2->valid ==0 && l2->PPN == 0){ //page fault page not assigned
                l2->PPN = startingPage;
                l2->valid=0;
                startingPage = startingPage+1;
                
                proc->missCount++;
                proc->TLBmissCount++;

                trans->pte_t= l2;
                trans->rvalue=-1;
                trans->PPN=l2->PPN;

                updateTLB(trans->PPN);

                return(trans);
                
            } else {
                // page page fault because valid bit is 0
                if (l2->valid ==0 && l2->PPN != 0){
                    trans->pte_t= l2;
                    trans->rvalue=-1;
                    trans->PPN=l2->PPN;

                    if ( updateTLB(trans->PPN) == -2 ){
                            proc->TLBhitcount++;
                            trans->rvalue=-4;
                    }else{
                        proc->TLBmissCount++;
                    }

                    proc->missCount++;

                    return(trans);
                    
                }
                if (l2->valid ==1 && l2->PPN != 0){ // hit
                    trans->pte_t= l2;
                    trans->rvalue=-3;
                    int i;

                    // check DREM
                    for ( i=0; i<DREM_ulti; i++){
                        if ((DREM + i)->number == l1->PPN ){
                            (DREM + i)->time = DREM_time;
                            DREM_time++;
                            break;
                        }
                    }
                    trans->PPN=l2->PPN;

                    //update TLB
                    if ( updateTLB(trans->PPN) == -2 ){
                            trans->rvalue=-2;
                            proc->TLBhitcount++;
                    }else{
                        proc->TLBmissCount++;
                    }

                    proc->hitCount++;

                    return(trans);
                }
            }
        }

    } else if (sysParam->Num_pagetable_levels == 3){ // 3 level page table

        char c1[sysParam->N1_in_bits/4];
        char c2[sysParam->N2_in_bits/4];
        char c3[sysParam->N3_in_bits/4];
        int i1, i2, i3;
        PTE *l1, *l2, *l3;
        

        memcpy(c1, VA+2, (sysParam->N1_in_bits/4));
        c1[sysParam->N1_in_bits/4]='\0';
        memcpy(c2, VA+2+(sysParam->N1_in_bits/4), (sysParam->N2_in_bits/4));
        c2[sysParam->N2_in_bits/4]='\0';
        memcpy(c3, VA+2+(sysParam->N1_in_bits/4)+(sysParam->N2_in_bits/4), (sysParam->N3_in_bits/4));
        c3[sysParam->N3_in_bits/4]='\0';

        i1 = strtol( c1, NULL, 16 );
        i2 = strtol( c2, NULL, 16 );
        i3 = strtol( c3, NULL, 16 );
        

        diskq *trans = (diskq *) malloc(sizeof(diskq));
        
        l1 = (proc->PTBR)+i1;
       
        if ( l1->valid ==0 && l1->PPN ==0 ){
            // no l2 and l3 page table and page mapped from this entry yet
            //page fault
            PTE* PTBA2 = (PTE*) malloc( (int) pow(2,sysParam->N2_in_bits) * sizeof(PTE) );
            l1->PPN = (long) PTBA2;
            PTE* PTBA3 = (PTE*) malloc( (int) pow(2,sysParam->N3_in_bits) * sizeof(PTE) );
            (PTBA2+i2)->PPN = (long) PTBA3;
            (PTBA3+i3)->PPN = startingPage;
            (PTBA3+i3)->valid = 0;
            startingPage = startingPage+1;
            
            proc->missCount++;
            proc->TLBmissCount++;

            trans->pte_t= (PTBA3+i3);
            trans->rvalue=-1;
            trans->PPN=(PTBA3+i3)->PPN;

            updateTLB(trans->PPN);
            
            return(trans);
            
        } else {
            l2 = ((PTE*) (l1->PPN))+i2;
            if ( l2->valid ==0 && l2->PPN ==0 ){ //creating l3 page table
                //page fault
                PTE* PTBA3 = (PTE*) malloc( (int) pow(2,sysParam->N3_in_bits) * sizeof(PTE) );
                (l2)->PPN = (long) PTBA3;
                (PTBA3+i3)->PPN = startingPage;
                (PTBA3+i3)->valid = 0;
                startingPage = startingPage+1;
                proc->missCount++;
                proc->TLBmissCount++;

                trans->pte_t= (PTBA3+i3);
                trans->rvalue=-1;
                trans->PPN=(PTBA3+i3)->PPN;

                updateTLB(trans->PPN);

                return(trans);
                
            } else {
                //page fault
                l3 = ((PTE*) (l2->PPN))+i3;
                if ( l3->valid ==0 && l3->PPN ==0 ){//page not assigned
                    l3->PPN = startingPage;
                    l3->valid=0;
                    startingPage = startingPage+1;
                    
                    proc->missCount++;
                    proc->TLBmissCount++;

                    trans->pte_t= (l3);
                    trans->rvalue=-1;
                    trans->PPN=l3->PPN;

                    updateTLB(trans->PPN);

                    return(trans);
                    
                } else { // page is not valid
                    if (l3->valid ==0 && l3->PPN != 0){
                        trans->pte_t= (l3);
                        trans->rvalue=-1;
                        trans->PPN=l3->PPN;

                        if ( updateTLB(trans->PPN) == -2 ){
                            proc->TLBhitcount++;
                            trans->rvalue=-4;
                        }else{
                            proc->TLBmissCount++;
                        }

                        proc->missCount++;
                        return(trans);
                        
                    }
                    if (l3->valid ==1 && l3->PPN != 0){ // hit
                        trans->pte_t= (l3);
                        trans->rvalue=-3; 
                        int i;

                        for ( i=0; i<DREM_ulti; i++){
                            if ((DREM + i)->number == l3->PPN ){
                                (DREM + i)->time = DREM_time;
                                DREM_time=DREM_time+1;
                                break;
                            }
                        }
                        trans->PPN=l3->PPN;

                        if ( updateTLB(trans->PPN) == -2 ){
                            trans->rvalue=-2;
                            proc->TLBhitcount++;
                        }else{
                            proc->TLBmissCount++;
                        }

                        proc->hitCount++;
                        return(trans);
                    
                    }
                }
            }
        }

    } else { // 1 level page table
        char c1[sysParam->N1_in_bits/4];
        memcpy(c1, VA+2, (sysParam->N1_in_bits/4));
        c1[sysParam->N1_in_bits/4]='\0';
        int i1 = strtol( c1, NULL, 16 );
        PTE * l1 = proc->PTBR+i1;
        diskq *trans = (diskq *) malloc(sizeof(diskq));

        if ( l1->valid ==0 && l1->PPN ==0 ){
            l1->PPN = startingPage;
            l1->valid=0;
            startingPage = startingPage+1;

            trans->pte_t= (l1);
            trans->rvalue=-1;
            trans->PPN=l1->PPN;
            return(trans);

        } else {
            if (l1->valid ==0 && l1->PPN != 0){
                trans->pte_t= (l1);
                trans->rvalue=-1;
                trans->PPN=l1->PPN;

                if ( updateTLB(trans->PPN) == -2 ){
                    proc->TLBhitcount++;
                    trans->rvalue=-4;
                }else{
                    proc->TLBmissCount++;
                }

                proc->missCount++;
                return(trans);
            }
            if (l1->valid ==1 && l1->PPN != 0){
                trans->pte_t= (l1);
                trans->rvalue=-3;
                int i; 

                for (i=0; i<DREM_ulti; i++){
                    if ((DREM + i)->number == l1->PPN ){
                        (DREM + i)->time = DREM_time;
                        DREM_time++;
                        break;
                    }
                }
                trans->PPN=l1->PPN;

                if ( updateTLB(trans->PPN) == -2 ){
                    trans->rvalue=-2;
                    proc->TLBhitcount++;
                }else{
                    proc->TLBmissCount++;
                }

                proc->hitCount++;
                return(trans);
            }
        }
    }
    return (0);
}

/*
    honors option: disk simulator
*/

void diskSim(){
    //running elevator algorithm
    //elevator algorithm: process pending disk requests in a order which let disk arm sweep from the outer track to inner track
    //which will avoid the case that outter track will be ignored if there is a constant access to inner tracks.
    //divide pages into tracks, 512 pages on a same track
    
    //This function is used to run the elevator algorithm which is change the disk request order in a order that from outter track to inner track
    int count = diskQueue->size;
    int i;
    int x;

    diskq* tempQiskArray = (diskq*) malloc( sizeof(diskq)*count );
    
    // sort the disk request to implement evelator algorithm
    for (i=0; i<count; i++){
        diskq* temp = (diskq*) gll_pop(diskQueue);
        (tempQiskArray+i)->PPN = temp->PPN;
        (tempQiskArray+i)->pte_t = temp->pte_t;
        (tempQiskArray+i)->rvalue = temp->rvalue;
        (tempQiskArray+i)->track = trackNum(temp->PPN);
        (tempQiskArray+i)->interrupt = temp->interrupt;
        
    }

    int e,j;
    char* temp = (char*) malloc(sizeof(diskq));

    for  (e=1; e<count; e++ ){
        for ( j=(count)-1; j>=e; j-- ){
            if ( (tempQiskArray+j)->track < (tempQiskArray+j-1)->track ){
                memcpy(temp, tempQiskArray+j, sizeof(diskq));
                memcpy(tempQiskArray+j, tempQiskArray+j-1, sizeof(diskq));
                memcpy(tempQiskArray+j-1, temp ,sizeof(diskq));
            }
        }
    
    }
    for (x=0; x<count; x++){
       gll_pushBack(diskQueue, (tempQiskArray+x)); 
    }
    

}



//returns 1 on success, 0 if trace ends, -1 if page fault
int readPage(struct PCB* p, uint64_t stopTime)
{   
    struct NextMem* addr = gll_first(p->memReq);
    uint64_t timeAvailable = stopTime - current_time;
    
    if(addr == NULL)
    {
        return 0;
    }
    if(debug == 1)
    {
        printf("Request::%s::%s::\n", addr->type, addr->address);
    }

    if(strcmp(addr->type, "NONMEM") == 0)
    {
        uint64_t timeNeeded = (p->fracLeft > 0)? p->fracLeft: sysParam->non_mem_inst_length;
 
        if(timeAvailable < timeNeeded)
        {
            current_time += timeAvailable;
            userTime += timeAvailable;
            p->user_time += timeAvailable;
            p->fracLeft = timeNeeded - timeAvailable;
        }
        else{
            gll_pop(p->memReq);
            current_time += timeNeeded; 
            userTime += timeNeeded;
            p->user_time += timeNeeded;
            p->fracLeft = 0;
        }

        if(gll_first(p->memReq) == NULL)
        {   
            return 0;
        }
        return 1;
    }
    else
    {
         //printf("   [LOG] readpage: %s, %s \n", addr->type, addr->address);

         

        //TODO: for MEM traces
        diskq* PPN = MMU(p, addr->address);
        //printf("process: %s, MMU rvalue:%d, MMU PPN:%ld\n", p->name ,PPN->rvalue,PPN->PPN);
      
        if ( PPN->rvalue == -1 || PPN->rvalue == -4  ){ //page fault -1 means only access to TLB for page fault
            gll_pop(p->memReq);
            current_time = current_time + sysParam->TLB_latency; 
            if (PPN->rvalue != -4){ // not found PTE in TLB, more access time to DREM
                current_time = current_time + sysParam->DRAM_latency; 
                userTime = userTime+ sysParam->DRAM_latency;
                p->user_time = p->user_time + sysParam->DRAM_latency;
            }
            
            userTime = userTime+ sysParam->TLB_latency;
            p->user_time = p->user_time + sysParam->TLB_latency;
            long nextIntrrupt = current_time + sysParam->Swap_latency;
            //printf("   [LOG] readpage: next intrrupt: %ld\n", nextIntrrupt);
            
            PPN->interrupt = nextIntrrupt;
            gll_pushBack(diskQueue, PPN);
            p->blockedTIme += sysParam->Swap_latency;
            p->diskInt++;

            
            if( diskQueue->size >= 4 ){
               
                diskSim();// disk simulator
            }
            diskInt++;
            

            return(-1);

        } else if ( PPN->rvalue == -2 ){
           //TLB page hit
            gll_pop(p->memReq);
            current_time = current_time + sysParam->TLB_latency; 
            userTime += sysParam->TLB_latency;
            p->user_time += sysParam->TLB_latency;
            return(1);

        } else if ( PPN->rvalue == -3 ){
            //MEM page hit
            gll_pop(p->memReq);
            current_time = current_time + sysParam->TLB_latency; 
            current_time = current_time + sysParam->DRAM_latency; 
            userTime += sysParam->TLB_latency + sysParam->DRAM_latency ;
            p->user_time += sysParam->TLB_latency + sysParam->DRAM_latency;
            return (1);
        }
    }
        
    return 0;
    
    
}

void schedulingRR(int pauseCause)
{
    //move first readyProcess to running
    gll_push(runningProcess, gll_first(readyProcess));
    gll_pop(readyProcess);

    if(gll_first(runningProcess) != NULL)
    {
        current_time = current_time + contextSwitchTime;
        OSTime += contextSwitchTime;
        numberContextSwitch++;
        struct PCB* temp = gll_first(runningProcess);
        temp->contSwitch++;
        //printf("   [LOG] RR: now running: %s \n", temp->name);
        gll_pushBack(resultStats.executionOrder, temp->name);
        //flush TLB when context switching
        free(TLB);
        TLB = (TLBE*) malloc( sysParam->TLB_size_in_entries * sizeof(TLBE));
        TLB_time=0;
        TLB_ulti=0;

    }
}

/*runs a process. returns 0 if page fault, 1 if quanta finishes, -1 if traceFile ends, 2 if no running process, 4 if disk Interrupt*/
int processSimulator()
{
    
    uint64_t stopTime = nextQuanta;
    int stopCondition = 1;
    
    if(gll_first(runningProcess)!=NULL)
    {
        //TODO
        //if(TODO: if there is a pending disk operation in the future)
        //{
            //TODO: stopTime = occurance of the first disk interrupt
        //    stopCondition = 4;
        //}

        if ( (diskQueue->size) != 0 ){
            if (stopTime >= ((diskq*) gll_first(diskQueue))->interrupt){
                stopTime = ((diskq*) gll_first(diskQueue))->interrupt;
                stopCondition = 4;
            }
        }

        //printf("   [LOG] processSimulator: processName: %s, currentTime: %llu, stopTime: %llu.\n\n", ((struct PCB* )(gll_first(runningProcess)))->name, current_time, stopTime );
        while(current_time < stopTime)
        {
            //printf("   [LOG] processSimulator2: processName: %s, currentTime: %llu, stopTime: %llu.\n\n", ((struct PCB* )(gll_first(runningProcess)))->name, current_time, stopTime );
            
            int read = readPage(gll_first(runningProcess), stopTime);
            if(debug == 1){
                printf("Read: %d\n", read);
                printf("Current Time %" PRIu64 ", Next Quanta Time %" PRIu64 " %" PRIu64 "\n",current_time, nextQuanta, stopTime);
            }
            if(read == 0)
            {
                return -1;
                break;
            }
            else if(read == -1) //page fault
            {
                if(gll_first(runningProcess) != NULL)
                {
                    gll_pushBack(blockedProcess, gll_first(runningProcess));
                    gll_pop(runningProcess);

                    return 0;
                }
                
            }
        }
        if(debug == 1)
        {
            printf("Stop condition found\n");
            printf("Current Time %" PRIu64 ", Next Quanta Time %" PRIu64 "\n",current_time, nextQuanta);
        }
        return stopCondition;
    }
    if(debug == 1)
    {
        printf("No running process found\n");
    }
    return 2;
    
}

void cleanUpProcess(struct PCB* p)
{
    //struct PCB* temp = gll_first(runningProcess);
    struct PCB* temp = p;
   //TODO: Adjust the amount of available memory as this process is finishing
    
    struct Stats* s = malloc(sizeof(stats));
    s->processName = temp->name;
    s->hitCount = temp->hitCount;
    s->missCount = temp->missCount;
    s->user_time = temp->user_time;
    s->OS_time = temp->OS_time;
    s->numberOfTLBhit = temp->TLBhitcount;
    s->numberOfTLBmiss = temp->TLBmissCount;
    s->blockedTIme = temp->blockedTIme;

    s->duration = current_time - temp->start_time;
    s->diskInt = temp -> diskInt;
    s->contSwitch = temp->contSwitch;
    
    gll_pushBack(resultStats.perProcessStats, s);
    
    gll_destroy(temp->memReq);
    closeTrace(temp->memoryFile);
    free(temp->PTBR);

}

void printPCB(void* v)
{
    struct PCB* p = v;
    if(p!=NULL){
        printf("%s, %" PRIu64 "\n", p->name, p->start_time);
    }
}

void printStats(void* v)
{
    struct Stats* s = v;
    if(s!=NULL){
        double hitRatio = s->hitCount / (1.0* s->hitCount + 1.0 * s->missCount);
        double TLBhitRatio = s->numberOfTLBhit / (1.0* s->numberOfTLBhit + 1.0 * s->numberOfTLBmiss);
        printf("\n\nProcess: %s: \nHit Ratio = %lf\t Page fault=%d\tPage fault ratio=%lf\tTLB Hit Ratio = %lf\tTLB miss Ratio = %lf\tTLB hit count = %d \tTLB miss count = %d \tdisk intrrupts = %d \ncontext switches = %d\tblocked time = %" PRIu64 " \tProcess completion time = %" PRIu64 
                "\nuser time = %" PRIu64 "\tOS time = %" PRIu64 "\n", s->processName, hitRatio,1-hitRatio, s->missCount, TLBhitRatio,1-TLBhitRatio,s->numberOfTLBhit,s->numberOfTLBmiss,s->diskInt,s->contSwitch,s->blockedTIme,  s->duration,  s->user_time, s->OS_time) ;
    }
}

void printExecOrder(void* v)
{
    char* c = v;
    if(c!=NULL){
        printf("%s\n", c) ;
    }
}

int trackNum(long PPN){ // use this to find simulated track number
    if ( PPN<=512 ){
        return 0;
    }
    if ( PPN<=1024 ){
        return 1;
    }
    if ( PPN<=1536 ){
        return 2;
    }
    if ( PPN<=2048 ){
        return 3;
    }
    if ( PPN<=2560 ){
        return 4;
    }
    if ( PPN<=3072 ){
        return 5;
    }

    return -1;

}

void diskToMemory()
{
    // TODO: Move requests from disk to memory
    // TODO: move appropriate blocked process to ready process
    if(debug == 1)
    {
        printf("Done diskToMemory\n");
    }
    
    
    if (DREM_ulti<1024){
        diskq* temp = gll_pop(diskQueue);
        
        temp->pte_t->valid = 1;
        
        (DREM+(DREM_ulti))->number = (temp->pte_t)->PPN;
        (DREM+(DREM_ulti))->time = DREM_time;
        (DREM+(DREM_ulti))->pte = temp->pte_t;


        
        DREM_ulti = DREM_ulti+1;
        DREM_time = DREM_time+1;

        struct PCB* blocked = gll_pop(blockedProcess);
        gll_pushBack(readyProcess, blocked);

        OSTime = OSTime+sysParam->Swap_interrupt_handling_time;
        OSTime = OSTime+sysParam->Page_fault_trap_handling_time;
        blocked->OS_time += sysParam->Swap_interrupt_handling_time;
        blocked->OS_time += sysParam->Page_fault_trap_handling_time;

        current_time = current_time+sysParam->Swap_interrupt_handling_time;
        current_time = current_time+sysParam->Page_fault_trap_handling_time;
        
        //printf("   ***[LOG] diskToMemory1: new DREM:%d, time:%llu, number:%ld, currenttime: %llu\n", DREM_ulti-1, DREM_time-1, (DREM+(DREM_ulti-1))->number, current_time);
    
    } else {
        Page *small = DREM;
        diskq* temp = gll_pop(diskQueue);
        int i;
        
        for ( i=0; i<DREM_ulti; i++){
            if ((DREM + i)->time < small->time){
                small = DREM + i;
            }
        }
        temp->pte_t->valid=1;

        small->number = temp->pte_t->PPN;
        small->time = DREM_time;
        small->pte->valid =0;
        small->pte = temp->pte_t;
        

        DREM_time = DREM_time+1;
        struct PCB* blocked = gll_pop(blockedProcess);
        gll_pushBack(readyProcess, blocked);

        OSTime = OSTime+sysParam->Swap_interrupt_handling_time;
        OSTime = OSTime+sysParam->Page_fault_trap_handling_time;

        blocked->OS_time += sysParam->Swap_interrupt_handling_time;
        blocked->OS_time += sysParam->Page_fault_trap_handling_time;

        current_time = current_time+sysParam->Swap_interrupt_handling_time;
        current_time = current_time+sysParam->Page_fault_trap_handling_time;
       
        
    }

    if ( gll_first(diskQueue)!=NULL && current_time >= ((diskq*) gll_first(diskQueue))->interrupt  ){
        diskToMemory();

    }
}


void simulate()
{   
    init();
    statsinit();

    //get the first ready process to running state
    struct PCB* temp = gll_first(readyProcess);
    gll_pushBack(runningProcess, temp);
    gll_pop(readyProcess);

    struct PCB* temp2 = gll_first(runningProcess);
    gll_pushBack(resultStats.executionOrder, temp2->name);

    while(1)
    {   
        //printf("   [LOG] simulate: processName: %s\n", temp2->name);
        int simPause = processSimulator();
        //printf("   [LOG] simulate: simPause: %d\n", simPause);
        //printf("   [LOG] simulate: current time: %llu, nextQ: %llu\n", current_time, nextQuanta );
        
           
        if(current_time == nextQuanta) //  used all quanta
        {
            nextQuanta = current_time + quantum;
        }
        
        //transfer ready processes from processList to readyProcess list
        struct PCB* temp = gll_first(processList);
        
        
        while((temp!= NULL) && ( temp->start_time <= current_time))
        {
            temp->memoryFile = openTrace(temp->memoryFilename);
            temp->numOfIns = readNumIns(temp->memoryFile);

            struct NextMem* tempAddr = readNextMem(temp->memoryFile);

	        while(tempAddr!= NULL)
            {
                gll_pushBack(temp->memReq, tempAddr);
                tempAddr = readNextMem(temp->memoryFile);
            }
            gll_pushBack(readyProcess, temp);
            gll_pop(processList);

            temp = gll_first(processList);
        }

        //move elements from disk to memory
        if(simPause == 4)
        {
            diskToMemory();
        }

        //This memory trace done
        if(simPause == -1)
        {
            //finish up this process
            cleanUpProcess(gll_first(runningProcess));
            gll_pop(runningProcess);
        }

        //move running process to readyProcess list
        int runningProcessNUll = 0;
        if(simPause == 1 || simPause == 4) // qunta finished or disk interuppt
        {
            if(gll_first(runningProcess) != NULL)
            {
                gll_pushBack(readyProcess, gll_first(runningProcess));
                gll_pop(runningProcess);
            }
            else{
                runningProcessNUll = 1;
            }
            if(simPause == 1)
            {
                nextQuanta = current_time + quantum;
            }
        }

        schedulingRR(simPause);

        //Nothing in running or ready. need to increase time to next timestamp when a process becomes ready.
        if((gll_first(runningProcess) == NULL) && (gll_first(readyProcess) == NULL))
        {
            if(debug == 1)
            {
                printf("\nNothing in running or ready\n");
            }
            if((gll_first(blockedProcess) == NULL) && (gll_first(processList) == NULL))
            {

                    if(debug == 1)
                    {
                        printf("\nAll done\n");
                    }
                    break;
            }
            struct PCB* tempProcess = gll_first(processList);
            struct PCB* tempBlocked = gll_first(blockedProcess);

            //TODO: Set correct value of timeOfNextPendingDiskInterrupt
            uint64_t timeOfNextPendingDiskInterrupt = 0;

            if (gll_first(diskQueue) != NULL){
                timeOfNextPendingDiskInterrupt = (uint64_t)((diskq*) gll_first(diskQueue))->interrupt;
                //printf("   [LOG] simulate: timeOfNextPendingDiskInterrupt: %llu\n", timeOfNextPendingDiskInterrupt);

            }
            
            

            if(tempBlocked == NULL)
            {
                if(debug == 1)
                {
                    printf("\nGoing to move from proess list to ready\n");
                }
                struct NextMem* tempAddr;
                tempProcess->memoryFile = openTrace(tempProcess->memoryFilename);
                tempProcess->numOfIns = readNumIns(tempProcess->memoryFile);
                tempAddr = readNextMem(tempProcess->memoryFile);
                while(tempAddr!= NULL)
                {
                    gll_pushBack(tempProcess->memReq, tempAddr);
                    tempAddr = readNextMem(tempProcess->memoryFile);
                }
                gll_pushBack(readyProcess, tempProcess);
                gll_pop(processList);
                
                while(nextQuanta < tempProcess->start_time)
                {   
                    
                    current_time = nextQuanta;
                    nextQuanta = current_time + quantum;
                }
                OSTime += (tempProcess->start_time-current_time);
                current_time = tempProcess->start_time; 
            }
            else
            {
                if(tempProcess == NULL)
                {
            
                    if(debug == 1)
                    {
                        printf("\nGoing to move from blocked list to ready\n");
                    }
                    OSTime += (timeOfNextPendingDiskInterrupt-current_time);
                    current_time = timeOfNextPendingDiskInterrupt;
                    while (nextQuanta < current_time)
                    {
                        nextQuanta = nextQuanta + quantum;
                    }
                    diskToMemory();
                }
                else if(tempProcess->start_time >= timeOfNextPendingDiskInterrupt)
                {
                    if(debug == 1)
                    {
                        printf("\nGoing to move from blocked list to ready\n");
                    }
                    OSTime += (timeOfNextPendingDiskInterrupt-current_time);
                    current_time = timeOfNextPendingDiskInterrupt;
                    while (nextQuanta < current_time)
                    {
                        nextQuanta = nextQuanta + quantum;
                    }
                    diskToMemory();
                }
                else{
                    struct NextMem* tempAddr;
                    if(debug == 1)
                    {
                        printf("\nGoing to move from proess list to ready\n");
                    }
                    tempProcess->memoryFile = openTrace(tempProcess->memoryFilename);
                    tempProcess->numOfIns = readNumIns(tempProcess->memoryFile);
                    tempAddr = readNextMem(tempProcess->memoryFile);
                    while(tempAddr!= NULL)
                    {
                        gll_pushBack(tempProcess->memReq, tempAddr);
                        tempAddr = readNextMem(tempProcess->memoryFile);
                    }
                    gll_pushBack(readyProcess, tempProcess);
                    gll_pop(processList);
                    
                    while(nextQuanta < tempProcess->start_time)
                    {   
                        current_time = nextQuanta;
                        nextQuanta = current_time + quantum;
                    }
                    OSTime += (tempProcess->start_time-current_time);
                    current_time = tempProcess->start_time; 
                }
            }   
        }
    }
}

int main(int argc, char** argv)
{
    if(argc == 1)
    {
        printf("No file input\n");
        exit(1);
    }
    traceFileName = argv[1];
    outputFileName = argv[2];

    simulate();
    finishAll();
    statsUpdate();

    if(writeToFile(outputFileName, resultStats) == 0)
    {
        printf("Could not write output to file\n");
    }
    printf("User time = %" PRIu64 "\nOS time = %" PRIu64 "\n", resultStats.userModeTime, resultStats.OSModetime);
    printf("Context switched = %d\n", resultStats.numberOfContextSwitch);
    printf("Start time = 0\nEnd time =%llu\n\n", current_time);
    printf("Total disk interrupts=%llu\n", diskInt);
    printf("Total assigned page=%d\n", startingPage);

    gll_each(resultStats.perProcessStats, &printStats);

    // printf("\nExec Order:\n");
    // gll_each(resultStats.executionOrder, &printExecOrder);
    printf("\n");
    free(TLB);
    free(DREM);
    TLB = NULL;
    DREM = NULL;
}