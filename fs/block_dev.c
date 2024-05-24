/*
*  linux/fs/block_dev.c
*  这个文件主要是块设备的读写,从块设备指定位置读n个字节到buf中，或将buf中n个字节写入块设备指定位置处
*  (C) 1991  Linus Torvalds
*/

#include "../include/errno.h"
#include "../include/linux/fs.h"
#include "../include/linux/sched.h"
#include "../include/linux/kernel.h"
#include "../include/asm/segment.h"
#include "../include/asm/system.h"

/* *pos 这个参数主要用来计算块号和偏移量,这个函数的作用是将count个从buf处开始的数据，写入*pos表示的block块中 */
int block_write(int dev, long * pos, char * buf, int count)
{
    int block = *pos >> BLOCK_SIZE_BITS;  /* *pos 的高 22 位 表示块号 */
    int offset = *pos & (BLOCK_SIZE-1);   /* *pos 的低 9 位表示块内偏移量 0~1023*/
    int chars;
    int written = 0;
    struct buffer_head * bh;
    register char * p;

    while (count>0) {   /* 数据还没读完 */
        chars = BLOCK_SIZE - offset;   /* 当前块还有多少可以读取的字节数 */
        if (chars > count) /* 若当前块还可读的数据大于count */
            chars=count;
        if (chars == BLOCK_SIZE) /* 偏移量为0的情况,且count == BLOCK_SIZE */
            bh = getblk(dev,block); /* 读取一个block即可,读取dev与block决定的buffer_head */
        else
            bh = breada(dev,block,block+1,block+2,-1); /* 当前block剩余可读的数量小于count，进行预读操作，获取当前块和接下来两个块的缓冲块 */
        block++;
        if (!bh) /* 获取缓冲区失败 */
            return written?written:-EIO;
        p = offset + bh->b_data; /* bh->bdata指向数据块的起始位置，+offset 即为偏移量 */
        offset = 0; /* offset 置为0, 为写下一个块做准备 */
        *pos += chars; /*这里提前就更新了文件位置指针，往后移动chars个字节 */
        written += chars;
        count -= chars;
        while (chars-->0) /* 实际从buf读数据，在block写数据发生在这里 */
            *(p++) = get_fs_byte(buf++); /* 这个是用汇编写的，功能就是从指定位置读取一个字节 */
        bh->b_dirt = 1;
        brelse(bh);
    }
    return written;
}

/* 从dev设备的*pos指定的位置读取count个字节数据到buf中 */
int block_read(int dev, unsigned long * pos, char * buf, int count)
{
    int block = *pos >> BLOCK_SIZE_BITS;
    int offset = *pos & (BLOCK_SIZE-1);
    int chars;
    int read = 0;
    struct buffer_head * bh;
    register char * p;

    while (count>0) {
        chars = BLOCK_SIZE-offset;
        if (chars > count)
        chars = count;
        if (!(bh = breada(dev,block,block+1,block+2,-1)))
            return read?read:-EIO;
        block++;
        p = offset + bh->b_data;
        offset = 0;
        *pos += chars;
        read += chars;
        count -= chars;
        while (chars-->0)
            put_fs_byte(*(p++),buf++); /* 将p处的字节放到buf处 */
        brelse(bh);
    }
    return read;
}
