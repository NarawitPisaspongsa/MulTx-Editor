/* S2 cpu micro architecture simulator

   5 December 2001
   s21  18 Jan 2007

   modify from s1 sim

   Computer Architecture 1997, 1998

   simplify (direct) for calculator project   29 Jan 2016
   no Z, S
   add address trace (Chinese new year)         10 Feb 2016
   add interrupt                               27 Mar 2016
   add MOS                                       1 Apr 2016
   most MOS is in assembly   (Happy Songkran)
     and fix "trap 16" bug                     13 Apr 2016

   redo for teaching interrupt programming   8 Nov 2016
   add io                                       19 Nov 2016
   update to "grandfather s21"   (Chinese NY)  28 Jan 2017
   add: readinputstring                        1 Feb 2017

   P. Chongstitvatana
   Department of Computer Engineering
   Chulalongkorn University

   Extended with Semaphore Waiting Queue Traps (#20-#22),
   Zeroed Heap Allocation (#19), and ANSI Display Helper (#23).
*/

#include "s21.h"
#include <string.h>

#define MAX_SEM     16
#define MAX_WAITERS 16

extern int timer0range, timer1range;
extern int display;

int R[32], Ir, Pc, M[MAXMEM], RetAds;
int intflag, intmask[4], intrq[4], intnum;
int runflag, savePc, cpuflag;
int heapfree = HEAP;

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

void init_s21_extensions(void){
    heapfree = HEAP;
    memset(sem_table, 0, sizeof(sem_table));
}

//extern char *cp2;        // output buffer

int signx2( int d ){   // sign bit 21 extended
    if( d & 0x0200000 ) return d | 0xFFC00000;
    return d;
}

char buf[80];        // input buffer

int myalloc(int k){
    int a, i;
    if( k <= 0 || (heapfree + k) > MAXMEM ){
        error("malloc: out of memory");
        return 0;
    }
    a = heapfree;
    heapfree += k;
    for( i = 0; i < k; i++ ){
        M[a + i] = 0;
    }
//    printf("malloc %d heapfree %d\n",k,heapfree);
    return a;
}

void trap( int reg, int num ){   // special function
    int n, i, j, x;
    int sem_id, init_val, pcb_addr, woken_pcb;

    switch( num ) {
    case 0:  // stop
        runflag = 0;
//        printf("stop, clock %d, execute %d instructions\n",clock,ninst);
        break;
    case 1:
        printf("%d ",R[reg]);
        fflush(stdout);
        break;  // print integer
    case 2:
        printf("%c",R[reg]);
        fflush(stdout);
        break;   // printc char
    case 3:                     // printstring
        i = R[reg];                // pointer to string
        x = M[i];
        j = 0;
        while( x != 0 && j < 79 ){        // convert Rz string to c string
            buf[j] = (char)x;
            j++;
            i++;
            x = M[i];
        }
        buf[j] = 0;                // terminate string
        printf("%s",buf);
        fflush(stdout);
        break;
    case 4: // input (return string)
        if( scanf("%79s",buf) == 1 ){
            i = 0;
            while( buf[i] != 0 ){        // copy to M[]
                M[STRSEG+i] = buf[i];
                i++;
            }
            M[STRSEG+i] = 0;        // terminate string
        } else {
            M[STRSEG] = 0;
        }
//        printf("%s\n",buf);
        R[RETV] = STRSEG;        // return pointer to string
        break;
//    case 5: R[RETV] = getchar();
//        printf("[%d:%c]",R[RETV],R[RETV]);
//        break;

//    case 10: printf("[%d]",R[reg]); break;        // print2
//    case 11: printf("(%d)",R[reg]); break;        // print3

    case 13: timer0range = R[reg]; break; // set timer0
    case 14: timer1range = R[reg]; break; // set timer1
    case 15:             // disable int
        n = R[reg];
        if( n >= 0 && n < 4 ) intmask[n] = 0;
        break;
    case 16:             // enable int
        n = R[reg];
        if( n >= 0 && n < 4 ) intmask[n] = 1;
        break;

    case 17: cpuflag = 0; break;   // put cpu to sleep (doze)
    case 18:
        n = R[reg];                // port number
//        printf("read port %d\n",n);
        switch(n){
        case 10: R[RETV] = readsinport(); break; // sin analog
        case 11: R[RETV] = readdigport(); break; // square
        case 12: R[RETV] = myrandom(); break;     // random num
        case 13: R[RETV] = clock; break;         // reads clock
        }
        break;
    case 19:             // malloc (Dynamic Memory Allocation Trap)
        R[RETV] = myalloc(R[reg]);
        R[reg]  = R[RETV];
        break;

    case 20:             // seminit(packed_id_and_val): (sem_id << 8) | init_val
        sem_id   = (R[reg] >> 8) & 0xFF;
        init_val = R[reg] & 0xFF;
        if( sem_id >= 0 && sem_id < MAX_SEM ){
            sem_table[sem_id].initialized = 1;
            sem_table[sem_id].val         = init_val;
            sem_table[sem_id].owner_pcb   = 0;
            sem_table[sem_id].head        = 0;
            sem_table[sem_id].tail        = 0;
            sem_table[sem_id].count       = 0;
            R[RETV] = sem_id;
            R[reg]  = sem_id;
        }
        break;

    case 21:             // wait_trap(packed_sem_and_pcb): (sem_id << 16) | pcb_addr
        sem_id   = (R[reg] >> 16) & 0xFFFF;
        pcb_addr = R[reg] & 0xFFFF;
        if( sem_id >= 0 && sem_id < MAX_SEM ){
            sem_table[sem_id].val--;
            if( sem_table[sem_id].val < 0 ){
                if( sem_table[sem_id].count < MAX_WAITERS ){
                    sem_table[sem_id].wait_q[sem_table[sem_id].tail] = pcb_addr;
                    sem_table[sem_id].tail = (sem_table[sem_id].tail + 1) % MAX_WAITERS;
                    sem_table[sem_id].count++;
                }
                R[RETV] = 1;   // 1 = Caller blocked and added to waiting queue
                R[reg]  = 1;
            } else {
                sem_table[sem_id].owner_pcb = pcb_addr;
                R[RETV] = 0;   // 0 = Lock acquired immediately
                R[reg]  = 0;
            }
        }
        break;

    case 22:             // signal_trap(sem_id)
        sem_id = R[reg];
        if( sem_id >= 0 && sem_id < MAX_SEM ){
            sem_table[sem_id].val++;
            if( sem_table[sem_id].count > 0 ){
                woken_pcb = sem_table[sem_id].wait_q[sem_table[sem_id].head];
                sem_table[sem_id].head = (sem_table[sem_id].head + 1) % MAX_WAITERS;
                sem_table[sem_id].count--;
                sem_table[sem_id].owner_pcb = woken_pcb;
                R[RETV] = woken_pcb; // Return woken PCB address to MOS kernel
                R[reg]  = woken_pcb;
            } else {
                sem_table[sem_id].owner_pcb = 0;
                R[RETV] = 0;
                R[reg]  = 0;
            }
        }
        break;

    case 23:             // ansicmd(cmd): ANSI live terminal display helper
        switch( R[reg] ){
        case 0: printf("\033[2J\033[H"); break; // Clear screen & home cursor
        case 1: printf("\033[0m");       break; // Reset attributes
        case 2: printf("\033[1;36m");    break; // Bold Cyan
        case 3: printf("\033[1;32m");    break; // Bold Green (Unlocked)
        case 4: printf("\033[1;31m");    break; // Bold Red (Locked)
        case 5: printf("\033[1;33m");    break; // Bold Yellow (Metadata)
        case 6: printf("\n");            break; // Newline
        }
        fflush(stdout);
        break;
    }
}

void interrupt(int n){
    if( n > 3 )
        error("unknown interrupt");
    printf("interrupt%d\n",n);
    intnum = n;
    RetAds = Pc;        // save pc
    R[31]  = Pc;        // mirror RetAds in R[31] for MOS PCB saves
    Pc = M[INTVEC+n];
    cpuflag = 1;         // wake up sleepy cpu
    intflag = 0;        // master disable interrupt
    intrq[n] = 0;        // clear int request
}

void run(void){       // execute one instruction
    int ads, d, r1, r2, r3, i;

    savePc = Pc;
    Ir = M[Pc];        // instruction fetch
    Pc++;
    ads = IRads();    // decode
    d = IRdisp();    // signx  17-bit
    r1 = IRr1();
    r2 = IRr2();
    r3 = IRr3();
    ninst++;
    switch( IRop() ) {
    case NOP:  break;
    case LDA:  R[r1] = M[ads]; break;
    case LDD:  R[r1] = M[R[r2]+d];  break;
    case STA:  M[ads] = R[r1];  break;
    case STD:  M[R[r2]+d] = R[r1];  break;
    case MVI:  R[r1] = signx2(ads); break;
    case JMP:  Pc = ads; break;
    case JAL:  R[r1] = Pc; Pc = ads; break;
    case JT: if( R[r1] != 0 ) Pc = ads; break;
    case JF: if( R[r1] == 0 ) Pc = ads; break;
    case ADDI: R[r1] = R[r2] + d; break;
    case SUBI: R[r1] = R[r2] - d; break;
    case MULI: R[r1] = R[r2] * d; break;
    case DIVI: R[r1] = R[r2] / d; break;
    case ANDI: R[r1] = R[r2] & d; break;
    case ORI:  R[r1] = R[r2] | d; break;
    case XORI: R[r1] = R[r2] ^ d; break;
    case EQI:  R[r1] = R[r2] == d; break;
    case NEI:  R[r1] = R[r2] != d; break;
    case LTI:  R[r1] = R[r2] <  d; break;
    case LEI:  R[r1] = R[r2] <= d; break;
    case GTI:  R[r1] = R[r2] >  d; break;
    case GEI:  R[r1] = R[r2] >= d; break;
    case SHLI: R[r1] = R[r2] << d; break;
    case SHRI: R[r1] = R[r2] >> d; break;
    case MODI: R[r1] = R[r2] %  d; break;
    case XOP:
        switch( IRxop() ){
        case ADD:  R[r1] = R[r2] + R[r3]; break;
        case SUB:  R[r1] = R[r2] - R[r3]; break;
        case MUL:  R[r1] = R[r2] * R[r3]; break;
        case DIV:  R[r1] = R[r2] / R[r3]; break;
        case AND:  R[r1] = R[r2] & R[r3]; break;
        case OR:   R[r1] = R[r2] | R[r3]; break;
        case XOR:  R[r1] = R[r2] ^ R[r3]; break;
        case EQ:   R[r1] = R[r2] == R[r3]; break;
        case NE:   R[r1] = R[r2] != R[r3]; break;
        case LT:   R[r1] = R[r2] <  R[r3]; break;
        case LE:   R[r1] = R[r2] <= R[r3]; break;
        case GT:   R[r1] = R[r2] >  R[r3]; break;
        case GE:   R[r1] = R[r2] >= R[r3]; break;
        case SHL:  R[r1] = R[r2] << R[r3]; break;
        case SHR:  R[r1] = R[r2] >> R[r3]; break;
        case MOD:  R[r1] = R[r2] %  R[r3]; break;
        case MOV:  R[r1] = R[r2]; break;
        case LDX:  R[r1] = M[R[r2] + R[r3]]; break;
        case STX:  M[R[r2] + R[r3]] = R[r1]; break;
        case RET:  Pc = R[r1]; break;
        case TRAP: trap(r1,r2); break;
        case PUSH:
            R[r1]++;            // increment stack pointer
            M[R[r1]] = R[r2];
            break;
        case POP:
            R[r2] = M[R[r1]];
            R[r1]--;            // decrement stack pointer
            break;
        case NOT:  R[r1] = (R[r2] == 0) ? ~0 : 0;  break;
        case INT:  interrupt(r1); break;
        case RETI:
            if( R[31] != RetAds && R[31] != 0 ) RetAds = R[31];
            Pc = RetAds;
            intflag = 1;    // enable interrupt
            break;
        case PUSHM:                // push r0..15 to stack
            for(i = 0; i < 16; i++){
                R[r1]++;        // sp++
                M[R[r1]] = R[i];
            }
            break;
        case POPM:                // pop stack to r15..r0
            for(i = 15; i >= 0; i--){
                R[i] = M[R[r1]];
                R[r1]--;        // sp--
            }
            R[0] = 0;
            break;
        case XCH:                // exchange RetAds:R[r1]
            d = RetAds;
            RetAds = R[r1];
            R[r1] = d;
            R[31] = RetAds;
            break;
        case SWI: cpuflag = 0; break; // put cpu to sleep
        default:
//            printf("xop %d\n",IRxop());
            error("undefine xop");
        }
        break;
    default:
        error("undefine op");
    }
//    if( (Ir & 0xFFC00FFF) == 0xFC000013 )   // trap 16, ei
//         ;     // do not interrupt just after ei
//    else

}

void checkinterrupt(void){
    if( intflag ){
        if( intmask[0] && intrq[0] ) interrupt(0);
        else if( intmask[1] && intrq[1] ) interrupt(1);
        else if( intmask[2] && intrq[2] ) interrupt(2);
        else if( intmask[3] && intrq[3] ) interrupt(3);
    }
}

//  run one instruction and check interrupt
void runoneclock(void){
    clock++;
    simdevices();
    if( runflag && cpuflag ){
        run();
        if( display ) show();
    }
//    if( (Ir & 0xF83E0FFF) == 0xF8200014 )   // trap 16, ei
//         ;     // do not interrupt just after ei
//    else
        checkinterrupt();
}

int main(int argc, char *argv[]){
    if( argc < 2 ) {
        printf("usage : sim21 objfile\n");
        exit(0);
    }
//    if( *argv[1] == '-' ) {
//        printf("execute %s %s\n",argv[1],argv[2]);
//        exit(0);
//    }
//    loadprogram(argv[1]);
//    bindsym();

    display = 1;
    init_io();
    init_s21_extensions();
    clock = 0;
    ninst = 0;
    Pc = 0;
    runflag = 1;
    cpuflag = 1;
    intflag = 1;    // master enable interrupt
    intmask[0] = 1;
    intmask[1] = 0;
    intmask[2] = 0;
    intmask[3] = 0;

    if( *argv[1] == '-' && *(argv[1]+1) == 'x' ){
        loadprogram(argv[2]);
        execute();
    }else{
        loadprogram(argv[1]);
        interp();
//        printf("stop, clock %d, execute %d instructions\n",clock,ninst);
    }
    return 0;
}
