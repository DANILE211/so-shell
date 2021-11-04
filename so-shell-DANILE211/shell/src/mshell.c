#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include<sys/wait.h>
#include <string.h>
#include "config.h"
#include "siparse.h"
#include "utils.h"
#include <errno.h>
#include <sys/stat.h>
#include <stdbool.h>
#include "builtins.h"
#include <fcntl.h>
// global variables init with 0;
bool wasInHandler=false;
background_info bInfo[ZOMBIEBUFF];
pid_t ListOfProcess[ZOMBIEBUFF];
volatile int CounterOfProcess;
/*
 volatile tells the compiler that your variable may be changed by other means,
 than the code that is accessing it. e.g., it may be a I/O-mapped memory location.
 If this is not specified in such cases, some variable accesses can be optimised, e.g.,
 its contents can be held in a register, and the memory location not read back in again.
 */
//preparations on global structures
void handler2(int sig_nb) {
    wasInHandler=true;
}
void handler(int sig_nb) {
    int temp_errno = errno;
    pid_t child;
    int status;
    bool IsInForeground = false;
    do {
        child = waitpid(-1, &status, WNOHANG);
        /*if WNOHANG was specified and one or
         * more child(ren) specified by pid exist,
         * but have not yet changed state, then 0 is returned*/
        if(child==0) continue;

        for (int i = 0; i < ZOMBIEBUFF; i++) {
            if (ListOfProcess[i] == child) {
                IsInForeground = true;
                ListOfProcess[i] = -2;
                break;
            }
        }
        if (IsInForeground) {
            CounterOfProcess--;
            IsInForeground=false;
        }
        else{
            if(child!=-1){
                int j = 0;
                while (bInfo[j].pid != -1 && bInfo[j].info != -1) j++;
                if (j + 1 < ZOMBIEBUFF) {
                    bInfo[j].pid = child;
                    bInfo[j].info = status;
                    bInfo[j + 1].pid = -1;
                    bInfo[j + 1].info = -1;
                } //else forget about them as in doc
            }
        }
    } while (child > 0);
    errno = temp_errno;
}
void PrintError(const char* Name){
    if(errno==ENOENT)
        fprintf(stderr, "%s: no such file or directory\n", Name);
    else if(errno==EACCES)
        fprintf(stderr, "%s: permission denied\n", Name);
    else fprintf(stderr, "%s: exec error\n", Name);
}
int CountArguments(command *com){
    int k=0;
    argseq * argseq = com->args;
    do{
        k++;
        argseq= argseq->next;
    }while(argseq!=com->args);
    return k;
}
void FillExecutedTab(char**tab, command* com, int k){
    argseq * argseq = com->args;
    for(int i=0;i<k;i++){
        tab[i]=argseq->arg;
        argseq= argseq->next;
    }
    tab[k]=NULL;
}
void ExecuteCommand(command *com){
    int k=CountArguments(com);
    char* tab[k+1];
    FillExecutedTab(tab,com,k);
    //variables
    redirseq * redirseq = com->redirs;
    int WhatToDo=-1;
    const char *pathToFile;

    if(redirseq!=NULL){
        do{
            pathToFile=redirseq->r->filename;
            if(IS_RIN(redirseq->r->flags)){
                int k=close(STDIN); // close fd inherited from parent
                if(k==-1){
                    printf("Close didnt work\n");
                    exit(EXEC_FAILURE);
                }
                WhatToDo=open(pathToFile, O_RDONLY);
                if(WhatToDo==-1){PrintError(pathToFile);exit(EXEC_FAILURE);}
            } // I have to change input
            if(IS_ROUT(redirseq->r->flags)){ // it works
                int k=close(STDOUT); // close fd inherited from parent
                if(k==-1){
                    printf("Close didnt work\n");
                    exit(EXEC_FAILURE);
                }
                WhatToDo=open(pathToFile,O_TRUNC|O_CREAT|O_RDWR,S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                if(WhatToDo==-1){PrintError(pathToFile);exit(EXEC_FAILURE);}
            }
            if(IS_RAPPEND(redirseq->r->flags)){
                int k=close(STDOUT); // close fd inherited from parent
                if(k==-1){
                    printf("Close didnt work\n");
                    exit(EXEC_FAILURE);
                }
                WhatToDo=open(pathToFile,O_APPEND|O_CREAT|O_RDWR,S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                if(WhatToDo==-1){PrintError(pathToFile);exit(EXEC_FAILURE);}
            }
            redirseq=redirseq->next;
        }while (redirseq != com->redirs);
    }
    if(execvp(tab[0],tab)==-1){
        PrintError(tab[0]);
        exit(EXEC_FAILURE);
    }else exit(0);
}
bool BuiltType(command *com) {
    int k=CountArguments(com);
    char* tab[k+1];
    FillExecutedTab(tab,com,k);

    int w = 0;
    bool WasBuiltInType = false;
    builtin_pair helper = builtins_table[w];
    while (helper.name != NULL) {
        if (!strcmp(tab[0], helper.name)) {
            helper.fun(tab);
            WasBuiltInType = true;
            break;
        } else {
            helper = builtins_table[++w];
        }
    }
    return  WasBuiltInType;
}
bool SeekEmptyCommand(pipelineseq * ln) {
    bool toReturn = false;
    pipelineseq *curr = ln;
    pipeline *currPipeline;
    commandseq *currCommandseq;
    command *commandToExecute;
    do {
        currPipeline = curr->pipeline;
        currCommandseq = currPipeline->commands;
        do {
            commandToExecute = currCommandseq->com;
            if (commandToExecute == NULL) {
                toReturn = true;
                break;
            }
            currCommandseq = currCommandseq->next;
        } while (currCommandseq != currPipeline->commands);
        curr = curr->next;
        if(toReturn) break;
    } while (curr != ln); // iterates over a whole line
    return toReturn;
}
void ForkingAndExecutingFORONE(pipelineseq * ln)
{
    if(ln==NULL){ // simpler solution
        fprintf(stderr, SYNTAX_ERROR_STR);
        return;
    }
    command *com=pickfirstcommand(ln); // com is null if sth went wrong
    bool back = (ln->pipeline->flags == INBACKGROUND);
    bool WasBuiltIn=false;
    if (com != NULL && !back) WasBuiltIn = BuiltType(com);

    if(com != NULL && !WasBuiltIn){
        int whatToDo=fork();
        if(whatToDo==0)// i am child
        {
            if(back) {
                setsid();
            }
            struct sigaction act;
            act.sa_handler=SIG_DFL;
            act.sa_flags = 0;
            sigemptyset(&act.sa_mask);
            sigaction(SIGINT,&act,NULL);
            ExecuteCommand(com);
        }
        else if (whatToDo==-1) { // creation of child failed; no child created
            printf("fork() failed. Abortng this command. Errno code of problem: %d\n", errno);
            exit(EXEC_FAILURE);
        }else {
            if (!back) {
                int p=0;
                while(ListOfProcess[p]!=-2) p++;
                ListOfProcess[p]=whatToDo;
                CounterOfProcess++;
            }
            //after working with arrays
            /*
            sigsuspend() temporarily replaces the signal mask of the calling
            thread with the mask given by mask and then suspends the thread until
            delivery of a signal whose action is to invoke a signal handler or to
            terminate a process.
             */
            sigset_t mask;
            sigemptyset(&mask);
            sigfillset(&mask);
            sigdelset(&mask, SIGCHLD);
            while (CounterOfProcess > 0) {
                sigsuspend(&mask);
            }

        } // i am parent
    }
}
void ForkingAndExecuting(pipelineseq * ln) {
    if (ln == NULL) { // simpler solution
        fprintf(stderr, SYNTAX_ERROR_STR);
        return;
    }
    //pointers
    pipelineseq *curr = ln;
    pipeline *currPipeline;
    commandseq *currCommandseq;
    command *commandToExecute;
    bool emptyCommand = SeekEmptyCommand(curr);
    if(emptyCommand) return;
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, NULL);
    //while working here (changing my structures) sihchild is blocked
    do {
        currPipeline = curr->pipeline;
        currCommandseq = currPipeline->commands;
        if (currPipeline->commands->next==currPipeline->commands) { // only one command
            ForkingAndExecutingFORONE(curr);
        }else {
            int last=-1;
            int fdNEW[2];
            bool back = (currPipeline->flags == INBACKGROUND); // get to know what type pipelinie is

            do {
                if (!back) CounterOfProcess++;
                commandToExecute = currCommandseq->com;
                if (currCommandseq->next != currPipeline->commands){
                    if (-1 == pipe(fdNEW)) exit(EXEC_FAILURE); // creating pipe
                }
                int whatToDo = fork();
                if (!whatToDo) { // i am child
                    if(back) {
                        setsid();
                    }
                    struct sigaction act;
                    act.sa_handler=SIG_DFL;
                    act.sa_flags = 0;
                    sigemptyset(&act.sa_mask);
                    sigaction(SIGINT,&act,NULL);
                    if (currCommandseq == currPipeline->commands) {//first fork
                        // leave read as in shell
                        close(STDOUT); // it happens locally for this child
                        close(fdNEW[0]); // we dont need to read from this pipe
                        dup2(fdNEW[1], STDOUT); // change write to writing into pipe
                        close(fdNEW[1]);
                        ExecuteCommand(commandToExecute);
                    } else if (currCommandseq->next == currPipeline->commands) {//last fork
                        // leave write as in shell
                        dup2(last, STDIN); // change read
                        close(last);
                        ExecuteCommand(commandToExecute);
                    } else {
                        close(fdNEW[0]); //dont need it
                        dup2(last, STDIN);
                        dup2(fdNEW[1], STDOUT);
                        close(last);
                        close(fdNEW[1]);
                        ExecuteCommand(commandToExecute);
                    }
                }
                if(last!=-1) close(last);
                close(fdNEW[1]);
                last = fdNEW[0];
                currCommandseq = currCommandseq->next;
                if(!back){
                    int where = 0;
                    while (ListOfProcess[where] != -2) where++;
                    ListOfProcess[where] = whatToDo;
                }
            } while (currCommandseq != currPipeline->commands);
        }
        sigfillset(&mask);
        sigdelset(&mask, SIGCHLD);
        while (CounterOfProcess > 0) {
            sigsuspend(&mask);
        }
        curr = curr->next;
    } while (curr != ln); // iterates over a whole line
    sigset_t mask2;
    sigemptyset(&mask2);
    sigaddset(&mask2, SIGCHLD);
    sigprocmask(SIG_UNBLOCK, &mask2, NULL);
}
int main(int argc, char *argv[]){
    // variables
    pipelineseq * ln;
    char buff[BUFF_LENGTH+2];
    struct stat check;
    int LengthOfCommand=1;

    // checking if working with terminal
    int toDo=fstat(STDIN, &check);
    if(toDo==-1) exit(EXEC_FAILURE); // didnt work // exit
    if(S_ISCHR(check.st_mode)) write(STDOUT, PROMPT_STR, sizeof(PROMPT_STR));

    // preparation of structures
    for(int k=0;k<ZOMBIEBUFF;k++)
        ListOfProcess[k]=-2;
    bInfo[0].pid=-1; bInfo[0].info=-1;

    //preparations for signal
    struct sigaction act;
    act.sa_handler=handler;
    act.sa_flags = SA_RESTART;
    sigemptyset(&act.sa_mask);
    sigaction(SIGCHLD,&act,NULL);

    struct sigaction act2;
    act2.sa_handler=handler2;
    act2.sa_flags = SA_RESTART;
    sigemptyset(&act2.sa_mask);
    sigaction(SIGINT,&act2,NULL);

    //which case
    bool CASE1=false;
    bool RESTART=false;
    bool RESTART_K=false;


    //first read
    int HowManyBytes=read(STDIN,buff,MAX_LINE_LENGTH);
    if(HowManyBytes==-1) {
        printf("read() failed. Errno code of problem: %d\n", errno); // exit
        exit(EXEC_FAILURE);
    }


    //indicators for parsing buff
    char *ComToExecute=buff;
    char *Nseeker=ComToExecute;
    char *EndOfRead=(buff+HowManyBytes-1); // it may be '\n' or char of incoming command in next read
    int k;

    while(HowManyBytes){ // global checking if we headed to EOF
        k=0;
        while(EndOfRead<=buff+MAX_LINE_LENGTH){
            while(Nseeker!=EndOfRead && *Nseeker != '\n' && LengthOfCommand<=MAX_LINE_LENGTH){
                Nseeker++;
                LengthOfCommand++;
            }
            if(*Nseeker == '\n' && Nseeker!=EndOfRead){ // legit command to do
                *Nseeker = '\0';
                ln=parseline(ComToExecute);
                //function works with ln=NULL // if com==NULL (perhaps comment) then do nothing
                ForkingAndExecuting(ln);

                // preparations for another iteration
                ComToExecute=(++Nseeker);
                LengthOfCommand=1;
                continue;
            }
            // if(LengthOfCommand==MAX_LINE_LENGTH){} // not necessary
            if(Nseeker==EndOfRead){
                if((*EndOfRead)=='\n'){ // perfect matching
                    *Nseeker = '\0';
                    ln=parseline(ComToExecute);
                    ForkingAndExecuting(ln);
                    ComToExecute=(++Nseeker);
                    LengthOfCommand=1;  // important
                    CASE1=true;
                }else{ // let him read more
                    Nseeker++;
                    CASE1=true;
                }
                break;
            }
        }
        while(EndOfRead>buff+MAX_LINE_LENGTH){ //CASE 2
            while(Nseeker!=EndOfRead && *Nseeker != '\n' && LengthOfCommand<=MAX_LINE_LENGTH){
                Nseeker++;
                LengthOfCommand++;
            }
            if(*Nseeker == '\n' && Nseeker!=EndOfRead){ // legit command to do
                *Nseeker = '\0';
                ln=parseline(ComToExecute);
                //function works with ln=NULL // if com==NULL (perhaps comment) then do nothing
                ForkingAndExecuting(ln);
                // preparations for another iteration
                ComToExecute=(++Nseeker);
                LengthOfCommand=1;
                continue;
            }
            if(LengthOfCommand==MAX_LINE_LENGTH+1){
                while(Nseeker!=EndOfRead && *Nseeker!='\n'){
                    Nseeker++;
                    //command is too long anyway, I am seeking if after this command is sth I want to execute
                }
                if(*Nseeker=='\n' && Nseeker != EndOfRead) { // have sth to do
                    Nseeker++;
                    while (Nseeker != EndOfRead + 1) { // writing at the begin // +1 important
                        buff[k] = (*Nseeker);
                        Nseeker++, k++;
                    }
                    RESTART_K = true;
                }
                if(Nseeker==EndOfRead){
                    int i;
                    char temp[1];
                    do{
                        i=read(STDIN,temp,1);
                        if(i==0) break;
                    }while (temp[0]!='\n');
                    RESTART=true;
                }
                fprintf(stderr, SYNTAX_ERROR_STR);
                break;
            }
            if(Nseeker==EndOfRead) {
                if ((*EndOfRead) == '\n') { // perfect matching
                    *Nseeker = '\0';
                    ln = parseline(ComToExecute);
                    ForkingAndExecuting(ln);
                    RESTART=true;
                } else {
                    while (ComToExecute != EndOfRead + 1) {
                        buff[k] = (*ComToExecute);
                        k++, ComToExecute++;
                    }
                    RESTART_K=true;
                }
                break;
            }
        }

        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGCHLD);
        sigprocmask(SIG_BLOCK, &mask, NULL);
        if(S_ISCHR(check.st_mode)){
            int i=0;
            bool wasInFile=false;
            while(bInfo[i].pid!=-1 && bInfo[i].info!=-1){
                if(bInfo[i].info==0) printf("Background process %d terminated. (exited with status %d)\n",bInfo[i].pid, bInfo[i].info);
                else printf("Background process %d terminated. (killed by signal %d)\n",bInfo[i].pid, bInfo[i].info);
                i++;
                wasInFile=true;
            }
            if(wasInFile) {
                for(int i=0;i<ZOMBIEBUFF;i++){bInfo[i].pid=0; bInfo[i].info=0;}
                bInfo[0].pid=-1; bInfo[0].info=-1;
            }
            if(!wasInHandler) {
                write(STDOUT, PROMPT_STR, sizeof(PROMPT_STR));
            }else{
                wasInHandler=false;
            }
        }
        sigprocmask(SIG_UNBLOCK, &mask, NULL);

        if(CASE1) {
            HowManyBytes=read(STDIN,EndOfRead+1,MAX_LINE_LENGTH);
            EndOfRead=Nseeker+HowManyBytes-1;
            CASE1=false;
        }
        if(RESTART){
            HowManyBytes=read(STDIN,buff,MAX_LINE_LENGTH);
            ComToExecute=buff;
            Nseeker=ComToExecute;
            LengthOfCommand=1;
            EndOfRead=ComToExecute+HowManyBytes-1;
            RESTART=false;
        }
        if(RESTART_K){
            HowManyBytes=read(STDIN,(buff+k),MAX_LINE_LENGTH);
            ComToExecute=buff;
            Nseeker=ComToExecute;
            EndOfRead=buff+k+HowManyBytes-1;
            LengthOfCommand=1;
            RESTART_K=false;
        }
        if(HowManyBytes==-1) {
            printf("read() faled. Errno code of problem: %d\n", errno); // exit
            exit(EXEC_FAILURE);
        }
    }
    return 0;
}

