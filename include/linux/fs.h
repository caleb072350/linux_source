/*
 * This file has definitions for some important file table
 * structures etc.
 */

#ifndef _FS_H
#define _FS_H

#include "../include/sys/types.h"

/* devices are as follows: (same as minix, so we can use the minix
 * file system. These are major numbers.)
 *
 * 0 - unused (nodev)
 * 1 - /dev/mem
 * 2 - /dev/fd
 * 3 - /dev/hd
 * 4 - /dev/ttyx
 * 5 - /dev/tty
 * 6 - /dev/lp
 * 7 - unnamed pipes
 */

#define IS_SEEKABLE(x) ((x) >= 1 && (x) <= 3)

#define READ 0
#define WRITE 1
#define READA 2 /* read-ahead - don't pause */

void buffer_init(long buffer_end);

#define MAJOR(a) (((unsigned)(a)) >> 8)  /* Linux 内核中，设备号由主设备号和次设备号组成，用于唯一标识设备，主设备号区分设备的类别，次设备号区分同类下的实例 */
#define MINOR(a) ((a) & 0xff)

#define NAME_LEN 14
#define ROOT_INO 1

#define I_MAP_SLOTS 8
#define Z_MAP_SLOTS 8
#define SUPER_MAGIC 0x137F

#define NR_OPEN 20
#define NR_INODE 32
#define NR_FILE 64
#define NR_SUPER 8
#define NR_HASH 307
#define NR_BUFFERS nr_buffers
#define BLOCK_SIZE 1024
#define BLOCK_SIZE_BITS 10
#ifndef NULL
#define NULL ((void *)0)
#endif

#define INODES_PER_BLOCK ((BLOCK_SIZE) / (sizeof(struct d_inode)))
#define DIR_ENTRIES_PER_BLOCK ((BLOCK_SIZE) / (sizeof(struct dir_entry)))

#define PIPE_HEAD(inode) ((inode).i_zone[0])
#define PIPE_TAIL(inode) ((inode).i_zone[1])
#define PIPE_SIZE(inode) ((PIPE_HEAD(inode) - PIPE_TAIL(inode)) & (PAGE_SIZE - 1))
#define PIPE_EMPTY(inode) (PIPE_HEAD(inode) == PIPE_TAIL(inode))
#define PIPE_FULL(inode) (PIPE_SIZE(inode) == (PAGE_SIZE - 1))
#define INC_PIPE(head) \
    __asm__("incl %0\n\tandl $4095,%0" ::"m"(head))

typedef char buffer_block[BLOCK_SIZE];

struct buffer_head
{
    char *b_data;             /* pointer to data block (1024 bytes) */
    unsigned short b_dev;     /* device (0 = free) */
    unsigned short b_blocknr; /* block number */
    unsigned char b_uptodate;
    unsigned char b_dirt;  /* 0-clean,1-dirty */
    unsigned char b_count; /* users using this block */
    unsigned char b_lock;  /* 0 - ok, 1 -locked */
    struct task_struct *b_wait;
    struct buffer_head *b_prev;
    struct buffer_head *b_next;
    struct buffer_head *b_prev_free;
    struct buffer_head *b_next_free;
};

struct d_inode
{
    unsigned short i_mode;
    unsigned short i_uid;
    unsigned long i_size;
    unsigned long i_time;
    unsigned char i_gid;
    unsigned char i_nlinks;
    unsigned short i_zone[9];
};

struct m_inode
{
    unsigned short i_mode; /* 文件或目录的访问权限信息 */
    unsigned short i_uid;  /* 文件或目录的所有者 */
    unsigned long i_size;  /* 文件大小 */
    unsigned long i_mtime; /* 文件或目录的修改时间 */
    unsigned char i_gid;   /* 组ID */
    unsigned char i_nlinks;   /* 指向该inode的硬链接数 */
    unsigned short i_zone[9]; /* 存储文件数据块的索引数组 */
    /* these are in memory also */
    struct task_struct *i_wait; /* 等待该inode的进程队列头指针 */
    unsigned long i_atime;   /* 最近的访问时间 */
    unsigned long i_ctime;   /* 创建时间 */
    unsigned short i_dev;    /* 设备号 */
    unsigned short i_num;    /* inode号码，该inode在系统中的唯一标识 */
    unsigned short i_count;  /* 有多少进程在使用该inode */
    unsigned char i_lock;    /* 锁状态 */
    unsigned char i_dirt;    /* 是否被修改 */
    unsigned char i_pipe;    /* 是否为管道 */
    unsigned char i_mount;   /* 是否为挂载点 */
    unsigned char i_seek;    /* 文件读写指针的位置 */
    unsigned char i_update;  /* 是否需要更新 */
};

struct file
{
    unsigned short f_mode;
    unsigned short f_flags;
    unsigned short f_count;
    struct m_inode *f_inode;
    off_t f_pos;
};

struct super_block
{
    unsigned short s_ninodes;
    unsigned short s_nzones;
    unsigned short s_imap_blocks;
    unsigned short s_zmap_blocks;
    unsigned short s_firstdatazone;
    unsigned short s_log_zone_size;
    unsigned long s_max_size;
    unsigned short s_magic;
    /* These are only in memory */
    struct buffer_head *s_imap[8];
    struct buffer_head *s_zmap[8];
    unsigned short s_dev;
    struct m_inode *s_isup;
    struct m_inode *s_imount;
    unsigned long s_time;
    struct task_struct *s_wait;
    unsigned char s_lock;
    unsigned char s_rd_only;
    unsigned char s_dirt;
};

struct d_super_block
{
    unsigned short s_ninodes;
    unsigned short s_nzones;
    unsigned short s_imap_blocks;
    unsigned short s_zmap_blocks;
    unsigned short s_firstdatazone;
    unsigned short s_log_zone_size;
    unsigned long s_max_size;
    unsigned short s_magic;
};

struct dir_entry
{
    unsigned short inode;
    char name[NAME_LEN];
};

extern struct m_inode inode_table[NR_INODE];
extern struct file file_table[NR_FILE];
extern struct super_block super_block[NR_SUPER];
extern struct buffer_head *start_buffer;
extern int nr_buffers;

extern void check_disk_change(int dev);
extern int floppy_change(unsigned int nr);
extern int ticks_to_floppy_on(unsigned int dev);
extern void floppy_on(unsigned int dev);
extern void floppy_off(unsigned int dev);
extern void truncate(struct m_inode *inode);
extern void sync_inodes(void);
extern void wait_on(struct m_inode *inode);
extern int bmap(struct m_inode *inode, int block);
extern int create_block(struct m_inode *inode, int block);
extern struct m_inode *namei(const char *pathname);
extern int open_namei(const char *pathname, int flag, int mode,
                      struct m_inode **res_inode);
extern void iput(struct m_inode *inode);
extern struct m_inode *iget(int dev, int nr);
extern struct m_inode *get_empty_inode(void);
extern struct m_inode *get_pipe_inode(void);
extern struct buffer_head *get_hash_table(int dev, int block);
extern struct buffer_head *getblk(int dev, int block);
extern void ll_rw_block(int rw, struct buffer_head *bh);
extern void brelse(struct buffer_head *buf);
extern struct buffer_head *bread(int dev, int block);
extern struct buffer_head *breada(int dev, int block, ...);
extern int new_block(int dev);
extern void free_block(int dev, int block);
extern struct m_inode *new_inode(int dev);
extern void free_inode(struct m_inode *inode);
extern int sync_dev(int dev);
extern struct super_block *get_super(int dev);
extern int ROOT_DEV;

extern void mount_root(void);

#endif
