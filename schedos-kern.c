#include "schedos-kern.h"
#include "x86.h"
#include "lib.h"

/*****************************************************************************
 * schedos-kern
 *
 *   This is the schedos's kernel.
 *   It sets up process descriptors for the 4 applications, then runs
 *   them in some schedule.
 *
 *****************************************************************************/

// The program loader loads 4 processes, starting at PROC1_START, allocating
// 1 MB to each process.
// Each process's stack grows down from the top of its memory space.
// (But note that SchedOS processes, like MiniprocOS processes, are not fully
// isolated: any process could modify any part of memory.)

#define NPROCS		5
#define PROC1_START	0x200000
#define PROC_SIZE	0x100000

// +---------+-----------------------+--------+---------------------+---------/
// | Base    | Kernel         Kernel | Shared | App 0         App 0 | App 1
// | Memory  | Code + Data     Stack | Data   | Code + Data   Stack | Code ...
// +---------+-----------------------+--------+---------------------+---------/
// 0x0    0x100000               0x198000 0x200000              0x300000
//
// The program loader puts each application's starting instruction pointer
// at the very top of its stack.
//
// System-wide global variables shared among the kernel and the four
// applications are stored in memory from 0x198000 to 0x200000.  Currently
// there is just one variable there, 'cursorpos', which occupies the four
// bytes of memory 0x198000-0x198003.  You can add more variables by defining
// their addresses in schedos-symbols.ld; make sure they do not overlap!


// A process descriptor for each process.
// Note that proc_array[0] is never used.
// The first application process descriptor is proc_array[1].
static process_t proc_array[NPROCS];

// A pointer to the currently running process.
// This is kept up to date by the run() function, in mpos-x86.c.
process_t *current;

// The preferred scheduling algorithm.
int scheduling_algorithm;


// random number generator
uint32_t seed;

void
srand(uint32_t s)
{
	seed = s;
}

uint32_t 
rand(void)
{
	seed = (seed * 1103515245 + 12345);
	return seed;
}

/*****************************************************************************
 * start
 *
 *   Initialize the hardware and process descriptors, then run
 *   the first process.
 *
 *****************************************************************************/

void
start(void)
{
	int i;

	// Set up hardware (schedos-x86.c)
	segments_init();
	interrupt_controller_init(0);
	console_clear();

	// Initialize process descriptors as empty
	memset(proc_array, 0, sizeof(proc_array));
	for (i = 0; i < NPROCS; i++) {
		proc_array[i].p_pid = i;
		proc_array[i].p_state = P_EMPTY;
		// algorithm 2
		proc_array[i].p_priority = proc_array[i].p_share = i;
		proc_array[i].p_sched_count = 0;
	}

	// Set up process descriptors (the proc_array[])
	for (i = 1; i < NPROCS; i++) {
		process_t *proc = &proc_array[i];
		uint32_t stack_ptr = PROC1_START + i * PROC_SIZE;

		// Initialize the process descriptor
		special_registers_init(proc);

		// Set ESP
		proc->p_registers.reg_esp = stack_ptr;

		// Load process and set EIP, based on ELF image
		program_loader(i - 1, &proc->p_registers.reg_eip);

		// Mark the process as runnable!
		proc->p_state = P_RUNNABLE;
	}

	// Initialize the cursor-position shared variable to point to the
	// console's first character (the upper left).
	cursorpos = (uint16_t *) 0xB8000;

	//initialize cursor lock to unlocked
	cursor_lock = 0;

	// Initialize the scheduling algorithm.
	scheduling_algorithm = 0;

	srand(read_cycle_counter());

	// Switch to the first process.
	run(&proc_array[1]);

	// Should never get here!
	while (1)
		/* do nothing */;
}



/*****************************************************************************
 * interrupt
 *
 *   This is the weensy interrupt and system call handler.
 *   The current handler handles 4 different system calls (two of which
 *   do nothing), plus the clock interrupt.
 *
 *   Note that we will never receive clock interrupts while in the kernel.
 *
 *****************************************************************************/

void
interrupt(registers_t *reg)
{
	// Save the current process's register state
	// into its process descriptor
	current->p_registers = *reg;

	switch (reg->reg_intno) {

	case INT_SYS_YIELD:
		// The 'sys_yield' system call asks the kernel to schedule
		// the next process.
		schedule();

	case INT_SYS_EXIT:
		// 'sys_exit' exits the current process: it is marked as
		// non-runnable.
		// The application stored its exit status in the %eax register
		// before calling the system call.  The %eax register has
		// changed by now, but we can read the application's value
		// out of the 'reg' argument.
		// (This shows you how to transfer arguments to system calls!)
		current->p_state = P_ZOMBIE;
		current->p_exit_status = reg->reg_eax;
		schedule();

	case INT_SYS_SETPRIORITY:
		// 'sys_user*' are provided for your convenience, in case you
		// want to add a system call.
		/* Your code here (if you want). */
		current->p_priority = (int) current->p_registers.reg_eax;
		schedule();

	case INT_SYS_SETSHARE: {
		// is equivalent to killing. disallow this.
		if (current->p_registers.reg_eax == 0)
			current->p_registers.reg_eax = -1;
		else {
			current->p_share = current->p_registers.reg_eax;
			current->p_registers.reg_eax = 0;
		} 
		schedule();
	}
	case INT_SYS_PRINTC:
		*cursorpos++ = (uint16_t) current->p_registers.reg_eax;
		run(current);

	case INT_CLOCK:
		// A clock interrupt occurred (so an application exhausted its
		// time quantum).
		// Switch to the next runnable process.
		schedule();

	default:
		while (1)
			/* do nothing */;

	}
}

/*****************************************************************************
 * schedule
 *
 *   This is the weensy process scheduler.
 *   It picks a runnable process, then context-switches to that process.
 *   If there are no runnable processes, it spins forever.
 *
 *   This function implements multiple scheduling algorithms, depending on
 *   the value of 'scheduling_algorithm'.  We've provided one; in the problem
 *   set you will provide at least one more.
 *
 *****************************************************************************/

void
schedule(void)
{
	pid_t pid = current->p_pid;

	if (scheduling_algorithm == 0)
		while (1) {
			pid = (pid + 1) % NPROCS;

			// Run the selected process, but skip
			// non-runnable processes.
			// Note that the 'run' function does not return.
			if (proc_array[pid].p_state == P_RUNNABLE)
				run(&proc_array[pid]);
		}
	// exercise 2
	else if (scheduling_algorithm == 1)
		while (1) {
			for (pid = 0; pid < NPROCS; pid++)
				if (proc_array[pid].p_state == P_RUNNABLE)
					run(&proc_array[pid]);
		}
	// exercise 4A. Priority Scheduling (processes can set priority).
	else if (scheduling_algorithm == 2) {
		int highest = ~(1 << 31);
		int i;
		for (i = 1; i < NPROCS; i++)
			if (proc_array[i].p_state == P_RUNNABLE && proc_array[i].p_priority < highest) 
				highest = proc_array[i].p_priority;
		// start at next pid to alternate b/w highest priority processes
		while (1) {
			pid = (pid + 1) % NPROCS;
			if (proc_array[pid].p_state == P_RUNNABLE && proc_array[pid].p_priority == highest)
				run(&proc_array[pid]);
		}
	}
	// exercise 4B. Proportional Share Scheduling
	else if (scheduling_algorithm == 3) {
		while (1) {
			int i;
			// try to schedule process that has not yet consumed its shares
			for (i = 0; i < NPROCS; i++) {
				if (proc_array[i].p_state == P_RUNNABLE && proc_array[i].p_sched_count < proc_array[i].p_share) {
					proc_array[i].p_sched_count++;
					run(&proc_array[i]);
				}
			}

			// if we get here then either there are no runnable processes or each process has consumed its share and can have its count set back to zero
			for (i = 0; i < NPROCS; i++) {
				if (proc_array[i].p_state == P_RUNNABLE && proc_array[i].p_sched_count == proc_array[i].p_share)
					proc_array[i].p_sched_count = 0;
			}
		}
	}
	// Exercise 7. Lottery scheduling
	else if (scheduling_algorithm == 4) {
		int total_shares = 0;
		int i;
		for (i = 0; i < NPROCS; i++)
			total_shares += proc_array[i].p_state == P_RUNNABLE ? proc_array[i].p_share : 0;

		int r = rand() % total_shares;

		for (i = 0; i < NPROCS; i++) {
			if (proc_array[i].p_state == P_RUNNABLE && r >= total_shares - proc_array[i].p_share)
				run(&proc_array[i]);
			else if (proc_array[i].p_state == P_RUNNABLE)
				total_shares -= proc_array[i].p_share;
		}

		while (1);
	}

	// If we get here, we are running an unknown scheduling algorithm.
	cursorpos = console_printf(cursorpos, 0x100, "\nUnknown scheduling algorithm %d\n", scheduling_algorithm);
	while (1)
		/* do nothing */;
}
