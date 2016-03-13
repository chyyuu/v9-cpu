
## keyword，operator的含义
// tokens and node types ( >= 128 so not to collide with ascii-valued tokens)
typedef enum {
  Num = 128, // low ordering of Num and Auto needed by nodc()

  // keyword grouping needed by main()  XXX missing extern and register
  Asm, Auto, Break, Case, Char, Continue, Default, Do, Double, Else, Enum, Float, For, Goto, If, Int, Long, Return, Short,
  Sizeof, Static, Struct, Switch, Typedef, Union, Unsigned, Void, While, Va_list, Va_start, Va_arg,

  Id, Numf, Ptr, Not, Notf, Nzf, Lea, Leag, Fun, FFun, Fcall, Label, FLabel,
  Cid ,Cud ,Cdi ,Cdu ,Cic ,Cuc ,Cis ,Cus,
  Dots,
  Addaf, Subaf, Mulaf, Dvua, Divaf, Mdua, Srua,
  Eqf, Nef, Ltu, Ltf, Geu, Gef, Sru,
  Addf, Subf, Mulf, Dvu, Divf, Mdu,

  // operator precedence order needed by expr()
  Comma,
  Assign,
  Adda, Suba, Mula, Diva, Moda, Anda, Ora, Xora, Shla, Shra,
  Cond,
  Lor, Lan,
  Or,  Xor, And,
  Eq,  Ne,
  Lt,  Gt,  Le,  Ge,
  Shl, Shr,
  Add, Sub,
  Mul, Div, Mod,
  Inc, Dec, Dot, Arrow, Brak, Paren
}TOKEN;


看decl了啥，然后再进一步处理

Cid, ...是啥意思？

va = vp = (int) new(VAR_SZ);　存储所有分析出来的变量

type : 判断当前token是啥，比如 function
decl　声明
　　　　　--> basetype : 类型声明，Void, Va_list, Unsigned Char/Short/Long/Int, signed Char/Short/Long/Int/Float/Double, Union, Struct, Enum, Id
     --> function:　函数声明，并进一步处理相关函数
     　　  --> state(): 进一步处理函数中的每条语句：if/else, while, return, break, continue, for, do, switch, case, asm, va_start, Default(像是标号), goto, id, 
             --> assign(): 先处理expr, 然后会有assign的处理
     　　      --> expr(); 处理表达式　Num, Add, Inc...，也包含funcall处理
     　　      　　　　--> node()　一个表达式节点的parse // expression parsing   int *e, // expression tree pointer
     　　      --> rv(): 根据expr的处理，形成的e，生成指令
     　　      　　　　--> op //ADD/SUB/OR/DIV....
     　　      　　　　--> opt cond_test  //Eq, Ne, 
     　　      　　　 --> Cxx, 格式转换 // Cid, Cdi, ...
     　　      　　　 --> Logical or/and/not...
     　　      　　　 --> Num, Ptr  用了LI, LX
     　　      　　　 --> Lea: LEA :　出栈　pop
     　　      　　　 --> Fun: LEAG: CALL fun
     　　      　　　 --> FFun, 前向fun
     　　      　　　 --> Auto: LL
     　　      　　　 --> Static: LG
     　　      　　　 --> Fcall: 生成调用函数的一系列操作，包括函数返回后去除堆栈中的参数     　　      　　　 
     --> Auto: 局部变量声明 
     --> Assign: 初始化的全局变量声明
     --> 其他: 未初始化的全局变量声明
