#include <string.h>
#include <inttypes.h>

#include "fileIO.h"



FILE* openTrace(char* traceName){
    FILE *fptr;
    char* foldername = "traces/";

    char* filename = malloc(sizeof(foldername) + sizeof(traceName) + 1 ); /* make space for the new string (should check the return value ...) */
    strcpy(filename, foldername); /* copy name into the new var */
    strcat(filename, traceName); /* add the extension */


    if ((fptr = fopen(filename,"r")) == NULL){
        printf("Error opening file %s.\n", filename);
        // Program exits if the file pointer returns NULL.
        exit(1);
    }
    // printf("File opened\n");

    return fptr;
}

int closeTrace(FILE* fptr){
    if(fptr)
    {
        return fclose(fptr);
        // printf("\n File closed\n");
        return 1;
    }
    return 0;
}

void fprintStats(gll_t* list, FILE* f)
{
    struct Stats* s;
    gll_node_t* currNode = list->first;
    while(currNode!=NULL){
        s = currNode->data;
        double hitRatio = s->hitCount / (1.0* s->hitCount + 1.0 * s->missCount);
        fprintf(f, "\n\nProcess: %s: \nHit Ratio = %lf \tProcess completion time = %" PRIu64 
                "\tuser time = %" PRIu64 "\tOS time = %" PRIu64 
                "\nBlocked state duration = %" PRIu64 "\tNumber of page faults = %d\t Number of TLB miss = %d\n", 
                s->processName, hitRatio, s->duration, s->user_time, s->OS_time, s->blockedStateDuration, s->numberOfPgFaults, s->numberOfTLBmiss) ;
        currNode = currNode->next;
    }
}

int writeToFile(char* filename, struct TotalStats resultStats){
    FILE* fptr;
    if ((fptr = fopen(filename,"w")) == NULL){
        printf("Error opening file %s.\n", filename);
        // Program exits if the file pointer returns NULL.
        return 0;
    }
    fprintf(fptr, "Context switched = %d\n", resultStats.numberOfContextSwitch);

    fprintf(fptr, "Start time = %llu, \tEnd time =%llu\n", resultStats.start_time, resultStats.end_time);
    fprintf(fptr, "User time = %" PRIu64 ", \tOS time = %" PRIu64 "\n", resultStats.userModeTime, resultStats.OSModetime);
    fprintf(fptr, "Number of disk interrupt = %d, \tNumber of page faults %d, \tNumber of TLB miss %d, \nBlocked state duration = %" PRIu64 "\n", resultStats.numberOfDiskInt, resultStats.totalPgFaults, resultStats.totalTLBmiss, resultStats.totalBlockedStateDuration);
    
    fprintStats(resultStats.perProcessStats, fptr);
    
    fclose(fptr);
    return 1;
}

struct PCB* readNextTrace(FILE *fptr){
    char* line = NULL;
    ssize_t read;
    size_t len = 0;
    struct PCB* p = NULL;
    char *token;
    char* extension = ".txt";


    if((read = getline(&line, &len, fptr)) != -1)
    {
        // printf("Retrieved line of length %zu:\n", read);
        // printf("%s", line);
        p = malloc(sizeof(PCBNode));

        token = strtok(line, " ");
        // printf( "token ::%s::\n",token);
        if((strcmp(token, "")==0) || (strcmp(token, "\n")==0) || (strcmp(token, " ")==0))
        {
            printf("Line in tracefile contains no data.\n");
            return NULL;
        }
        p->name = token;
        // printf( "NAME %s::\n", p->name );

        char* filename = malloc(sizeof(token) + sizeof(extension) + 1 ); /* make space for the new string (should check the return value ...) */
        strcpy(filename, token); /* copy name into the new var */
        strcat(filename, extension); /* add the extension */
        p->memoryFilename = filename;
        // printf( "fileNAME %s::\n", p->memoryFilename );

        token = strtok(NULL, " ");
        p->start_time = atoi(token);
        //printf("START TIME %" PRIu64 "\n", p->start_time);

        // token = strtok(NULL, " ");
        // // printf( "TOKEN %s\n", token );
        // p->cpu_time = atof(token);
        // // printf("%lf \n", p->cpu_time);

        p->memReq = gll_init();
        p->hitCount = 0;
        p->missCount = 0;
        p->fracLeft = 0;
        p->user_time = 0;
        p->OS_time = 0;

        
    }
   return p;
}

struct NextMem* readNextMem(FILE* fptr)
{
    char* line = NULL;
    ssize_t read;
    size_t len = 0;

    if(fptr == NULL)
    {
        return NULL;
    }

    if((read = getline(&line, &len, fptr)) != -1)
    {
        // printf("Retrieved line of length %zu:\n", read);
        // printf("%s::\n", line);
        struct NextMem* lineRead = (struct NextMem*)malloc(sizeof(struct NextMem));
        line[strcspn(line, "\n")] = '\0'; //removes trailing newline characters, if any

        if (strcmp(line, "NONMEM") == 0) {
            lineRead->type = "NONMEM";
            lineRead->address = NULL;
        }
        else {
            lineRead->type = "MEM";
            lineRead->address = (char*) malloc(sizeof(strlen(line)-4));
            strcpy(lineRead->address, line+4);
            // printf("Address returning: %s\n",lineRead->address);
        }
        return lineRead;
    }
    return NULL;
}

int readNumIns(FILE* fptr)
{
    char* line = NULL;
    ssize_t read;
    size_t len = 0;
    char *token;

    if(fptr == NULL)
    {
        return -1;
    }

    read = getline(&line, &len, fptr);
    if(read != -1)
    {
        token = strtok(line, " ");
        token = strtok(NULL, " ");
        // printf("Read %s::\n", token);
        return atoi(token);
    }
    
    printf("Error reading system parameter from input file\n");
    exit(1);
}


struct SystemParameters* readSysParam(FILE* fptr)
{

    if(fptr == NULL)
    {
        printf("No file found to read input.\n");
        exit(1);
    }

    struct SystemParameters* sysParam = malloc(sizeof(systemParameters));
    char* line = NULL;
    char* TLBtypeLine = NULL;
    char* TLBrepLine = NULL;
    char* PGrepLine = NULL;
    ssize_t read;
    size_t len = 0;
    char *token;
    
    //reading comment lines
    read = getline(&line, &len, fptr);
    read = getline(&line, &len, fptr);

    //start reading
    read = getline(&line, &len, fptr);
    if(read != -1)
    {
        token = strtok(line, " ");
        token = strtok(NULL, " ");
        // printf("NON mem INST length string , %s::\n", token);
        sysParam->non_mem_inst_length = atoi(token);
        // printf("NON mem INST length, %d::\n", sysParam->non_mem_inst_length);
    }
    else{
        printf("Error reading system parameter from input file\n");
        exit(1);
    }

    read = getline(&line, &len, fptr);
    if(read != -1)
    {
        token = strtok(line, " ");
        token = strtok(NULL, " ");
        // printf("Virtual-addr-size-in-bits string , %s::\n", token);
        sysParam->virtual_addr_size_in_bits = atoi(token);
        // printf("Virtual-addr-size-in-bits, %d::\n", sysParam->virtual_addr_size_in_bits);
    }
    else{
        printf("Error reading system parameter from input file\n");
        exit(1);
    }

    read = getline(&line, &len, fptr);
    if(read != -1)
    {
        token = strtok(line, " ");
        token = strtok(NULL, " ");
        // printf("DRAM_size_in_MB string , %s::\n", token);
        sysParam->DRAM_size_in_MB = atoi(token);
        // printf("DRAM_size_in_MB, %d::\n", sysParam->DRAM_size_in_MB);
    }
    else{
        printf("Error reading system parameter from input file\n");
        exit(1);
    }

    read = getline(&line, &len, fptr);
    if(read != -1)
    {
        token = strtok(line, " ");
        token = strtok(NULL, " ");
        // printf("TLB_size_in_entries string , %s::\n", token);
        sysParam->TLB_size_in_entries = atoi(token);
        // printf("TLB_size_in_entries, %d::\n", sysParam->TLB_size_in_entries);
    }
    else{
        printf("Error reading system parameter from input file\n");
        exit(1);
    }

    read = getline(&line, &len, fptr);
    if(read != -1)
    {
        token = strtok(line, " ");
        token = strtok(NULL, " ");
        // printf("TLB-latency string , %s::\n", token);
        sysParam->TLB_latency = atoi(token);
        // printf("TLB-latency, %d::\n", sysParam->TLB_latency);
    }
    else{
        printf("Error reading system parameter from input file\n");
        exit(1);
    }

    read = getline(&line, &len, fptr);
    if(read != -1)
    {
        token = strtok(line, " ");
        token = strtok(NULL, " ");
        // printf("DRAM-latency string , %s::\n", token);
        sysParam->DRAM_latency = atoi(token);
        // printf("DRAM-latency, %d::\n", sysParam->DRAM_latency);
    }
    else{
        printf("Error reading system parameter from input file\n");
        exit(1);
    }


    read = getline(&line, &len, fptr);
    if(read != -1)
    {
        token = strtok(line, " ");
        token = strtok(NULL, " ");
        // printf("Swap-latency string , %s::\n", token);
        sysParam->Swap_latency = atoi(token);
        // printf("Swap-latency, %d::\n", sysParam->Swap_latency);
    }
    else{
        printf("Error reading system parameter from input file\n");
        exit(1);
    }

    read = getline(&line, &len, fptr);
    if(read != -1)
    {
        token = strtok(line, " ");
        token = strtok(NULL, " ");
        // printf("Page-fault-trap-handling-time string , %s::\n", token);
        sysParam->Page_fault_trap_handling_time = atoi(token);
        // printf("Page-fault-trap-handling-time, %d::\n", sysParam->Page_fault_trap_handling_time);
    }
    else{
        printf("Error reading system parameter from input file\n");
        exit(1);
    }

    read = getline(&line, &len, fptr);
    if(read != -1)
    {
        token = strtok(line, " ");
        token = strtok(NULL, " ");
        // printf("Swap-interrupt-handling-time string , %s::\n", token);
        sysParam->Swap_interrupt_handling_time = atoi(token);
        // printf("Swap-interrupt-handling-time, %d::\n", sysParam->Swap_interrupt_handling_time);
    }
    else{
        printf("Error reading system parameter from input file\n");
        exit(1);
    }
    
    read = getline(&TLBtypeLine, &len, fptr);
    if(read != -1)
    {
        token = strtok(TLBtypeLine, " ");
        token = strtok(NULL, " ");
        // printf("TLB_type string , %s::\n", token);
        token[strcspn(token, "\n")] = '\0'; //removes trailing newline characters, if any
        sysParam->TLB_type = token;
        // printf("TLB-type, %s::\n", sysParam->TLB_type);
    }
    else{
        printf("Error reading system parameter from input file\n");
        exit(1);
    }

    read = getline(&TLBrepLine, &len, fptr);
    if(read != -1)
    {
        token = strtok(TLBrepLine, " ");
        token = strtok(NULL, " ");
        // printf("TLB-replacement-policy string , %s::\n", token);
        token[strcspn(token, "\n")] = '\0'; //removes trailing newline characters, if any
        sysParam->TLB_replacement_policy = token;
        // printf("TLB-replacement-policy, %s::\n", sysParam->TLB_replacement_policy);
    }
    else{
        printf("Error reading system parameter from input file\n");
        exit(1);
    }

    //reading comment line
    read = getline(&line, &len, fptr);

    read = getline(&line, &len, fptr);

    if(read != -1)
    {
        token = strtok(line, " ");
        token = strtok(NULL, " ");
        // printf("P-in-bits string , %s::\n", token);
        sysParam->P_in_bits = atoi(token);
        // printf("P-in-bits, %d::\n", sysParam->P_in_bits);
    }
    else{
        printf("Error reading system parameter from input file\n");
        exit(1);
    }


    read = getline(&line, &len, fptr);
    if(read != -1)
    {
        token = strtok(line, " ");
        token = strtok(NULL, " ");
        // printf("Frac-mem-inst string , %s::\n", token);
        sysParam->Frac_mem_inst = atof(token);
        // printf("Frac-mem-inst, %lf::\n", sysParam->Frac_mem_inst);
    }
    else{
        printf("Error reading system parameter from input file\n");
        exit(1);
    }
    
    read = getline(&line, &len, fptr);
    if(read != -1)
    {
        token = strtok(line, " ");
        token = strtok(NULL, " ");
        // printf("Num-pagetable-levels string , %s::\n", token);
        sysParam->Num_pagetable_levels = atoi(token);
        // printf("Num-pagetable-levels, %d::\n", sysParam->Num_pagetable_levels);
    }
    else{
        printf("Error reading system parameter from input file\n");
        exit(1);
    }

    read = getline(&line, &len, fptr);
    if(read != -1)
    {
        token = strtok(line, " ");
        token = strtok(NULL, " ");
        // printf("N1-in-bits string , %s::\n", token);
        sysParam->N1_in_bits = atoi(token);
        // printf("N1-in-bits, %d::\n", sysParam->N1_in_bits);
    }
    else{
        printf("Error reading system parameter from input file\n");
        exit(1);
    }


    read = getline(&line, &len, fptr);
    if(read != -1)
    {
        token = strtok(line, " ");
        token = strtok(NULL, " ");
        // printf("N2-in-bits string , %s::\n", token);
        sysParam->N2_in_bits = atoi(token);
        // printf("N2-in-bits, %d::\n", sysParam->N2_in_bits);
    }
    else{
        printf("Error reading system parameter from input file\n");
        exit(1);
    }

    read = getline(&line, &len, fptr);
    if(read != -1)
    {
        token = strtok(line, " ");
        token = strtok(NULL, " ");
        // printf("N3-in-bits string , %s::\n", token);
        sysParam->N3_in_bits = atoi(token);
        // printf("N3-in-bits, %d::\n", sysParam->N3_in_bits);
    }
    else{
        printf("Error reading system parameter from input file\n");
        exit(1);
    }


    read = getline(&PGrepLine, &len, fptr);
    if(read != -1)
    {
        token = strtok(PGrepLine, " ");
        token = strtok(NULL, " ");
        // printf("Page-replacement-policy string , %s::\n", token);
        token[strcspn(token, "\n")] = '\0'; //removes trailing newline characters, if any
        sysParam->Page_replacement_policy = token;
        // printf("Page-replacement-policy, %s::\n", sysParam->Page_replacement_policy);
    }
    else{
        printf("Error reading system parameter from input file\n");
        exit(1);
    }


    read = getline(&line, &len, fptr);
    if(read != -1)
    {
        token = strtok(line, " ");
        token = strtok(NULL, " ");
        // printf("Num-procs  string , %s::\n", token);
        sysParam->Num_procs = atoi(token);
        // printf("Num-procs , %d::\n", sysParam->Num_procs);
    }
    else{
        printf("Error reading system parameter from input file\n");
        exit(1);
    }

    //reading comment line
    read = getline(&line, &len, fptr);

    return sysParam;

}