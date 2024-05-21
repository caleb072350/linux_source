/*
*  linux/fs/super.c
*
*  (C) 1991  Linus Torvalds
*/

/*
* super.c contains code to handle the super-block tables.
*/
#include "include/linux/config.h"
#include "include/linux/sched.h"
#include "include/linux/kernel.h"
#include "include/asm/system.h"

#include "include/errno.h"
#include "include/sys/stat.h"

int sync_dev(int dev);
void wait_for_keypress(void);

/* set_bit uses setb, as gas doesn't recognize setc */
#define set_bit(bitnr,addr) ({ \
register int __res __asm__("ax"); \
__asm__("bt %2,%3;setb %%al":"=a" (__res):"a" (0),"r" (bitnr),"m" (*(addr))); \
__res; })

struct super_block super_block[NR_SUPER];
/* this is initialized in init/main.c */
int ROOT_DEV = 0;

/* 给 super_block 加锁，即将s_lock字段置为1，但要判断super_block是否被其他进程锁住，如果锁住，则自旋等待，直到锁被释放 */
static void lock_super(struct super_block * sb)
{
    cli();
    while (sb->s_lock)       // 自旋
        sleep_on(&(sb->s_wait));
    sb->s_lock = 1;
    sti();
}

/* 释放 super_block 的锁 */
static void free_super(struct super_block * sb)
{
    cli();
    sb->s_lock = 0;
    wake_up(&(sb->s_wait));
    sti();
}

/* 等待 super_block 的锁被释放 */
static void wait_on_super(struct super_block * sb)
{
    cli();
    while (sb->s_lock)
        sleep_on(&(sb->s_wait));
    sti();
}

/* 获取指定dev的super_block */
struct super_block * get_super(int dev)
{
    struct super_block * s;

    if (!dev) // 有效dev 必须>0
        return NULL;
    s = 0+super_block;
    while (s < NR_SUPER+super_block)
        if (s->s_dev == dev) {
            wait_on_super(s);  /* 如果有其他进程占据了super_block,等待super_block的锁被释放 */
            if (s->s_dev == dev)
                return s;
            s = 0+super_block;
        } else
            s++;
    return NULL;
}

/* 释放dev设备,将dev对应的super_block槽子中的信息抹去 */
void put_super(int dev)
{
    struct super_block * sb;
    int i;

    if (dev == ROOT_DEV) { // 系统启动时候的硬盘设备，其super_block已经写入
        printk("root diskette changed: prepare for armageddon\n\r");
        return;
    }
    if (!(sb = get_super(dev)))   // super_block的槽子中没有这个dev，直接返回
        return;
    if (sb->s_imount) {
        printk("Mounted disk changed - tssk, tssk\n\r");
        return;
    }
    lock_super(sb);
    sb->s_dev = 0;
    for(i=0;i<I_MAP_SLOTS;i++)
        brelse(sb->s_imap[i]);
    for(i=0;i<Z_MAP_SLOTS;i++)
        brelse(sb->s_zmap[i]);
    free_super(sb);
    return;
}

/* 读取设备dev的super_block块信息 */
static struct super_block * read_super(int dev)
{
    struct super_block * s;
    struct buffer_head * bh;
    int i,block;

    if (!dev)
        return NULL;
    check_disk_change(dev);
    if (s = get_super(dev)) // 之前已经注册过，直接返回
        return s;
    for (s = 0+super_block ;; s++) {
        if (s >= NR_SUPER+super_block)
            return NULL;
        if (!s->s_dev)
            break;         // 找到第一个未被使用的槽子
    }
    s->s_dev = dev;        // 初始化该槽子
    s->s_isup = NULL;
    s->s_imount = NULL;
    s->s_time = 0;
    s->s_rd_only = 0;
    s->s_dirt = 0;
    lock_super(s);
    if (!(bh = bread(dev,1))) {  // 读取dev设备的第一个block块
        s->s_dev=0;
        free_super(s);
        return NULL;
    }
    *((struct d_super_block *) s) = *((struct d_super_block *) bh->b_data);   // dev 设备的第一个block块存储的是super_block的元数据
    brelse(bh);
    if (s->s_magic != SUPER_MAGIC) {    // 校验magic失败
        s->s_dev = 0;
        free_super(s);
        return NULL;
    }
    for (i=0;i<I_MAP_SLOTS;i++)
        s->s_imap[i] = NULL;
    for (i=0;i<Z_MAP_SLOTS;i++)
        s->s_zmap[i] = NULL;
    block=2;
    for (i=0 ; i < s->s_imap_blocks ; i++)
        if (s->s_imap[i]=bread(dev,block)) // 从数据块读取inode位图，为NULL表示数据读取完毕
            block++;
        else
            break;
    for (i=0 ; i < s->s_zmap_blocks ; i++) // 读取数据区位图，为NULL表示数据读取完毕
        if (s->s_zmap[i]=bread(dev,block))
            block++;
        else
            break;
    if (block != 2+s->s_imap_blocks+s->s_zmap_blocks) { // 正常的话，block == 2+s->s_imap_blocks+s->s_zmap_blocks
        for(i=0;i<I_MAP_SLOTS;i++)
            brelse(s->s_imap[i]);
        for(i=0;i<Z_MAP_SLOTS;i++)
            brelse(s->s_zmap[i]);
        s->s_dev=0;
        free_super(s);
        return NULL;
    }
    s->s_imap[0]->b_data[0] |= 1;
    s->s_zmap[0]->b_data[0] |= 1;
    free_super(s);
    return s;
}

/* 卸载文件系统 */
int sys_umount(char * dev_name)
{
    struct m_inode * inode;
    struct super_block * sb;
    int dev;

    if (!(inode=namei(dev_name)))
        return -ENOENT;
    dev = inode->i_zone[0];          // 在m_inode的i_zone数组获取设备的设备号
    if (!S_ISBLK(inode->i_mode)) {   // 通过i_mode校验该设备是否为块设备
        iput(inode);
        return -ENOTBLK;
    }
    iput(inode);   // 释放掉inode结构体
    if (dev==ROOT_DEV)              // 尝试卸载根文件系统，返回错误
        return -EBUSY;
    if (!(sb=get_super(dev)) || !(sb->s_imount))
        return -ENOENT;
    if (!sb->s_imount->i_mount)
        printk("Mounted inode has i_mount=0\n");
    for(inode=inode_table+0 ; inode<inode_table+NR_INODE ; inode++) // 遍历inode_table,检查是否有待卸载设备的inode结构体在使用，如果有，则报错
        if (inode->i_dev==dev && inode->i_count)
            return -EBUSY;
    sb->s_imount->i_mount=0;
    iput(sb->s_imount);
    sb->s_imount = NULL;
    iput(sb->s_isup);
    sb->s_isup = NULL;
    put_super(dev);  // 释放dev对应的super_block
    sync_dev(dev);
    return 0;
}

/* 挂载文件系统 */
int sys_mount(char * dev_name, char * dir_name, int rw_flag)
{
    struct m_inode * dev_i, * dir_i;
    struct super_block * sb;
    int dev;

    if (!(dev_i=namei(dev_name)))
        return -ENOENT;
    dev = dev_i->i_zone[0];
    if (!S_ISBLK(dev_i->i_mode)) {
        iput(dev_i);
        return -EPERM;
    }
    iput(dev_i);
    if (!(dir_i=namei(dir_name)))
        return -ENOENT;
    if (dir_i->i_count != 1 || dir_i->i_num == ROOT_INO) {
        iput(dir_i);
        return -EBUSY;
    }
    if (!S_ISDIR(dir_i->i_mode)) {
        iput(dir_i);
        return -EPERM;
    }
    if (!(sb=read_super(dev))) {
        iput(dir_i);
        return -EBUSY;
    }
    if (sb->s_imount) {
        iput(dir_i);
        return -EBUSY;
    }
    if (dir_i->i_mount) {
        iput(dir_i);
        return -EPERM;
    }
    sb->s_imount=dir_i;
    dir_i->i_mount=1;
    dir_i->i_dirt=1;                /* NOTE! we don't iput(dir_i) */
    return 0;                       /* we do that in umount */
}

/* 挂载根文件系统 */
void mount_root(void)
{
    int i,free;
    struct super_block * p;
    struct m_inode * mi;

    if (32 != sizeof (struct d_inode))
        panic("bad i-node size");
    for(i=0;i<NR_FILE;i++)
        file_table[i].f_count=0;
    if (MAJOR(ROOT_DEV) == 2) {
        printk("Insert root floppy and press ENTER");
        wait_for_keypress();
    }
    for(p = &super_block[0] ; p < &super_block[NR_SUPER] ; p++) {   // 初始化super_block数组
        p->s_dev = 0;
        p->s_lock = 0;
        p->s_wait = NULL;
    }
    if (!(p=read_super(ROOT_DEV)))  // 读取根设备的super_block
        panic("Unable to mount root");
    if (!(mi=iget(ROOT_DEV,ROOT_INO)))         // 读取根设备的根inode
        panic("Unable to read root i-node");
    mi->i_count += 3 ;      /* NOTE! it is logically used 4 times, not 1 */
    p->s_isup = p->s_imount = mi;    // 根设备的super_block块的上级节点和挂载节点设置为根inode
    current->pwd = mi;        // 将当前进程的工作目录设置为根inode
    current->root = mi;       // 将当前进程的根目录设置为根inode
    free=0;
    i=p->s_nzones;
    while (-- i >= 0)
        if (!set_bit(i&8191,p->s_zmap[i>>13]->b_data))
        free++;
    printk("%d/%d free blocks\n\r",free,p->s_nzones);
    free=0;
    i=p->s_ninodes+1;
    while (-- i >= 0)
        if (!set_bit(i&8191,p->s_imap[i>>13]->b_data))
            free++;
    printk("%d/%d free inodes\n\r",free,p->s_ninodes);
}

