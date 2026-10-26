
// mon.c  monitor for command line interpreter

#include "s21.h"

#define MXBUF  80
#define MXOBUF 2000
#define MXBP  20
#define sep  "\t\n "

extern int runflag;
extern int R[32], Ir, Pc, M[], RetAds;

int Q, nump, currentp, qq, sem1, sem2;

void run(void);
void show(void);

int abreak, nbp;					// breakpoint address
char ibuf[MXBUF], *cp = NULL;		// input buffer
FILE *fi, *fo;
int display;

PRIVATE char *tok(void){	// get one token from input
	if( cp != NULL ) cp = strtok(NULL,sep);
	while ( cp == NULL ){
		printf(">");
		cp = fgets(ibuf,MXBUF,fi);
		if( cp != NULL ) cp = strtok(ibuf, sep);
	}

  for (char *p = cp; *p; ++p) {
    *p = toupper((unsigned char)*p);
  }
// printf("%s ",cp);
	return cp;
}

PRIVATE char  *readA(void){
	char *w;
	w = tok();
	return w;
}

PRIVATE void setBp(int ads){
	printf("set breakpoint %d\n",ads);
	abreak = ads;
	nbp = 1;
}

PRIVATE void clearBp(void) { nbp = 0; }

PRIVATE int isbreak(void){
	if( nbp && (Pc == abreak)) return 1;
	return 0;
}

PRIVATE void trace(void){
	display = 0;
	while(!isbreak() && runflag && clock < MAXSIM)
		runoneclock();
	display = 1;
}

PRIVATE int isReg(char *s){
	int n;
	if( s[0] == 'R') {
		n = atoi(s+1);
		if( n >= 0 && n < 32 ) return 1;
	}
	return 0;
}

PRIVATE void setArg(char *s, int value){
	if( isReg(s) )
		R[atoi(s+1)] = value;
	else if ( eqs(s,"PC") )
		Pc = value;
	else if ( s[0] == 'M' )
		M[atoi(s+1)] = value;
	else
		printf("set unknow argument\n");
}

PRIVATE void dumpMem(int ads, int n){
	int i,k;
	k = 0;
	printf("%4d : ",ads);
	for(i=ads;i<ads+n;i++){
		printf("%d ",M[i]);
		k++;
		if( k%10 == 0 ) printf("\n%4d : ",i+1);
	}
	printf("\n");
}

//PRIVATE int aQ;

void printL(int ads, int n){
	int p, i;
	p = M[ads];
	for(i = 0; i < n; i++){
		if(p == 0) break;
		printf("%d ",M[p]);
		p = M[p+1];
	}
}

// show data structure (semaphore) of MOS
void printsem(int ads){
	printf("sem &%d : %d.%d wait ",ads, M[ads], M[ads+1]);
	printL(M[ads+1],3);
	printf("\n");
}
/*
// show data structure of MOS
void xray(void){
	int i, pcb[10], p, j, a;
	printf("Q %d, nump %d, currentp %d\n",M[Q],M[nump],M[currentp]);
	// dump Q
	aQ = M[Q];
	p = M[aQ];   // point to first cell in Q list
	printf("Q header[%d.%d] ",M[aQ],M[aQ+1]);
	for(i = 0; i < M[nump]; i++){
		pcb[i] = M[p];
		printf("%d.%d ",M[p],M[p+1]);
		p = M[p+1];
	}
	printf("\nqq %d\n",M[qq]);
	printf("PCB\n");
	// dump PCB
	for(i = 0; i < M[nump]; i++){
		a = pcb[i];
		printf("&%d : ", a);
		for(j = a; j < a+6; j++)
			printf("%d ",M[j]);
		printf("\n");
	}
	// dump semaphore
	printsem(M[sem1]);
	printsem(M[sem2]);
}
*/
PRIVATE void help(void){
	printf("%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n",
	  "g - go", "t - single step", "b ads - set breakpoint",
	  "c - clear breakpoint", "s [rn,mn,pc] v - set",
	  "d ads n - dump", "r - show register",
	  "q - quit", "h - help");
}

void interp(void){
	char *w, *arg, *v, *v2;
	int i, runf;
	runf = 1;
	fi = stdin;
	cp = NULL;
	while(runf){
		w = tok();
		switch( w[0] ){
		case 'G': trace(); break;
		case 'T': runoneclock(); break;
		case 'B':
			arg = readA();
			setBp(atoi(arg));
			break;
		case 'C': clearBp(); runflag = 1; break;
		case 'S':
			arg = readA();
			v = readA();
			setArg(arg,atoi(v));
			break;
		case 'D':
			arg = readA();
			v = readA();
			dumpMem(atoi(arg),atoi(v));
			break;
		case 'R': dumpreg(); break;
		case 'Q': runf = 0; break;
		case 'H': help(); break;
//		case 'X': xray(); break;
		default: printf("unknow command\n");
		}
	}
}

void execute(void){
	fi = stdin;
	cp = NULL;
	trace();
}

extern int timer0, timer1, ninst;

void dumpreg(void){
	int i;
	for(i=0;i<32;i++){
	   if(i%8 == 0) printf("\n");
	   printf("r%-2d:%-4d ",i,R[i]);
	}
	printf("\n");
	printf("PC:%d RetAds:%d timer0 %d timer1 %d clock %d\n",Pc,RetAds,timer0,timer1,ninst);
}

/*
void testtok(){
	char *w;
	fi = stdin;
	cp = NULL;
	while(1){
		w = tok();
		printf("%s ",w);
		if( eqs(w,"QUIT")) break;
	}
}
*/
