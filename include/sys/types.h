#ifndef SYS_TYPES_H
#define SYS_TYPES_H

#ifndef SIZE_T 
#define SIZE_T
typedef unsigned int size_t;
#endif 

#ifndef TIME_T 
#define TIME_T 
typedef long time_t;
#endif

#ifndef PTRDIFF_T
#define PTRDIFF_T
typedef long ptrdiff_t;
#endif 

#ifndef NULL
#define NULL ((void*) 0)
#endif 

typedef int pid_t;
typedef unsigned short uid_t;
typedef unsigned char gid_t;
typedef unsigned short dev_t;
typedef unsigned short ino_t;
typedef unsigned short mode_t;
typedef unsigned short umode_t;
typedef unsigned char nlink_t;
typedef int daddr_t;
typedef long off_t;
typedef unsigned char u_char;
typedef unsigned short ushort;

typedef struct { int quot,rem;} div_t;
typedef struct { long quot,rem;} ldiv_t;

struct ustat {
    daddr_t f_tfree;
    ino_t tinode;
    char f_fname[6];
    char f_fpack[6];
};

#endif