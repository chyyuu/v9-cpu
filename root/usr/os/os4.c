// os4.c -- one user task and implement write syscall

#include <u.h>

enum {    // processor fault codes
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

char task0_stack[1000];
char task0_kstack[1000];

int *task0_sp;

int current;

out(port, val)  { asm(LL,8); asm(LBL,16); asm(BOUT); }
ivec(void *isr) { asm(LL,8); asm(IVEC); }
stmr(int val)   { asm(LL,8); asm(TIME); }
halt(value)     { asm(LL,8); asm(HALT); }

sys_write(fd, char *p, n) { int i; for (i=0; i<n; i++) out(fd, p[i]); return i; }

write() { asm(LL,8); asm(LBL,16); asm(LCL,24); asm(TRAP,S_write); }

task0()
{
  while(current < 10)
    write(1, "_", 1);

  write(1,"task0 exit\n", 11);
  halt(0);
}

trap(int *sp, int c, int b, int a, int fc, unsigned *pc)
//trap(int *sp, int c, int b, int a, int fc, unsigned *pc)
{
  switch (fc) {
  case FSYS + USER: // syscall
    switch (pc[-1] >> 8) {
    case S_write: a = sys_write(a, b, c); break;
    default: sys_write(1, "panic! unknown syscall\n", 23); asm(HALT);
    }
    break;
    
  case FTIMER:  // timer
  case FTIMER + USER:
    out(1,'x');
    current++;
    break;
    
  default:
    default: sys_write(1, "panic! unknown interrupt\n", 25); asm(HALT);  
  }
}

alltraps()
{
  asm(PSHA);
  asm(PSHB);
  asm(PSHC);
  asm(LUSP);
  asm(PSHA);
  trap();                // registers passed by reference/magic
  asm(POPA);
  asm(SUSP);
  asm(POPC);
  asm(POPB);
  asm(POPA);
  asm(RTI);
}

trapret()
{
  asm(POPA); //a=*sp, sp+=8 byte
  asm(SUSP); //usp=a
  asm(POPC); //c=*sp, sp+=8 byte
  asm(POPB); //b=*sp, sp+=8 byte
  asm(POPA); //a=*sp, sp+=8 byte
  asm(RTI);  //return from interrupt, mode=USER, pc=&task0
}

main()
{
  int *kstack;
  
  stmr(5000);
  ivec(alltraps);

  task0_sp = &task0_kstack[1000];
  task0_sp -= 2; *task0_sp = &task0;
  task0_sp -= 2; *task0_sp = USER; // fault code
  task0_sp -= 2; *task0_sp = 0; // a
  task0_sp -= 2; *task0_sp = 0; // b
  task0_sp -= 2; *task0_sp = 0; // c
  task0_sp -= 2; *task0_sp = &task0_stack[1000]; //user stack
  task0_sp -= 2; *task0_sp = &trapret;

  kstack = task0_sp; //kernel stack
  
  asm(LL, 4); // a = kstack
  asm(SSP);   // sp = a
  asm(LEV);   // return
}