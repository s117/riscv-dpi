//#include "svdpi.h"


#include "sim.h"
#include "htif.h"
#include "cachesim.h"
#include "extension.h"
#include <dlfcn.h>
#include <fesvr/option_parser.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <vector>
#include <string>
#include <memory>
#include "debug.h"
#include "dpisim.h"

static void help()
{
  fprintf(stderr, "usage: micros [host options] <target program> [target options]\n");
  fprintf(stderr, "Host Options:\n");
  fprintf(stderr, "  -p <n>             Simulate <n> processors\n");
  fprintf(stderr, "  -m <n>             Provide <n> MB of target memory\n");
  fprintf(stderr, "  -s <n>             Fast skip <n> instructions before microarchitectural simulation\n");
  fprintf(stderr, "  -e <n>             End simulation after <n> instructions have been committed by microarchitectural simulation\n");
  fprintf(stderr, "  -l <n>             Enable logging after <n> commits if compiled with support\n");
  fprintf(stderr, "  -d                 Interactive debug mode\n");
  fprintf(stderr, "  -g                 Track histogram of PCs\n");
  fprintf(stderr, "  -h                 Print this help message\n");
  fprintf(stderr, "  --ic=<S>:<W>:<B>   Instantiate a cache model with S sets,\n");
  fprintf(stderr, "  --dc=<S>:<W>:<B>     W ways, and B-byte blocks (with S and\n");
  fprintf(stderr, "  --l2=<S>:<W>:<B>     B both powers of 2).\n");
  fprintf(stderr, "  --extension=<name> Specify RoCC Extension\n");
  fprintf(stderr, "  --extlib=<name>    Shared library to load\n");
  exit(1);
}

/* execution start time */
time_t start_time;

//extern void tokenize(char* job, int& argc, char** argv);
extern void read_config_from_file(int& nargs, char*** args, FILE** fp_job);

// Should be global variables for access from all DPI functions
static debug_buffer_t* pipe;
//debug_buffer_t pipe(128);
static sim_t*  s_isa;
static sim_t*  s_dpi;
static long long arch_pc; // Keeps track of the architectural PC of the functional simulator
static int numMismatches = 0;
static bool debug = false;
static bool histogram = false;
static size_t nprocs = 1;
static size_t mem_mb = 0;
static size_t skip_amt = 0;

extern "C" {

  int initializeSim()
  {
  
  
    FILE*   fp_job;
    char    job[256];
    int     argc;
    char**  argv;
    argv    = 0; //Must initialize to NULL pointer, otherwise read_from_config fails
  
//    bool debug = false;
//    bool histogram = false;
//    size_t nprocs = 1;
//    size_t mem_mb = 0;
//    size_t skip_amt = 0;
    std::unique_ptr<icache_sim_t> ic;
    std::unique_ptr<dcache_sim_t> dc;
    std::unique_ptr<cache_sim_t> l2;
    std::function<extension_t*()> extension;
  
    option_parser_t parser;
    parser.help(&help);
    parser.option('h', 0, 0, [&](const char* s){help();});
    parser.option('d', 0, 0, [&](const char* s){debug = true;});
    parser.option('g', 0, 0, [&](const char* s){histogram = true;});
    parser.option('l', 0, 1, [&](const char* s){logging_on_at = atoll(s);});
    parser.option('p', 0, 1, [&](const char* s){nprocs = atoi(s);});
    parser.option('m', 0, 1, [&](const char* s){mem_mb = atoi(s);});
    parser.option('s', 0, 1, [&](const char* s){skip_amt = atoll(s);});
    parser.option('e', 0, 1, [&](const char* s){stop_amt = atoll(s);});
    parser.option(0, "ic", 1, [&](const char* s){ic.reset(new icache_sim_t(s));});
    parser.option(0, "dc", 1, [&](const char* s){dc.reset(new dcache_sim_t(s));});
    parser.option(0, "l2", 1, [&](const char* s){l2.reset(cache_sim_t::construct(s, "L2$"));});
    parser.option(0, "extension", 1, [&](const char* s){extension = find_extension(s);});
    parser.option(0, "extlib", 1, [&](const char *s){
      void *lib = dlopen(s, RTLD_NOW | RTLD_GLOBAL);
      if (lib == NULL) {
        fprintf(stderr, "Unable to load extlib '%s': %s\n", s, dlerror());
        exit(-1);
      }
    });
  
    // Read the arguments from config file as command line arguments cannot
    // be passed in an RTL simulation. Put the read arguments as a fake argv
    // so that rest of the option parsing remains the same as micrs-sim.
    read_config_from_file(argc, &argv, &fp_job);
  
    // Parse the arguments passed to DPI SIM
    auto argv1 = parser.parse(argv);
    if (!*argv1)
      help();
  

    // Turn on logging if user requested logging from the start.
    // This way even run_ahead instructions will be logged.
    if(logging_on_at == -1)
      logging_on = true;
  
    ifprintf(logging_on,stderr,"Instantiating the simulators\n");

    std::vector<std::string> htif_args(argv1, (const char*const*)argv + argc);
    s_dpi = new sim_t(nprocs, mem_mb, htif_args, DPI_SIM);

    ifprintf(logging_on,stderr,"Instantiated MICRO simulator\n");
  
    if (ic && l2) ic->set_miss_handler(&*l2);
    if (dc && l2) dc->set_miss_handler(&*l2);
    for (size_t i = 0; i < nprocs; i++)
    {
      if (ic) s_dpi->get_core(i)->get_mmu()->register_memtracer(&*ic);
      if (dc) s_dpi->get_core(i)->get_mmu()->register_memtracer(&*dc);
      if (extension) s_dpi->get_core(i)->register_extension(extension());
    }
  

    s_dpi->set_debug(debug);
    s_dpi->set_histogram(histogram);
  
    #ifdef RISCV_MICRO_CHECKER
      s_isa = new sim_t(nprocs, mem_mb, htif_args, ISA_SIM);
      ifprintf(logging_on,stderr,"Instantiated ISA simulator\n");

      pipe = new debug_buffer_t(PIPE_QUEUE_SIZE);
      ifprintf(logging_on,stderr,"Instantiated PIPE\n");
  
      pipe->set_isa_sim(s_isa);
  
      s_isa->set_procs_pipe(pipe);
      s_dpi->set_procs_pipe(pipe);
    #endif
  
    int i, exit_code, exec_index;
    char c, *all_options;
  
    /* opening banner */
  
    ///* 8/20/05 ER: Add banner for 721 simulator. */
    //fprintf(stderr,
  	//  "\n\nCopyright (c) 1999-2005 by Eric Rotenberg.  All Rights Reserved.\n");
    //fprintf(stderr,
  	//  "Welcome to the ECE 721 Simulator.  This is a custom simulator\n");
    //fprintf(stderr,
    //        "developed at North Carolina State University by Eric Rotenberg\n");
    //fprintf(stderr,
  	//  "and his students.  It uses the Simplescalar ISA and only those\n");
    //fprintf(stderr,
  	//  "files from the Simplescalar toolset needed to functionally\n");
    //fprintf(stderr,
  	//  "simulate a Simplescalar binary, copyright below:\n\n");
  
  
    /* record start of execution time, used in rate stats */
    start_time = time((time_t *)NULL);
  
    //pipe->skip_till_pc(0x10000,0);
  
    //logging_on = true;
  
    int htif_code;
  

    #ifdef RISCV_MICRO_CHECKER
      ifprintf(logging_on,stderr,"Booting ISA simulators\n");
      s_isa->boot();
  
      // If skip amount is provided, fast skip in the ISA sim
      ifprintf(logging_on,stderr, "Fast skipping Spike for %lu instructions\n",skip_amt);
      htif_code = s_isa->run_fast(skip_amt);
  
      // Fill the debug buffer
      htif_code = pipe->run_ahead();
    #endif
  
  
    // Boot the DPI SIM
    s_dpi->boot();
  
    // Runs Micors
    ifprintf(logging_on,stderr, "Fast skipping DPI SIM for %lu instructions\n",skip_amt);
    htif_code = s_dpi->run_fast(skip_amt);
    // Stop simulation if HTIF returns non-zero code
    if(!htif_code){
      ifprintf(logging_on,stderr, "Simulation finished during initialization\n");
    }
    // Check if simulation has already completed
    if(!s_dpi->running()){
      end_rtl_simulation();
      ifprintf(logging_on,stderr, "Stopping DPI SIM: HTIF Exit Code %d\n",htif_code);
    }
  
    // Turn on logging if user requested logging from the start.
    // This way even run_ahead instructions will be logged.
    if(logging_on_at == 0)
      logging_on = true;

    // Initialize the arch_pc to the architectural PC after skipping
    arch_pc = ((dpisim_t*)(s_dpi->get_core(0)))->get_pc();
  
    ifprintf(logging_on,stderr, "Starting DPI SIM\n");

    return 0;

  } //initializeSim()



  long long getArchRegValue(int reg_id)
  {
  
    ifprintf(logging_on,stderr, "Architecture Reg Value: %u -> 0x%lX\n",reg_id, ((dpisim_t*)(s_dpi->get_core(0)))->get_arch_reg_value(reg_id));
    return(((dpisim_t*)(s_dpi->get_core(0)))->get_arch_reg_value(reg_id));
  
  }


  long long getArchPC()
  {
  
    ifprintf(logging_on,stderr, "Architecture PC is: 0x%lX\n",((dpisim_t*)(s_dpi->get_core(0)))->get_pc());
    return(((dpisim_t*)(s_dpi->get_core(0)))->get_pc());
  
  }

  
  int getInstruction(long long inst_pc, int* exception)
  {
    //printf("I am in getInstruction\n");
    //ifprintf(logging_on,stderr, "Instruction PC is: 0x%llX\n",inst_pc);
    *exception = 0;
    int instruction = 0;
    try{
      instruction = ((dpisim_t*)(s_dpi->get_core(0)))->get_instruction(inst_pc);
    }
	  catch(trap_t& t) {
      ifprintf(logging_on, stderr, "Instruction Fetch Exception vaddr: 0x%llX cause: %lu\n",inst_pc,t.cause());
      *exception = t.cause();
    }
    return instruction;
  }


  long long loadDouble(long long cycle, long long ld_addr, int* exception)
  {
    *exception = 0;
    long long ld_data = 0;
    try{
      ld_data = (((dpisim_t*)(s_dpi->get_core(0)))->get_mmu())->load_uint64(ld_addr);
    }
    catch (mem_trap_t& t)
	  {
      ifprintf(logging_on, stderr, "Load Exception vaddr: 0x%llX cause: %lu\n",ld_addr,t.cause());
      *exception = t.cause();
    }
    ifprintf(logging_on,stderr, "Cycle: %lld Load addr is: 0x%llX data is: 0x%llX Exception: %d\n",cycle,ld_addr,ld_data,*exception);
    return ld_data;
  }

  long long loadWord(long long ld_addr, int* exception)
  {
    //printf("I am in loadWord\n");
    ifprintf(logging_on,stderr, "Load addr is: 0x%llX\n",ld_addr);
    *exception = 0;
    long long ld_data = 0;
    try{
      ld_data = (((dpisim_t*)(s_dpi->get_core(0)))->get_mmu())->load_uint64(ld_addr);
    }
    catch (mem_trap_t& t)
	  {
      ifprintf(logging_on, stderr, "Load Exception vaddr: 0x%llX cause: %lu\n",ld_addr,t.cause());
      *exception = t.cause();
    }
    return ld_data;
  }

  long long loadHalf(long long ld_addr, int* exception)
  {
    ifprintf(logging_on,stderr, "Load addr is: 0x%llX\n",ld_addr);
    *exception = 0;
    long long ld_data = 0;
    try{
      ld_data = (((dpisim_t*)(s_dpi->get_core(0)))->get_mmu())->load_uint64(ld_addr);
    }
    catch (mem_trap_t& t)
	  {
      ifprintf(logging_on, stderr, "Load Exception vaddr: 0x%llX cause: %lu\n",ld_addr,t.cause());
      *exception = t.cause();
    }
    return ld_data;
  }

  long long loadByte(long long ld_addr, int* exception)
  {
    ifprintf(logging_on,stderr, "Load addr is: 0x%llX\n",ld_addr);
    *exception = 0;
    long long ld_data = 0;
    try{
      ld_data = (((dpisim_t*)(s_dpi->get_core(0)))->get_mmu())->load_uint64(ld_addr);
    }
    catch (mem_trap_t& t)
	  {
      ifprintf(logging_on, stderr, "Load Exception vaddr: 0x%llX cause: %lu\n",ld_addr,t.cause());
      *exception = t.cause();
    }
    return ld_data;
  }
 
  void storeDouble(long long st_addr, long long st_data, int* exception)
  {
    ifprintf(logging_on,stderr, "Store addr is: 0x%llX and store data is: 0x%llX\n",st_addr,st_data);
    *exception = 0;
    try{
      (((dpisim_t*)(s_dpi->get_core(0)))->get_mmu())->store_uint64(st_addr,st_data);
    }
    catch (mem_trap_t& t)
	  {
      ifprintf(logging_on, stderr, "Store Exception vaddr: 0x%llX cause: %lu\n",st_addr,t.cause());
      *exception = t.cause();
    }
  }

  void storeWord(long long st_addr, long long st_data, int* exception)
  {
    ifprintf(logging_on,stderr, "Store addr is: 0x%llX and store data is: 0x%llX\n",st_addr,st_data);
    *exception = 0;
    try{
      (((dpisim_t*)(s_dpi->get_core(0)))->get_mmu())->store_uint32(st_addr,st_data);
    }
    catch (mem_trap_t& t)
	  {
      ifprintf(logging_on, stderr, "Store Exception vaddr: 0x%llX cause: %lu\n",st_addr,t.cause());
      *exception = t.cause();
    }
  }

  void storeHalf(long long st_addr, long long st_data, int* exception)
  {
    ifprintf(logging_on, stderr, "Store addr is: 0x%llX and store data is: 0x%llX\n",st_addr,st_data);
    *exception = 0;
    try{
      (((dpisim_t*)(s_dpi->get_core(0)))->get_mmu())->store_uint16(st_addr,st_data);
    }
    catch (mem_trap_t& t)
	  {
      ifprintf(logging_on, stderr, "Store Exception vaddr: 0x%llX cause: %lu\n",st_addr,t.cause());
      *exception = t.cause();
    }
  }

  void storeByte(long long cycle, long long st_addr, long long st_data, int* exception)
  {
    ifprintf(logging_on, stderr, "Cycle %lld: Store addr is: 0x%llX and store data is: 0x%llX\n",cycle,st_addr,st_data);
    *exception = 0;
    try{
      (((dpisim_t*)(s_dpi->get_core(0)))->get_mmu())->store_uint8(st_addr,st_data);
    }
    catch (mem_trap_t& t)
	  {
      ifprintf(logging_on, stderr, "Cycle %lld: Store Exception vaddr: 0x%llX cause: %lu\n",cycle,st_addr,t.cause());
      *exception = t.cause();
    }
  }

  long long dumpDouble(long long addr, int* exception)
  {
    long long data = 0;
    *exception = 0;
    try{
      data = (((dpisim_t*)(s_dpi->get_core(0)))->get_mmu())->load_uint64(addr);
    }
    catch (mem_trap_t& t)
	  {
      ifprintf(logging_on, stderr, "Dump Exception vaddr: 0x%llX cause: %lu\n",addr,t.cause());
      *exception = t.cause();
    }
    ifprintf(logging_on,stderr, "Dump addr is: 0x%llX and data is: 0x%llX\n ",addr,data);
    return data;
  }


  long long virt_to_phys(long long virt_addr, int bytes, int store_access, int fetch_access, int* exception)
  {
    ifprintf(logging_on, stderr, "Translate vaddr: 0x%llX bytes: %d\n",virt_addr,bytes);
    *exception = 0;
    long long phy_addr  = 0;
    try{
      phy_addr = (long long)(((dpisim_t*)(s_dpi->get_core(0)))->get_mmu())->translate(virt_addr, bytes, store_access, fetch_access);
    }
    catch (mem_trap_t& t)
	  {
      ifprintf(logging_on, stderr, "Memory Access Exception vaddr: 0x%llX cause: %lu\n",virt_addr,t.cause());
      *exception = t.cause();
    }
    return phy_addr;
  }

  int checkInstruction(long long v_cycle, long long v_commit, long long v_pc,int v_dest,long long v_dest_value, int is_fission)
  {

    //printf("I am in checkInstruction\n");
    #ifdef RISCV_MICRO_DEBUG
     fflush(0);
    #endif

    db_t *actual;		// Pointer to corresponding instruction in the functional simulator.

    ifprintf(logging_on, stderr, "Cycle %lld: Commit: %lld Checking instruction for PC 0x%llx\n",v_cycle,v_commit,v_pc);
    int check_passed = 1;

    long long fs_pc, fs_dest_value, fs_addr, fs_ld_data;
    int fs_dest, fs_exception;

    if(!is_fission){
      
    //printf("I am in checkInstruction\n");
      // Get pointer to the corresponding instruction in the functional simulator.
      // This enables checking results of the pipeline simulator.
      // arch_pc keeps track of the current architectural pc
	    debug_index_t db_index = pipe->first(arch_pc);
	    actual = pipe->pop(db_index);
	    arch_pc = actual->a_next_pc;
      
    //printf("I am in checkInstruction\n");
      // Validate the instruction PC.
      fs_pc = actual->a_pc;
      if (actual->a_num_rdst > 0) {
        fs_dest = actual->a_rdst[0].n;
        fs_dest_value = actual->a_rdst[0].value;
        fs_addr = actual->a_addr; 
        fs_ld_data = actual->real_upper; 
        fs_exception = actual->a_exception;
      }
      
      // CHECK PC
      if (v_pc != fs_pc) {
        fprintf(stderr, "*ER PC MISMATCH!!\n");
        fprintf(stderr, " CYCLE: %lld ", v_cycle);
        fprintf(stderr, " V_PC=0x%016llx  ",v_pc);
        fprintf(stderr, " FS_PC=0x%016llx\n", fs_pc);
        numMismatches++;
        check_passed = 0;
        //exit(0);
      }
      else if (actual->a_num_rdst > 0) {
        // INSTRUCTION HAS A DESTINATION
        if (v_dest != fs_dest) {
          fprintf(stderr, "*ER RDST MISMATCH!!\n");
          fprintf(stderr, " CYCLE: %lld ", v_cycle);
          fprintf(stderr, " V_PC=0x%016llx V_RDST=%2d V_RDST_VALUE=0x%016llx",v_pc, v_dest, v_dest_value);
          fprintf(stderr, " FS_PC=0x%016llx FS_RDST=%2d FS_RDST_VALUE=0x%016llx\n", fs_pc, fs_dest, fs_dest_value);
          numMismatches++;
          check_passed = 0;
          //exit(0);
        }
        else if (v_dest_value != fs_dest_value) {
          fprintf(stderr, "*ER RDST_VALUE MISMATCH!!\n");
          fprintf(stderr, " CYCLE: %lld ", v_cycle);
          fprintf(stderr, " V_PC=0x%016llx V_RDST=%2d V_RDST_VALUE=0x%016llx",v_pc, v_dest, v_dest_value);
          fprintf(stderr, " FS_PC=0x%016llx FS_RDST=%2d FS_RDST_VALUE=0x%016llx FS_ADDR=0x%016llx FS_LD_DATA=0x%016llx\n", 
                              fs_pc, fs_dest, fs_dest_value,fs_addr,fs_ld_data);
          numMismatches++;
          check_passed = 0;
          //exit(0);
        }
      } // HAS DESTINAITON

      dpisim_t* dpi_sim = ((dpisim_t*)(s_dpi->get_core(0)));
      if(!check_passed)
        pipe->dump(dpi_sim, actual, stderr);

      check_passed = check_passed && !(dpi_sim->check_state(dpi_sim->get_state(),actual->a_state,actual));
    }
    
    return check_passed;
  }

  int htif_tick(int* htif_ret)
  {
    int htif_code = (int)((s_dpi->get_htif())->tick());
    if(!htif_code){
      ifprintf(logging_on,stderr, "Simulation finished during HTIF tick\n");
      fflush(0);
    }
    // Check if simulation has completed
    if(!s_dpi->running()){
      end_rtl_simulation();
    }
    *htif_ret = htif_code;
    return 0;
  }

  void set_interrupt(int which, bool on)
  {
    dpisim_t* sim = ((dpisim_t*)(s_dpi->get_core(0)));
    state_t* state = sim->get_state();
    uint32_t mask = (1 << (which + SR_IP_SHIFT)) & SR_IP;
    if (on){
      state->sr |= mask;
    }
    else{
      state->sr &= ~mask;
    }
  }

  int get_logging_mode()
  {
    return logging_on;
  }


  void set_pcr(int which,long long val)
  {

    ifprintf(logging_on, stderr, "Write CSR 0x%x ->  0x%llX\n",which,val);
    dpisim_t* sim = ((dpisim_t*)(s_dpi->get_core(0)));
    state_t* state = sim->get_state();
    reg_t rv64 = (state->sr & SR_S) ? (state->sr & SR_S64) : (state->sr & SR_U64);
  
    switch (which)
    {
      case CSR_FFLAGS:
        state->fflags = val & (FSR_AEXC >> FSR_AEXC_SHIFT);
        break;
      case CSR_FRM:
        state->frm = val & (FSR_RD >> FSR_RD_SHIFT);
        break;
      case CSR_FCSR:
        state->fflags = (val & FSR_AEXC) >> FSR_AEXC_SHIFT;
        state->frm = (val & FSR_RD) >> FSR_RD_SHIFT;
        break;
      case CSR_STATUS:
        state->sr = (val & ~SR_IP) | (state->sr & SR_IP);
  #ifndef RISCV_ENABLE_64BIT
        state->sr &= ~(SR_S64 | SR_U64);
  #endif
  #ifndef RISCV_ENABLE_FPU
        state->sr &= ~SR_EF;
  #endif
        // Hardcoding this as ext = false for DPI SIM
        //if (!ext)
          state->sr &= ~SR_EA;
        state->sr &= ~SR_ZERO;
        rv64 = (state->sr & SR_S) ? (state->sr & SR_S64) : (state->sr & SR_U64);
        //mmu->flush_tlb();
        break;
      case CSR_EPC:
        state->epc = val;
        break;
      case CSR_BADVADDR:
        state->badvaddr = val;
        break;
      case CSR_EVEC:
        state->evec = val & ~3;
        break;
      case CSR_COUNT:
        state->count = val;
        if(state->count > (uint64_t)logging_on_at)
          logging_on = true;
        break;
      case CSR_COUNTH:
        state->count = (val << 32) | (uint32_t)state->count;
        break;
      case CSR_COMPARE:
        //serialize();
        set_interrupt(IRQ_TIMER, false);
        state->compare = val;
        break;
      case CSR_CAUSE:
        state->cause = val;
        break;
      case CSR_PTBR:
        state->ptbr = val & ~(PGSIZE-1);
        break;
      case CSR_SEND_IPI:
        s_dpi->send_ipi(val);
        break;
      case CSR_CLEAR_IPI:
        set_interrupt(IRQ_IPI, val & 1);
        break;
      case CSR_SUP0:
        state->pcr_k0 = val;
        break;
      case CSR_SUP1:
        state->pcr_k1 = val;
        break;
      case CSR_TOHOST:
        if (state->tohost == 0)
          state->tohost = val;
        break;
      case CSR_FROMHOST:
        // When this is called by the RTL, it will always
        // be to clear the interrupt (i.e. val = 0).
        set_interrupt(IRQ_HOST, val != 0);
        state->fromhost = val;
        break;
    }
  }

  long long get_pcr(int which)
  {

    ifprintf(logging_on, stderr, "Read CSR 0x%x\n",which);
    dpisim_t* sim = ((dpisim_t*)(s_dpi->get_core(0)));
    state_t* state = sim->get_state();
    reg_t rv64 = (state->sr & SR_S) ? (state->sr & SR_S64) : (state->sr & SR_U64);

    switch (which)
    {
      case CSR_FFLAGS:
        //require_fp;
        return state->fflags;
      case CSR_FRM:
        //require_fp;
        return state->frm;
      case CSR_FCSR:
        //require_fp;
        return (state->fflags << FSR_AEXC_SHIFT) | (state->frm << FSR_RD_SHIFT);
      case CSR_STATUS:
        return state->sr;
      case CSR_EPC:
        return state->epc;
      case CSR_BADVADDR:
        return state->badvaddr;
      case CSR_EVEC:
        return state->evec;
      case CSR_CYCLE:
      case CSR_TIME:
      case CSR_INSTRET:
      case CSR_COUNT:
        //serialize();
        return state->count;
      case CSR_CYCLEH:
      case CSR_TIMEH:
      case CSR_INSTRETH:
      case CSR_COUNTH:
        if (rv64)
          break;
        //serialize();
        return state->count >> 32;
      case CSR_COMPARE:
        return state->compare;
      case CSR_CAUSE:
        return state->cause;
      case CSR_PTBR:
        return state->ptbr;
      case CSR_SEND_IPI:
      case CSR_CLEAR_IPI:
        return 0;
      case CSR_ASID:
        return 0;
      case CSR_FATC:
        //mmu->flush_tlb();
        return 0;
      case CSR_HARTID:
        //return id;
        return 0;
      case CSR_IMPL:
        return 1;
      case CSR_SUP0:
        return state->pcr_k0;
      case CSR_SUP1:
        return state->pcr_k1;
      case CSR_TOHOST:
        //sim->get_htif()->tick(); // not necessary, but faster
        return state->tohost;
      case CSR_FROMHOST:
        //sim->get_htif()->tick(); // not necessary, but faster
        return state->fromhost;
      case CSR_UARCH0:
      case CSR_UARCH1:
      case CSR_UARCH2:
      case CSR_UARCH3:
      case CSR_UARCH4:
      case CSR_UARCH5:
      case CSR_UARCH6:
      case CSR_UARCH7:
      case CSR_UARCH8:
      case CSR_UARCH9:
      case CSR_UARCH10:
      case CSR_UARCH11:
      case CSR_UARCH12:
      case CSR_UARCH13:
      case CSR_UARCH14:
      case CSR_UARCH15:
        return 0;
    }
    throw trap_illegal_instruction();
  }

} // extern "C"
