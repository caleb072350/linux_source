/*
*  linux/lib/setsid.c
*
*  (C) 1991  Linus Torvalds
*/

#define __LIBRARY__
#include "../include/unistd.h"

_syscall0(pid_t,setsid)