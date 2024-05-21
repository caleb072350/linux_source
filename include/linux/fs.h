/* 
 * This file has definitions for some important file table
 * structure etc.
*/

#ifndef FS_H
#define FS_H

#include "../sys/types.h"

/* devices are as follows: (same as minix, so we can use the minix
 * file system. These are major numbers.)
 *
 * 0 - unused (nodev)
 * 1 - /dev/mem  内存？
 * 2 - /dev/fd   软盘？
 * 3 - /dev/hd   硬盘？
 * 4 - /dev/ttyx 输入？
 * 5 - /dev/tty  输出？
 * 6 - /dev/lp
 * 7 - unnamed pipes
 */

#define IS_SEEKABLE(x) ((x)>=1 && (x)<=3)

#define READ 0
#define WRITE 1
#define READA 2         /* read-ahead - don't pause */

void buffer_init();

#define MAJOR(a) (((unsigned)(a))>>8)
#define MINOR(a) ((a)&0xff)

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
#define NULL ((void *) 0)
#endif

#define INODES_PER_BLOCK ((BLOCK_SIZE)/(sizeof (struct d_inode)))
#define DIR_ENTRIES_PER_BLOCK ((BLOCK_SIZE)/(sizeof (struct dir_entry)))

#define PIPE_HEAD(inode) ((inode).i_zone[0])
#define PIPE_TAIL(inode) ((inode).i_zone[1])
#define PIPE_SIZE(inode) ((PIPE_HEAD(inode)-PIPE_TAIL(inode))&(PAGE_SIZE-1))
#define PIPE_EMPTY(inode) (PIPE_HEAD(inode)==PIPE_TAIL(inode))
#define PIPE_FULL(inode) (PIPE_SIZE(inode)==(PAGE_SIZE-1))
#define INC_PIPE(head) \
__asm__("incl %0\n\tandl $4095,%0"::"m" (head))

typedef char buffer_block[BLOCK_SIZE];

/*
 * inode是什么？
 * 早期linux系统中，inode是一种数据结构，用于描述文件或目录在文件系统中的属性和位置信息。每个文件或目录都有一个对应的inode，其中包含有
 * 文件类型、权限、所有者、文件大小、时间戳等元信息，以及指向文件数据块的指针。通过inode，文件系统可以准确定位和管理文件的实际数据。每种
 * 文件系统都有自己的inode表，用于存储所有文件和目录的inode信息，inode表通常存储在磁盘的特定位置。每种文件系统的inode表的实现方式上可能有不同。
 * 
 * linux启动的时候，如何读取inode表？
 * 1. 引导加载程序(Bootloader): 系统启动时，引导加载程序(如GRUB)会加载内核映像(vmliuz)到内存中，并启动内核。
 * 2. 文件系统初始化:内核启动后，会初始化文件系统驱动程序，这些驱动程序负责管理文件系统的元数据信息，包括inode表。
 * 3. 磁盘访问:文件系统驱动程序会通过磁盘驱动程序访问磁盘上的存储区域。
 * 4. 读取inode表:文件系统驱动程序会根据文件系统的结构和元数据信息，定位并读取inode表的内容。早期linux系统中，通常读取磁盘上的特定位置的数据块，
 *    其中包括inode表。
 * 5. 加载inode表:一旦读取到inode表到内存中，系统就可以根据文件名和目录名来查找和访问对应的inode，从而获得磁盘上的文件数据。
*/

/*
 * 磁盘数据块
 * 在linux的早期源码中，buffer_head 结构体在文件系统中起着重要作用。这个结构体用于管理缓存数据块，是linux内核中的核心部分，用于处理I/O子系统
 * 和内存管理。struct buffer_head 结构体的字段包括指向数据块的指针、设备号、块号、数据更新状态、脏数据标记、使用该块的用户数、锁状态等信息。
 * 此结构体的主要功能是追踪缓存中的数据块的状态，标识数据块是否需要写回磁盘、有多少用户正在使用该块等信息。在早期linux内核中用于缓存磁盘块，
 * 以提高文件系统的性能和效率。
 * 随着时间的推移，Linux的内核逐渐采用了更高效的方式，如“page cache”,这种缓存方式管理文件数据的页面而不是磁盘块。然而，struct buffer_head
 * 结构体仍然存在于当前内核中，尤其是在某些系统中仍然使用。尽管缓冲头结构体在缓存管理中的角色已经结束，但它仍在内核的某些部分中扮演着重要角色，
 * 用于跟踪内存中缓存数据与持久存储中数据位置之间的映射关系。一些文件系统，如ext4，仍然广泛使用strcut buffer_head结构体。
*/
struct buffer_head {
    char * b_data;                  /* pointer to data block (1024 bytes) */
    unsigned short b_dev;           /* device (0 = free) */
    unsigned short b_blocknr;       /* block number */
    unsigned char b_uptodate;
    unsigned char b_dirt;           /* 0-clean,1-dirty */
    unsigned char b_count;          /* users using this block */
    unsigned char b_lock;           /* 0 - ok, 1 -locked */
    struct task_struct * b_wait;
    struct buffer_head * b_prev;
    struct buffer_head * b_next;
    struct buffer_head * b_prev_free;
    struct buffer_head * b_next_free;
};

/*
 * 目录项 inode 元数据信息
 * d_inode 用于表示目录项(directory entry)中的inode信息。在Linux文件系统中，目录项是文件系统中的目录条目，用于将文件名映射到对应的inode。
 * 而 struct d_inode 结构体则用于存储目录项所指向的inode信息，包括文件的权限模式(i_mode)、所有者用户ID(i_uid)、文件大小(i_size)、
 * 时间戳(i_time)、所有者组ID(i_gid)、硬链接数(i_nlinks)以及文件数据块的索引(i_zone).这些信息对于文件系统的管理和操作非常重要，可以帮助
 * 内核跟踪和管理文件的元数据信息和数据块的位置。
*/
struct d_inode {
    unsigned short i_mode;      // 权限模式
    unsigned short i_uid;       // 所有者ID     
    unsigned long i_size;       // 文件大小
    unsigned long i_time;       // 时间戳
    unsigned char i_gid;        // 所有者组ID
    unsigned char i_nlinks;     // 硬链接数量
    unsigned short i_zone[9];   // 文件数据块的索引
};

/*
 * 表示文件系统的索引节点
 * 存储文件或目录的元数据信息，包括类型、权限、用户id、文件大小、修改时间、所有者组id、硬链接数、数据块索引等。
 * 此外，还有一些用于文件系统管理和操作的辅助字段，如等待队列指针、设备号、计数器、脏数据标记等。
*/
struct m_inode {
    unsigned short i_mode;
    unsigned short i_uid;
    unsigned long i_size;
    unsigned long i_mtime;
    unsigned char i_gid;
    unsigned char i_nlinks;
    unsigned short i_zone[9];     // 数据块索引
/* these are in memory also */
    struct task_struct * i_wait;  // 等待进程队列
    unsigned long i_atime;        // 文件或目录的最后访问时间 access time
    unsigned long i_ctime;        // 文件或目录的状态改变时间 change time，记录元数据信息(如权限、所有者等)最后一次发生变化的时间戳
    unsigned short i_dev;         // 设备号
    unsigned short i_num;         // inode号，该字段存储了文件或目录在文件系统中的唯一标识
    unsigned short i_count;       // 当前正在访问该inode的进程数量
    unsigned char i_lock;         // 锁状态
    unsigned char i_dirt;         // 脏标识
    unsigned char i_pipe;         // 标识该inode是否与管道相关联
    unsigned char i_mount;        // 是否作为文件系统的根节点被挂在
    unsigned char i_seek;         // 文件读写指针位置
    unsigned char i_update;       // 是否需要更新到磁盘的标志
};

/* 打开的文件 */
struct file {
    unsigned short f_mode;        // 存储文件访问权限和类型信息
    unsigned short f_flags;       // 存储文件的标志信息，如读写模式等
    unsigned short f_count;       // 记录打开该文件的进程数量，用于引用计数。
    struct m_inode * f_inode;     // 文件在文件系统中的索引节点
    off_t f_pos;                  // 当前读写指针的位置
};

/*
 * 系统初始化的时候，读取硬盘特定位置的文件系统信息，初始化super_block块，通常一块硬盘对应一个super_block
*/
struct super_block {
    unsigned short s_ninodes;              // inode的总数
    unsigned short s_nzones;               // 数据区的总数
    unsigned short s_imap_blocks;          // inode位图占用的块数
    unsigned short s_zmap_blocks;          // 数据区位图占用的块数
    unsigned short s_firstdatazone;        // 第一个数据区的块号
    unsigned short s_log_zone_size;        // 数据区块的对数
    unsigned long s_max_size;              // 文件系统支持的最大文件大小
    unsigned short s_magic;                // 文件系统标识号，用于区别不同的文件系统
/* These are only in memory */
    struct buffer_head * s_imap[8];        // inode位图的缓冲头指针数组
    struct buffer_head * s_zmap[8];        // 数据区位图的缓冲头指针数组
    unsigned short s_dev;                  // 文件系统所在设备的设备号
    struct m_inode * s_isup;               // 根目录的索引节点
    struct m_inode * s_imount;             // 安装该文件系统的索引节点
    unsigned long s_time;                  // 最后一次修改超级块的时间
    struct task_struct * s_wait;           // 等待超级块的进程队列指针
    unsigned char s_lock;                  // 锁标志
    unsigned char s_rd_only;               // 是否只读
    unsigned char s_dirt;                  // 是否被修改过
};

/* super_block的简略描述，只有一些元数据信息 */
struct d_super_block {
    unsigned short s_ninodes;
    unsigned short s_nzones;
    unsigned short s_imap_blocks;
    unsigned short s_zmap_blocks;
    unsigned short s_firstdatazone;
    unsigned short s_log_zone_size;
    unsigned long s_max_size;
    unsigned short s_magic;
};

/* 目录 数据结构*/
struct dir_entry {
    unsigned short inode;  // 目录对应的inode号，唯一
    char name[NAME_LEN];   // 目录名
};

extern struct m_inode inode_table[NR_INODE];
extern struct file file_table[NR_FILE];
extern struct super_block super_block[NR_SUPER];
extern struct buffer_head * start_buffer;
extern int nr_buffers;

extern void check_disk_change(int dev);
extern int floppy_change(unsigned int nr);
extern int ticks_to_floppy_on(unsigned int dev);
extern void floppy_on(unsigned int dev);
extern void floppy_off(unsigned int dev);
extern void truncate(struct m_inode * inode);
extern void sync_inodes(void);
extern void wait_on(struct m_inode * inode);
extern int bmap(struct m_inode * inode,int block);
extern int create_block(struct m_inode * inode,int block);
extern struct m_inode * namei(const char * pathname);
extern int open_namei(const char * pathname, int flag, int mode,
        struct m_inode ** res_inode);
extern void iput(struct m_inode * inode);                          // inode put,释放掉inode结构体
extern struct m_inode * iget(int dev,int nr);
extern struct m_inode * get_empty_inode(void);
extern struct m_inode * get_pipe_inode(void);
extern struct buffer_head * get_hash_table(int dev, int block);
extern struct buffer_head * getblk(int dev, int block);
extern void ll_rw_block(int rw, struct buffer_head * bh);
extern void brelse(struct buffer_head * buf);
extern struct buffer_head * bread(int dev,int block);
extern struct buffer_head * breada(int dev,int block,...);
extern int new_block(int dev);
extern void free_block(int dev, int block);
extern struct m_inode * new_inode(int dev);
extern void free_inode(struct m_inode * inode);
extern int sync_dev(int dev);
extern struct super_block * get_super(int dev);
extern int ROOT_DEV;

extern void mount_root(void);
#endif