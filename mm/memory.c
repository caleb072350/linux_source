/*
 *  linux/mm/memory.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include "../include/signal.h"

#include "../include/linux/head.h"
#include "../include/linux/kernel.h"
#include "../include/asm/system.h"

int do_exit(long code);

/* 这段汇编用来刷新页表 */
#define invalidate() \
    __asm__("movl %%eax,%%cr3" ::"a"(0))

/* these are not to be changed without changing head.s etc */
#define LOW_MEM 0x100000                         //系统的低端物理内存的地址，用于在内核中确定内存的起始位置，内存分页是从这里开始的。
                                                 // 低于LOW_MEM的物理内存用于内核代码和数据结构的存储
#define PAGING_MEMORY (15 * 1024 * 1024)         // 表示系统中用于分页的内存大小，这里为 15 MB
#define PAGING_PAGES (PAGING_MEMORY >> 12)       // 系统中分页的总页数
#define MAP_NR(addr) (((addr) - LOW_MEM) >> 12)  // 将给定地址映射为对应的页号
#define USED 100                                 // 页面被使用的标识

static long HIGH_MEMORY = 0;

/* 将内存中的一页数据从地址 from 复制到地址 to */
#define copy_page(from, to) \
    __asm__("cld ; rep ; movsl" ::"S"(from), "D"(to), "c"(1024) : "cx", "di", "si")

static unsigned char mem_map[PAGING_PAGES] = {0,};  // 标记某一个页是否被使用

/*
 * Get physical address of first (actually last :-) free page, and mark it
 * used. If no free pages left, return 0.
 * 在系统中查找一个空闲的页面，将其标记为使用，并返回该页面的物理地址
 */
unsigned long get_free_page(void)
{
    register unsigned long __res asm("ax");

    __asm__("std ; repne ; scasb\n\t"
            "jne 1f\n\t"
            "movb $1,1(%%edi)\n\t"
            "sall $12,%%ecx\n\t"
            "addl %2,%%ecx\n\t"
            "movl %%ecx,%%edx\n\t"
            "movl $1024,%%ecx\n\t"
            "leal 4092(%%edx),%%edi\n\t"
            "rep ; stosl\n\t"
            "movl %%edx,%%eax\n"
            "1:"
            : "=a"(__res)
            : "0"(0), "i"(LOW_MEM), "c"(PAGING_PAGES),
              "D"(mem_map + PAGING_PAGES - 1)
            : "di", "cx", "dx");
    return __res;
}

/*
 * Free a page of memory at physical address 'addr'. Used by
 * 'free_page_tables()'
 * 释放一个指定的物理地址对应的页面
 */
void free_page(unsigned long addr)
{
    if (addr < LOW_MEM)
        return;
    if (addr > HIGH_MEMORY)
        panic("trying to free nonexistent page");
    addr -= LOW_MEM;
    addr >>= 12;
    if (mem_map[addr]--)
        return;
    mem_map[addr] = 0;
    panic("trying to free free page");
}

/*
 * This function frees a continuos block of page tables, as needed
 * by 'exit()'. As does copy_page_tables(), this handles only 4Mb blocks.
 * 释放从指定位置 from 开始的一定大小的页表
 */
int free_page_tables(unsigned long from, unsigned long size)
{
    unsigned long *pg_table;
    unsigned long *dir, nr;

    if (from & 0x3fffff)  // 检查起始地址from是否按照正确的边界对齐(低22位为0)
        panic("free_page_tables called with wrong alignment");
    if (!from) // 检查起始地址是否为0，如果是，表示尝试释放交换空间，这是不允许的。
        panic("Trying to free up swapper memory space");
    size = (size + 0x3fffff) >> 22; // 这行代码应该是为了确保对齐和截断操作。我理解一个页表项下面有1024个页，每个页是4KB
    dir = (unsigned long *)((from >> 20) & 0xffc); /* _pg_dir = 0 取最高位的10位，我理解这里是找到页表的起始位置 */  
    for (; size-- > 0; dir++) // dir[0]、dir[1]、dir[2]... 为不同的页表
    {
        if (!(1 & *dir)) //表示该页表未使用
            continue;
        pg_table = (unsigned long *)(0xfffff000 & *dir); /* 取高20位, 高20位存储页表项的物理地址，低12位存储页表项偏移量 */
        for (nr = 0; nr < 1024; nr++)                    /* 每个页表有1024个页表项 */
        {
            if (1 & *pg_table)
                free_page(0xfffff000 & *pg_table);
            *pg_table = 0;
            pg_table++;
        }
        free_page(0xfffff000 & *dir);  // dir 看起来像是二维数组
        *dir = 0;
    }
    invalidate();
    return 0;
}

/*
 *  Well, here is one of the most complicated functions in mm. It
 * copies a range of linerar addresses by copying only the pages.
 * Let's hope this is bug-free, 'cause this one I don't want to debug :-)
 *
 * Note! We don't copy just any chunks of memory - addresses have to
 * be divisible by 4Mb (one page-directory entry), as this makes the
 * function easier. It's used only by fork anyway.
 *
 * NOTE 2!! When from==0 we are copying kernel space for the first
 * fork(). Then we DONT want to copy a full page-directory entry, as
 * that would lead to some serious memory waste - we just copy the
 * first 160 pages - 640kB. Even that is more than we need, but it
 * doesn't take any more memory - we don't copy-on-write in the low
 * 1 Mb-range, so the pages can be shared with the kernel. Thus the
 * special case for nr=xxxx.
 */
int copy_page_tables(unsigned long from, unsigned long to, long size)
{
    unsigned long *from_page_table;
    unsigned long *to_page_table;
    unsigned long this_page;
    unsigned long *from_dir, *to_dir;
    unsigned long nr;

    if ((from & 0x3fffff) || (to & 0x3fffff))   // from 和 to 都必须是4MB的整数倍
        panic("copy_page_tables called with wrong alignment");
    from_dir = (unsigned long *)((from >> 20) & 0xffc); /* _pg_dir = 0 */
    to_dir = (unsigned long *)((to >> 20) & 0xffc);
    size = ((unsigned)(size + 0x3fffff)) >> 22;
    for (; size-- > 0; from_dir++, to_dir++)
    {
        if (1 & *to_dir)
            panic("copy_page_tables: already exist");
        if (!(1 & *from_dir))
            continue;
        from_page_table = (unsigned long *)(0xfffff000 & *from_dir);
        if (!(to_page_table = (unsigned long *)get_free_page()))
            return -1; /* Out of memory, see freeing */
        *to_dir = ((unsigned long)to_page_table) | 7;
        nr = (from == 0) ? 0xA0 : 1024;
        for (; nr-- > 0; from_page_table++, to_page_table++)
        {
            this_page = *from_page_table;
            if (!(1 & this_page))
                continue;
            this_page &= ~2;
            *to_page_table = this_page;
            if (this_page > LOW_MEM)
            {
                *from_page_table = this_page;
                this_page -= LOW_MEM;
                this_page >>= 12;
                mem_map[this_page]++;
            }
        }
    }
    invalidate();
    return 0;
}

/*
 * This function puts a page in memory at the wanted address.
 * It returns the physical address of the page gotten, 0 if
 * out of memory (either when trying to access page-table or
 * page.)
 */
unsigned long put_page(unsigned long page, unsigned long address)
{
    unsigned long tmp, *page_table;

    /* NOTE !!! This uses the fact that _pg_dir=0 */

    if (page < LOW_MEM || page > HIGH_MEMORY)
        printk("Trying to put page %p at %p\n", page, address);
    if (mem_map[(page - LOW_MEM) >> 12] != 1)
        printk("mem_map disagrees with %p at %p\n", page, address);
    page_table = (unsigned long *)((address >> 20) & 0xffc);
    if ((*page_table) & 1)
        page_table = (unsigned long *)(0xfffff000 & *page_table);
    else
    {
        if (!(tmp = get_free_page()))
            return 0;
        *page_table = tmp | 7;
        page_table = (unsigned long *)tmp;
    }
    page_table[(address >> 12) & 0x3ff] = page | 7;
    return page;
}

void un_wp_page(unsigned long *table_entry)
{
    unsigned long old_page, new_page;

    old_page = 0xfffff000 & *table_entry;
    if (old_page >= LOW_MEM && mem_map[MAP_NR(old_page)] == 1)
    {
        *table_entry |= 2;
        return;
    }
    if (!(new_page = get_free_page()))
        do_exit(SIGSEGV);
    if (old_page >= LOW_MEM)
        mem_map[MAP_NR(old_page)]--;
    *table_entry = new_page | 7;
    copy_page(old_page, new_page);
}

/*
 * This routine handles present pages, when users try to write
 * to a shared page. It is done by copying the page to a new address
 * and decrementing the shared-page counter for the old page.
 */
void do_wp_page(unsigned long error_code, unsigned long address)
{
    un_wp_page((unsigned long *)(((address >> 10) & 0xffc) + (0xfffff000 &
                                                              *((unsigned long *)((address >> 20) & 0xffc)))));
}

void write_verify(unsigned long address)
{
    unsigned long page;

    if (!((page = *((unsigned long *)((address >> 20) & 0xffc))) & 1))
        return;
    page &= 0xfffff000;
    page += ((address >> 10) & 0xffc);
    if ((3 & *(unsigned long *)page) == 1) /* non-writeable, present */
        un_wp_page((unsigned long *)page);
    return;
}

void do_no_page(unsigned long error_code, unsigned long address)
{
    unsigned long tmp;

    if (tmp = get_free_page())
        if (put_page(tmp, address))
            return;
    do_exit(SIGSEGV);
}

/* 系统初始化阶段初始化内存管理子系统 start_mem、end_mem以字节为单位 */
void mem_init(long start_mem, long end_mem)
{
    int i;

    HIGH_MEMORY = end_mem;   // 将系统可用的最高内存设置为end_mem
    for (i = 0; i < PAGING_PAGES; i++)
        mem_map[i] = USED;   // mem_map 用于跟踪系统中每一页内存的状态，初始化的时候将其标记为已使用
    i = MAP_NR(start_mem);   // 计算start_mem对应的页号，这里应该是物理内存的页号
    end_mem -= start_mem;    // 这两行代码计算内存范围的页数，并存储在end_mem中
    end_mem >>= 12;          // 这里12是因为在x86架构下，一个页面大小是4Kb，即2^12字节
    while (end_mem-- > 0)    // 重新遍历所有的内存页，标记为未使用
        mem_map[i++] = 0;
}

void calc_mem(void)
{
    int i, j, k, free = 0;
    long *pg_tbl;

    for (i = 0; i < PAGING_PAGES; i++)
        if (!mem_map[i])
            free++;
    printk("%d pages free (of %d)\n\r", free, PAGING_PAGES);
    for (i = 2; i < 1024; i++)
    {
        if (1 & pg_dir[i])
        {
            pg_tbl = (long *)(0xfffff000 & pg_dir[i]);
            for (j = k = 0; j < 1024; j++)
                if (pg_tbl[j] & 1)
                    k++;
            printk("Pg-dir[%d] uses %d pages\n", i, k);
        }
    }
}