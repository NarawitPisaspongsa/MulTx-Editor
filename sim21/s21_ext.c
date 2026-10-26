/* ============================================================================

 * s21_extensions.c — Drop-in Extensions for S21 Simulator (s21.c / sim21)

 * Implements:

 *   1. Extended instructions: int (xop 23), reti (xop 24), pushm (xop 26),

 *      popm (xop 27), xch (xop 28), swi (xop 29)

 *   2. Trap #19: Dynamic Memory Allocation (alloc / malloc)

 *   3. Trap #20: Semaphore Initialization (seminit)

 *   4. Trap #21: Semaphore Wait with FIFO Waiting List (wait_trap)

 *   5. Trap #22: Semaphore Signal with FIFO Wakeup (signal_trap)

 *   6. Trap #23: ANSI Terminal Control Helper (ansicmd)

 * ============================================================================ */

#include <stdio.h>

#include <stdlib.h>

#include <string.h>

#define MAXMEM      32768

#define HEAP_BASE   10000

#define HEAP_LIMIT  19999

#define MAX_SEM     16

#define MAX_WAITERS 16

/* Standard S21 simulator globals (from s21.h / s21.c) */

extern int M[MAXMEM];

extern int R[32];

extern int PC;

extern int RetAds;       /* Hardware interrupt return address register (or R[31]) */

extern int int_enable[4];

extern int in_isr;       /* 1 when inside ISR, 0 otherwise */

extern int sleep_flag;   /* 1 when CPU is sleeping via swi / doze() */

extern int timer0_interval;

extern int run_flag;

/* Heap pointer for Trap #19 (Dynamic Memory Allocation) */

static int heap_ptr = HEAP_BASE;

/* Hardware-Assisted Semaphore Structure with FIFO Waiting List */

typedef struct {

    int initialized;

    int val;

    int owner_pcb;                 /* PCB address of current holder (or 0) */

    int wait_q[MAX_WAITERS];       /* Circular FIFO queue of blocked PCB addresses */

    int head;

    int tail;

    int count;

} SimSemaphore;

static SimSemaphore sem_table[MAX_SEM];

/* Initialize heap and semaphore table on simulator reset */

void init_s21_extensions(void) {

    heap_ptr = HEAP_BASE;

    memset(sem_table, 0, sizeof(sem_table));

}

/* ----------------------------------------------------------------------------

 * Extended Opcode (X-format, op == 31) Handler Additions for s21.c

 * ---------------------------------------------------------------------------- */

void exec_extended_os_instructions(int xop, int r1, int r2, int r3) {

    int i, tmp;

    switch (xop) {

        case 23: /* int r1 : Software Interrupt */

            RetAds = PC;

            R[31]  = PC;

            in_isr = 1;

            sleep_flag = 0;

            PC = M[1000]; /* Jump to interrupt vector at M[1000] */

            break;

        case 24: /* reti : Return from Interrupt */

            PC = R[31];   /* Restore interrupted PC from R[31] / RetAds */

            RetAds = R[31];

            in_isr = 0;

            int_enable[0] = 1;

            break;

        case 26: /* pushm r1 : Push registers r0..r15 onto stack R[r1] */

            for (i = 0; i <= 15; i++) {

                R[r1]++;

                M[R[r1]] = R[i];

            }

            break;

        case 27: /* popm r1 : Pop registers r15..r0 from stack R[r1] */

            for (i = 15; i >= 0; i--) {

                R[i] = M[R[r1]];

                R[r1]--;

            }

            R[0] = 0; /* Enforce r0 == 0 */

            break;

        case 28: /* xch r1 : Exchange R[r1] with RetAds (R[31]) */

            tmp = R[r1];

            R[r1] = R[31];

            R[31] = tmp;

            RetAds = R[31];

            break;

        case 29: /* swi : Sleep and wait for interrupt (doze) */

            sleep_flag = 1;

            break;

    }

}

/* ----------------------------------------------------------------------------

 * Extended Trap Handler (op == 31, xop == 20: trap r1 #n)

 * Convention:

 *   r1 holds the primary argument register.

 *   n (encoded in r2 field of instruction) is the trap number (0..31).

 *   Return values are placed in R[29] (retv) and R[r1].

 * ---------------------------------------------------------------------------- */

void exec_trap(int r1, int trap_no) {

    int i, size, base, sem_id, init_val, pcb_addr, woken_pcb;

    switch (trap_no) {

        case 0: /* trap r0 #0 : Stop simulation */

            run_flag = 0;

            break;

        case 1: /* trap r1 #1 : Print integer */

            printf("%d ", R[r1]);

            fflush(stdout);

            break;

        case 2: /* trap r1 #2 : Print ASCII character */

            printf("%c", (char)(R[r1] & 0xFF));

            fflush(stdout);

            break;

        case 3: /* trap r1 #3 : Print null-terminated word-string at M[R[r1]] */

            i = R[r1];

            while (i >= 0 && i < MAXMEM && M[i] != 0) {

                printf("%c", (char)(M[i] & 0xFF));

                i++;

            }

            fflush(stdout);

            break;

        case 13: /* trap r1 #13 : settimer0(R[r1]) */

            timer0_interval = R[r1];

            break;

        case 15: /* trap r1 #15 : disable interrupt (or pushm in legacy mos2) */

            if (R[r1] >= 0 && R[r1] < 4) int_enable[R[r1]] = 0;

            break;

        case 16: /* trap r1 #16 : enable interrupt (or popm in legacy mos2) */

            if (R[r1] >= 0 && R[r1] < 4) int_enable[R[r1]] = 1;

            break;

        case 17: /* trap r0 #17 : doze() / sleep and wait for interrupt */

            sleep_flag = 1;

            break;

        case 19: /* trap r1 #19 : Dynamic Memory Allocation (alloc(size)) */

            size = R[r1];

            if (size <= 0 || (heap_ptr + size) > HEAP_LIMIT) {

                printf("\n[SIM21 ERROR] Heap exhausted (requested %d words)\n", size);

                R[29] = 0;

                R[r1] = 0;

            } else {

                base = heap_ptr;

                for (i = 0; i < size; i++) {

                    M[base + i] = 0;

                }

                heap_ptr += size;

                R[29] = base; /* Return base address in retv (r29) */

                R[r1] = base;

            }

            break;

        case 20: /* trap r1 #20 : seminit(sem_id, init_val)

                  * Packed argument in R[r1]: (sem_id << 16) | (init_val & 0xFFFF)

                  * Or if init_val is 1 by default when high bits are 0: */

            sem_id   = (R[r1] >> 8) & 0xFF;

            init_val = R[r1] & 0xFF;

            if (sem_id >= 0 && sem_id < MAX_SEM) {

                sem_table[sem_id].initialized = 1;

                sem_table[sem_id].val         = init_val;

                sem_table[sem_id].owner_pcb   = 0;

                sem_table[sem_id].head        = 0;

                sem_table[sem_id].tail        = 0;

                sem_table[sem_id].count       = 0;

                R[29] = sem_id;

            }

            break;

        case 21: /* trap r1 #21 : wait_trap(packed_sem_and_pcb)

                  * Input R[r1]: (sem_id << 16) | (current_pcb_addr & 0xFFFF)

                  * Output R[29] (retv):

                  *   0 = Acquired immediately (no block needed)

                  *   1 = Blocked (current_pcb_addr added to sem_table[sem_id].wait_q) */

            sem_id   = (R[r1] >> 16) & 0xFFFF;

            pcb_addr = R[r1] & 0xFFFF;

            if (sem_id >= 0 && sem_id < MAX_SEM) {

                sem_table[sem_id].val--;

                if (sem_table[sem_id].val < 0) {

                    /* Add caller PCB to semaphore FIFO waiting list */

                    if (sem_table[sem_id].count < MAX_WAITERS) {

                        sem_table[sem_id].wait_q[sem_table[sem_id].tail] = pcb_addr;

                        sem_table[sem_id].tail = (sem_table[sem_id].tail + 1) % MAX_WAITERS;

                        sem_table[sem_id].count++;

                    }

                    R[29] = 1; /* Caller must block and yield via tswitch */

                    R[r1] = 1;

                } else {

                    sem_table[sem_id].owner_pcb = pcb_addr;

                    R[29] = 0; /* Lock acquired! */

                    R[r1] = 0;

                }

            }

            break;

        case 22: /* trap r1 #22 : signal_trap(sem_id)

                  * Input R[r1]: sem_id

                  * Output R[29] (retv):

                  *   0 = No waiting thread needed waking

                  *   >0 = PCB address of the woken thread popped from FIFO wait_q */

            sem_id = R[r1];

            if (sem_id >= 0 && sem_id < MAX_SEM) {

                sem_table[sem_id].val++;

                if (sem_table[sem_id].count > 0) {

                    woken_pcb = sem_table[sem_id].wait_q[sem_table[sem_id].head];

                    sem_table[sem_id].head = (sem_table[sem_id].head + 1) % MAX_WAITERS;

                    sem_table[sem_id].count--;

                    sem_table[sem_id].owner_pcb = woken_pcb;

                    R[29] = woken_pcb; /* Return woken PCB address to MOS kernel */

                    R[r1] = woken_pcb;

                } else {

                    sem_table[sem_id].owner_pcb = 0;

                    R[29] = 0;

                    R[r1] = 0;

                }

            }

            break;

        case 23: /* trap r1 #23 : ANSI Terminal Control Helper (ansicmd(cmd))

                  * cmd 0: Clear screen & home cursor (\033[2J\033[H)

                  * cmd 1: Reset ANSI attributes (\033[0m)

                  * cmd 2: Bold Cyan header (\033[1;36m)

                  * cmd 3: Bold Green [UNLOCKED / IDLE] (\033[1;32m)

                  * cmd 4: Bold Red [LOCKED / WRITING] (\033[1;31m)

                  * cmd 5: Bold Yellow [WAITING / METADATA] (\033[1;33m)

                  * cmd 6: Print newline (\n) */

            switch (R[r1]) {

                case 0: printf("\033[2J\033[H"); break;

                case 1: printf("\033[0m"); break;

                case 2: printf("\033[1;36m"); break;

                case 3: printf("\033[1;32m"); break;

                case 4: printf("\033[1;31m"); break;

                case 5: printf("\033[1;33m"); break;

                case 6: printf("\n"); break;

            }

            fflush(stdout);

            break;

    }

}
