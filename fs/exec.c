/*
 *  linux/fs/exec.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include "../include/errno.h"
#include "../include/string.h"
#include "../include/sys/stat.h"
#include "../include/a.out.h"
#include "../include/linux/fs.h"
#include "../include/linux/sched.h"
#include "../include/linux/kernel.h"
#include "../include/linux/mm.h"
#include "../include/asm/segment.h"

extern int sys_exit(int exit_code);
extern int sys_close(int fd);

/*
 * MAX_ARG_PAGES defines the number of pages allocated for arguments
 * and envelope for the new program. 32 should suffice, this gives
 * a maximum env+arg of 128kB !
 */
#define MAX_ARG_PAGES 32

/* 汇编级别内存块的复制操作 */
#define cp_block(from, to)                    \
    __asm__("pushl $0x10\n\t"                 \
            "pushl $0x17\n\t"                 \
            "pop %%es\n\t"                    \
            "cld\n\t"                         \
            "rep\n\t"                         \
            "movsl\n\t"                       \
            "pop %%es" ::"c"(BLOCK_SIZE / 4), \
            "S"(from), "D"(to)                \
            : "cx", "di", "si")

/*
 * read_head() reads blocks 1-6 (not 0). Block 0 has already been
 * read for header information.
 */
int read_head(struct m_inode *inode, int blocks)
{
    struct buffer_head *bh;
    int count;

    if (blocks > 6)
        blocks = 6;
    for (count = 0; count < blocks; count++)
    {
        if (!inode->i_zone[count + 1]) // inode->i_zone[count + 1] 存的是block号码，一个inode最多存10个block号码，对应硬盘中的10个block
            continue;
        if (!(bh = bread(inode->i_dev, inode->i_zone[count + 1])))
            return -1;
        cp_block(bh->b_data, count * BLOCK_SIZE);  /* 将数据从硬盘的block块对应的buffer_head的b_data字段拷贝到内存，以1024字节为单位 */
        brelse(bh);
    }
    return 0;
}

/* 从间接块读取数据 dev-设备号 ind-间接块的块号 size-读取的数据大小 offset-内存的偏移位置 */
int read_ind(int dev, int ind, long size, unsigned long offset)
{
    struct buffer_head *ih, *bh;
    unsigned short *table, block;

    if (size <= 0)
        panic("size<=0 in read_ind");
    if (size > 512 * BLOCK_SIZE)
        size = 512 * BLOCK_SIZE;
    if (!ind)
        return 0;
    if (!(ih = bread(dev, ind)))
        return -1;
    table = (unsigned short *)ih->b_data;
    while (size > 0)
    {
        if (block = *(table++))
            if (!(bh = bread(dev, block)))
            {
                brelse(ih);
                return -1;
            }
            else
            {
                cp_block(bh->b_data, offset);
                brelse(bh);
            }
        size -= BLOCK_SIZE;
        offset += BLOCK_SIZE;
    }
    brelse(ih);
    return 0;
}

/*
 * read_area() reads an area into %fs:mem.
 */
int read_area(struct m_inode *inode, long size)
{
    struct buffer_head *dind;
    unsigned short *table;
    int i, count;

    if ((i = read_head(inode, (size + BLOCK_SIZE - 1) / BLOCK_SIZE)) ||
        (size -= BLOCK_SIZE * 6) <= 0)
        return i;
    if ((i = read_ind(inode->i_dev, inode->i_zone[7], size, BLOCK_SIZE * 6)) ||
        (size -= BLOCK_SIZE * 512) <= 0)
        return i;
    if (!(i = inode->i_zone[8]))
        return 0;
    if (!(dind = bread(inode->i_dev, i)))
        return -1;
    table = (unsigned short *)dind->b_data;
    for (count = 0; count < 512; count++)
        if ((i = read_ind(inode->i_dev, *(table++), size,
                          BLOCK_SIZE * (518 + count))) ||
            (size -= BLOCK_SIZE * 512) <= 0)
            return i;
    panic("Impossibly long executable");
}

/*
 * create_tables() parses the env- and arg-strings in new user
 * memory and creates the pointer tables from them, and puts their
 * addresses on the "stack", returning the new stack pointer value.
 * 解析新用户空间的环境字符串和参数字符串，并从中创建指针表，并把他们放入
 * “堆栈”，并返回新的堆栈指针值。p指向用户空间环境字符串和参数字符串的指针
 * argc - 参数数量  envc - 环境变量的数量
 */
static unsigned long *create_tables(char *p, int argc, int envc)
{
    unsigned long *argv, *envp;
    unsigned long *sp;

    sp = (unsigned long *)(0xfffffffc & (unsigned long)p);
    sp -= envc + 1; /* 因为最后一个环境变量的后面用0填充，所以多一个 */
    envp = sp; /* 环境变量表指针 */
    sp -= argc + 1;
    argv = sp;  /* 参数变量表指针 */
    put_fs_long((unsigned long)envp, --sp);
    put_fs_long((unsigned long)argv, --sp);
    put_fs_long((unsigned long)argc, --sp);
    while (argc-- > 0)
    {
        put_fs_long((unsigned long)p, argv++);
        while (get_fs_byte(p++)) /* nothing */
            ;
    }
    put_fs_long(0, argv);
    while (envc-- > 0)
    {
        put_fs_long((unsigned long)p, envp++);
        while (get_fs_byte(p++)) /* nothing */
            ;
    }
    put_fs_long(0, envp);
    return sp;
}

/*
 * count() counts the number of arguments/envelopes
 */
static int count(char **argv)
{
    int i = 0;
    char **tmp;

    if (tmp = argv)
        while (get_fs_long((unsigned long *)(tmp++)))
            i++;

    return i;
}

/*
 * 'copy_string()' copies argument/envelope strings from user
 * memory to free pages in kernel mem. These are in a format ready
 * to be put directly into the top of new user memory.
 *
 * Modified by TYT, 11/24/91 to add the from_kmem argument, which specifies
 * whether the string and the string array are from user or kernel segments:
 *
 * from_kmem     argv *        argv **
 *    0          user space    user space
 *    1          kernel space  user space
 *    2          kernel space  kernel space
 *
 * We do this by playing games with the fs segment register.  Since it
 * it is expensive to load a segment register, we try to avoid calling
 * set_fs() unless we absolutely have to.
 */
static unsigned long copy_strings(int argc, char **argv, unsigned long *page,
                                  unsigned long p, int from_kmem)
{
    char *tmp, *pag;
    int len, offset = 0;
    unsigned long old_fs, new_fs;

    if (!p)
        return 0; /* bullet-proofing */
    new_fs = get_ds();
    old_fs = get_fs();
    if (from_kmem == 2)
        set_fs(new_fs);
    while (argc-- > 0)
    {
        if (from_kmem == 1)
            set_fs(new_fs);
        if (!(tmp = (char *)get_fs_long(((unsigned long *)argv) + argc)))
            panic("argc is wrong");
        if (from_kmem == 1)
            set_fs(old_fs);
        len = 0; /* remember zero-padding */
        do
        {
            len++;
        } while (get_fs_byte(tmp++));
        if (p - len < 0)
        { /* this shouldn't happen - 128kB */
            set_fs(old_fs);
            return 0;
        }
        while (len)
        {
            --p;
            --tmp;
            --len;
            if (--offset < 0)
            {
                offset = p % PAGE_SIZE;
                if (from_kmem == 2)
                    set_fs(old_fs);
                if (!(pag = (char *)page[p / PAGE_SIZE]) &&
                    !(pag = (char *)page[p / PAGE_SIZE] =
                          (unsigned long *)get_free_page()))
                    return 0;
                if (from_kmem == 2)
                    set_fs(new_fs);
            }
            *(pag + offset) = get_fs_byte(tmp);
        }
    }
    if (from_kmem == 2)
        set_fs(old_fs);
    return p;
}

/* 这段代码的作用是在操作系统中更改局部描述符表(Local Descriptor Table, LDT),主要用于设置代码段和
 * 数据段的基址和限长，并确保文件系统指向新的数据段
 */
static unsigned long change_ldt(unsigned long text_size, unsigned long *page)
{
    unsigned long code_limit, data_limit, code_base, data_base;
    int i;

    code_limit = text_size + PAGE_SIZE - 1; /* 计算代码段限长*/
    code_limit &= 0xFFFFF000;  /* 按照页面对齐进行截断 */
    data_limit = 0x4000000;      /* 设置数据段限长 */
    code_base = get_base(current->ldt[1]); /* 获取当前进程的代码段基址 */
    data_base = code_base; /* 将数据段基址设置为与代码段基址相同 */
    set_base(current->ldt[1], code_base); /* 设置当前进程的代码段基址 */
    set_limit(current->ldt[1], code_limit); /* 设置当前进程的代码段限长 */
    set_base(current->ldt[2], data_base);  /* 设置当前进程的数据段基址 */
    set_limit(current->ldt[2], data_limit); /* 设置当前进程的数据段限长 */
    /* make sure fs points to the NEW data segment */
    __asm__("pushl $0x17\n\tpop %%fs" ::);
    data_base += data_limit;
    for (i = MAX_ARG_PAGES - 1; i >= 0; i--)
    {
        data_base -= PAGE_SIZE;
        if (page[i])
            put_page(page[i], data_base);
    }
    return data_limit;
}

/*
 * 'do_execve()' executes a new program.
 */
int do_execve(unsigned long *eip, long tmp, char *filename,
              char **argv, char **envp)
{
    struct m_inode *inode;
    struct buffer_head *bh;
    struct exec ex;
    unsigned long page[MAX_ARG_PAGES];
    int i, argc, envc;
    int e_uid, e_gid;
    int retval;
    int sh_bang = 0;
    char *buf = 0;
    unsigned long p = PAGE_SIZE * MAX_ARG_PAGES - 4;

    if ((0xffff & eip[1]) != 0x000f)
        panic("execve called from supervisor mode");
    for (i = 0; i < MAX_ARG_PAGES; i++) /* clear page-table */
        page[i] = 0;
    if (!(inode = namei(filename))) /* get executables inode */
        return -ENOENT;
    argc = count(argv);
    envc = count(envp);

restart_interp:
    if (!S_ISREG(inode->i_mode))
    { /* must be regular file */
        retval = -EACCES;
        goto exec_error2;
    }
    i = inode->i_mode;
    e_uid = (i & S_ISUID) ? inode->i_uid : current->euid;
    e_gid = (i & S_ISGID) ? inode->i_gid : current->egid;
    if (current->euid == inode->i_uid)
        i >>= 6;
    else if (current->egid == inode->i_gid)
        i >>= 3;
    if (!(i & 1) &&
        !((inode->i_mode & 0111) && suser()))
    {
        retval = -ENOEXEC;
        goto exec_error2;
    }
    if (!(bh = bread(inode->i_dev, inode->i_zone[0])))
    {
        retval = -EACCES;
        goto exec_error2;
    }
    ex = *((struct exec *)bh->b_data); /* read exec-header */
    if ((bh->b_data[0] == '#') && (bh->b_data[1] == '!') && (!sh_bang))
    {
        /*
         * This section does the #! interpretation.
         * Sorta complicated, but hopefully it will work.  -TYT
         */

        char *cp, *interp, *i_name, *i_arg;
        unsigned long old_fs;

        if (!buf)
            buf = malloc(1024);
        strncpy(buf, bh->b_data + 2, 1022);
        brelse(bh);
        iput(inode);
        buf[1022] = '\0';
        if (cp = strchr(buf, '\n'))
        {
            *cp = '\0';
            for (cp = buf; (*cp == ' ') || (*cp == '\t'); cp++)
                ;
        }
        if (!cp || *cp == '\0')
        {
            retval = -ENOEXEC; /* No interpreter name found */
            goto exec_error1;
        }
        interp = i_name = cp;
        i_arg = 0;
        for (; *cp && (*cp != ' ') && (*cp != '\t'); cp++)
        {
            if (*cp == '/')
                i_name = cp + 1;
        }
        if (*cp)
        {
            *cp++ = '\0';
            i_arg = cp;
        }
        /*
         * OK, we've parsed out the interpreter name and
         * (optional) argument.
         */
        if (sh_bang++ == 0)
        {
            p = copy_strings(envc, envp, page, p, 0);
            p = copy_strings(--argc, argv + 1, page, p, 0);
        }
        /*
         * Splice in (1) the interpreter's name for argv[0]
         *           (2) (optional) argument to interpreter
         *           (3) filename of shell script
         *
         * This is done in reverse order, because of how the
         * user environment and arguments are stored.
         */
        p = copy_strings(1, &filename, page, p, 1);
        argc++;
        if (i_arg)
        {
            p = copy_strings(1, &i_arg, page, p, 2);
            argc++;
        }
        p = copy_strings(1, &i_name, page, p, 2);
        argc++;
        if (!p)
        {
            retval = -ENOMEM;
            goto exec_error1;
        }
        /*
         * OK, now restart the process with the interpreter's inode.
         */
        old_fs = get_fs();
        set_fs(get_ds());
        if (!(inode = namei(interp)))
        { /* get executables inode */
            set_fs(old_fs);
            retval = -ENOENT;
            goto exec_error1;
        }
        set_fs(old_fs);
        goto restart_interp;
    }
    brelse(bh);
    if (N_MAGIC(ex) != ZMAGIC || ex.a_trsize || ex.a_drsize ||
        ex.a_text + ex.a_data + ex.a_bss > 0x3000000 ||
        inode->i_size < ex.a_text + ex.a_data + ex.a_syms + N_TXTOFF(ex))
    {
        retval = -ENOEXEC;
        goto exec_error2;
    }
    if (N_TXTOFF(ex) != BLOCK_SIZE)
    {
        printk("%s: N_TXTOFF != BLOCK_SIZE. See a.out.h.", filename);
        retval = -ENOEXEC;
        goto exec_error2;
    }
    if (!sh_bang)
    {
        p = copy_strings(envc, envp, page, p, 0);
        p = copy_strings(argc, argv, page, p, 0);
        if (!p)
        {
            retval = -ENOMEM;
            goto exec_error2;
        }
    }
    /* OK, This is the point of no return */
    if (buf)
        free_s(buf, 1024);
    for (i = 0; i < 32; i++)
        current->sigaction[i].sa_handler = NULL;
    for (i = 0; i < NR_OPEN; i++)
        if ((current->close_on_exec >> i) & 1)
            sys_close(i);
    current->close_on_exec = 0;
    free_page_tables(get_base(current->ldt[1]), get_limit(0x0f));
    free_page_tables(get_base(current->ldt[2]), get_limit(0x17));
    if (last_task_used_math == current)
        last_task_used_math = NULL;
    current->used_math = 0;
    p += change_ldt(ex.a_text, page) - MAX_ARG_PAGES * PAGE_SIZE;
    p = (unsigned long)create_tables((char *)p, argc, envc);
    current->brk = ex.a_bss +
                   (current->end_data = ex.a_data +
                                        (current->end_code = ex.a_text));
    current->start_stack = p & 0xfffff000;
    current->euid = e_uid;
    current->egid = e_gid;
    i = read_area(inode, ex.a_text + ex.a_data);
    iput(inode);
    if (i < 0)
        sys_exit(-1);
    i = ex.a_text + ex.a_data;
    while (i & 0xfff)
        put_fs_byte(0, (char *)(i++));
    eip[0] = ex.a_entry; /* eip, magic happens :-) */
    eip[3] = p;          /* stack pointer */
    return 0;
exec_error2:
    iput(inode);
exec_error1:
    if (buf)
        free(buf);
    for (i = 0; i < MAX_ARG_PAGES; i++)
        free_page(page[i]);
    return (retval);
}
