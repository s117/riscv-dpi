// This file uses source code from the University of Berkeley RISC-V 
// project in original or modified form. 
// See LICENSE for license details.

#include "sim.h"
#include "htif.h"
#include <map>
#include <iostream>
#include <climits>
#include <cstdlib>
#include <cassert>
#include <signal.h>
#include <iostream>
#include <fstream>
#include "dpisim.h"

volatile bool ctrlc_pressed = false;
static void handle_signal(int sig)
{
	if (ctrlc_pressed) {
		exit(-1);
	}
	ctrlc_pressed = true;
	signal(sig, &handle_signal);
}

sim_t::sim_t(size_t nprocs, size_t mem_mb, const std::vector<std::string>& args, proc_type_t _proc_type)
	: htif(new htif_isasim_t(this, args)), procs(std::max(nprocs, size_t(1))),
	  current_step(0), idle_cycles(0), current_proc(0), debug(false), checkpointing_enabled(false)
{
	signal(SIGINT, &handle_signal);
	// allocate target machine's memory, shrinking it as necessary
	// until the allocation succeeds
	size_t memsz0 = (size_t)mem_mb << 20;
	size_t quantum = 1L << 20;
	if (memsz0 == 0) {
		memsz0 = 1L << (sizeof(size_t) == 8 ? 32 : 30);
	}

	memsz = memsz0;
	while ((mem = (char*)calloc(1, memsz)) == NULL) {
		memsz = memsz*10/11/quantum*quantum;
	}

	if (memsz != memsz0)
		fprintf(stderr, "warning: only got %lu bytes of target mem (wanted %lu)\n",
		        (unsigned long)memsz, (unsigned long)memsz0);

	debug_mmu = new mmu_t(mem, memsz, DEBUG_MMU); //set debug type true

  this->proc_type = _proc_type;

	for (size_t i = 0; i < procs.size(); i++) {
    //Instantiate the correct processor type here depending upon
    //the simulator type.
    if(_proc_type == ISA_SIM){
		  procs[i] = new processor_t(this, new mmu_t(mem, memsz), i);
		  procs[i]->set_proc_type("ISA_SIM");
    }
    else{
		  procs[i] = new dpisim_t(
		      this,
          // Set this as MICRO_MMU so that mem operations
          // do not push to debug buffer. This is necessary
          // as we use the same class as ISA sim to instantiate
          // the mmu.
		      new mmu_t(mem, memsz, MICRO_MMU),  
		      i,
		      FETCH_QUEUE_SIZE,
		      NUM_CHECKPOINTS,
		      ACTIVE_LIST_SIZE,
		      ISSUE_QUEUE_SIZE,
		      LQ_SIZE,
		      SQ_SIZE,
		      FETCH_WIDTH,
		      DISPATCH_WIDTH,
		      ISSUE_WIDTH,
		      RETIRE_WIDTH,
		      FU_LANE_MATRIX);
		  procs[i]->set_proc_type("DPI_SIM");
    }
	}

}

sim_t::~sim_t()
{
	for (size_t i = 0; i < procs.size(); i++)
	{
		mmu_t* pmmu = procs[i]->get_mmu();
		delete procs[i];
		delete pmmu;
	}
	delete debug_mmu;
	free(mem);
}

void sim_t::send_ipi(reg_t who)
{
	if (who < procs.size()) {
		procs[who]->deliver_ipi();
	}
}

reg_t sim_t::get_scr(int which)
{
	switch (which)
	{
		case 0:
			return procs.size();
		case 1:
			return memsz >> 20;
		default:
			return -1;
	}
}

void sim_t::boot()
{
  // This tick will initialize the processor.
	bool htif_return = htif->tick();

}

int sim_t::run()
{
	//while (htif->tick())
  bool htif_return = true;
  // This tick will initialize the processor.
	//bool htif_return = htif->tick();
	while (htif_return)
	{
		if (debug || ctrlc_pressed) {
			interactive();
		}
		else {
      //TODO: Changed this to match HTIF tick timings b/w ISA and MICRO
			//step(INTERLEAVE);
			htif_return = step(1);
		}
	}
  if(!htif_return)
    ifprintf(logging_on,stderr, "Stopping Core: HTIF Exit Code %d\n",htif_return);

	return htif->exit_code();
}

bool sim_t::step(size_t n)
{
  bool htif_return = true;
  bool stop_simulation = false;
	for (size_t i = 0, steps = 0; i < n; i += steps)
	{
    size_t instret = 0;
    if(get_proc_type() == ISA_SIM){
		  steps = std::min(n - i, INTERLEAVE - current_step);
  		procs[current_proc]->step(steps,instret);
    } else {
		  steps = (INTERLEAVE - current_step);
    // This function continues until it has retired "steps" instructions
    // or it encounters a cycle with 0 retired instructions.
  		stop_simulation = ((dpisim_t*)procs[current_proc])->step_micro(steps,instret);
      if(stop_simulation)
        return 0;
    }

    if(instret){
      idle_cycles = 0;
    }else{
      idle_cycles++;
    }

		//current_step += steps;
		current_step += instret;
    // Either the core has retired INTERLEAVE number of instructions
    // or it has been idle for a INTERLEAVE steps, do a HTIF tick and move to 
    // the next core.
		if (current_step == INTERLEAVE || idle_cycles == INTERLEAVE)
		{
			current_step = 0;//current_step % INTERLEAVE;
      idle_cycles  = 0;
      // TODO: This causes mismatch between ISA sim and
      // micro sim due to out of order timing of micro sim
			//procs[current_proc]->yield_load_reservation();
			if (++current_proc == procs.size()) {
				current_proc = 0;
			}

      // If HTIF is done, this will return false
			htif_return = htif->tick();
		}
	}

  if(!htif_return)
    ifprintf(logging_on,stderr, "Stopping Core: HTIF Exit Code %d\n",htif_return);


  return htif_return;
}

// Currently supports only one core - can be easily extended to all cores
bool sim_t::run_fast(size_t n)
{
  bool old_debug = get_procs_debug();
  bool old_checker = get_procs_checker();
  //set_procs_debug(true);
  set_procs_checker(false);

  bool htif_return = true;
  size_t total_retired = 0;
  size_t steps = 0;
  while(total_retired < n && htif_return)
	{
    size_t instret = 0;
		steps = std::min(n - total_retired, INTERLEAVE - current_step);

    // This function continues until it has retired "steps" instructions
    // or it encounters a cycle with 0 retired instructions.
  	procs[current_proc]->step(steps,instret);

    if(instret){
      idle_cycles = 0;
    }else{
      idle_cycles++;
    }

    total_retired += instret;
		//current_step += steps;
		current_step += instret;
    // Either the core has retired INTERLEAVE number of instructions
    // or it has been idle for a INTERLEAVE steps, do a HTIF tick and move to 
    // the next core.
		if (current_step == INTERLEAVE || idle_cycles == INTERLEAVE)
		{
			current_step = 0;//current_step % INTERLEAVE;
      idle_cycles  = 0;
			procs[current_proc]->yield_load_reservation();
			if (++current_proc == procs.size()) {
				current_proc = 0;
			}

      // If HTIF is done, this will return false
			htif_return = htif->tick();
		}
	}

  //// Copy registers from fast skip state to pipeline register file.
  //// Also reset the AMT.
  //if(proc_type == DPI_SIM){
  //  ifprintf(logging_on,stderr,"Copying state after skipping %lu instructions\n",total_retired);
  //  ((dpisim_t*)procs[current_proc])->copy_state_to_micro();
  //}

  //Flush all files, including stdout and stderr
  fflush(0);

  ifprintf(logging_on,stderr,"State for %s:\n",proc_type == DPI_SIM ? "dpi_sim" : "isa_sim");
  if(logging_on){
    procs[current_proc]->get_state()->dump(stderr);
  }

  set_procs_debug(old_debug);
  set_procs_checker(old_checker);
  return htif_return;
}

void sim_t::step_till_pc(reg_t break_pc,unsigned int proc_n)
{
  procs[proc_n]->set_debug(true);
  bool checker = procs[proc_n]->get_checker();
  procs[proc_n]->set_checker(false);
  size_t instret = 0;
  while (procs[proc_n]->get_pc() != break_pc){
    procs[proc_n]->step(1,instret);
    htif->tick();
  }
  procs[proc_n]->set_checker(checker);
}

bool sim_t::running()
{
	for (size_t i = 0; i < procs.size(); i++)
		if (procs[i]->running()) {
			return true;
		}
	return false;
}

void sim_t::init_checkpoint(std::string _checkpoint_file)
{
  checkpointing_enabled = true; 
  checkpoint_file = _checkpoint_file;
  htif->start_checkpointing(checkpoint_file+".syscall");
}

bool sim_t::create_checkpoint()
{
  bool htif_return = true;

  htif->stop_checkpointing();
  create_memory_checkpoint(checkpoint_file+".memory");
  create_proc_checkpoint(checkpoint_file+".proc");

  return htif_return;
}

bool sim_t::restore_checkpoint(std::string restore_file)
{
/*-----Changes: Mohit (Modified restore checkpoint to support '.gz' type checkpoint format)-------*/
// Refer to 721sim for create and restore checkpoint logic
  //bool htif_return = true;

  //fprintf(stderr,"Trying to restore checkpoint from %s.*\n",restore_file.c_str());
  //// This tick will restore the checkpoint.
  //  htif_return = htif->restore_checkpoint(restore_file+".syscall");
  //restore_memory_checkpoint(restore_file+".memory");
  //restore_proc_checkpoint(restore_file+".proc");

  //fprintf(stderr,"Done restoring checkpoint from %s.*\n",restore_file.c_str());

  //return htif_return;
///////////////////////////////////////////////////////////////////////////////////////////////////////////
  bool htif_return = true;

  // Check if file name has .gz extension. If not, append .gz to the name
  if(restore_file.substr(restore_file.find_last_of(".") + 1) != "gz") {
    restore_file = restore_file+".gz";
  }

  //std::cerr << "Trying to restore HTIF checkpoint from " << restore_file << std::endl;
  fflush(0);
  restore_chkpt.open (restore_file.c_str(), std::ios::in | std::ios::binary);
  if ( ! restore_chkpt.good()) {
    std::cerr << "ERROR: Opening file `" << restore_file << "' failed.\n";
	  return false;
  }

  // This tick will restore the checkpoint.
	htif_return = htif->restore_checkpoint(restore_chkpt);
  std::cerr << "Done restoring HTIF checkpoint from " << restore_file << std::endl;

  //std::cerr << "Trying to restore mem/reg HTIF checkpoint from " << restore_file << std::endl;
  restore_memory_checkpoint(restore_chkpt);
  restore_proc_checkpoint(restore_chkpt);
  restore_chkpt.close();
  std::cerr << "Done restoring mem/reg checkpoint from " << restore_file << std::endl;

  return htif_return;

}

void sim_t::create_memory_checkpoint(std::string memory_file)
{

  std::fstream memory_chkpt;
  memory_chkpt.open (memory_file, std::ios::out | std::ios::binary);
  //uint64_t buf[1024];
  //for (size_t i = 0; i < 0x80000; i++){
  //  for (size_t j = 0; i < 1024; i++){
  //    buf[j] = debug_mmu->load_uint64((i*1024)+(j*8));
  //  }
  //  memory_chkpt.write((char *)&buf[0],1024*8);
  //}
  uint64_t signature = 0xbaadbeefdeadbeef;
  memory_chkpt.write((char*)&signature,8);
  memory_chkpt.write(mem,memsz);
  memory_chkpt.close();
}

void sim_t::create_proc_checkpoint(std::string proc_file)
{
  std::fstream proc_chkpt;
  proc_chkpt.open (proc_file, std::ios::out | std::ios::binary);
  state_t *state = procs[current_proc]->get_state();
  uint64_t signature = 0xdeadbeefbaadbeef;
  proc_chkpt.write((char*)&signature,8);
  proc_chkpt.write((char *)state,sizeof(state_t));
  proc_chkpt.close();

  fprintf(stderr,"Checkpointed State for %s:\n",proc_type == DPI_SIM ? "dpi_sim" : "isa_sim");
  procs[current_proc]->get_state()->dump(stderr);
}

//void sim_t::restore_memory_checkpoint(std::string memory_file)
void sim_t::restore_memory_checkpoint(std::istream& memory_chkpt)
{
/*-----Changes: Mohit (Modified to support memory checkpoint restore for '.gz' file)-------*/
// Refer to 721sim for memory checkpoint restore                                            //fprintf(stderr,"Trying to restore memory state from %s\n",memory_file.c_str());
  //fflush(0);
  //std::fstream memory_chkpt;
  //memory_chkpt.open (memory_file, std::ios::in | std::ios::binary);
  ////uint64_t buf[1024];
  ////for (size_t i = 0; i < 0x80000; i++){
  ////  memory_chkpt.read((char *)&buf[0],1024*8);
  ////  for (size_t j = 0; i < 1024; i++){
  ////    debug_mmu->store_uint64(((i*1024)+(j*8)),buf[j]);
  ////  }
  ////}
  //uint64_t signature;
  //memory_chkpt.read((char*)&signature,8);
  //assert(signature == 0xbaadbeefdeadbeef);
  //memory_chkpt.read(mem,memsz);
  //memory_chkpt.close();
  //fprintf(stderr,"Done restoring memory state from %s\n",memory_file.c_str());
///////////////////////////////////////////////////////////////////////////////////////////////////////////
  uint64_t signature;
  uint64_t chkpt_memsz;
  memory_chkpt.read((char*)&signature,8);
  assert(signature == 0xbaadbeefdeadbeef);
  // Check that the checkpointed memory size the current simulator memory size are same
  memory_chkpt.read((char*)&chkpt_memsz,sizeof(chkpt_memsz));
  assert(memsz == chkpt_memsz);
  memory_chkpt.read(mem,memsz);
}

//void sim_t::restore_proc_checkpoint(std::string proc_file)
void sim_t::restore_proc_checkpoint(std::istream& proc_chkpt)
{
/*-----Changes: Mohit (Modified processor checkpoint restore to support '.gz' type checkpoint format)-------*/
  //fflush(0);
  //std::fstream proc_chkpt;
  //proc_chkpt.open (proc_file, std::ios::in | std::ios::binary);
  //state_t *state = procs[0]->get_state();
  //uint64_t signature;
  //proc_chkpt.read((char*)&signature,8);
  //assert(signature = 0xdeadbeefbaadbeef);
  //proc_chkpt.read((char *)state,sizeof(state_t));
  //proc_chkpt.close();

  ////// Copy registers from fast skip state to pipeline register file.
  ////// Also reset the AMT.
  ////if(proc_type == DPI_SIM){
  ////  ifprintf(logging_on,stderr,"Copying state after restoring checkpoint\n");
  ////  ((dpisim_t*)procs[current_proc])->copy_state_to_micro();
  ////}

  //fprintf(stderr,"State for %s:\n",proc_type == DPI_SIM ? "dpi_sim" : "isa_sim");
  //procs[current_proc]->get_state()->dump(stderr);

  //fprintf(stderr,"Done restoring proc state from %s\n",proc_file.c_str());
///////////////////////////////////////////////////////////////////////////////////////////////////////////
  state_t *state = procs[0]->get_state();
  uint64_t signature;
  proc_chkpt.read((char*)&signature,8);
  assert(signature = 0xdeadbeefbaadbeef);
  proc_chkpt.read((char *)state,sizeof(state_t));
}

void sim_t::stop()
{
	procs[0]->state.tohost = 1;
	while (htif->tick())
		;
}

//bool sim_t::htif_tick()
//{
//	return(htif->tick());
//}

void sim_t::set_debug(bool value)
{
	debug = value;
}

void sim_t::set_histogram(bool value)
{
	histogram_enabled = value;
	for (size_t i = 0; i < procs.size(); i++) {
		procs[i]->set_histogram(histogram_enabled);
	}
}

void sim_t::set_procs_debug(bool value)
{
	for (size_t i=0; i< procs.size(); i++) {
		procs[i]->set_debug(value);
	}
}

bool sim_t::get_procs_debug()
{
	for (size_t i=0; i< procs.size(); i++) {
		if(procs[i]->get_debug())
      return true;

	}
  return false;
}

void sim_t::set_procs_pipe(debug_buffer_t* pipe)
{
	for (size_t i=0; i< procs.size(); i++) {
		procs[i]->set_pipe(pipe);
	}
}

void sim_t::set_procs_checker(bool value)
{
	for (size_t i=0; i< procs.size(); i++) {
		procs[i]->set_checker(value);
	}
}

bool sim_t::get_procs_checker()
{
	for (size_t i=0; i< procs.size(); i++) {
		if(procs[i]->get_checker())
      return true;

	}
  return false;
}

// Whenever memory is written by HTIF, flush the caches in RTL
void sim_t::flush_caches()
{
  if(get_proc_type() == DPI_SIM)
    flush_caches_in_rtl();
}

