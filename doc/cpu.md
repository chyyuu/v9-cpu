# v9-cpu

## 概述
v9-cpu是一个假想的简单CPU，用于操作系统教学实验和练习．

## 寄存器组:
总共有 9 个寄存器,其中 7 个为 32 位,2 个为 64 位(浮点寄存器)。本文档只针对 CPU
进行描述,对于相关的硬件配套外设(例如中断控制器等)在此不做介绍。其中：

 - a, b, c : 三个通用寄存器
 - f, g 两个浮点寄存器,是用来进行各种指令操作的。
 - sp 为当前栈指针
 - pc 为程序计数器（指向下一条指令），其指向的内存内容（即具体的指令值）会放到ir中，给CPU解码并执行
 - tsp 为栈顶指针(本 CPU 的栈是从顶部往底部增长)
 - flags 为状态寄存器(包括当前的运行模式,是否中断使能,是否有自陷,以及是否使用虚拟地址等)。
 
 

### flags寄存器标志位
 - user   : 1; 用户态或内核态(user mode or not.)
 - iena   : 1; 中断使能/失效 (interrupt flag.)
 - trap   : 1; 异常/错误编码 (fault code.)
 - paging : 1; 页模式使能/失效（ virtual memory enabled or not.）

 
## 指令集

总共有209条指令,一条指令大小为32bit, 具体的命令编码在指令的低8位,高24位为操作数。
但很多指令只是某一类指令的多种形式，很容易触类旁通。

整体来看,指令分为如下几类:

 - 运算指令：如ADD, SUB等
 - 跳转指令：如JMP, JSR，LEV等
 - 访存（Load/Store）指令：如LL, LBL, SL等
 - 系统命令:如HALT, RTI, IDLE，SSP, USP,IVEC, PDIR，目的是为了操作系统设计
 - 扩展函数库命令：如MCPY/MCMP/MCHR/MSET, MATH类，NET类，目的为了简化编译器设计

按照指令对条件的需求，指令可分类如下：

 - HALT, RTI, IDLE等，不需要立即数来参与，也不需要读取或修改寄存器值。
 - ADD, MCPY，PSHA等，不需要立即数来参与，但需要读取或修改寄存器值。
 - ADDI, S/L系列，B系列等，需要立即数和寄存器来参与。
 


v9-cpu的指令集如下：


### 显示格式
```
指令名字　指令值　　指令含义
```

### 指令集明细

#### system
```
HALT	0xiiiiii00	halt system
ENT		0xiiiiii01  sp += imme
LEV		0xiiiiii02  pc =  *sp,	sp += imme+8
JMP		0xiiiiii03	pc += imme
JMPI	0xiiiiii04	pc += imme+ra>>2
JSR		0xiiiiii05	*sp = pc,	sp -= 8,	pc += imme
JSRA	0xiiiiii06	*sp = pc,	sp -= 8,	pc += ra
LEA		0xiiiiii07	ra = sp+imme
LEAG	0xiiiiii08	ra = pc+imme
CYC		0xiiiiii09	ra = current cycle related with pc
MCPY	0xiiiiii0a	memcpy(ra, rb, rc)
MCMP	0xiiiiii0b	memcmp(ra, rb, rc)
MCHR	0xiiiiii0c  memchr(ra, rb, rc)
MSET	0xiiiiii0d	memset(ra, rb, rc)
```

#### load to register a
```
LL		0xiiiiii0e	ra = *(*unit)  (sp+imme)
LLS		0xiiiiii0f	ra = *(*short) (sp+imme)
LLH		0xiiiiii10	ra = *(*ushort)(sp+imme)
LLC		0xiiiiii11	ra = *(*char)  (sp+imme)
LLB		0xiiiiii12	ra = *(*uchar) (sp+imme)
LLD		0xiiiiii13	ra = *(*double)(sp+imme)
LLF		0xiiiiii14	ra = *(*float) (sp+imme)
LG		0xiiiiii15	ra = *(*unit)  (pc+imme)
LGS		0xiiiiii16	ra = *(*short) (pc+imme)
LGH		0xiiiiii17	ra = *(*ushort)(pc+imme)
LGC		0xiiiiii18	ra = *(*char)  (pc+imme)
LGB		0xiiiiii19	ra = *(*uchar) (pc+imme)
LGD		0xiiiiii1a	ra = *(*double)(pc+imme)
LGF		0xiiiiii1b	ra = *(*float) (pc+imme)
LX		0xiiiiii1c	ra = *(*unit)  conv_to_phy_addr(imme)
LXS		0xiiiiii1d	ra = *(*short) conv_to_phy_addr(imme)
LXH		0xiiiiii1e	ra = *(*ushort)conv_to_phy_addr(imme)
LXC		0xiiiiii1f	ra = *(*char)  conv_to_phy_addr(imme)
LXB		0xiiiiii20	ra = *(*uchar) conv_to_phy_addr(imme)
LXD		0xiiiiii21	ra = *(*double)conv_to_phy_addr(imme)
LXF		0xiiiiii22	ra = *(*float) conv_to_phy_addr(imme)
LI		0xiiiiii23	ra = imme
LHI		0xiiiiii24	ra = (ra<<24)|(imme>>8)
LIF		0xiiiiii25	rf = double(imme)
```

#### load to register b
```
LBL		0xiiiiii26	rb = *(*uint)  (sp+imme)
LBLS	0xiiiiii27	rb = *(*short) (sp+imme)
LBLH	0xiiiiii28	rb = *(*ushort)(sp+imme)
LBLC	0xiiiiii29	rb = *(*char)  (sp+imme)
LBLB	0xiiiiii2a	rb = *(*uchar) (sp+imme)
LBLD	0xiiiiii2b	rb = *(*double)(sp+imme)
LBLF	0xiiiiii2c	rb = *(*float) (sp+imme)
LBG		0xiiiiii2d	rb = *(*uint)  (pc+imme)
LBGS	0xiiiiii2e	rb = *(*short) (pc+imme)
LBGH	0xiiiiii2f	rb = *(*ushort)(pc+imme)
LBGC	0xiiiiii30	rb = *(*char)  (pc+imme)
LBGB	0xiiiiii31	rb = *(*uchar) (pc+imme)
LBGD	0xiiiiii32	rb = *(*double)(pc+imme)
LBGF	0xiiiiii33	rb = *(*float) (pc+imme)
LBX		0xiiiiii34	rb = *(*uint)  conv_to_phy_addr(imme)
LBXS	0xiiiiii35	rb = *(*short) conv_to_phy_addr(imme)
LBXH	0xiiiiii36	rb = *(*ushort)conv_to_phy_addr(imme)
LBXC	0xiiiiii37	rb = *(*char)  conv_to_phy_addr(imme)
LBXB	0xiiiiii38	rb = *(*uchar) conv_to_phy_addr(imme)
LBXD	0xiiiiii39	rb = *(*double)conv_to_phy_addr(imme)
LBXF	0xiiiiii3a	rb = *(*float) conv_to_phy_addr(imme)
LBI		0xiiiiii3b  rb = imme
LBHI	0xiiiiii3c  rb = (rb<<24)|(imme>>8)
LBIF	0xiiiiii3d  rb = double(imme)
LBA		0xiiiiii3e  rb = ra
LBAD    0xiiiiii3f  rg = rf
```

#### store register a to memory 
```
SL		0xiiiiii40	*(*uint)  (sp+imme) = (uint)  (ra)
SLH		0xiiiiii41	*(*ushort)(sp+imme) = (ushort)(ra)
SLB		0xiiiiii42	*(*uchar) (sp+imme) = (ushort)(ra)
SLD		0xiiiiii43	*(*double)(sp+imme) = (double)(ra)
SLF		0xiiiiii44	*(*float) (sp+imme) = (float) (ra)
SG		0xiiiiii45	*(*uint)  (pc+imme) = (uint)  (ra)
SGH		0xiiiiii46	*(*ushort)(pc+imme) = (ushort)(ra)
SGB		0xiiiiii47	*(*uchar) (pc+imme) = (ushort)(ra)
SGD		0xiiiiii48	*(*double)(pc+imme) = (double)(ra)
SGF		0xiiiiii49	*(*float) (pc+imme) = (float) (ra)
SX		0xiiiiii4a  *(*uint)  conv_to_phy_addr(imme) = (uint)  (ra)
SXH		0xiiiiii4b	*(*ushort)conv_to_phy_addr(imme) = (ushort)(ra)
SXB		0xiiiiii4c	*(*uchar) conv_to_phy_addr(imme) = (ushort)(ra)
SXD		0xiiiiii4d	*(*double)conv_to_phy_addr(imme) = (double)(ra)
SXF		0xiiiiii4e	*(*float) conv_to_phy_addr(imme) = (float) (ra)
```

#### arithmetic
```
ADDF	0xiiiiii4f	rf = rf+rg
SUBF	0xiiiiii50	rf = rf-rg
MULF	0xiiiiii51	rf = rf*rg
DIVF	0xiiiiii52	rf = rf/rg
ADD		0xiiiiii53  ra = ra+rb
ADDI	0xiiiiii54	ra = ra+imme
ADDL	0xiiiiii55	ra = ra+(*(int*)(sp+imme))
SUB		0xiiiiii56  ra = ra-rb
SUBI	0xiiiiii57	ra = ra-imme
SUBL	0xiiiiii58	ra = ra-(sp+imme)
MUL		0xiiiiii59	ra = (int)(ra)*(int)(rb)
MULI	0xiiiiii5a	ra = (int)(ra)*(int)(imme)
MULL	0xiiiiii5b	ra = (int)(ra)*(*(int*)(sp+imme))
DIV		0xiiiiii5c	ra = (int)(ra)/(int)(rb)
DIVI	0xiiiiii5d	ra = (int)(ra)/(int)(imme)
DIVL	0xiiiiii5e	ra = (int)(ra)/(*(int*)(sp+imme))
DVU		0xiiiiii5f	ra = (uint)(ra)/(uint)(rb)
DVUI	0xiiiiii60	ra = (uint)(ra)/(uint)(imme)
DVUL	0xiiiiii61	ra = (uint)(ra)/(*(uint*)(sp+imme))
MOD		0xiiiiii62	ra = (int)(ra)%(int)(rb)
MODI	0xiiiiii63	ra = (int)(ra)%(int)(imme)
MODL	0xiiiiii64	ra = (int)(ra)%(*(int*)(sp+imme))
MDU		0xiiiiii65	ra = (uint)(ra)%(uint)(rb)
MDUI	0xiiiiii66	ra = (uint)(ra)%(uint)(imme)
MDUL	0xiiiiii67	ra = (uint)(ra)%(*(uint*)(sp+imme))
AND		0xiiiiii68	ra = ra&rb
ANDI	0xiiiiii69	ra = ra&imme
ANDL	0xiiiiii6a	ra = ra&(*(int*)(sp+imme))
OR		0xiiiiii6b	ra = ra|rb
ORI		0xiiiiii6c	ra = ra|imme
ORL		0xiiiiii6d	ra = ra|(*(int*)(sp+imme))
XOR		0xiiiiii6e	ra = ra^rb
XORI	0xiiiiii6f	ra = ra^imme
XORL	0xiiiiii70	ra = ra^(*(int*)(sp+imme))
SHL		0xiiiiii71	ra = ra<<(uint)(rb)
SHLI	0xiiiiii72	ra = ra<<(uint)(imme)
SHLL	0xiiiiii73	ra = ra<<(*(uint*)(sp+imme))
SHR		0xiiiiii74	ra = (int)(ra)>>(uint)(rb)
SHRI	0xiiiiii75	ra = (int)(ra)>>(uint)(imme)
SHRL	0xiiiiii76	ra = (int)(ra)>>(*(uint*)(sp+imme))
SRU		0xiiiiii77	ra = (uint)(ra)>>(uint)(rb)
SRUI	0xiiiiii78	ra = (uint)(ra)>>(uint)(imme)
SRUL	0xiiiiii79	ra = (uint)(ra)>>(*(uint*)(sp+imme))
EQ		0xiiiiii7a  ra = (ra == rb)
EQF		0xiiiiii7b	ra = (rf == rg)
NE		0xiiiiii7c	ra = (ra != rb)
NEF		0xiiiiii7d	ra = (rf != rg)
LT		0xiiiiii7e	ra = ((int)a < (int)b)
LTU		0xiiiiii7f	ra = ((uint)a < (uint)b)
LTF		0xiiiiii80	ra = (f < g)
GE		0xiiiiii81	ra = ((int)a > (int)b)
GEU		0xiiiiii82	ra = ((uint)a > (uint)b)
GEF		0xiiiiii83	ra = (f > g)
```
#### conditional branch
```
BZ		0xiiiiii84	if (ra == 0)  pc = pc+imme
BZF		0xiiiiii85	if (rf == 0)  pc = pc+imme
BNZ		0xiiiiii86  if (ra != 0)  pc = pc+imme
BNZF	0xiiiiii87	if (rf != 0)  pc = pc+imme
BE		0xiiiiii88	if (ra == rb) pc = pc+imme
BEF		0xiiiiii89	if (rf == rg) pc = pc+imme
BNE		0xiiiiii8a  if (ra != rb) pc = pc+imme
BNEF	0xiiiiii8b	if (rf != rg) pc = pc+imme
BLT		0xiiiiii8c	if ((int)a < (int)b) pc = pc+imme
BLTU	0xiiiiii8d	if ((uint)a < (uint)b) pc = pc+imme
BLTF	0xiiiiii8e	if (f < g) pc = pc+imme
BGE		0xiiiiii8f	if ((int)a < (int)b) pc = pc+imme
BGEU	0xiiiiii90	if ((uint)a < (uint)b) pc = pc+imme
BGEF	0xiiiiii91	if (f < g) pc = pc+imme
```

#### conversion
```
CID		0xiiiiii92  f = (double)((int)a)
CUD		0xiiiiii93	f = (double)((uint)a)
CDI		0xiiiiii94	a = (int)(f)
CDU		0xiiiiii95	a = (uint)(f)
```

#### misc
```
CLI		0xiiiiii96	a = iena,	iena = 0
STI		0xiiiiii97	if generated by hardware: set trap, and process the interrupt; else: iena = 1
RTI		0xiiiiii98	return from interrupt, set pc, sp, may switch user/kernel mode; if has pending interrupt, process the interrupt
BIN		0xiiiiii99	a = kbchar,	kbchar is the value from outside io
BOUT	0xiiiiii9a	a = write(a, &b, 1);
NOP		0xiiiiii9b	no operation.
SSP		0xiiiiii9c	ksp = a -- ksp is kernel sp
PSHA	0xiiiiii9d	sp -= 8, *sp = a
PSHI	0xiiiiii9e	sp -= 8, *sp = imme
PSHF	0xiiiiii9f	sp -= 8, *(double *)sp = f
PSHB	0xiiiiiia0	sp -= 8, *sp = b
POPB	0xiiiiiia1	b = *sp, sp += 8
POPF	0xiiiiiia2	f = *(double *)sp, sp += 8
POPA	0xiiiiiia3	a = *sp, sp += 8
IVEC	0xiiiiiia4	ivec = a -- set interrupt vector by a
PDIR	0xiiiiiia5	pdir = mem + a -- set page directory physical memory by a
SPAG	0xiiiiiia6	paging = a -- enable/disable virtual memory feature by a
TIME	0xiiiiiia7	if operand0 is 0: timeout = a -- set current timeout from a; else: printk("timer%d=%u timeout=%u", operand0, timer, timeout)
LVAD	0xiiiiiia8	a = vadr -- vadr is bad virtual address
TRAP	0xiiiiiia9	trap = FSYS
LUSP	0xiiiiiiaa	a = usp	
SUSP	0xiiiiiiab	usp = a -- usp is user stack pointer
LCL		0xiiiiiiac	c = *(uint *)(sp + imme)
LCA		0xiiiiiiad	c = a
PSHC	0xiiiiiiae	sp -= 8, *sp = c
POPC	0xiiiiiiaf	c = *sp, sp += 8
MSIZ 	0xiiiiiib0	a = memsz -- move physical memory to a.
PSHG	0xiiiiiib1	sp -= 8, *sp = g
POPG	0xiiiiiib2	g = *sp, sp += 8
NET1	0xiiiiiib3	No use
NET2	0xiiiiiib4	No use
NET3	0xiiiiiib5	No use
NET4	0xiiiiiib6	No use
NET5	0xiiiiiib7	No use
NET6	0xiiiiiib8	No use
NET7	0xiiiiiib9	No use
NET8	0xiiiiiiba	No use
NET9	0xiiiiiibb	No use
```

#### math 
```
POW		0xiiiiiibc	rf = power(rf, rg)
ATN2	0xiiiiiibd	rf = atan2(rf, rg)
FABS	0xiiiiiibe	rf = fabs(rf, rg)
ATAN	0xiiiiiic0	rf = atan(rf)
LOG 	0xiiiiiic1	rf = log(rf)
LOGT	0xiiiiiic2	rf = log10(rf)
EXP 	0xiiiiiic3	rf = exp(rf)
FLOR	0xiiiiiic4	rf = floor(rf)
CEIL	0xiiiiiic5	rf = ceil(rf)
HYPO	0xiiiiiic6	rf = hypo(rf, rg)
SIN 	0xiiiiiic7	rf = sin(rf)
COS 	0xiiiiiic8	rf = cos(rf)
TAN 	0xiiiiiic9	rf = tan(rf)
ASIN	0xiiiiiica	rf = asin(rf)
ACOS	0xiiiiiicb	rf = acos(rf)
SINH	0xiiiiiicc	rf = sinh(rf)
COSH	0xiiiiiicd	rf = cosh(rf)
TANH	0xiiiiiice	rf = tanh(rf)
SQRT	0xiiiiiicf	rf = sqrt(rf)
FMOD	0xiiiiiid0	rf = fmod(rf, rg)
```

#### cpu idle
```
IDLE 	0xiiiiiid1	response hardware interrupt (include timer).
```

## 内存
缺省内存大小为128MB，可以通过启动参数"-m XXX"，设置为XXX MB大小．
在TLB中，设置了4个1MB大小页转换表（page translation buffer array）
 - kernel read page translation table
 - kernel write page translation table
 - user read page translation table
 - user write page translation table

有两个指针tr/tw, tw指向内核态或用户态的read/write　page translation table．
```
tr/tw[page number]=phy page number //页帧号
```
还有一个tpage buffer array, 保存了所有tr/tw中的虚页号，这些虚页号是tr/tw数组中的index 
```
tpage[tpages++] = v //v是page number
```

## IO操作
### 写外设（类似串口写）的步骤
 - 1 --> a
 - 一个字符'char' --> b
 - BOUT　　//如果在内核态，在终端上输出一个字符'char', 1-->a，如果在用户态，产生FPRIV异常

### 读外设（类似串口读）的步骤
　- BIN  //如果在内核态，kchar -->a  kchar是模拟器定期轮询获得的一个终端输入字符
 　　
如果iena(中断使能)，则在获得一个终端输入字符后，会产生FKEYBD中断 
 
### 设置timer的timeout
 - val --> a
 - TIME // 如果在内核态，设置timer的timeout为a; 如果在用户态，产生FPRIV异常
 


## 中断/异常
### 一些变量的含义：
 - ivec: 中断向量的地址
 
### 中断/异常类型
```
- FMEM,          // bad physical address 
- FTIMER,        // timer interrupt
- FKEYBD,        // keyboard interrupt
- FPRIV,         // privileged instruction
- FINST,         // illegal instruction
- FSYS,          // software trap
- FARITH,        // arithmetic trap
- FIPAGE,        // page fault on opcode fetch
- FWPAGE,        // page fault on write
- FRPAGE,        // page fault on read
- USER 　　　　      // user mode exception 
```

### 设置中断向量
 - val --> a
 - IVEC // 如果在内核态，设置中断向量的地址ivec为a; 如果在用户态，产生FPRIV异常

### 中断/异常产生的处理
 - 如果终端产生了键盘输入，且iean=1，则ipend |= FKEYBD，0-->iena
 - 如果timer产生了timeout，且iean=1，则ipend |= FTIMER，0-->iena
 - 如果产生了其他异常，则会有相应的处理，
 
 然后，保存中断的地址到kkernel mode的sp中，pc会跳到中断向量的地址ivec处执行
 
　
## CPU执行过程
### 一些变量的含义：
主要集中在em.c的cpu()函数中

 - a: a寄存器
 - b: b寄存器
 - c: c寄存器
 - f: f浮点寄存器
 - g: g浮点寄存器
 - ir:　指令寄存器
 - xpc: pc在host内存中的值
 - fpc: pc在host内存中所在页的下一页的起始地址值
 - tpc: pc在host内存中所在页的起始地址值
 - xsp: sp在host内存中的值
 - tsp: sp在host内存中所在页的起始地址值
 - fsp: 辅助判断是否要经过tr/tw的分析
 - ssp: 内核态的栈指针
 - usp: 用户态的栈指针
 - cycle: 指令执行周期计数
 - xcycle:　用于判断外设中断的执行频度，和调整最终的指令执行周期计数（需进一步明确?）
 - timer: 当前时钟计时（和时间时间中断有关）
 - timeout: 时钟周期，当timer>timeout时，产生时钟中断 
 -　detla:　一次指令执行的时间，timer+=delta
 
 ###执行过程概述
 
 1. 首先，读入os kernel文件到内存的底部，并把pc放置到os kernel文件指定的内存位置，
 设置可用sp为　MEM_SZ-FS_SZ=124MB
 1. 然后从os kernel文件的起始地址开始执行
 1. 如果碰到异常或中断，则保存中断的地址，并跳到中断向量的地址ivec处执行
