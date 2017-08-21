// This function reads the simulation parameters from a config file, opens the job file, loads the ckeckpoint,
// performs the skip.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

extern char *verilog_optstring;
extern bool logging_on;
#define MAX_ARGS 64

void read_config_from_file(int& nargs, char ***args, FILE **fp_job) {

  FILE *fp_config;
  char buf[256];
  int num, exec_index;
  char c;
  int i;

  fprintf(stderr, "Opening job file\n");
  num = 0;
  fp_config = fopen("job","r"); 

  if (fp_config == NULL) {
    fprintf(stderr, "Cannot open job file\n");
    exit(0);
  }

  if(logging_on)
    fprintf(stderr, "Reading from job file\n");

  if ((*args) != NULL) {
    fprintf(stderr,"Must initialize *args to NULL before passign the pointer here\n");
  } else{
    (*args) = (char**) malloc (MAX_ARGS*sizeof(char*));
  }
  // Pre allocate multiple of 8 space so that address spaces are not interfered with
  // Saw a real bug where having 10 characters in an argument was leading to function pointer 
  // issue and function not being called.
  for(int i=0;i<MAX_ARGS;i++){
	    (*args)[i] = (char *) malloc(64 * sizeof(char));
  }
  while (!feof(fp_config)) {
    fscanf(fp_config, "%s", buf);
    if (strcmp(buf,"\n")) {
      //if ((*args)[num] == NULL)
	    //(*args)[num] = (char *) malloc(strlen(buf) * sizeof(char));
	    //(*args)[num] = (char *) malloc(64 * sizeof(char));
      strcpy((*args)[num], buf);
      // fprintf(stderr, "%d %s\n", num, (*args)[num]);
      num++;
    }
    strcpy(buf , "");
  }
  nargs = num - 1;
  if (nargs == 0)
    fprintf(stderr, "No parameters or job file specified in config!!\n");

  if(logging_on){
    fprintf(stderr, "Finished reading from config file. nargs = %d\n", nargs);
    for (i = 0; i < nargs; i++)
      fprintf(stderr, "%d %s\n", i, (*args)[i]);
  }

} // read_config_from_file



void tokenize(char *job, int& argc, char **argv) {
  char delimit[4] = " \t\n";	// tokenize based on "space", "tab", eol

  argc = 0;
  while ((argc < MAX_ARGS) &&
         (argv[argc] = strtok((argc ? (char *)NULL : job), delimit))) {
     argc++;
  }
  //fprintf(stderr,"ARGC: %d %s %s %s %s\n",argc,argv[0],argv[1],argv[2],argv[3]);
  if (argc == 0){
     fprintf(stderr,"No thread specified.");
     exit(0);

  }
}

