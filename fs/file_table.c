/*
 *  linux/fs/file_table.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include "../include/linux/fs.h"

struct file file_table[NR_FILE];