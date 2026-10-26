/* gencode.c
   code generator som-c		2 Jan 2004
	improve clean, not gen when empty body  14 Mar 2004
	gen logical and or						30 Aug 2011
	add compile "run", "syscall" for noss	4 Sept 2011
	gen S2.3 code for embed sys				9 Jan 2013
*/

#include "compile.h"

#define freereg 	20
#define retvalR     28      // return value register

#define head(e)		car(e)
#define tail(e) 	cdr(e)
#define arg1(e)		car(e)
#define arg2(e)		item2(e)
#define arg3(e)		item3(e)

// test atom type
#define isnum(a)		(car(a) == NUM)
#define iscommute(op)	(commute[cdr(op)-50])

// parameterized print to output
#define out0(str)			sprintf(ob,str); outCode()
#define out1(str,a1)		sprintf(ob,str,a1); outCode()
#define out2(str,a1,a2)		sprintf(ob,str,a1,a2); outCode()
#define out3(str,a1,a2,a3)	sprintf(ob,str,a1,a2,a3); outCode()

#define  eqs(a,b)  (strcmp(a,b) == 0)

extern char strbuf[];

char ob[256];							// one line obj buffer
char bufOb[5000], *currentOb = bufOb;	// code buffer
int currentf = 0;						// current function idx
int codeBufq = 0;						// code buffer flag

void outCode(void){
	if( codeBufq ){
		strcpy(currentOb,ob);
		currentOb += strlen(ob);
	}else
		printf("%s",ob);
}

PRIVATE void pushOb(void){
	codeBufq = 1;
}

PRIVATE void topOb(void){
	codeBufq = 0;
}

PRIVATE void popOb(void){
//	printf("; <<  popOb\n");
	printf("%s",bufOb);
//	printf("; >>  popOb\n");
	currentOb = bufOb;
	codeBufq = 0;
}

// register allocation
PRIVATE int nreg = 1;
PRIVATE int FS = 0;
PRIVATE int RR[32];

PRIVATE void clearRR(void){
	int i;
	for(i = 1; i < freereg; i++)
		RR[i] = 0;
	nreg = FS;
}

PRIVATE int newR(void){
	int i;
	for(i = FS+1; i < freereg; i++){
		if( RR[i] == 0 ) {
			RR[i] = 1;
			if( nreg < i ) nreg = i;
			return i;
		}
	}
	return 0;
}

PRIVATE void freeR(int a){
	RR[a] = 0;
}

PRIVATE int nlabel = 100;
PRIVATE int newLabel(void){
	nlabel++;
	return nlabel;
}
PRIVATE int atomeq(int e, int type, int value){
	return (car(e) == type) && (cdr(e) == value);
}

// (ex..ex)
PRIVATE void genlist(int e){
	while( e != NIL ){
		genex(car(e));
		e = cdr(e);
	}
}

/*  short circuit logical and
x && y
if x then y else 0
x || y
if x then 1 else y
*/
PRIVATE int gnLogic(char *op, int e){
	int lab, d, a;

	d = newR();
	a = genex(car(e));
	lab = newLabel();
	out2("ne r%d r%d r0\n",d,a);
	out3("%s r%d L%d\n",op,d,lab);
	freeR(a);
	a = genex(item2(e));
	out2("ne r%d r%d r0\n",d,a);
	freeR(a);
	out1(":L%d\n",lab);
	return d;
}
/*
// deref x is (* v)
PRIVATE int isDeref(int x){
	return atomeq(car(x),OPER,tkDEREF);
}
*/
PRIVATE int isString(int a){
	return isatom(a) && (car(a) == STRING);
}

// s2 opcode
PRIVATE char opS2[][8] = {
	"mul","div","sub","add","=","eq","and","&&","or","||",
	"not","ne","lt","le","gt","ge"
};

PRIVATE char *getS2(int op){
	return opS2[cdr(op)-50];
}

// e = (lv (+ lv 1))
PRIVATE int isinc(int e){
	int a, ref, op;
	a = car(e);
	if( car(a) != LNAME ) return 0;
	ref = cdr(a);
	e = item2(e);		// e = (+ lv 1)
	op = car(e);
	if( !(atomeq(op,OPER,tkPLUS) || atomeq(op,OPER,tkMINUS)) )
		return 0;
	a = item2(e);
	if( ! atomeq(a,LNAME,ref) ) return 0;
	if( ! atomeq(item3(e),NUM,1) ) return 0;
	return 1;
}
// pass fun call param
void gnPass(int n){
	int i;
	for(i = n; i >= 1; i-- ){
		out1("pop sp r%d\n",i);
	}
}
// save registers on entry
void gnSaveR(int n){
	int i;
	for(i = 1; i <= n; i++){
		out2("st r%d @%d fp\n",i,i);	// save reg
	}
	out1("add fp fp #%d\n",n+1);		// adjust fp
	out0("st rads @0 fp\n");			// save rads
}
// restore registers on return
void gnRestoreR(int n){
	int i;
	out0("ld rads @0 fp\n");		// res rads
	out1("sub fp fp #%d\n",n+1);	// res fp
	for(i = n; i >= 1; i--){
		out2("ld r%d @%d fp\n",i,i);	// res reg
	}
}

int gnAtom(int a){
	int d, ref;
	ref = cdr(a);
	switch(car(a)){
	case GNAME:
		d = newR();
		out2("ld r%d %s\n",d,getName(ref));
		return d;
	case LNAME:
	case NUM: return ref;
	}
	return 0;
}
//  put num to r, a must be isnum()
int gnNum(int a){
	int d;
	d = newR();
	out2("mov r%d #%d\n",d,cdr(a));
	return d;
}

// e = (name idx), LHS op "st", RHS op "ld"
void gnVec(char *op, int e, int dest){
	int ref, a, b, c;
	a = car(e);
	ref = cdr(a);		// base
	b = item2(e);		// index
	if isnum(b)
		c = gnNum(b);
	else
		c = genex(b);
	switch(car(a)){
	case GNAME:
		sprintf(ob,"%s r%d @%s r%d\n",op,dest,getName(ref),c);
		outCode();
		break;
	case LNAME:
		sprintf(ob,"%s r%d +r%d r%d\n",op,dest,ref,c);
		outCode();
		break;
	}
	freeR(c);
}

int gnBop(int e){
	int op, a, b, d, ref;
	int a1, a2, prefix;
	printf("; gnBop :");
	printList(e);
	printf("\n");
	op = car(e);
	a = item2(e);
	b = item3(e);
	// set a1, first arg
	if( isatom(a) ){
		ref = cdr(a);
		switch(car(a)){  // type
		case GNAME:
			a1 = newR();
			out2("ld r%d %s\n",a1,getName(ref));
			break;
		case LNAME:
			a1 = ref;
			break;
		case NUM:
			a1 = newR();
			out2("mov r%d #%d\n",a1,ref);
			break;
		}
	}else{
		a1 = genex(a);
	}
	// set a2, second arg
	if( isatom(b) ){
		ref = cdr(b);
		switch(car(b)){	// type
		case GNAME:
			a2 = newR();
			out2("ld r%d %s\n",a2,getName(ref));
			break;
		case LNAME:
		case NUM: a2 = ref; break;
		}
		prefix = isnum(b) ? '#' : 'r';
	}else{
		a2 = genex(b);
		prefix = 'r';
	}
	d = newR();
	sprintf(ob,"%s r%d r%d %c%d\n",getS2(op),d,a1,prefix,a2);
	outCode();
	freeR(a1);
	freeR(a2);
	return d;
}

// gen assignment statement
void gnAsg(int e){
	int a, b, c, d, ref, prefix;
	printf("; gnAsg :");
	printList(e);
	printf("\n");
	a = car(e);			// e = (var/vec ex)
	b = item2(e);
	if( isnum(b) )		// rhs
		d = gnNum(b);
	else
		d = genex(b);
//	d = genex(b);		// rhs
	if(isatom(a)){		// var
		ref = cdr(a);
		switch(car(a)){	// type
		case GNAME:
//			if(isnum(b)) d = gnNum(b);
			out2("st r%d %s\n",d,getName(ref));
			break;
		case LNAME:
			prefix = isnum(b) ? '#' : 'r';
			out3("mov r%d %c%d\n",ref,prefix,d);
			break;
		}
	}else{				// vec lhs
		out0("; vec LHS\n");
		gnVec("st",cdr(a),d);
/*
		e = item3(a);			// idx
		c = isnum(e) ? gnNum(e) : genex(e);
//		if(isnum(b)) d = gnNum(b);
		ref = cdr(item2(a));	// base
		switch(car(item2(a)){
		case GNAME:
			out3("st r%d @%s r%d\n",d,getName(ref),c);
			break;
		case LNAME:
			out3("st r%d +r%d r%d\n",d,ref,c);
			break;
		}
		freeR(c);
//		freeR(d);
*/
	}
	freeR(d);
/*
	else if(isDeref(a)){	// a is (* v)
		genex(item2(a));
		outa(icLit,0);
		genex(e1);			// RHS
		outs(icStx);
	}
*/
}

// return label to the end of e
int gnIf(int e){
	int lab, d;
	d = genex(car(e));	// e = (cond ex)
	lab = newLabel();
	out2("jf r%d L%d\n",d,lab);
	freeR(d);
	d = genex(item2(e));
	freeR(d);
	return lab;
}

PRIVATE int retlab;

int genex(int x){
	int a, e, e1, d, n;
	int idx, ref, lab, lab1;

	if( x == NIL ) return 0;
	if(isatom(x)) return gnAtom(x);

	a = car(x);			// (op ex .. )
	e = cdr(x);			// e = (ex .. )
	if(car(a) != OPER)
		seterror("genex: expect operator");
	d = 0;
	switch(cdr(a)){
	case tkPLUS:
	case tkMINUS:
	case tkSTAR:
	case tkSLASH:
	case tkAND:
	case tkOR:
//	case tkCARET:
	case tkEQEQ:
	case tkNE:
	case tkLT:
	case tkLE:
	case tkGE:
	case tkGT: d = gnBop(x); break;

//	case tkMOD:
//	case tkGTGT:
//	case tkLTLT:

	case tkANDAND: 	d = gnLogic("jf",e); break;
	case tkOROR: 	d = gnLogic("jt",e); break;
	case tkUNOT:
		d = genex(car(e));
		out2("eq r%d r%d r0\n",d,d);
		break;

	case tkDO: genlist(e); break;	// (block ex .. ex), e = (ex .. ex)
	case tkCALL:
		idx = cdr(car(e));		// e = (name ex .. ex)

		if(eqs(getName(idx),"asm")){
			e1 = car(cdr(e));
			out0("; asm\n");
			if( isString(e1) )
				out1("%s\n",&strbuf[cdr(e1)]);
		}else {
			e = cdr(e);		// arguments of function
			while( e != NIL ){
				e1 = car(e);
				d = isnum(e1) ? gnNum(e1) : genex(e1);
				out1("push sp r%d\n",d);
				freeR(d);
				e = cdr(e);
			}
			out1("jal rads %s\n",getName(idx));
			d = retvalR;
		}
		break;
	case tkFUN:				// (fun name ex)
		idx = cdr(car(e));	// e = (name ex)
		currentf = idx;
		FS = getFs(idx);
		retlab = newLabel();
		clearRR();
		e1 = item2(e);
		if(eqs(getName(idx),"interrupt")){
			printf(":%s\n",getName(idx));
			if( FS == 0 ){
				genex(e1);		// 	no frame
			}else{
				pushOb();		//  no pass param
				genex(e1);
				topOb();
				gnSaveR(nreg);
				popOb();		//  body has no "return"
				gnRestoreR(nreg);
			}
			out0("ret r31\n");	// ret from inter
		}else if( e1 != NIL ){
			printf("; fun %s pv %d fs %d\n",
				getName(idx),getArg(idx),getFs(idx));
			printf(":%s\n",getName(idx));
			pushOb();
			gnPass(getArg(idx));
			genex(e1);
			topOb();
			gnSaveR(nreg);
			popOb();
			out1(":L%d\n",retlab);
			gnRestoreR(nreg);
			out0("ret rads\n");
		}
		break;
/*
	case tkDEREF:			// RHS *
		genex(car(e));
		outa(icLit,0);
		outs(icLdx);
		break;
	case tkADS:				// RHS &
		e1 = car(e);
		if(isatom(e1)){
			type = car(e1);
			ref = cdr(e1);
			switch(type){
			case GNAME: outa(icLit,getRef(ref)); break;
			case LNAME: outa(icRef,ref); break;
			}
		}else{				// (vec name ex)
			genvec(cdr(e1));
			outs(icAdd);
		}
		break;
*/
	case tkVEC:				// vec RHS
		out0("; vec RHS\n");
		d = newR();
		gnVec("ld",e,d);
/*
		e1 = item2(e);
		n = genex(e1);
		if( isnum(e1)){
			a = newR();
			out2("mov r%d #%d\n",a,n);
			freeR(a);
		}
		d = newR();
		ref = cdr(car(e));	// base
		switch(car(car(e))){
		case GNAME:
			out3("ld r%d @%s r%d\n",d,getName(ref),n);
			break;
		case LNAME:
			out3("ld r%d +r%d r%d\n",d,ref,n);
			break;
		}
*/
		break;
	case tkEQ:						// (= var/vec ex)
		if(isinc(e)){
			e1 = car(item2(e));		// op, e = (lv (+ lv 1))
			ref = cdr(car(e)); 		// lvar ref
			n = (cdr(e1) == tkPLUS) ? 1 : -1;
			out3("add r%d r%d #%d\n",ref,ref,n);
		}else
			gnAsg(e);
/*
		}else if(isDeref(a)){	// a is (* v)
			genex(item2(a));
			outa(icLit,0);
			genex(e1);			// RHS
			outs(icStx);
		}
*/
		break;
	case tkIF:
		lab = gnIf(e);
		out1(":L%d\n",lab);
		break;
	case tkELSE:
		lab = gnIf(e);			// e = (cond t f)
		lab1 = newLabel();
		out1("jmp L%d\n",lab1);
		out1(":L%d\n",lab);
		d = genex(item3(e));
		out1(":L%d\n",lab1);
		freeR(d);
		break;
	case tkWHILE: 						// e = (e1 e)
		lab = newLabel();
		out1("jmp L%d\n",lab);			// branch to cond
		lab1 = newLabel();
		out1(":L%d\n",lab1);
		d = genex(item2(e));			// body of while
		freeR(d);
		out1(":L%d\n",lab);
		d = genex(car(e));				// cond
		out2("jt r%d L%d\n",d,lab1);	// jcond loop
		freeR(d);
		break;
	case tkRETURN:
		e1 = car(e);
		if(isnum(e1)){
			out1("mov retval #%d\n",cdr(e1));
		}else{
			d = genex(e1);
			out1("mov retval r%d\n",d);
			freeR(d);
		}
		out1("jmp L%d\n",retlab);
		break;
	case tkPRINT:
		while( e != NIL ){
			e1 = head(e);
			d = genex(e1);
			if( isnum(e1) ){
				a = newR();
				out2("mov r%d #%d\n",a,d);
				out1("trap r%d #1\n",a);
				freeR(a);
			}else{
				out1("trap r%d #1\n",d);
			}
			freeR(d);
/*
			else if( head(e1) == STRING )
				printf("trap r%d #2\n",d);
*/
			e = tail(e);
		}
		break;
	default:
		printf("op %d ",cdr(a));
		seterror("unknown op\n");
	}
	return d;
}
