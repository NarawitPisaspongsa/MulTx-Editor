/* hash.c
   symbol table routines	26 July 98

   int hash(char *s)
   int putsym(char *s)
   void setsym(int idx, int value, int type, int arg)
   int searchsym(char *s)
   int getValue(int idx)
   int getType(int idx)
   int getArg(int idx)

   for as21									22 Jan 2007
   separate hashtable from symboltable      14 Feb 2017 (valentine)
*/

#include "as21.h"

#define TSIZE  2003	 // must be next prime double num. of sym. (7919)
#define MAXSYM 1000  // max number of symbol

struct{
  char name[32];
  int value;
  int type;			// op, sym, label
  int arg;			// chk arg type: MEM, MOV, RRI
} symtab[MAXSYM];	// symbol table

int htable[TSIZE];	// hash table
int nsym = 0;		// number of symbol in hash table

PRIVATE int hash( char *name ){	// simple hash func.
  int i, idx;
  for( idx=0, i=0; name[i] != 0; i++ )
    idx += name[i];
  return idx % TSIZE;
}

void setsym(int idx, int value, int type, int arg){
	symtab[idx].value = value;
	symtab[idx].type = type;
	symtab[idx].arg = arg;
}
// search and insert name, return index
int putsym( char *name){
	int idx;

	idx = hash(name);
	while( htable[idx] != 0 ){
		if( strcmp(symtab[htable[idx]].name, name) == 0 )
			return htable[idx];		// found
		idx = (idx + 1) % TSIZE;
	}
	// insert sym as UNDEF
	nsym++;
	if( nsym >= MAXSYM )
		error("symbol table is full\n");
	htable[idx] = nsym;
	strcpy(symtab[nsym].name, name);
	symtab[nsym].type = SYM;
	symtab[nsym].value = UNDEF;
	return nsym;
}

extern FILE *fo;
extern int numop;

int isLabel(char *name){
	int k, i;
	k = strlen(name);
	if( name[0] != 'L' ) return 0;
	for(i = 1; i < k; i++)
		if( !isdigit(name[i]) ) return 0;
	return 1;
}

// mark all labels ("LXXX"), return number of label
PRIVATE int markLabel(void){
	int cnt, i;
	cnt = 0;
	for(i=numop+1; i<=nsym; i++){
		if( isLabel(symtab[i].name) ){
			symtab[i].type = LAB;
		    cnt++;
		}
	}
	return cnt;
}

// dump only user defined symbols
void dumpsymbol(void){
	int i, k;
	k = markLabel();
	fprintf(fo,"s %d\n",nsym-numop-k);
	for(i=numop+1; i<=nsym; i++)
		if( symtab[i].type == SYM )		// sym
			fprintf(fo,"%s %d\n",symtab[i].name, symtab[i].value);
}

// return index, -1 if not found
int searchsym(char *name){
	int idx;
	idx = hash(name);
	while( htable[idx] != 0 ){
		if( strcmp(symtab[htable[idx]].name, name) == 0 )
			return htable[idx];		// found
		idx = (idx + 1) % TSIZE;
	}
	return -1;
}

int getValue(int idx){ return symtab[idx].value;}
int getType(int idx){ return symtab[idx].type; }
int getArg(int idx){ return symtab[idx].arg; }


