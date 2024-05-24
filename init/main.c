/*
 *  linux/init/main.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include "../include/unistd.h"
#include "../include/time.h"

/*
 * we need this inline - forking from kernel space will result
 * in NO COPY ON WRITE (!!!), until an execve is executed. This
 * is no problem, but for the stack. This is handled by not letting
 * main() use the stack at all after fork(). Thus, no function
 * calls - which means inline code for fork too, as otherwise we
 * would use the stack upon exit from 'fork()'.
 *
 * Actually only pause and fork are needed inline, so that there
 * won't be any messing with the stack from main(), but we define
 * some others too.
 * 这些系统调用函数为什么使用内联定义而不是普通函数定义？
 * 使用内联的原因是为了规避这些系统调用时使用栈，至于为什么要避免使用栈，有以下原因：
 * 1. 减少函数调用开销：普通函数调用会涉及到栈操作，包括参数的压栈和返回地址的保存。而内联则是在调用处直接展开。
 *    对于频繁的系统调用，内联以提高效率。
 * 2. 避免栈操作带来的问题：在 fork 系统调用中，如果在main函数中调用普通函数，会导致栈的使用，而在fork调用后，
 *    子进程会继承父进程的栈，可能会导致意外的问题。
 */
static inline _syscall0(int, fork)  /* 创建一个新进程，新进程是调用进程的子进程。这个系统调用用来实现进程的复制，创建一个新的进程，新进程和原来进程的
                                     * 代码段、数据段、堆栈等都是一样的。fork系统调用不需要参数。
									 * 在fork系统调用中，Copy-on-Write(写时复制)用于延迟分配和复制物理内存页给子进程，直到这些副本真正需要被修改时
									 * 才进程复制。当一个进程调用fork创建子进程时，子进程最初与父进程共享相同的物理内存页。
									 * 当父进程或子进程尝试修改内存页时，Copy-on-Write机制就会生效。此时，操作系统会捕获这个修改尝试，然后为修改的进程
									 * 分配一个新的物理内存页，将修改之后的数据复制到新的内存页中。这样父进程和子进程就各有拥有自己的物理内存页。
									 * Copy-on-Write是在内核态实现的。
									 */
static inline _syscall0(int, pause) /* 使调用进程挂起(暂停)直到收到一个信号，这个系统调用通常用于进程间的同步。 */
static inline _syscall1(int, setup, void *, BIOS) /* 用于设置系统的一些参数，参数是一个void* 类型的指针 */
static inline _syscall0(int, sync)  /* 用于将文件系统的缓冲区数据写入磁盘，确保数据持久化 */

#include "../include/linux/tty.h"
#include "../include/linux/sched.h"
#include "../include/linux/head.h"
#include "../include/asm/system.h"
#include "../include/asm/io.h"

#include "../include/stddef.h"
#include "../include/stdarg.h"
#include "../include/unistd.h"
#include "../include/fcntl.h"
#include "../include/sys/types.h"

#include "../include/linux/fs.h"

static char printbuf[1024];

extern int  vsprintf();       	 /* 定义在 kernel/vsprintf.c 中 */
extern void init(void);       	 /* 在C语言中，函数声明默认是extern的，即使在同一个文件中定义了函数，也可以用extern来声明 */
extern void blk_dev_init(void);  /* 块设备初始化，硬盘、软盘 */
extern void chr_dev_init(void);  /* 字符设备初始化，键盘、鼠标*/
extern void hd_init(void);       /* 硬盘初始化 */
extern void floppy_init(void);   /* 软盘初始化 */
extern void mem_init(long start, long end); /* 内存初始化 */
extern long kernel_mktime(struct tm *tm);   /* 内核初始化时间 */
extern long startup_time;

/*
 * This is set up by the setup-routine at boot-time
 */
#define EXT_MEM_K (*(unsigned short *)0x90002)
#define DRIVE_INFO (*(struct drive_info *)0x90080)
#define ORIG_ROOT_DEV (*(unsigned short *)0x901FC)

/*
 * Yeah, yeah, it's ugly, but I cannot find how to do this correctly
 * and this seems to work. I anybody has more info on the real-time
 * clock I'd be interested. Most of this was trial and error, and some
 * bios-listing reading. Urghh.
 * 主板上有一个CMOS芯片，读取该芯片的特定物理地址即可初始化的时候获取时间
 */

#define CMOS_READ(addr) ({     \
	outb_p(0x80 | addr, 0x70); \
	inb_p(0x71);               \
})

/* 
 * 将二进制码(Binary-Coded-Decimal)转换为二进制数值，val的范围是0-99，其二进制码一共8个bit。
 * BCD码通常是4位bit，可用来表示0到9这10个数码，每个十进制数字用4位二进制数来表示。
 * ((val) & 15) 获取个位数值，((val) >> 4)，val右移4位获取十位数值，乘以10获得十位值。
 * 相加获得对应的二进制数值。
 * 我猜这个与早期的CMOS编码有关系.
*/
#define BCD_TO_BIN(val) ((val) = ((val) & 15) + ((val) >> 4) * 10)

static void time_init(void)
{
	struct tm time;

	do
	{
		time.tm_sec = CMOS_READ(0);
		time.tm_min = CMOS_READ(2);
		time.tm_hour = CMOS_READ(4);
		time.tm_mday = CMOS_READ(7);
		time.tm_mon = CMOS_READ(8);
		time.tm_year = CMOS_READ(9);
	} while (time.tm_sec != CMOS_READ(0));
	BCD_TO_BIN(time.tm_sec);
	BCD_TO_BIN(time.tm_min);
	BCD_TO_BIN(time.tm_hour);
	BCD_TO_BIN(time.tm_mday);
	BCD_TO_BIN(time.tm_mon);
	BCD_TO_BIN(time.tm_year);
	time.tm_mon--;
	startup_time = kernel_mktime(&time);
}

static long memory_end = 0;
static long buffer_memory_end = 0;

struct drive_info
{
	char dummy[32];
} drive_info;

void main(void) /* This really IS void, no error here. */
{				/* The startup routine assumes (well, ...) this */
	/*
	 * Interrupts are still disabled. Do necessary setups, then
	 * enable them
	 */
	ROOT_DEV = ORIG_ROOT_DEV;
	drive_info = DRIVE_INFO;
	memory_end = (1 << 20) + (EXT_MEM_K << 10);
	memory_end &= 0xfffff000;    // memory_end 只有前20位是有效的
	if (memory_end > 16 * 1024 * 1024)
		memory_end = 16 * 1024 * 1024;  // memory_end最大是16MB
	if (memory_end > 6 * 1024 * 1024)
		buffer_memory_end = 2 * 1024 * 1024;  // buffer_memory_end 最大是2MB
	else
		buffer_memory_end = 1 * 1024 * 1024;
	mem_init(buffer_memory_end, memory_end);  // 将内存页标记为未使用
	trap_init();
	blk_dev_init();
	chr_dev_init();
	tty_init();
	time_init();
	sched_init();
	buffer_init(buffer_memory_end);
	hd_init();
	floppy_init();
	sti();  // 汇编指令,用于设置处理器的中断允许位,即打开中断,允许处理器响应外部中断.
	move_to_user_mode(); // 将处理器从内核模式切换到用户模式,以便用户程序可以安全运行并访问系统资源.
	if (!fork())  // fork()函数创建一个子进程,如果返回0,表示子进程,进入if语句
	{ /* we count on this going ok */
		init();   // 子进程初始化环境和状态
	}
	/*
	 *   NOTE!!   For any other task 'pause()' would mean we have to get a
	 * signal to awaken, but task0 is the sole exception (see 'schedule()')
	 * as task 0 gets activated at every idle moment (when no other tasks
	 * can run). For task0 'pause()' just means we go check if some other
	 * task can run, and if not we return here.
	 * 对于task0任务(即内核初始化任务),pause并不会让任务真正休眠等待信号唤醒,而是
	 * 在空闲时刻检查是否有其他任务可以运行,如果没有则返回.
	 */
	for (;;)
		pause();
}

static int printf(const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	write(1, printbuf, i = vsprintf(printbuf, fmt, args));
	va_end(args);
	return i;
}

static char *argv[] = {"-/bin/sh", NULL};
static char *envp[] = {"HOME=/usr/root", NULL};

void init(void)
{
	int i, j;

	setup((void *)&drive_info); // 设置驱动信息
	if (!fork()) // 创建一个子进程,用于执行下面一行代码
		_exit(execve("/bin/update", NULL, NULL)); //子进程执行execve系统调用,加载并执行/bin/update程序,这是个系统更新操作.
	(void)open("/dev/tty0", O_RDWR, 0); // 以读写的方式打开控制台设备 /dev/tty0
	(void)dup(0); // 复制文件描述符0(标准输入)
	(void)dup(0);
	printf("%d buffers = %d bytes buffer space\n\r", NR_BUFFERS,
		   NR_BUFFERS * BLOCK_SIZE);
	printf("Free mem: %d bytes\n\r", memory_end - buffer_memory_end);
	printf(" Ok.\n\r");
	if ((i = fork()) < 0)
		printf("Fork failed in init\r\n");
	else if (!i) // 子进程
	{
		close(0); // 关闭标准输入
		close(1); // 关闭标准输出
		close(2); // 关闭错误输出
		setsid();  // 设置新会话组ID
		(void)open("/dev/tty0", O_RDWR, 0); // 重新打开控制台设备
		(void)dup(0);                       // 复制文件描述符0
		(void)dup(0);
		_exit(execve("/bin/sh", argv, envp)); // 加载并执行 /bin/sh , 启动一个shell
	}
	j = wait(&i); // 等待子进程结束,并获取子进程的退出状态.
	printf("child %d died with code %04x\n", j, i);
	sync();   /* 将缓冲区数据写入磁盘 */
	_exit(0); /* NOTE! _exit, not exit() */
}
