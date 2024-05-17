/*
*       boot.s

        boot.s is loaded at 0x7c00 by the bios-startup routines, and moves itself
        out of the way to address 0x90000, and jumps there.

        It then loads the system at 0x10000, using BIOS interrupts. Thereafter
        it disables all interrupts, moves the system down to 0x0000, changes
        to protected mode, and calls the start of system. System then must
        RE-initialize the protected mode in it's own tables, and enable
        interrupts as needed.
  
        NOTE! currently system is at most 8*65536 bytes long. This should be no
        problem, even in the future. I want to keep it simple. This 512 kB
        kernel size should be enough - in fact more would mean we'd have to move
        not just these start-up routines, but also do something about the cache-
        memory (block IO devices). The area left over in the lower 640 kB is meant
        for these. No other memory is assumed to be "physical", ie all memory
        over 1Mb is demand-paging. All addresses under 1Mb are guaranteed to match
        their physical addresses.
  
        NOTE1 abouve is no longer valid in it's entirety. cache-memory is allocated
        above the 1Mb mark as well as below. Otherwise it is mainly correct.
  
        NOTE 2! The boot disk type must be set at compile-time, by setting
        the following equ. Having the boot-up procedure hunt for the right
        disk type is severe brain-damage.
        The loader has been made as simple as possible (had to, to get it
        in 512 bytes with the code to move to protected mode), and continuos
        read errors will result in a unbreakable loop. Reboot by hand. It
        loads pretty fast by getting whole sectors at a time whenever possible.
  
        1.44Mb disks:
        sectors = 18
        1.2Mb disks:
        sectors = 15
        720kB disks:
        sectors = 9
*/ 