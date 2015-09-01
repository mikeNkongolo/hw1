/**********************************************************************************
*
*  Change List:
*     1. HW1 - task8 - background job
*     2. HW1 - task9 - Foreground/background switching
*  
***********************************************************************************/

#include "process.h"
#include "shell.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <termios.h>

extern int shell_is_interactive;
extern int shell_terminal; //File descripter for the terminal.

// For test
void sig_handler(int signum) {
   printf("Received signal %d\n", signum);
}

/**
 * Store the status of the process pid that was returned by waitpid
 * Return 0 if all went all, nonzero otherwise 
 */
int mark_process_status(pid_t pid, int status) {
  process *p;

  #ifdef DEBUG
  printf("invoke mark_process_status for process %d\n", pid);
  #endif 

  if(pid > 0) {
	for (p = first_process; p; p = p->next) {
	   // fprintf(stderr, "p: %d, target pid: %d\n", p->pid, pid);
           if (p->pid == pid) {
		p->status = status;
		if (WIFSTOPPED(status)) {
			p->stopped = 1;
			return 0;
		}
		else if (WIFEXITED(status)) {
			p->completed = 1;			
			if (WIFSIGNALED(status))
			    fprintf(stderr, "%d: Terminated by signal %d.\n", (int)pid, WTERMSIG(p->status));
			return 0;
		}
	   }
	}
	fprintf(stderr, "No child process %d.\n", pid);
	return -1;
  }
  else if (pid == 0 || errno == ECHILD)
	/* No processes ready to report. */
	return -1;
  else {
	/* Other weird errors. */
	perror("waitpid");
	return -1;
  }
}

/* Check for processes that have status information available,
 * without blocking. 
 */
void update_status(void) {
   int status;
   pid_t pid;

   do {
	/* specifies that waitpid should return status information about any child process.  */
	pid = waitpid(WAIT_ANY, &status, WUNTRACED|WNOHANG);
        #ifdef DEBUG
	printf("update_status: waitpid return %d\n", pid);
	#endif
   } while(!mark_process_status(pid, status));
}

void wait_for_job(process *p) {
   int status;
   pid_t pid;

   do {
	// printf("wait on process on %d\n", (long)p->pid);
	pid = waitpid(WAIT_ANY, &status, WUNTRACED);
	mark_process_status(pid, status);
   } while (!p->completed && !p->stopped);
	  
}

/* Return true if all the background processes have completed */
int bgjobs_is_completed() {
   process *p;
   
   for (p = first_process; p; p = p->next) 
	if ((!p->completed) && p->background) {
		return 0;
	}
   return 1;
}

void wait_for_bgjobs() {
   int status;
   pid_t pid;

   /* loop until all the backgroun jobs completed */
   do {
	pid = waitpid(WAIT_ANY, &status, WNOHANG|WUNTRACED);
	mark_process_status(pid, status);
   } while (!bgjobs_is_completed());
}

void format_job_info(process *p, const char *status) {
  fprintf(stderr, "%ld (%s): %s %s\n", (long)p->pid, status, 
		p->argv == NULL ? "" : (p->argv)[0],
  		p->background ? "background" : "foreground" );
}

void do_job_notification(pid_t pid) {
   process *p, *q;

   /* Update status information for child processes. */
   update_status();

   for(p = first_process; p; p = q) {
	q = p->next;
	if(pid == 0 || pid == p->pid) {	
	       /* if all processes have completed, tell the user 
		* the job has completed and delete it from the list of active jobs. */
	       if(p->completed) {
		  format_job_info(p, "completed");
		  remove_process(p);
		  free(p);
	       }
	       else if(p->stopped) {
		  format_job_info(p, "stopped");
	       }
	       /* Don't day anything about jobs that are still running. */
	       else {
		  format_job_info(p, "running");
	       } 
	}
   }
}


/*
 * Executes the process p.
 * If the shell is in interactive mode and the process is a foreground process,
 * then p should take control of the terminal.
 */
void launch_process(process *p)
{
  /** YOUR CODE HERE */
  pid_t pid;

  if (shell_is_interactive) {
       /* Put the process into the process group and give the process group
 	* ther terminal, if appropriate
 	* This has to be done both by the shell and in the individual 
 	* child processes because of potential race conditions
 	*/
	pid = getpid();

	printf("Lanuch process %d\n", pid); // Debug

	setpgid(pid, pid);
	if (!p->background) {
		tcsetpgrp (shell_terminal, pid);
		tcsetattr(shell_terminal, TCSADRAIN, &p->tmodes);
	}
	
	/* Set the handling for job control signals back to the default. */
	signal(SIGINT, SIG_DFL);
	signal(SIGQUIT, SIG_DFL);
	signal(SIGTSTP, SIG_DFL);
	signal(SIGTTIN, SIG_DFL);
	signal(SIGTTOU, SIG_DFL);
	signal(SIGCHLD, SIG_DFL);

	/* Set the standard input/output channels of the new process. */
	// printf("stdin %d\n", p->stdin);
	if(p->stdin != STDIN_FILENO) {
		dup2(p->stdin, STDIN_FILENO);
		close(p->stdin);
	}

	// printf("stdout %d\n", p->stdout);
	if(p->stdout != STDOUT_FILENO) {
		dup2(p->stdout, STDOUT_FILENO);
		close(p->stdout);
	}
	if(p->stderr != STDIN_FILENO) {
		dup2(p->stderr, STDERR_FILENO);
		close(p->stderr);
	}

	// execvp (p->argv[0], p->argv);
	// perror("execvp");
	// exit(1);
  }

}

/* Put a process in the foreground. This function assumes that the shell
 * is in interactive mode. If the cont argument is true, send the process
 * group a SIGCONT signal to wake it up.
 */
void
put_process_in_foreground (process *p, int cont)
{
  /** YOUR CODE HERE */
  if (p == NULL) return;

  p->background = 0;
  
  /* Put the job into the foreground. */
  tcsetpgrp(shell_terminal, p->pid);  

  /* Send the job a continue signal, if necessary. */
  if (cont) {
	tcsetattr (shell_terminal, TCSADRAIN, &p->tmodes);
        if (kill(- p->pid, SIGCONT) < 0)
	      perror("Kill (SIGCONT)");
  }

  /* Wait for it to report. */
  /* wait_for_job(p); */
  // waitpid(p->pid, NULL, WUNTRACED); 
  wait_for_job(p);

  /* Put the shell back in the foreground. */
  tcsetpgrp (shell_terminal, shell_pgid);

  /* Restore the shell's terminal modes. */
  tcgetattr(shell_terminal, &p->tmodes);
  tcsetattr(shell_terminal, TCSADRAIN, &shell_tmodes);
}

/* Put a process in the background. If the cont argument is true, send
 * the process group a SIGCONT signal to wake it up. */
void
put_process_in_background (process *p, int cont)
{
  /** YOUR CODE HERE */
  if (p == NULL) return;

  p->background = 1;

  /* Send the job a continue signal, if necessary. */ 
  if (cont) {
	if (kill (- p->pid, SIGCONT) < 0)
		perror("kill (SIGCONT)");
  }
}

/* 06/22/2015 added thinkhy   
 * Mark a stopped process as being running again.
 */
void mark_process_as_running(process* p) {
	if (p == NULL) return;
	p->stopped = 0;
}

/* 06/22/2015 added thinkhy   
 * Mark a stopped process as being running again.
 */
void continue_process(pid_t pid, int foreground) {
   process *p;

   for(p = first_process; p&&p->pid != pid; p = p->next);
   if(p == NULL) return;

   mark_process_as_running(p);

   if (foreground)
	put_process_in_foreground(p, 1);
   else
	put_process_in_background(p, 1);
}


