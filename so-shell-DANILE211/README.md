# SO-shell
Template for the shell project in Operating Systems course at tcs@JU.

issue#2

if(HowManyBytes==-1)
{
printf("read() faled. Errno code of problem: %d\n", errno); // exit
exit(EXEC_FAILURE);
}

issue#8
void signal_handler(int signo){
    int temp_errno = errno;
    //code here may change the errno
    errno = temp_errno;
}

4-th test of builtin (lls) test not passing. But this is the issue of order.