/*------------------------
*
*  
*     a- include the current working directory in the prompt
*     b- fork a child process to execute a valid UNIX command
*     c- access the PATH variable from the environment
*     d- supports redirecting
*     e- process bookkeeping
*     f- signal handling
*     h- task9 - foreground/background switching
*  
*---------------------*/

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <assert.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define FALSE 0
#define TRUE 1
#define INPUT_STRING_SIZE  80
#define MAX_FILE_SIZE 1024

#include "io.h"
#include "parse.h"
#include "process.h"
#include "shell.h"

process* find_process_by_pid(pid_t pid);
void add_process(process* p);
int check_background(char *arg[]);


int cmd_quit(tok_t arg[]) {
  printf("Bye\n");
  exit(0);
  return 1;
}

int cmd_cd(tok_t arg[]) {
  if(chdir(arg[0]) == -1) {
	fprintf(stdout, "Can not access directory %s\n", arg[0]);
	return -1;		
  }

  return 1;
}

int cmd_wait(tok_t arg[]) {
   wait_for_bgjobs();

   return 1;
}

int process_job(tok_t arg[], int foreground) {
   process *p;
   pid_t pid;

   // if pid not specified, then move the most recently
   // launched process to the foreground
   if (arg[0] == NULL) {
	p = first_process->next;	
        if (p == NULL)
	    return 0;		
	else
	    pid = p->pid;
   }
   // else find the process with the specified pid
   else {
        pid = atoi(arg[0]);
   }

   continue_process(pid, foreground);

   return 1;
}

int cmd_fg(tok_t arg[]) {
   process_job(arg, 1);
   return 1;
}

int cmd_bg(tok_t arg[]) {
   process_job(arg, 0);
   return 1;
}


int cmd_exec(tok_t arg[]) {
   int wstatus;
   int pid;
   char pathname[MAX_FILE_SIZE+1];

   if(arg == NULL || arg[0] == NULL) {
	perror("cmd_exec: invalide argument\n");
	return -1;
   }

   // create a process bookkeeping
   process* cur_process = (process*)m supporting background processes and process control,alloc(sizeof(process));
   assert(cur_process != NULL);
   // cur_process->argc = argc;
   cur_process->argv = arg;
   cur_process->completed = cur_process->stopped = 0;
   cur_process->stdin = cur_process->stdout = -1;
   cur_process->tmodes = shell_tmodes;
   (cur_process->tmodes).c_lflag |= (IEXTEN | ISIG | ICANON ); 
   cur_process->background = process_background_sign(arg); // foreground process by default

   if(path_resolution(arg[0], pathname, MAX_FILE_SIZE) != 0) 
      strncpy(pathname, arg[0], MAX_FILE_SIZE); 

   // printf("exec file: %s\n", filename);
   if(io_redirection(cur_process, arg) != 0) {
      fprintf(stderr, "Failed to process input/output redirection\n");
      exit(-1);
   }

   add_process(cur_process);

   pid = fork();

   // child process
   if(pid == 0) {
   	cur_process->pid = getpid();

        launch_process(cur_process);
	if(execv(pathname, arg) == -1) { 
		fprintf(stderr, "Failed to exec: %s\n", arg[0]);
                exit(-2);
	}
	perror("Child process unexpected trace");
	exit(-3);
   }
   else if(pid < 0) {
	fprintf(stderr, "Failed to exec: %s\n", arg[0]);
  	return -1;
   }
   // parent process
   else {
	// sync pgid info in parent child
	cur_process->pid = pid;
	setpgid(pid, pid);

	if(!cur_process->background) {
		put_process_in_foreground(cur_process, 0);
	}
	else {
		put_process_in_background(cur_process, 0);
	}
   }

   return 1;		
}

int cmd_help(tok_t arg[]);


// 06/21/2015 added by thinkhy
// check if background sign & is placed at the end of command line
int process_background_sign(char *arg[]) { 
    int i;
    if(arg == NULL) return 0;

    // find the last field 
    for (i = 0; arg[i]; i++);
    i--;
    if (i < 0) return 0;

    // pattern like'ls &', & is placed in the last field
    if(arg[i][0] == '&' && arg[i][1] == '\0') {
	arg[i] = NULL;
	return 1;
    }
    // pattern like'ls&', & is placed at the ending position of last field
    else {
	int len = strlen(arg[i]);
	if (len > 0 && arg[i][len-1] == '&') {
	   arg[i][len-1] = '\0';
	   return 1;
	}
    } 

    return 0;
}


// function: parse input/output redirection syntax 
// and replace stdin or stdout with specific file descriptor

int io_redirection(process *p, tok_t arg[]) {
     int index = 1; // first argument is the program itself
     int outfd, infd;
     int ret;
     while(index < MAXTOKS && arg[index]) {
	switch(arg[index][0]) {
	    case '>':
		outfd = open(arg[++index], O_WRONLY|O_CREAT, S_IRWXU|S_IRWXG|S_IROTH);
		if(outfd == -1) {
		    fprintf(stderr, "Failed to open %s\n",arg[index]);
		    return -1;
		}
		// close(1);
		// ret = dup2(outfd, 1);
                // if(ret < 0) {
		//  fprintf(stderr, "cannot invoke dup2 for stdout, return code is %d\n", ret);
		//    return -1;
	        //}
		p->stdout = outfd;
		arg[index-1] = arg[index] = NULL;
                break;

	    case '<':
		infd = open(arg[++index], O_RDONLY);
		if(infd == -1) {
		    fprintf(stderr, "Failed to open %s\n",arg[index]);
		    return -1;
		}
		// close(0);
		// ret = dup2(infd, 0);
                // if(ret < 0) {
		//     fprintf(stderr, "cannot invoke dup2 for stdin, return code is %d\n", ret);
		//     return -1;
	        // }
		p->stdout = infd;
		arg[index-1] = arg[index] = NULL;
                break;
        }

	index++;
     }
     return 0;
}


// first  arg[IN]:  executable file
// second arg[OUT]: valid path
int path_resolution(const char* filename, 
	            char pathname[], 
		    int size) {
    
   int pos;
   int i, j;
   char * cur_path; 
   
   if(filename == NULL ||filename[0] == '/' 
	|| filename[0] == '.') {
	return 1;	
   }

   // get PATH env var
   char * path_env = getenv("PATH");

   // for(cur_path = strtok(path_env, ":"); 
   //	cur_path != NULL; cur_path = strtok(NULL, ":")) {
   
   for(pos = 0; path_env[pos]; pos++) {
        for(i = 0; 
	     i < size && path_env[pos+i] && path_env[pos+i] != ':'; i++) 
			pathname[i] = path_env[pos+i];
	if(path_env[pos+i] == '\0')
		break;
	else
		pos += i;

	// strip the ending slash
	if(pathname[i-1] != '/') pathname[i++] = '/';

	// concatenate path and file 
	for(j = 0; i < size && filename[j]; i++, j++)
		 pathname[i] = filename[j];

	if(i < size) 
		pathname[i] = '\0';
	else {
		fprintf(stderr, "size: %d, i: %d\n", size, i);
		perror("path_resolution failed: pathname over bound");
	}

	// if file is exectualbe, return OK
	if(access(pathname, X_OK) == 0) {
		return 0;
        }
   }

   return 1; 
}


/* Command Lookup table */
typedef int cmd_fun_t (tok_t args[]); /* cmd functions take token array and return int */
typedef struct fun_desc {
  cmd_fun_t *fun;
  char *cmd;
  char *doc;
} fun_desc_t; supporting background processes and process control,

fun_desc_t cmd_table[] = {
  { cmd_help, "?",    "show this help menu" },
  { cmd_quit, "quit", "quit the command shell" },
  { cmd_cd,   "cd",   "change current working directory" },
  { cmd_wait, "wait", "wait until all background jobs have finished their process before redirecting to the prompt" },
  { cmd_fg,   "fg", "fg [pid]; change to the process with id pid to the foreground. If pid is not defined, then move the most recently launched process to the foreground." },
  { cmd_bg,   "bg", "bg [pid]; change to the process with id pid to the background. If pid is not determined, then move the most recently launched process to the background." },
};

int cmd_help(tok_t arg[]) {
  int i;
  for (i=0; i < (sizeof(cmd_table)/sizeof(fun_desc_t)); i++) {
    printf("%s - %s\n",cmd_table[i].cmd, cmd_table[i].doc);
  }
  return 1;
}

int lookup(char cmd[]) {
  int i;
  for (i=0; i < (sizeof(cmd_table)/sizeof(fun_desc_t)); i++) {
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0)) return i;
  }
  return -1;
}

void init_shell()
{
  /* Check if we are running interactively */
  shell_terminal = STDIN_FILENO;

  /** Note that we cannot take control of the terminal if the shell
      is not interactive */
  shell_is_interactive = isatty(shell_terminal);

  if(shell_is_interactive){

    /* Force into foreground */
    while(tcgetpgrp (shell_terminal) != (shell_pgid = getpgrp()))
      kill( - shell_pgid, SIGTTIN);

    /* Ignore interactive and job-control  */
    signal(SIGINT, SIG_IGN);   
    signal(SIGQUIT, SIG_IGN);   
    signal(SIGTSTP, SIG_IGN);   
    signal(SIGTTIN, SIG_IGN);   
    signal(SIGTTOU, SIG_IGN);   
    
    // If parent process's SIGCHLD is ignored,   
    // waitpid will return -1 with errorno set to ECHILD
    //signal(SIGCHLD, SIG_IGN);   

    shell_pgid = getpid();
    /* Put shell in its own process group */
    if(setpgid(shell_pgid, shell_pgid) < 0){
      perror("Couldn't put the shell in its own process group");
      exit(1);
    }

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);
    tcgetattr(shell_terminal, &shell_tmodes);

  }
	
  /** YOUR CODE HERE */
	
}

/**
 * Add a process to our process list
 */
void add_process(process* p)
{   
    assert(first_process != NULL);

    if(p == NULL) return;
    printf("add_process: %d\n", p->pid);
    // 150617 added by thinkhy 
    p->next = first_process->next;
    if(first_process->next != NULL)
	    first_process->next->prev = p;
    p->prev = first_process;
    first_process->next = p;
}

/**
 * Remove a process from our process list
 */
void remove_process(process* p) {
    assert(first_process != NULL);
    assert(first_process != p);

    if(p == NULL) return;
    printf("remove_process: %d\n", p->pid);
    p->prev->next = p->next;
    if (p->next)
	    p->next->prev = p->prev;
}

/**
 * creates a process given the inputstring from stdin
 */
process* create_process(char* inputString)
{
  /** YOUR CODE HERE */
  return NULL;
}


/**
 * find a process by process ID
 * 150617 added by thinkhy
 */
process* find_process_by_pid(pid_t pid) {
  process *p = first_process;
  while(p && p->pid != pid) {
	p = p->next;
  }

  return p;
}


int shell (int argc, char *argv[]) {
  char *s = malloc(INPUT_STRING_SIZE+1);			/* user input string */
  char cwd[MAX_FILE_SIZE+1];
  tok_t *t;			/* tokens parsed from input */
  int lineNum = 0;
  int fundex = -1;
  pid_t pid = getpid();		/* get current processes PID */
  pid_t ppid = getppid();	/* get parents PID */
  pid_t cpid, tcpid, cpgid;


  init_shell();

  // create the head node and initialize it
  first_process = (process*)malloc(sizeof(process));
  assert(first_process != NULL);
  first_process->pid = pid;
  first_process->completed = first_process->stopped  = 0;
  first_process->status = 0;
  first_process->background = 0;
  first_process->tmodes = shell_tmodes;
  first_process->argc = argc;
  first_process->argv = argv;
  first_process->stdin = STDIN_FILENO;
  first_process->stdout = STDOUT_FILENO;
  first_process->stderr = STDERR_FILENO;
  first_process->next = first_process->prev = NULL;

  printf("%s running as PID %d under %d\n",argv[0],pid,ppid);

  getcwd(cwd, MAX_FILE_SIZE);
  lineNum=0;
  fprintf(stdout, "%s - %d: ", cwd, lineNum);
  while ((s = freadln(stdin))) {
    t = getToks(s); /* break the line into tokens */
    fundex = lookup(t[0]); /* Is first token a shell literal */
    if(fundex >= 0) cmd_table[fundex].fun(&t[1]);
    else if(t && t[0]) {
       cmd_exec(&t[0]);
      /* fprintf(stdout, "This shell only supports built-ins. Replace this to run programs as commands.\n"); */
    }

    do_job_notification(0);
    getcwd(cwd, MAX_FILE_SIZE);
    fprintf(stdout, "%s - %d: ", cwd, lineNum);
  }

  return 0;
}


