// os_lab3.c - based on xv6 with heavy modifications
#include <os_lab3.h>

struct trapframe { // layout of the trap frame built on the stack by trap handler
	int sp, pad1;
	double g, f;
	int c,  pad2;
	int b,  pad3;
	int a,  pad4;
	int fc, pad5;
	int pc, pad6;
};

//process
struct proc { 
	uint sz;               // size of process virtual memory (bytes)
	uint physz;			   // size of process physical memory (bytes)
	uint *pdir;            // page directory
	char *kstack;          // bottom of kernel stack for this process
	struct trapframe *tf;  // trap frame for current syscall
	int context;           // return here to run process
};
// *** Globals ***
struct proc proc0;
struct proc *init;
char *mem_top;           // current top of unused memory
uint mem_sz;             // size of physical memory
uint kreserved;          // start of kernel reserved memory heap
uint *kpdir;             // kernel page directory
uint ticks;
char *memdisk;

// *** Code ***

out(port, val)  { asm(LL,8); asm(LBL,16); asm(BOUT); }
ivec(void *isr) { asm(LL,8); asm(IVEC); }
lvadr()         { asm(LVAD); }
uint msiz()     { asm(MSIZ); }
stmr(val)       { asm(LL,8); asm(TIME); }
pdir(val)       { asm(LL,8); asm(PDIR); }
spage(val)      { asm(LL,8); asm(SPAG); }
splhi()         { asm(CLI); }
splx(int e)     { if (e) asm(STI); }

// page allocator
char *kalloc()
{
  char *r; int e = splhi();
  if ((uint)(r = mem_top) < P2V+(mem_sz - FSSIZE)) {
	  mem_top += PAGE; //XXX uint issue is going to be a problem with other pointer compares!
	  //printf("kalloc success! ,mem_top is 0x%x\n",mem_top); 
  } else {
      printf("kalloc failure! ,mem_top is 0x%x, max addr is 0x%x",P2V+(mem_sz - FSSIZE)); 
	  panic("\n");//XXX need to sleep here!
  }
  splx(e);
  return r;
}

// console device
cout(char c)
{
  out(1, c);
}

panic(char *s)
{
  asm(CLI);
  out(1,'P'); out(1,'A'); out(1,'N'); out(1,'I'); out(1,'C'); out(1,':'); out(1,':'); 
  while (*s) out(1,*s++);
  out(1,'\n');
  asm(HALT);
}

// fake IDE disk; stores blocks in memory.  useful for running kernel without scratch disk.  XXX but no good for stressing demand pageing logic!
ideinit()
{
  memdisk = P2V+(mem_sz - FSSIZE);
}

// sync buf with disk.  if B_DIRTY is set, write buf to disk, clear B_DIRTY, set B_VALID.
// else if B_VALID is not set, read buf from disk, set B_VALID.
iderw(unit sector, char *b, int is_r) // XXX rename?!
{
	if(is_r){
		memcpy(memdisk + sector*PAGE, b, PAGE);
	} else {
		memcpy(b, memdisk + sector*PAGE, PAGE);
	}
}

// *** end of syscalls ***

// a forked child's very first scheduling will swtch here
forkret()
{
  asm(POPA); asm(SUSP);
  asm(POPG);
  asm(POPF);
  asm(POPC);
  asm(POPB);
  asm(POPA);
  asm(RTI);
}

// hand-craft the first process
init_start()
{
  int i,j,n,m;
  int * adr;
  j=1;
  while(j){
	  for (i=PAGE;i<UVIRSZ+16; i+=4){
		  adr=i;
		  *adr=j;
	  }		  
  };
  
}

userinit()
{
	char *mem;
	char *sp; 
	uint virnum, phynum, i;
	
	init = &proc0;
	
	// allocate kernel stack leaving room for trap frame
	sp = (init->kstack = kalloc()) + PAGE - sizeof(struct trapframe);
	init->tf = (struct trapframe *)sp;
	// set up new context to start executing at forkret
	sp -= 8;
	*(uint *)sp = (uint)forkret;
	init->context = sp;
	
	init->sz = UVIRSZ;
	init->physz = UPHYSZ;
	virnum=init->sz / PAGE ;
	phynum=init->physz/ PAGE;

	init->pdir = memcpy(kalloc(), kpdir, PAGE);
	
	for(i=0;i<phynum;i++) {
		if (i ==0) 
			mem = memcpy(memset(kalloc(), 0, PAGE), (char *)init_start, (uint)userinit - (uint)init_start);
		else
			mem = memset(kalloc(), 0, PAGE);
		mappage(init->pdir, i*PAGE, V2P+mem, PTE_P | PTE_W | PTE_U);
	}

	init->tf->sp = STACKSZ;
	init->tf->fc = USER;
	init->tf->pc = 0;
	printf("user app init sp 0x%x, pdir 0x%x, virnum %d, phynum %d, i %d\n",init->tf->sp,init->pdir, virnum, phynum,i);
}

// set up kernel page table
setupkvm()
{
  uint i, *pde, *pt;

  kpdir = memset(kalloc(), 0, PAGE); // kalloc returns physical addresses here (kfree wont work until later on)

  for (i=0; i<mem_sz; i += PAGE) {
    pde = &kpdir[(P2V+i) >> 22];
    if (*pde & PTE_P)
      pt = *pde & -PAGE;
    else
      *pde = (uint)(pt = memset(kalloc(), 0, PAGE)) | PTE_P | PTE_W;
    pt[((P2V+i) >> 12) & 0x3ff] = i | PTE_P | PTE_W;
  }
}

// create PTE for a page
mappage(uint *pd, uint va, uint pa, int perm)
{
  uint *pde, *pte, *pt;

  if (*(pde = &pd[va >> 22]) & PTE_P)
    pt = P2V+(*pde & -PAGE);
  else
    *pde = (V2P+(uint)(pt = memset(kalloc(), 0, PAGE))) | PTE_P | PTE_W | PTE_U;
  pte = &pt[(va >> 12) & 0x3ff];
  if (*pte & PTE_P) { printf("*pte=0x%x pd=0x%x va=0x%x pa=0x%x perm=0x%x", *pte, pd, va, pa, perm); panic("remap"); }
  *pte = pa | perm;
}

trap(uint *sp, double g, double f, int c, int b, int a, int fc, uint *pc)  
{
  uint va;
  switch (fc) {
  //syscalls
  case FSYS: panic("FSYS from kernel");
  case FSYS + USER:
    init->tf = &sp;
    switch (pc[-1] >> 8) {
    default: printf("user app invoke unknown syscall %d\n", a); a = -1; break;
    }
    return;
  //exceptions  
  case FMEM:          panic("FMEM from kernel\n");
  case FMEM   + USER: panic("FMEM + USER\n");  
  case FPRIV:         panic("FPRIV from kernel");
  case FPRIV  + USER: panic("FPRIV + USER\n"); 
  case FINST:         panic("FINST from kernel");
  case FINST  + USER: panic("FINST + USER\n"); 
  case FARITH:        panic("FARITH from kernel");
  case FARITH + USER: panic("FARITH + USER\n");
  case FIPAGE:        printf("FIPAGE from kernel [0x%x]", lvadr()); panic("!\n");

  case FWPAGE:        printf("FWPAGE + KER  [0x%x]", lvadr()); panic("!\n");
  case FRPAGE:        printf("FRPAGE + KER  [0x%x]", lvadr()); panic("!\n");	  
  case FIPAGE + USER: printf("FIPAGE + USER [0x%x] ", lvadr());	 goto PGFAULT;  
  case FWPAGE + USER: printf("FWPAGE + USER [0x%x] ", lvadr());  goto PGFAULT;
  case FRPAGE + USER: printf("FRPAGE + USER [0x%x] ", lvadr());
PGFAULT:  if ((va = lvadr()) >= init->sz) {
			  printf(">= init->sz 0x%x ", init->sz); panic("!\n");
		  } else {
			  printf("ADD new phy page\n");	
              pc--; // restart instruction
              mappage(init->pdir, va & -PAGE, V2P+(memset(kalloc(), 0, PAGE)), PTE_P | PTE_W | PTE_U);
              return;
		  }
  //device interrupts
  case FTIMER: 
	ticks++;
	cout('-');
	return;	  
  case FTIMER + USER: 
    ticks++;
	cout('+');
    return;
    
  case FKEYBD:
  case FKEYBD + USER:
    return; //??XXX postkill?
  }
}

alltraps()
{
  asm(PSHA);
  asm(PSHB);
  asm(PSHC);
  asm(PSHF);
  asm(PSHG);
  asm(LUSP); asm(PSHA);
  trap();                // registers passed back out by magic reference :^O
  asm(POPA); asm(SUSP);
  asm(POPG);
  asm(POPF);
  asm(POPC);
  asm(POPB);
  asm(POPA);
  asm(RTI);
}

mainc()
{
  int *kstack;
  kpdir[0] = 0;          // don't need low map anymore
  ivec(alltraps);        // trap vector
  ideinit();             // disk
  stmr(10*1024);        // set timer
  userinit();            // first user process
  printf("mainc %x, Welcome!\n",mainc);

  pdir(V2P+(uint)(proc0.pdir));
  kstack = proc0.context; //proc0 kstack
  asm(SSP);   // sp = a
  asm(STI);
  asm(LEV);   // return
}

main()
{
  int *ksp;              // temp kernel stack pointer
  static char kstack[4096]; // temp kernel stack
  static int endbss;     // last variable in bss segment
  printf("main addr 0x%x, Welcome!\n",main);  
  // initialize memory allocation
  mem_top = kreserved = ((uint)&endbss + PAGE + 3) & -PAGE; 
  mem_sz = msiz();
  printf("phy mem total size 0x%x\nfree mem begin 0x%x, top 0x%x\n",mem_sz, mem_top,mem_sz-FSSIZE);  
  // initialize kernel page table
  setupkvm();
  kpdir[0] = kpdir[(uint)USERTOP >> 22]; // need a 1:1 map of low physical memory for awhile

  // initialize kernel stack pointer
  ksp = ((uint)kstack + sizeof(kstack) - 8) & -8;
  asm(LL, 4);
  asm(SSP);

  // turn on paging
  pdir(kpdir);
  spage(1);
  kpdir = P2V+(uint)kpdir;
  mem_top = P2V+mem_top;

  // jump (via return) to high memory
  ksp = P2V+(((uint)kstack + sizeof(kstack) - 8) & -8);
  *ksp = P2V+(uint)mainc;
  asm(LL, 4);
  asm(SSP);
  asm(LEV);
}
