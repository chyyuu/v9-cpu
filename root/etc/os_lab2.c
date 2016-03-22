// os_lab2.c - based on xv6 with heavy modifications
#include <os_lab2.h>

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
  uint sz;               // size of process memory (bytes)
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
  if ((uint)(r = mem_top) < P2V+(mem_sz - FSSIZE)) 
	  mem_top += PAGE; //XXX uint issue is going to be a problem with other pointer compares!
  else panic("kalloc failure!");  //XXX need to sleep here!
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
  out(1,'p'); out(1,'a'); out(1,'n'); out(1,'i'); out(1,'c'); out(1,':'); out(1,' '); 
  while (*s) out(1,*s++);
  out(1,'\n');
  asm(HALT);
}

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
  int i;
  i=1;
  while(i){};
  
}

userinit()
{
  char *mem;
  char *sp; 
  
  init = &proc0;
  
  // allocate kernel stack leaving room for trap frame
  sp = (init->kstack = kalloc()) + PAGE - sizeof(struct trapframe);
  init->tf = (struct trapframe *)sp;
  // set up new context to start executing at forkret
  sp -= 8;
  *(uint *)sp = (uint)forkret;
  init->context = sp;
  
  init->pdir = memcpy(kalloc(), kpdir, PAGE);
  mem = memcpy(memset(kalloc(), 0, PAGE), (char *)init_start, (uint)userinit - (uint)init_start);
  mappage(init->pdir, 0, V2P+mem, PTE_P | PTE_W | PTE_U);

  init->sz = PAGE;
  init->tf->sp = PAGE;
  init->tf->fc = USER;
  init->tf->pc = 0;
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
  case FSYS: panic("FSYS from kernel");
  case FSYS + USER:
    init->tf = &sp;
    switch (pc[-1] >> 8) {
    default: printf("user app invoke unknown syscall %d\n", a); a = -1; break;
    }
    return;
    
  case FMEM:          panic("FMEM from kernel\n");
  case FMEM   + USER: panic("FMEM + USER\n");  // XXX psignal(SIGBUS)
  case FPRIV:         panic("FPRIV from kernel");
  case FPRIV  + USER: panic("FPRIV + USER\n"); // XXX psignal(SIGINS)
  case FINST:         panic("FINST from kernel");
  case FINST  + USER: panic("FINST + USER\n"); // psignal(SIGINS)
  case FARITH:        panic("FARITH from kernel");
  case FARITH + USER: panic("FARITH + USER\n"); // XXX psignal(SIGFPT)
  case FIPAGE:        printf("FIPAGE from kernel [0x%x]", lvadr()); panic("!\n");
  case FIPAGE + USER: printf("FIPAGE + USER [0x%x]", lvadr()); panic("!\n"); // XXX psignal(SIGSEG) or page in
  case FWPAGE:
  case FWPAGE + USER:
  case FRPAGE:        // XXX
  case FRPAGE + USER: // XXX
    if ((va = lvadr()) >= init->sz) panic("va > u->sz\n");;
    pc--; // printf("fault"); // restart instruction
    mappage(init->pdir, va & -PAGE, V2P+(memset(kalloc(), 0, PAGE)), PTE_P | PTE_W | PTE_U);
    return;

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
  static char kstack[256]; // temp kernel stack
  static int endbss;     // last variable in bss segment
  printf("main %x, Welcome!\n",main);  
  // initialize memory allocation
  mem_top = kreserved = ((uint)&endbss + PAGE + 3) & -PAGE; 
  mem_sz = msiz();
  
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
