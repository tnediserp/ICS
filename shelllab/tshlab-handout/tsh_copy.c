 /*
  2  * tsh - A tiny shell program with job control
  3  *
  4  * 2014.12.1
  5  * 
  6  */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
 
/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */
 
/* Job states */
#define UNDEF         0   /* undefined */
#define FG            1   /* running in foreground */
#define BG            2   /* running in background */
#define ST            3   /* stopped */
 
 /*
 32  * Jobs states: FG (foreground), BG (background), ST (stopped)
 33  * Job state transitions and enabling actions:
 34  *     FG -> ST  : ctrl-z
 35  *     ST -> FG  : fg command
 36  *     ST -> BG  : bg command
 37  *     BG -> FG  : fg command
 38  * At most 1 job can be in the FG state.
 39  */
  
/* Parsing states */
#define ST_NORMAL   0x0   /* next token is an argument */
#define ST_INFILE   0x1   /* next token is the input file */
#define ST_OUTFILE  0x2   /* next token is the output file */

/* Global variables */
extern char **environ; /* defined in libc */
char prompt[] = "tsh> "; /* command line prompt (DO NOT CHANGE) */
int verbose = 0; /* if true, print additional output */
int nextjid = 1; /* next job ID to allocate */
char sbuf[MAXLINE]; /* for composing sprintf messages */
 
struct job_t { /* The job struct */
    pid_t pid; /* job PID */
 55     int jid; /* job ID [1, 2, ...] */
 56     int state; /* UNDEF, BG, FG, or ST */
 57     char cmdline[MAXLINE]; /* command line */
 58 };
 59 struct job_t job_list[MAXJOBS]; /* The job list */
 60 
 61 struct cmdline_tokens {
 62     int argc; /* Number of arguments */
 63     char *argv[MAXARGS]; /* The arguments list */
 64     char *infile; /* The input file */
 65     char *outfile; /* The output file */
 66     enum builtins_t { /* Indicates if argv[0] is a builtin command */
 67         BUILTIN_NONE, BUILTIN_QUIT, BUILTIN_JOBS, BUILTIN_BG, BUILTIN_FG
 68     } builtins;
 69 };
 70 /* End global variables */
 71 
 72 /* Function prototypes */
 73 void eval(char *cmdline);
 74 
 75 void sigchld_handler(int sig);
 76 void sigtstp_handler(int sig);
 77 void sigint_handler(int sig);
 78 
 79 /* Here are helper routines that we've provided for you */
 80 int parseline(const char *cmdline, struct cmdline_tokens *tok);
 81 void sigquit_handler(int sig);
 82 
 83 void clearjob(struct job_t *job);
 84 void initjobs(struct job_t *job_list);
 85 int maxjid(struct job_t *job_list);
 86 int addjob(struct job_t *job_list, pid_t pid, int state, char *cmdline);
 87 int deletejob(struct job_t *job_list, pid_t pid);
 88 pid_t fgpid(struct job_t *job_list);
 89 struct job_t *getjobpid(struct job_t *job_list, pid_t pid);
 90 struct job_t *getjobjid(struct job_t *job_list, int jid);
 91 int pid2jid(pid_t pid);
 92 void listjobs(struct job_t *job_list, int output_fd);
 93 
 94 void usage(void);
 95 void unix_error(char *msg);
 96 void app_error(char *msg);
 97 typedef void handler_t(int);
 98 handler_t *Signal(int signum, handler_t *handler);
 99 
100 /*
101  * main - The shell's main routine
102  */
103 int main(int argc, char **argv) {
104     char c;
105     char cmdline[MAXLINE]; /* cmdline for fgets */
106     int emit_prompt = 1; /* emit prompt (default) */
107 
108     /* Redirect stderr to stdout (so that driver will get all output
109      * on the pipe connected to stdout) */
110     dup2(1, 2);
111 
112     /* Parse the command line */
113     while ((c = getopt(argc, argv, "hvp")) != EOF) {
114         switch (c) {
115         case 'h': /* print help message */
116             usage();
117             break;
118         case 'v': /* emit additional diagnostic info */
119             verbose = 1;
120             break;
121         case 'p': /* don't print a prompt */
122             emit_prompt = 0; /* handy for automatic testing */
123             break;
124         default:
125             usage();
126         }
127     }
128 
129     /* Install the signal handlers */
130 
131     /* These are the ones you will need to implement */
132     Signal(SIGINT, sigint_handler); /* ctrl-c */
133     Signal(SIGTSTP, sigtstp_handler); /* ctrl-z */
134     Signal(SIGCHLD, sigchld_handler); /* Terminated or stopped child */
135     Signal(SIGTTIN, SIG_IGN);
136     Signal(SIGTTOU, SIG_IGN);
137 
138     /* This one provides a clean way to kill the shell */
139     Signal(SIGQUIT, sigquit_handler);
140 
141     /* Initialize the job list */
142     initjobs(job_list);
143 
144     /* Execute the shell's read/eval loop */
145     while (1) {
146 
147         if (emit_prompt) {
148             printf("%s", prompt);
149             fflush(stdout);
150         }
151         if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
152             app_error("fgets error");
153         if (feof(stdin)) {
154             /* End of file (ctrl-d) */
155             printf("\n");
156             fflush(stdout);
157             fflush(stderr);
158             exit(0);
159         }
160 
161         /* Remove the trailing newline */
162         cmdline[strlen(cmdline) - 1] = '\0';
163 
164         /* Evaluate the command line */
165         eval(cmdline);
166 
167         fflush(stdout);
168         fflush(stdout);
169     }
170 
171     exit(0); /* control never reaches here */
172 }
173 
174 /*
175  * eval - Evaluate the command line that the user has just typed in
176  *
177  * If the user has requested a built-in command (quit, jobs, bg or fg)
178  * then execute it immediately. Otherwise, fork a child process and
179  * run the job in the context of the child. If the job is running in
180  * the foreground, wait for it to terminate and then return.  Note:
181  * each child process must have a unique process group ID so that our
182  * background children don't receive SIGINT (SIGTSTP) from the kernel
183  * when we type ctrl-c (ctrl-z) at the keyboard.
184  */
185 void eval(char *cmdline) {
186     int bg; /* should the job run in bg or fg? */
187     struct cmdline_tokens tok;
188 
189     /* Parse command line */
190     bg = parseline(cmdline, &tok);
191 
192     if (bg == -1)
193         return; /* parsing error */
194     if (tok.argv[0] == NULL)
195         return; /* ignore empty lines */
196 
197     int stdi,stdo;
198     stdi=dup(STDIN_FILENO);
199     stdo=dup(STDOUT_FILENO);
200 
201     int infg, outfg;
202     infg = -1;
203     outfg = -1;
204     if (tok.infile != NULL) {
205         infg = open(tok.infile, O_RDONLY, 0);
206         dup2(infg, STDIN_FILENO);
207     }
208     if (tok.outfile != NULL) {
209         outfg = open(tok.outfile, O_RDWR, 0);
210         dup2(outfg, STDOUT_FILENO);
211     }
212 
213     pid_t pid;
214     struct job_t *job;
215     sigset_t mask;
216     sigemptyset(&mask);
217     sigaddset(&mask, SIGCHLD);
218     sigaddset(&mask, SIGINT);
219     sigaddset(&mask, SIGTSTP);
220     if (tok.builtins == BUILTIN_NONE) {
221         sigprocmask(SIG_BLOCK, &mask, NULL);
222 
223         if ((pid = fork()) == 0) {
224             sigprocmask(SIG_UNBLOCK, &mask, NULL);
225             setpgid(0, 0);
229 
230             execve(tok.argv[0], tok.argv, environ);
231 
232             if(infg!=-1)
233                 close(infg);
234             if(outfg!=-1)
235                 close(outfg);
236 
237         } else {
238 
239             addjob(job_list, pid, bg + 1, cmdline);
240             job = getjobpid(job_list, pid);
241             sigprocmask(SIG_UNBLOCK, &mask, NULL);
242 
243             sigemptyset(&mask);
244             if (!bg) {
245                 while(pid==fgpid(job_list))
246                     sleep(0);
247             } else {
248                 printf("[%d] (%d) %s\n", job->jid, pid, job->cmdline);
249             }
250         }
251 
252     } else {
253         if(tok.builtins==BUILTIN_QUIT)
254             exit(0);
255         else if(tok.builtins==BUILTIN_JOBS) {
256             listjobs(job_list,STDOUT_FILENO);
257         }
258         else{
259             int jid;
260             if(tok.argv[1][0]=='%')
261                 jid=atoi((tok.argv[1])+sizeof(char));
262             else
263                 jid=pid2jid(atoi(tok.argv[1]));
264             job=getjobjid(job_list,jid);
265             if(tok.builtins==BUILTIN_BG) {
266                 printf("[%d] (%d) %s\n",job->jid,job->pid,job->cmdline);
267                 job->state=BG;
268                 kill(-(job->pid),SIGCONT);
269             } else {
270                 job->state=FG;
271                 kill(-(job->pid),SIGCONT);
272             }
273         }
274     }
275 
276     dup2(stdi, STDIN_FILENO);
277     dup2(stdo, STDOUT_FILENO);
278     if(infg!=-1)
279         close(infg);
280     if(outfg!=-1)
281         close(outfg);
282     return;
283 }
284 
285 /*
286  * parseline - Parse the command line and build the argv array.
287  *
288  * Parameters:
289  *   cmdline:  The command line, in the form:
290  *
291  *                command [arguments...] [< infile] [> oufile] [&]
292  *
293  *   tok:      Pointer to a cmdline_tokens structure. The elements of this
294  *             structure will be populated with the parsed tokens. Characters
295  *             enclosed in single or double quotes are treated as a single
296  *             argument.
297  * Returns:
298  *   1:        if the user has requested a BG job
299  *   0:        if the user has requested a FG job
300  *  -1:        if cmdline is incorrectly formatted
301  *
302  * Note:       The string elements of tok (e.g., argv[], infile, outfile)
303  *             are statically allocated inside parseline() and will be
304  *             overwritten the next time this function is invoked.
305  */
306 int parseline(const char *cmdline, struct cmdline_tokens *tok) {
307 
308     static char array[MAXLINE]; /* holds local copy of command line */
309     const char delims[10] = " \t\r\n"; /* argument delimiters (white-space) */
310     char *buf = array; /* ptr that traverses command line */
311     char *next; /* ptr to the end of the current arg */
312     char *endbuf; /* ptr to the end of the cmdline string */
313     int is_bg; /* background job? */
314 
315     int parsing_state; /* indicates if the next token is the
316      input or output file */
317 
318     if (cmdline == NULL) {
319         (void) fprintf(stderr, "Error: command line is NULL\n");
320         return -1;
321     }
322 
323     (void) strncpy(buf, cmdline, MAXLINE);
324     endbuf = buf + strlen(buf);
325 
326     tok->infile = NULL;
327     tok->outfile = NULL;
328 
329     /* Build the argv list */
330     parsing_state = ST_NORMAL;
331     tok->argc = 0;
332 
333     while (buf < endbuf) {
334         /* Skip the white-spaces */
335         buf += strspn(buf, delims);
336         if (buf >= endbuf)
337             break;
338 
339         /* Check for I/O redirection specifiers */
340         if (*buf == '<') {
341             if (tok->infile) {
342                 (void) fprintf(stderr, "Error: Ambiguous I/O redirection\n");
343                 return -1;
344             }
345             parsing_state |= ST_INFILE;
346             buf++;
347             continue;
348         }
349         if (*buf == '>') {
350             if (tok->outfile) {
351                 (void) fprintf(stderr, "Error: Ambiguous I/O redirection\n");
352                 return -1;
353             }
354             parsing_state |= ST_OUTFILE;
355             buf++;
356             continue;
357         }
358 
359         if (*buf == '\'' || *buf == '\"') {
360             /* Detect quoted tokens */
361             buf++;
362             next = strchr(buf, *(buf - 1));
363         } else {
364             /* Find next delimiter */
365             next = buf + strcspn(buf, delims);
366         }
367 
368         if (next == NULL) {
369             /* Returned by strchr(); this means that the closing
370              quote was not found. */
371             (void) fprintf(stderr, "Error: unmatched %c.\n", *(buf - 1));
372             return -1;
373         }
374 
375         /* Terminate the token */
376         *next = '\0';
377 
378         /* Record the token as either the next argument or the input/output file */
379         switch (parsing_state) {
380         case ST_NORMAL:
381             tok->argv[tok->argc++] = buf;
382             break;
383         case ST_INFILE:
384             tok->infile = buf;
385             break;
386         case ST_OUTFILE:
387             tok->outfile = buf;
388             break;
389         default:
390             (void) fprintf(stderr, "Error: Ambiguous I/O redirection\n");
391             return -1;
392         }
393         parsing_state = ST_NORMAL;
394 
395         /* Check if argv is full */
396         if (tok->argc >= MAXARGS - 1)
397             break;
398 
399         buf = next + 1;
400     }
401 
402     if (parsing_state != ST_NORMAL) {
403         (void) fprintf(stderr,
404                 "Error: must provide file name for redirection\n");
405         return -1;
406     }
407 
408     /* The argument list must end with a NULL pointer */
409     tok->argv[tok->argc] = NULL;
410 
411     if (tok->argc == 0) /* ignore blank line */
412         return 1;
413 
414     if (!strcmp(tok->argv[0], "quit")) { /* quit command */
415         tok->builtins = BUILTIN_QUIT;
416     } else if (!strcmp(tok->argv[0], "jobs")) { /* jobs command */
417         tok->builtins = BUILTIN_JOBS;
418     } else if (!strcmp(tok->argv[0], "bg")) { /* bg command */
419         tok->builtins = BUILTIN_BG;
420     } else if (!strcmp(tok->argv[0], "fg")) { /* fg command */
421         tok->builtins = BUILTIN_FG;
422     } else {
423         tok->builtins = BUILTIN_NONE;
424     }
425 
426     /* Should the job run in the background? */
427     if ((is_bg = (*tok->argv[tok->argc - 1] == '&')) != 0)
428         tok->argv[--tok->argc] = NULL;
429 
430     return is_bg;
431 }
432 
433 /*****************
434  * Signal handlers
435  *****************/
436 
437 /*
438  * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
439  *     a child job terminates (becomes a zombie), or stops because it
440  *     received a SIGSTOP, SIGTSTP, SIGTTIN or SIGTTOU signal. The
441  *     handler reaps all available zombie children, but doesn't wait
442  *     for any other currently running children to terminate.
443  */
444 void sigchld_handler(int sig) {
445     pid_t pid;
446     int status;
447     while ((pid = waitpid(-1, &status, WUNTRACED | WNOHANG)) > 0) {
448         if (WIFSTOPPED(status)) {
449             int jid=pid2jid(pid);
450             if(jid!=0) {
451                 printf("Job [%d] (%d) stopped by signal %d\n",jid,pid,WSTOPSIG(status));
452                 (getjobpid(job_list,pid))->state=ST;
453             }
454         } else if (WIFSIGNALED(status)) {
455             if (WTERMSIG(status) == SIGINT) {
456                 int jid=pid2jid(pid);
457                 if(jid!=0) {
458                     printf("Job [%d] (%d) terminated by signal %d\n",jid,pid,SIGINT);
459                     deletejob(job_list,pid);
460                 }
461             }
462             else
463                 deletejob(job_list, pid);
464         } else
465             deletejob(job_list, pid);
466     }
467     return;
468 }
469 
470 /*
471  * sigint_handler - The kernel sends a SIGINT to the shell whenver the
472  *    user types ctrl-c at the keyboard.  Catch it and send it along
473  *    to the foreground job.
474  */
475 void sigint_handler(int sig) {
476     pid_t pid = fgpid(job_list);
477     if (pid != 0) {
478         kill(-pid, sig);
479     }
480     return;
481 }
482 
483 /*
484  * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
485  *     the user types ctrl-z at the keyboard. Catch it and suspend the
486  *     foreground job by sending it a SIGTSTP.
487  */
488 void sigtstp_handler(int sig) {
489     pid_t pid = fgpid(job_list);
490     if (pid != 0) {
491         kill(-pid, sig);
492     }
493     return;
494 }
495 
496 /*********************
497  * End signal handlers
498  *********************/
499 
500 /***********************************************
501  * Helper routines that manipulate the job list
502  **********************************************/
503 
504 /* clearjob - Clear the entries in a job struct */
505 void clearjob(struct job_t *job) {
506     job->pid = 0;
507     job->jid = 0;
508     job->state = UNDEF;
509     job->cmdline[0] = '\0';
510 }
511 
512 /* initjobs - Initialize the job list */
513 void initjobs(struct job_t *job_list) {
514     int i;
515 
516     for (i = 0; i < MAXJOBS; i++)
517         clearjob(&job_list[i]);
518 }
519 
520 /* maxjid - Returns largest allocated job ID */
521 int maxjid(struct job_t *job_list) {
522     int i, max = 0;
523 
524     for (i = 0; i < MAXJOBS; i++)
525         if (job_list[i].jid > max)
526             max = job_list[i].jid;
527     return max;
528 }
529 
530 /* addjob - Add a job to the job list */
531 int addjob(struct job_t *job_list, pid_t pid, int state, char *cmdline) {
532     int i;
533 
534     if (pid < 1)
535         return 0;
536 
537     for (i = 0; i < MAXJOBS; i++) {
538         if (job_list[i].pid == 0) {
539             job_list[i].pid = pid;
540             job_list[i].state = state;
541             job_list[i].jid = nextjid++;
542             if (nextjid > MAXJOBS)
543                 nextjid = 1;
544             strcpy(job_list[i].cmdline, cmdline);
545             if (verbose) {
546                 printf("Added job [%d] %d %s\n", job_list[i].jid,
547                         job_list[i].pid, job_list[i].cmdline);
548             }
549             return 1;
550         }
551     }
552     printf("Tried to create too many jobs\n");
553     return 0;
554 }
555 
556 /* deletejob - Delete a job whose PID=pid from the job list */
557 int deletejob(struct job_t *job_list, pid_t pid) {
558     int i;
559 
560     if (pid < 1)
561         return 0;
562 
563     for (i = 0; i < MAXJOBS; i++) {
564         if (job_list[i].pid == pid) {
565             clearjob(&job_list[i]);
566             nextjid = maxjid(job_list) + 1;
567             return 1;
568         }
569     }
570     return 0;
571 }
572 
573 /* fgpid - Return PID of current foreground job, 0 if no such job */
574 pid_t fgpid(struct job_t *job_list) {
575     int i;
576 
577     for (i = 0; i < MAXJOBS; i++)
578         if (job_list[i].state == FG)
579             return job_list[i].pid;
580     return 0;
581 }
582 
583 /* getjobpid  - Find a job (by PID) on the job list */
584 struct job_t *getjobpid(struct job_t *job_list, pid_t pid) {
585     int i;
586 
587     if (pid < 1)
588         return NULL;
589     for (i = 0; i < MAXJOBS; i++)
590         if (job_list[i].pid == pid)
591             return &job_list[i];
592     return NULL;
593 }
594 
595 /* getjobjid  - Find a job (by JID) on the job list */
596 struct job_t *getjobjid(struct job_t *job_list, int jid) {
597     int i;
598 
599     if (jid < 1)
600         return NULL;
601     for (i = 0; i < MAXJOBS; i++)
602         if (job_list[i].jid == jid)
603             return &job_list[i];
604     return NULL;
605 }
606 
607 /* pid2jid - Map process ID to job ID */
608 int pid2jid(pid_t pid) {
609     int i;
610 
611     if (pid < 1)
612         return 0;
613     for (i = 0; i < MAXJOBS; i++)
614         if (job_list[i].pid == pid) {
615             return job_list[i].jid;
616         }
617     return 0;
618 }
619 
620 /* listjobs - Print the job list */
621 void listjobs(struct job_t *job_list, int output_fd) {
622     int i;
623     char buf[MAXLINE];
624 
625     for (i = 0; i < MAXJOBS; i++) {
626         memset(buf, '\0', MAXLINE);
627         if (job_list[i].pid != 0) {
628             sprintf(buf, "[%d] (%d) ", job_list[i].jid, job_list[i].pid);
629             if (write(output_fd, buf, strlen(buf)) < 0) {
630                 fprintf(stderr, "Error writing to output file\n");
631                 exit(1);
632             }
633             memset(buf, '\0', MAXLINE);
634             switch (job_list[i].state) {
635             case BG:
636                 sprintf(buf, "Running    ");
637                 break;
638             case FG:
639                 sprintf(buf, "Foreground ");
640                 break;
641             case ST:
642                 sprintf(buf, "Stopped    ");
643                 break;
644             default:
645                 sprintf(buf, "listjobs: Internal error: job[%d].state=%d ", i,
646                         job_list[i].state);
647             }
648             if (write(output_fd, buf, strlen(buf)) < 0) {
649                 fprintf(stderr, "Error writing to output file\n");
650                 exit(1);
651             }
652             memset(buf, '\0', MAXLINE);
653             sprintf(buf, "%s\n", job_list[i].cmdline);
654             if (write(output_fd, buf, strlen(buf)) < 0) {
655                 fprintf(stderr, "Error writing to output file\n");
656                 exit(1);
657             }
658         }
659     }
660     if (output_fd != STDOUT_FILENO)
661         close(output_fd);
662 }
663 /******************************
664  * end job list helper routines
665  ******************************/
666 
667 /***********************
668  * Other helper routines
669  ***********************/
670 
671 /*
672  * usage - print a help message
673  */
674 void usage(void) {
675     printf("Usage: shell [-hvp]\n");
676     printf("   -h   print this message\n");
677     printf("   -v   print additional diagnostic information\n");
678     printf("   -p   do not emit a command prompt\n");
679     exit(1);
680 }
681 
682 /*
683  * unix_error - unix-style error routine
684  */
685 void unix_error(char *msg) {
686     fprintf(stdout, "%s: %s\n", msg, strerror(errno));
687     exit(1);
688 }
689 
690 /*
691  * app_error - application-style error routine
692  */
693 void app_error(char *msg) {
694     fprintf(stdout, "%s\n", msg);
695     exit(1);
696 }
697 
698 /*
699  * Signal - wrapper for the sigaction function
700  */
701 handler_t *Signal(int signum, handler_t *handler) {
702     struct sigaction action, old_action;
703 
704     action.sa_handler = handler;
705     sigemptyset(&action.sa_mask); /* block sigs of type being handled */
706     action.sa_flags = SA_RESTART; /* restart syscalls if possible */
707 
708     if (sigaction(signum, &action, &old_action) < 0)
709         unix_error("Signal error");
710     return (old_action.sa_handler);
711 }
712 
713 /*
714  * sigquit_handler - The driver program can gracefully terminate the
715  *    child shell by sending it a SIGQUIT signal.
716  */
717 void sigquit_handler(int sig) {
718     printf("Terminating after receipt of SIGQUIT signal\n");
719     exit(1);
720 }