/* compile.c
    P.Chongstitvatana
    19 Aug 97
    1 Sept 97
    3 Apr 2001  start project RZ
    1 Jan 2002  generate s2 code
    27 Sept 2010   begin project rz3, generate parse-tree
    8 Jan 2013    gen code for s2.3
    redo for teaching int programming   8 Nov 2016
    integrate with IoT board            28 Jan 2017 (Chinese New Year)
    support MOS                         11 Feb 2017 (Makabucha day)
    add seminit, wait, signal, ansicmd predefined functions
*/

#include "compile.h"

extern int pass(void);        // parse.c
extern int nsym;              // number of symbol in symbol table
FILE *FO;                     // output file

void strcat2(char *s1, char *s2, char *s3){
    strcpy(s1,s2);
    strcat(s1,s3);
}

// search symbol table for Function and show it
void showParseTree(void){
    int i;
    for(i=1; i<=nsym; i++)
        if(getType(i) == tyFUNCTION){
            printList(getRef(i));
            printf("\n");
        }
}
void showGlobal(void){
    int i, ty;
    for(i=1; i<=nsym; i++){
        ty = getType(i);
        if(ty == tySCALAR || ty == tyVECTOR){
            printf("%s type %d ref %d\n",getName(i),ty,getRef(i));
        }
    }
}
/*
void testlex(void){
    mylex();
    while( tok != tkEOF ){
        printf(" ");
        prtoken(tok);
        mylex();
    }
}
*/

// search symbol table for FUNCTION and generate its code
void genall(void){
    int i;
    for(i=1; i<=nsym; i++)
        if(getType(i) == tyFUNCTION){
            genex(getRef(i));
        }
}

void epilog(void){
    printf(".data 200\n.end\n");
}

// move c-string from strbuf to M[] at STRBASE
/*
extern char strbuf[];
extern int freestr;
int M[MAXMEM];

void dumpString(void){
    int i;
    for(i=0; i<freestr; i++)
        M[STRBASE+i] = (int)(unsigned char)strbuf[i];
}
// produce object code
void outObj(string fn){
    FO = fopen(fn,"wt");
    fprintf(FO,"%d\n",SOM_V2_MAGIC);
//    outM(CS,1,CP-1);
    dumpString();
//    outM(M,STRBASE,STRBASE+freestr-1);
    dumpSymTab();
    fclose(FO);
}
*/
void outHeader(void){
    int i, ty;
    printf(".symbol\n fp 30\n sp 29\n retval 28\n rads 27\n");
    for(i = 1; i <= nsym; i++){
        ty = getType(i);
        if(ty == tySCALAR || ty == tyVECTOR){
            printf(" %s %d\n",getName(i),getRef(i));
        }
    }
    printf(".code 0\n mov fp #3500\n mov sp #3000\n");
    printf(" jal rads main\n trap r0 #0\n");
}

// index asm 1, doze 2, ei 3, di 4, readport 5, writeport 6
//   settimer0 7, settimer1 8, printc 9, prints 10, input 11, malloc 12
//   seminit 13, wait 14, signal 15, ansicmd 16, wait_trap 17, signal_trap 18
//   use in gencode.c
void predefineFun(void){
    int idx, flag;
    idx = installGlobal("asm",tyFUNCTION,0,1,&flag);
    idx = installGlobal("doze",tyFUNCTION,0,0,&flag);
    idx = installGlobal("ei",tyFUNCTION,0,1,&flag);
    idx = installGlobal("di",tyFUNCTION,0,1,&flag);
    idx = installGlobal("readport",tyFUNCTION,0,1,&flag);
    idx = installGlobal("writeport",tyFUNCTION,0,1,&flag);
    idx = installGlobal("settimer0",tyFUNCTION,0,1,&flag);
    idx = installGlobal("settimer1",tyFUNCTION,0,1,&flag);
    idx = installGlobal("printc",tyFUNCTION,0,1,&flag);
    idx = installGlobal("prints",tyFUNCTION,0,1,&flag);
    idx = installGlobal("input",tyFUNCTION,0,0,&flag);
    idx = installGlobal("malloc",tyFUNCTION,0,1,&flag);
    idx = installGlobal("seminit",tyFUNCTION,0,1,&flag);
    idx = installGlobal("wait",tyFUNCTION,0,1,&flag);
    idx = installGlobal("signal",tyFUNCTION,0,1,&flag);
    idx = installGlobal("ansicmd",tyFUNCTION,0,1,&flag);
    idx = installGlobal("wait_trap",tyFUNCTION,0,1,&flag);
    idx = installGlobal("signal_trap",tyFUNCTION,0,1,&flag);
}

int main( int argc, char *argv[] ){
    string source, code, list, iasm, name;
    char *fname;

    if( argc < 2 ) {
        printf("usage : compile source\n");
        exit(-1);
    }
    strcpy(source,argv[1]);
//    strcpy(name,source);
//    fname = strtok(name,".");
//    if( fname == NULL ) fname = name;
//    strcat2(code,fname,".obj");
//    strcat2(list,fname,".lst");
//    strcat2(iasm,fname,".s2");

    predefineFun();
    readinfile(source);
    initlex();
//    testlex();
    pass();
//    showParseTree();
//    showGlobal();
    outHeader();
    genall();
//    FO = stdout;
//  dumpSymTab();
    epilog();
    return 0;
}
