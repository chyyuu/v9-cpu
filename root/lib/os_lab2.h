// os_lab2.h

// instruction set
enum {
  HALT,ENT ,LEV ,JMP ,JMPI,JSR ,JSRA,LEA ,LEAG,CYC ,MCPY,MCMP,MCHR,MSET, // system
  LL  ,LLS ,LLH ,LLC ,LLB ,LLD ,LLF ,LG  ,LGS ,LGH ,LGC ,LGB ,LGD ,LGF , // load a
  LX  ,LXS ,LXH ,LXC ,LXB ,LXD ,LXF ,LI  ,LHI ,LIF ,
  LBL ,LBLS,LBLH,LBLC,LBLB,LBLD,LBLF,LBG ,LBGS,LBGH,LBGC,LBGB,LBGD,LBGF, // load b
  LBX ,LBXS,LBXH,LBXC,LBXB,LBXD,LBXF,LBI ,LBHI,LBIF,LBA ,LBAD,
  SL  ,SLH ,SLB ,SLD ,SLF ,SG  ,SGH ,SGB ,SGD ,SGF ,                     // store
  SX  ,SXH ,SXB ,SXD ,SXF ,
  ADDF,SUBF,MULF,DIVF,                                                   // arithmetic
  ADD ,ADDI,ADDL,SUB ,SUBI,SUBL,MUL ,MULI,MULL,DIV ,DIVI,DIVL,
  DVU ,DVUI,DVUL,MOD ,MODI,MODL,MDU ,MDUI,MDUL,AND ,ANDI,ANDL,
  OR  ,ORI ,ORL ,XOR ,XORI,XORL,SHL ,SHLI,SHLL,SHR ,SHRI,SHRL,
  SRU ,SRUI,SRUL,EQ  ,EQF ,NE  ,NEF ,LT  ,LTU ,LTF ,GE  ,GEU ,GEF ,      // logical
  BZ  ,BZF ,BNZ ,BNZF,BE  ,BEF ,BNE ,BNEF,BLT ,BLTU,BLTF,BGE ,BGEU,BGEF, // conditional
  CID ,CUD ,CDI ,CDU ,                                                   // conversion
  CLI ,STI ,RTI ,BIN ,BOUT,NOP ,SSP ,PSHA,PSHI,PSHF,PSHB,POPB,POPF,POPA, // misc
  IVEC,PDIR,SPAG,TIME,LVAD,TRAP,LUSP,SUSP,LCL ,LCA ,PSHC,POPC,MSIZ,
  PSHG,POPG,NET1,NET2,NET3,NET4,NET5,NET6,NET7,NET8,NET9,
  POW ,ATN2,FABS,ATAN,LOG ,LOGT,EXP ,FLOR,CEIL,HYPO,SIN ,COS ,TAN ,ASIN, // math
  ACOS,SINH,COSH,TANH,SQRT,FMOD,
  IDLE
};

// system calls
enum {
  S_fork=1, S_exit,   S_wait,   S_pipe,   S_write,  S_read,   S_close,  S_kill,
  S_exec,   S_open,   S_mknod,  S_unlink, S_fstat,  S_link,   S_mkdir,  S_chdir,
  S_dup2,   S_getpid, S_sbrk,   S_sleep,  S_uptime, S_lseek,  S_mount,  S_umount,
  S_socket, S_bind,   S_listen, S_poll,   S_accept, S_connect, 
};

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;

enum {
	PAGE    = 4096,       // page size
	USERTOP = 0xc0000000, // end of user address space
	P2V     = +USERTOP,   // turn a physical address into a virtual address
	V2P     = -USERTOP,   // turn a virtual address into a physical address
	FSSIZE  = PAGE*1024,  // 4MB
};

enum { // page table entry flags   XXX refactor vs. i386
	PTE_P = 0x001, // present
	PTE_W = 0x002, // writeable
	PTE_U = 0x004, // user
	PTE_A = 0x020, // accessed
	PTE_D = 0x040, // dirty
};

enum { // processor fault codes
	FMEM,   // bad physical address
	FTIMER, // timer interrupt
	FKEYBD, // keyboard interrupt
	FPRIV,  // privileged instruction
	FINST,  // illegal instruction
	FSYS,   // software trap
	FARITH, // arithmetic trap
	FIPAGE, // page fault on opcode fetch
	FWPAGE, // page fault on write
	FRPAGE, // page fault on read
	USER=16 // user mode exception
};

void *memcpy(void *d, void *s, uint n) { asm(LL,8); asm(LBL, 16); asm(LCL,24); asm(MCPY); asm(LL,8); }
void *memset(void *d, uint c,  uint n) { asm(LL,8); asm(LBLB,16); asm(LCL,24); asm(MSET); asm(LL,8); }

printn(int n)
{
	if (n > 9) { printn(n / 10); n %= 10; }
	cout(n + '0');
}
printx(uint n)
{
	if (n > 15) { printx(n >> 4); n &= 15; }
	cout(n + (n > 9 ? 'a' - 10 : '0'));
}
printf(char *f, ...) // XXX simplify or chuck
{
	int n, e = splhi(); char *s; va_list v;
	va_start(v, f);
	while (*f) {
		if (*f != '%') { cout(*f++); continue; }
		switch (*++f) {
			case 'd': f++; if ((n = va_arg(v,int)) < 0) { cout('-'); printn(-n); } else printn(n); continue;
			case 'x': f++; printx(va_arg(v,int)); continue;
			case 's': f++; for (s = va_arg(v, char *); *s; s++) cout(*s); continue;
		}
		cout('%');
	}
	splx(e);
}

safestrcpy(char *s, char *t, int n) // like strncpy but guaranteed to null-terminate.
{
	if (n <= 0) return;
	while (--n > 0 && (*s++ = *t++));
	*s = 0;
}
