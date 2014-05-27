#include "schedos-app.h"
#include "x86sync.h"
/*****************************************************************************
 * schedos-1
 *
 *   This tiny application prints red "1"s to the console.
 *   It yields the CPU to the kernel after each "1" using the sys_yield()
 *   system call.  This lets the kernel (schedos-kern.c) pick another
 *   application to run, if it wants.
 *
 *   The other schedos-* processes simply #include this file after defining
 *   PRINTCHAR appropriately.
 *
 *****************************************************************************/

/***********************************/
/* Exercise 6. Uncomment to enable */
/***********************************/
// #define EX6

/***********************************/
/* Exercise 8. Uncomment to enable */
/***********************************/
// #define EX8

#ifndef PRINTCHAR
#define PRINTCHAR	('1' | 0x0C00)
#endif

void
start(void)
{
	int i;

	for (i = 0; i < RUNCOUNT; i++) {
		// Write characters to the console, yielding after each one.
#if defined(EX6) && !defined(EX8)
		sys_printc(PRINTCHAR);
#elif defined(EX8)
		lock_acquire(&cursor_lock);
		*cursorpos++ = PRINTCHAR;
		lock_release(&cursor_lock);
#else
		*cursorpos++ = PRINTCHAR;
#endif
		sys_yield();
	}

	sys_exit(0); // replaced the infinite loop of sys_yield()
}
