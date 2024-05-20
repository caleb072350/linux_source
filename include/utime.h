#ifndef UTIME_H
#define UTIME_H

#include "sys/types.h"

struct utimbuf {
    time_t actime;
    time_t modtime;
};

extern int utime(const char *filename, struct utimbuf *times);

#endif