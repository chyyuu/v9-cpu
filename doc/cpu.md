# v9-cpu

## 概述
v9-cpu是一个假想的简单CPU，用于操作系统教学实验和练习．

## 寄存器组:
总共有 9 个寄存器,其中 7 个为 32 位,2 个为 64 位(浮点寄存器)。本文档只针对 CPU
进行描述,对于相关的硬件配套外设(例如中断控制器等)在此不做介绍。其中：

 - a, b, c : 三个通用寄存器
 - f, g 两个浮点寄存器,是用来进行各种指令操作的。
 - sp 为当前栈指针
 - pc 为程序计数器
 - tsp 为栈顶指针(本 CPU 的栈是从顶部往底部增长)
 - flags 为状态寄存器(包括当前的运行模式,是否中断使能,是否有自陷,以及是否使用虚拟地址等)。

### flags寄存器标志位
 - user   : 1; 用户态或内核态(user mode or not.)
 - iena   : 1; 中断使能/失效 (interrupt flag.)
 - trap   : 1; 异常/错误编码 (fault code.)
 - paging : 1; 页模式使能/失效（ virtual memory enabled or not.）

 
## 指令集

总共有 209 条指令,具体的命令在指令的低 8 位,高 24 位为操作数 0。对于具体的命令,
其中 4 条既不需要操作数,也不需要当前的 CPU 信息;还有 92 条也不需要操作数,但需要当
前 CPU 的信息;剩下的 113 条命令,需要一个 24 位的操作数和当前的 CPU 信息。


v9-cpu的指令集如下：

```
指令				  编码								功能

计算指令

ADDIU rt rs imme  001001ssssstttttiiiiiiiiiiiiiiii  rt <- rs + sign_extend(imme)
ANDI  rt rs imme  001100ssssstttttiiiiiiiiiiiiiiii  rt <- rs & zero_extend(imme)
ORI   rt rs imme  001101ssssstttttiiiiiiiiiiiiiiii  rt <- rs | zero_extend(imme)
XORI  rt rs imme  001110ssssstttttiiiiiiiiiiiiiiii  rt <- rs ^ zero_extend(imme)
ADDU  rd rs rt	  000000ssssstttttddddd00000100001  rd <- rs + rt
SUBU  rd rs rt    000000ssssstttttddddd00000100011  rd <- rs - rt
AND   rd rs rt	  000000ssssstttttddddd00000100100  rd <- rs & rt
OR    rd rs rt    000000ssssstttttddddd00000100101  rd <- rs | rt
XOR   rd rs rt    000000ssssstttttddddd00000100110  rd <- rs ^ rt
NOR   rd rs rt    000000ssssstttttddddd00000100111  rd <- ~(rs | rt)
MULT  rs rt       000000sssssttttt0000000000011000  lo <- (rs * rt) >> 32,  hi <- (rs * rt) & 65535
DIV   rs rt       000000sssssttttt0000000000011010  lo <- rs / rt,  hi <- rs % rt
SLL   rd rt imme  00000000000tttttdddddiiiii000000  rd <- rt sll imme
SRL   rd rt imme  00000000000tttttdddddiiiii000010  rd <- rt srl imme
SRA   rd rt imme  00000000000tttttdddddiiiii000011  rd <- rt sra imme
SLLV  rd rt rs    000000ssssstttttddddd00000000100  rd <- rt sll rs
SRLV  rd rt rs    000000ssssstttttddddd00000000110  rd <- rt srl rs
SRAV  rd rt rs    000000ssssstttttddddd00000000111  rd <- rt sra rs

条件跳转指令

BLTZ  rs imme     000001sssss00000iiiiiiiiiiiiiiii  if (rs < 0)   PC <- PC + sign_extend(imme)
BGEZ  rs imme     000001sssss00001iiiiiiiiiiiiiiii  if (rs >= 0)  PC <- PC + sign_extend(imme)
BEQ   rs rt imme  000100ssssstttttiiiiiiiiiiiiiiii  if (rs == rt) PC <- PC + sign_extend(imme)
BNE   rs rt	imme  000101ssssstttttiiiiiiiiiiiiiiii  if (rs != rt) PC <- PC + sign_extend(imme)
BLEZ  rs imme     000110sssss00000iiiiiiiiiiiiiiii  if (rs <= 0)  PC <- PC + sign_extend(imme)
BGTZ  rs imme	  000111sssss00000iiiiiiiiiiiiiiii  if (rs > 0)   PC <- PC + sign_extend(imme)

跳转指令

J     imme        000010iiiiiiiiiiiiiiiiiiiiiiiiii  PC <- sign_extend(imme)
JAL   imme        000011iiiiiiiiiiiiiiiiiiiiiiiiii  PC <- sign_extend(imme), ra <- RPC
JR    rs		  000000sssss000000000000000001000  PC <- rs
JALR  rs rd		  000000sssss00000ddddd00000001001  PC <- rs, rd <- RPC


SLT   rd rs rt	  000000ssssstttttddddd00000101010  rd <- (rs < rt) (sign_compare)
SLTU  rd rs rt    000000ssssstttttddddd00000101011  rd <- (rs < rt) (unsign_compare)
SLTI  rt rs imme  001010ssssstttttiiiiiiiiiiiiiiii  rt <- (rs < sign_extend(imme)) (sign_compare)
SLTIU rt rs imme  001011ssssstttttiiiiiiiiiiiiiiii  rt <- (rs < zero_extend(imme)) (unsign_compare)

MFLO  rd		  0000000000000000ddddd00000010010  rd <- lo
MFHI  rd		  0000000000000000ddddd00000010000  rd <- hi
MTLO  rd		  0000000000000000ddddd00000010011  lo <- rd
MTHI  rd		  0000000000000000ddddd00000010001  hi <- rd
MFC0  rt rd		  01000000000tttttddddd00000000000  rt <- cp0[rd]
MTC0  rt rd       01000000100tttttddddd00000000000  cp0[rd] <- rt

LB    rt rs imme  100000ssssstttttiiiiiiiiiiiiiiii  sign_extend(rt <- MEM[rs+sign_extend(imme)] & 255)
LBU   rt rs imme  100100ssssstttttiiiiiiiiiiiiiiii  zero_extend(rt <- MEM[rs+sign_extend(imme)] & 255)
LH    rt rs imme  100001ssssstttttiiiiiiiiiiiiiiii  sign_extend(rt <- MEM[rs+sign_extend(imme)] & 65535)
LHU   rt rs imme  100101ssssstttttiiiiiiiiiiiiiiii  zero_extend(rt <- MEM[rs+sign_extend(imme)] & 65535)
LW    rt rs imme  100011ssssstttttiiiiiiiiiiiiiiii  rt <- MEM[rs+sign_extend(imme)]
SB    rt rs imme  101000ssssstttttiiiiiiiiiiiiiiii  MEM[rs+sign_extend(imme)] <- MEM[rs+sign_extend(imme)] & ~(255) | (rt & 255)
SW    rt rs imme  101011ssssstttttiiiiiiiiiiiiiiii  MEM[rs+sign_extend(imme)] <- rt

SYSCALL           00000000000000000000000000001100  system call
ERET			  01000010000000000000000000000000  PC <- cp0[EPC], cp0_status <- cp0_status & ~(1<<EXL)
TLBWI			  01000010000000000000000000000010  write TLB table item in special index.
```


## 内存

v9-cpu的内存是设在javascript环境下的一段预设数组中，数组储存的内容为32位整数。内存设置了与处理器相连的三个数据通路，分别用于指令的读取，数据的读取以及数据的写入。数据通路由32位的数据线，22位的地址线和读写使能构成。读写使能跟一般的RAM相比做了很大的简化。当写入数据时，直接将OE和WE置0，当读取数据时，将OE和RE置0。内存划分为指令区域，只读区域，读写区域以及IO缓存区域，指令存放的地址空间为0x0000~0x0fff（虚地址，下同），只能读取不能更改的地址空间为0x1000~0x2fff，自由读写的地址空间为0x3000~0xbeff，IO的缓存即串口缓存的地址为0xbf00,0xbf01,0xbf02,0xbf03。
实地址与虚地址之间的转换通过地址映射方法（静态）以及常规的由TLB寄存器储存的页表（动态）来配合实现。TLB的表项长度为64位，前22位为虚地址的前22位，后面依次跟着奇偶实地址对应的前22位和标志位。实虚地址转换的过程如下：
（1）	对输入的虚地址进行合法性检测，并触发虚地址非法异常。
（2）	静态方法和动态页表同时进行实地址生成。当动态页表缺失时触发TLB MISS异常，跳转到异常处理代码重填TLB。
v9-cpu的输入输出包括键盘的输入和屏幕的输出。键盘输入的字符存储于地址0xbf00（串口1），屏幕的输出为0xbf01（串口2）。

中断/异常协 理的一般流程如下:
(1)		保存中断信息,主要是 EPC,BadVAddr,Status,Cause 等寄存器的信息。
EPC:存储异常协理之后程序恢复执行的地址。选于一般异常,当前开生错错的指令地址即为EPC应当保存的地址;而选于硬件中断,由于是异步产生则可以任意设定一条并未执行完成的指令地址保存,但在进 入下一步协理之前,该指令前的指令都应当被执行完。
BadVAddr:捕捉最近一次地址错或TLB异常(重填、失效、修改)时的虚拟地址。 
Status:将EXL位置为1,进入kernel模式进行中断协理
Cause:记下异常号。
EnrtyHi:tlb异常时,记记下BadVAddr的部分高位。
(2)		根据Cause中的异常号跳转到相应的异常协理函数入口
(3)		中断协理
(4)		通过调用ERET指令恢复实复, 返回EPC所存地址执行并且将Status中的EXL重置为0表示进入user模式。
