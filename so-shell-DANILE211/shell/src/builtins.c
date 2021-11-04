#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "builtins.h"
#include <sys/types.h>
#include <signal.h>
#include <dirent.h>

#define BUFF 512

int echo(char*[]);
int lexit(char * argv[]);
int lls(char * argv[]);
int lkill(char * argv[]);
int cd(char * argv[]);

builtin_pair builtins_table[]={
        {"exit",	&lexit},
        {"lecho",	&echo},
        {"lcd",		&cd},
        {"cd",		&cd},
        {"lkill",	&lkill},
        {"lls",		&lls},
        {NULL,NULL}
};

int echo( char * argv[])
{
	int i =1;
	if (argv[i]) printf("%s", argv[i++]);
	while  (argv[i])
		printf(" %s", argv[i++]);

	printf("\n");
	fflush(stdout);
	return 0;
}

int lexit(char * argv[]) {
    exit(0);
}

int cd(char * argv[]) {
    const char *s = getenv("HOME");

    int HowManyArguments=-1;
    int iter=0;
    while(argv[iter] != NULL){HowManyArguments++; iter++;}
    if(HowManyArguments>1){
        fprintf(stderr,"Builtin lcd error.\n");
        return -1;
    }

    int WhatToDo;
    if (argv[1] == NULL) {
        WhatToDo=chdir(s);
    }else{
        WhatToDo=chdir(argv[1]);
    }
    if(WhatToDo==-1) fprintf(stderr,"Builtin lcd error.\n");
}

int lkill(char * argv[]) {
    //variables
    int HowManyArguments=-1;
    int iter=0;
    int sig;
    pid_t pid;
    int WhatToDo;

    while(argv[iter] != NULL){HowManyArguments++; iter++;}

    if (HowManyArguments==1) {
        sig = SIGTERM;
        pid=atoi(argv[1]);
    } else if(HowManyArguments==0){
        WhatToDo=-1;
    }
    else {
        char*temp=argv[1];
        sig=atoi(temp+1);
        pid=atoi(argv[2]);
    }
    // atoi returns 0 on error
    if(pid != 0 && sig !=0) WhatToDo = kill(pid, sig);
    if (WhatToDo == -1) fprintf(stderr, "Builtin lkill error.\n");
}

int lls(char * argv[]) {
    char cwd[BUFF];
    getcwd(cwd, BUFF);
    DIR *dp = opendir(cwd);
    struct dirent *ep;
    if (!dp) {
        fprintf(stderr, "Builtin lls error.\n");
        return -1;
    }
    char buff[BUFF];
    //printf("%s:\n", cwd);
    while ((ep = readdir(dp)))
        if (strncmp(ep->d_name, ".", 1))
            printf("%s\n", ep->d_name);
    closedir(dp);
    fflush(stdout);
    return 0;
}



