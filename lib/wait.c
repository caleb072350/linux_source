/*
*  linux/lib/wait.c
*
*  (C) 1991  Linus Torvalds
*/

#define __LIBRARY__
#include "../include/unistd.h"
#include "../include/sys/wait.h"

_syscall3(pid_t,waitpid,pid_t,pid,int *,wait_stat,int,options)

pid_t wait(int * wait_stat)
{
    return waitpid(-1,wait_stat,0);
}
