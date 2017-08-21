// See LICENSE for license details.

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
#include <algorithm>
#include "debug.h"
#include "parameters.h"
#include <signal.h>

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
  fprintf(stderr, "  --cp <n>           <n> branch checkpoints for mispredict recovery\n");
  fprintf(stderr, "  --btb <n>          BTB has <n> entries\n");
  fprintf(stderr, "  --ctiq <n>         CTIQ / BranchQ has <n> entries\n");
  fprintf(stderr, "  --bp <n>           Brach Counter Table has <n> entries\n");
  fprintf(stderr, "  --ras <n>          RAS has <n> entries\n");
  fprintf(stderr, "  --fq <n>           Fetch queue has <n> entries\n");
  fprintf(stderr, "  --al <n>           Active List has <n> entries\n");
  fprintf(stderr, "  --iq <n>           Issue Queue has <n> entries\n");
  fprintf(stderr, "  --lsq <n>          Load/Store Queue has <n> entries\n");
  fprintf(stderr, "  --fw <n>           <n> wide fetch\n");
  fprintf(stderr, "  --dw <n>           <n> wide dispatch\n");
  fprintf(stderr, "  --iw <n>           <n> wide issue / <n> execution lanes\n");
  fprintf(stderr, "  --rw <n>           <n> wide retire\n");
  fprintf(stderr, "  --phase <n>        Phase interval is <n>\n");
  fprintf(stderr, "  --nol2             Do not use an L2 cache\n");
  fprintf(stderr, "  --lane=<B>:<L>:<S>:<C>:<LFP>:<FP>:<MTF>  Phase interval is <n>\n");
  fprintf(stderr, "  --ic=<S>:<W>:<B>   Instantiate a cache model with S sets,\n");
  fprintf(stderr, "  --dc=<S>:<W>:<B>   W ways, and B-byte blocks (with S and\n");
  fprintf(stderr, "  --l2=<S>:<W>:<B>   B both powers of 2).\n");
  fprintf(stderr, "  --extension=<name> Specify RoCC Extension\n");
  fprintf(stderr, "  --extlib=<name>    Shared library to load\n");
  exit(1);
}

/* execution start time */
time_t start_time;

extern "C" void set_csr_in_rtl(int, uint64_t){};
extern "C" uint64_t get_csr_from_rtl(int){return 0;};
extern "C" void end_rtl_simulation(){};
extern "C" void flush_caches_in_rtl(){};

static void sim_stats(FILE* stream)
{
}  

static void exit_now(int sigtype)
{
  sim_stats(stderr);
  //for (unsigned int i = 0; i < NumThreads; i++)
  //   THREAD[i]->mem_stats(stderr);
  exit(1);
}

static void sim_config(FILE* stream)
{
}

static void mem_config(FILE* stream)
{
}

static void set_lane_matrix(const char* config)
{
  //const char* bp = strchr(config, ':');
  //if (!bp++) help();
  //const char* lp = strchr(bp, ':');
  //if (!lp++) help();
  //const char* sp = strchr(lp, ':');
  //if (!sp++) help();
  //const char* cp = strchr(sp, ':');
  //if (!cp++) help();
  //const char* fp = strchr(cp, ':');
  //if (!fp++) help();
  //const char* lfp = strchr(fp, ':');
  //if (!lfp++) help();
  //const char* mtfp = strchr(lfp, ':');
  //if (!mtfp++) help();

  if(std::count(config, config+std::strlen(config), ':') != 6)
    help();

  char *pEnd;
  FU_LANE_MATRIX[0] = strtol(config ,&pEnd,16)   /*     BR: 0000 0010 */;
  pEnd++;
  FU_LANE_MATRIX[1] = strtol(pEnd   ,&pEnd,16)   /*     LS: 0001 0001 */;
  pEnd++;
  FU_LANE_MATRIX[2] = strtol(pEnd   ,&pEnd,16)   /*  ALU_S: 0000 1110 */;
  pEnd++;
  FU_LANE_MATRIX[3] = strtol(pEnd   ,&pEnd,16)   /*  ALU_C: 0000 0010 */;
  pEnd++;
  FU_LANE_MATRIX[4] = strtol(pEnd   ,&pEnd,16)   /*  LS_FP: 0001 0001 */;
  pEnd++;
  FU_LANE_MATRIX[5] = strtol(pEnd   ,&pEnd,16)   /* ALU_FP: 0000 0110 */;
  pEnd++;
  FU_LANE_MATRIX[6] = strtol(pEnd   ,NULL ,16)   /*    MTF: 0000 0010 */;

  fprintf(stderr,"Lane Matrix: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n"
                      ,FU_LANE_MATRIX[0]
                      ,FU_LANE_MATRIX[1]
                      ,FU_LANE_MATRIX[2]
                      ,FU_LANE_MATRIX[3]
                      ,FU_LANE_MATRIX[4]
                      ,FU_LANE_MATRIX[5]
                      ,FU_LANE_MATRIX[6]);

}

/* exit when this becomes non-zero */
//int sim_exit_now = FALSE;
// Should be global variables for access from all DPI functions
debug_buffer_t* pipe;
sim_t*  s_isa;
sim_t*  s_micro;

static void endSimulation(int signal)
{
  //*** Must delete the simulator instances in order to dump stats ***
  // Stats are dumped in the destructor for the processor instances.
  delete s_isa;
  delete s_micro;
}  



int main(int argc, char** argv)
{
  bool debug = false;
  bool histogram = false;
  size_t nprocs = 1;
  size_t mem_mb = 0;
  size_t skip_amt = 0;
  bool skip_enable = false;
  std::unique_ptr<icache_sim_t> ic;
  std::unique_ptr<dcache_sim_t> dc;
  std::unique_ptr<cache_sim_t> l2;
  std::function<extension_t*()> extension;

  bool restore_checkpoint = false;
  std::string checkpoint_file = "checkpoint";

  option_parser_t parser;
  parser.help(&help);
  parser.option('h', 0, 0, [&](const char* s){help();});
  parser.option('d', 0, 0, [&](const char* s){debug = true;});
  parser.option('g', 0, 0, [&](const char* s){histogram = true;});
  parser.option('l', 0, 1, [&](const char* s){logging_on_at = atoll(s);});
  parser.option('p', 0, 1, [&](const char* s){nprocs = atoi(s);});
  parser.option('m', 0, 1, [&](const char* s){mem_mb = atoi(s);});
  parser.option('s', 0, 1, [&](const char* s){skip_amt = atoll(s); skip_enable = true;});
  parser.option('e', 0, 1, [&](const char* s){stop_amt = atoll(s); use_stop_amt = true;});
  parser.option('c', 0, 1, [&](const char* s){checkpoint_file = s; restore_checkpoint = true;});
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
  parser.option(0, "cp"  , 1, [&](const char* s){NUM_CHECKPOINTS = atoi(s);});
  parser.option(0, "btb" , 1, [&](const char* s){BTB_SIZE = atoi(s); BTB_MASK = BTB_SIZE-1;});
  parser.option(0, "ctiq", 1, [&](const char* s){CTIQ_SIZE = atoi(s); CTIQ_MASK = CTIQ_SIZE-1;});
  parser.option(0, "bp"  , 1, [&](const char* s){BP_TABLE_SIZE = atoi(s); BP_INDEX_MASK = BP_TABLE_SIZE-1;});
  parser.option(0, "ras" , 1, [&](const char* s){RAS_SIZE = atoi(s);});
  parser.option(0, "fq"  , 1, [&](const char* s){FETCH_QUEUE_SIZE = atoi(s);});
  parser.option(0, "al"  , 1, [&](const char* s){ACTIVE_LIST_SIZE = atoi(s);});
  parser.option(0, "iq"  , 1, [&](const char* s){ISSUE_QUEUE_SIZE = atoi(s);});
  parser.option(0, "lsq" , 1, [&](const char* s){LQ_SIZE = atoi(s);SQ_SIZE = atoi(s);});
  parser.option(0, "fw"  , 1, [&](const char* s){FETCH_WIDTH = atoi(s);});
  parser.option(0, "dw"  , 1, [&](const char* s){DISPATCH_WIDTH = atoi(s);});
  parser.option(0, "iw"  , 1, [&](const char* s){ISSUE_WIDTH = atoi(s);});
  parser.option(0, "rw"  , 1, [&](const char* s){RETIRE_WIDTH = atoi(s);});
  parser.option(0, "phase",1, [&](const char *s){phase_interval = atoll(s);});
  parser.option(0, "lane" ,1, [&](const char *s){set_lane_matrix(s);});
  parser.option(0, "nol2", 1, [&](const char* s){L2_PRESENT = false;});

  auto argv1 = parser.parse(argv);
  if (!*argv1)
    help();
  std::vector<std::string> htif_args(argv1, (const char*const*)argv + argc);
  s_micro = new sim_t(nprocs, mem_mb, htif_args, DPI_SIM);

  if (ic && l2) ic->set_miss_handler(&*l2);
  if (dc && l2) dc->set_miss_handler(&*l2);
  for (size_t i = 0; i < nprocs; i++)
  {
    if (ic) s_micro->get_core(i)->get_mmu()->register_memtracer(&*ic);
    if (dc) s_micro->get_core(i)->get_mmu()->register_memtracer(&*dc);
    if (extension) s_micro->get_core(i)->register_extension(extension());
  }

  s_micro->set_debug(debug);
  s_micro->set_histogram(histogram);

  #ifdef RISCV_MICRO_CHECKER
    s_isa = new sim_t(nprocs, mem_mb, htif_args, ISA_SIM);
    pipe = new debug_buffer_t(PIPE_QUEUE_SIZE);

    pipe->set_isa_sim(s_isa);

    s_isa->set_procs_pipe(pipe);
    s_micro->set_procs_pipe(pipe);
  #endif

  int i, exit_code, exec_index;
  char c, *all_options;

  /* opening banner */

  /* 8/20/05 ER: Add banner for 721 simulator. */
  fprintf(stderr,
	  "\n\nCopyright (c) 1999-2005 by Eric Rotenberg.  All Rights Reserved.\n");
  fprintf(stderr,
	  "Welcome to the ECE 721 Simulator.  This is a custom simulator\n");
  fprintf(stderr,
          "developed at North Carolina State University by Eric Rotenberg\n");
  fprintf(stderr,
	  "and his students.  It uses the Simplescalar ISA and only those\n");
  fprintf(stderr,
	  "files from the Simplescalar toolset needed to functionally\n");
  fprintf(stderr,
	  "simulate a Simplescalar binary, copyright below:\n");


  ///* print out the program arguments */
  //fprintf(stderr, "**ARGS**: ");
  //for (i=0; i<argc; i++)
  //  fprintf(stderr, "%s ", argv[i]);
  //fprintf(stderr, "\n");

  //if (argc < 2)
  //  usage(argc, argv);

  struct sigaction sigIntHandler;

  sigIntHandler.sa_handler = endSimulation;
  sigemptyset(&sigIntHandler.sa_mask);
  sigIntHandler.sa_flags = 0;

  sigaction(SIGINT,   &sigIntHandler, NULL);

  /* catch SIGUSR1 and dump intermediate stats */
  sigaction(SIGUSR1,  &sigIntHandler, NULL);

  /* catch SIGUSR1 and dump intermediate stats */
  sigaction(SIGUSR2,  &sigIntHandler, NULL);

  /* register an error handler */
  //fatal_hook(sim_stats);

  /* set up a non-local exit point */
  //if ((exit_code = setjmp(sim_exit_buf)) != 0)
  //{
  //  /* special handling as longjmp cannot pass 0 */
  //  exit_now(exit_code-1);
  //}

  ///* compute legal options */
  //all_options = getopt_combine_options("v", sim_optstring);
  //all_options = getopt_combine_options(all_options, mem_optstring);

  ///* parse global options */
  //getopt_init();
  //while ((c = getopt_next(argc, argv, all_options)) != EOF)
  //{
  //  switch (c) {
  //  case 'v':
	//    verbose = TRUE;
	//    break;
  //  case '?':
	//    usage(argc, argv);
  //  }
  //}
  //exec_index = getopt_index;

  ///* parse simulator options */
  //sim_options(argc, argv);
  //mem_options(argc, argv);

  //if (exec_index == argc)
  //  usage(argc, argv);

  //// 10/3/99 ER: Added support for multiple (funcsim) threads.
  //FILE *fp = fopen(argv[exec_index], "r");
  //if (fp)
  //   init_threads(fp, envp);
  //else
  //   fatal("Could not open the job file '%s'.", argv[exec_index]);

  /* record start of execution time, used in rate stats */
  start_time = time((time_t *)NULL);

  /* output simulation conditions */
  sim_config(stderr);
  mem_config(stderr);

  //pipe->skip_till_pc(0x10000,0);

  //logging_on = true;

  int htif_code;

  // Turn on logging if user requested logging from the start.
  // This way even run_ahead instructions will be logged.
  if(logging_on_at == -1)
    logging_on = true;

  #ifdef RISCV_MICRO_CHECKER
    s_isa->boot();

    if(restore_checkpoint){
      s_isa->restore_checkpoint(checkpoint_file);
    }

    // If skip amount is provided, fast skip in the ISA sim
    if(skip_enable & (!restore_checkpoint)){
      //s_isa->init_checkpoint("isa_checkpoint");
      fprintf(stderr, "Fast skipping Spike for %lu instructions\n",skip_amt);
      htif_code = s_isa->run_fast(skip_amt);
      //htif_code = s_isa->create_checkpoint();
    }


    // Fill the debug buffer
    pipe->run_ahead();
  #endif


  s_micro->boot();
  //exit(0);

  if(restore_checkpoint){
    s_micro->restore_checkpoint(checkpoint_file);
  }

  // Runs Micors
  if(skip_enable & (!restore_checkpoint)){
    fprintf(stderr, "Fast skipping MICROS for %lu instructions\n",skip_amt);
    htif_code = s_micro->run_fast(skip_amt);
    // Stop simulation if HTIF returns non-zero code
    if(!htif_code) return htif_code;
  }

  //htif_code = s_micro->create_checkpoint();
  // Stop simulation if HTIF returns non-zero code
  //if(!htif_code) return htif_code;

  // Turn on logging if user requested logging from the start of timing simulation.
  if(logging_on_at == 0)
    logging_on = true;

  fprintf(stderr, "Starting MICROS\n");
  htif_code = s_micro->run();
  fprintf(stderr, "Stopping MICROS: HTIF Exit Code %d\n",htif_code);

  //*** Must delete the simulator instances in order to dump stats ***
  // Stats are dumped in the destructor for the processor instances.
  delete s_isa;
  delete s_micro;

  return htif_code;
}
