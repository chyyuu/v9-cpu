// em -- cpu emulator
//
// Usage:  em [-v] [-m memsize] [-f filesys] file
//
// Description:
//
// Written by Robert Swierczek

#include <u.h>
#include <libc.h>
#include <libm.h>
#include <dir.h>
#include <ctype.h>

enum {
    MEM_SZ = 128 * 1024 * 1024, // default memory size of virtual machine (128M)
    TB_SZ = 1024 * 1024, // page translation buffer array size (4G / pagesize)
    FS_SZ = 4 * 1024 * 1024, // ram file system size (4M)
    TPAGES = 4096,          // maximum cached page translations
};

enum {           // page table entry flags
    PTE_P = 0x001, // Present
    PTE_W = 0x002, // Writeable
    PTE_U = 0x004, // User
    PTE_A = 0x020, // Accessed
    PTE_D = 0x040, // Dirty
};

enum {           // processor fault codes (some can be masked together)
    FMEM,          // bad physical address
    FTIMER,        // timer interrupt
    FKEYBD,        // keyboard interrupt
    FPRIV,         // privileged instruction
    FINST,         // illegal instruction
    FSYS,          // software trap
    FARITH,        // arithmetic trap
    FIPAGE,        // page fault on opcode fetch
    FWPAGE,        // page fault on write
    FRPAGE,        // page fault on read
    USER = 16      // user mode exception
};

uint verbose,    // chatty option -v
        mem, memsz,    // physical memory
        user,          // user mode
        iena,          // interrupt enable
        ipend,         // interrupt pending
        trap,          // fault code
        ivec,          // interrupt vector
        vadr,          // bad virtual address
        paging,        // virtual memory enabled
        pdir,          // page directory
        tpage[TPAGES], // valid page translations
        tpages,        // number of cached page translations
        *trk, *twk,    // kernel read/write page transation tables
        *tru, *twu,    // user read/write page transation tables
        *tr, *tw;     // current read/write page transation tables

char *cmd;       // command name

static int dbg;  // debugger enable flag
static char dbgbuf[0x200];

struct {
    uint magic, bss, entry, flags;
} hdr;

// ===========================================================================================================================
//	new feature

const int UNDEFINED = 0x3fffefff;

char ops[] =
        "HALT,ENT ,LEV ,JMP ,JMPI,JSR ,JSRA,LEA ,LEAG,CYC ,MCPY,MCMP,MCHR,MSET," // system
                "LL  ,LLS ,LLH ,LLC ,LLB ,LLD ,LLF ,LG  ,LGS ,LGH ,LGC ,LGB ,LGD ,LGF ," // load a
                "LX  ,LXS ,LXH ,LXC ,LXB ,LXD ,LXF ,LI  ,LHI ,LIF ,"
                "LBL ,LBLS,LBLH,LBLC,LBLB,LBLD,LBLF,LBG ,LBGS,LBGH,LBGC,LBGB,LBGD,LBGF," // load b
                "LBX ,LBXS,LBXH,LBXC,LBXB,LBXD,LBXF,LBI ,LBHI,LBIF,LBA ,LBAD,"
                "SL  ,SLH ,SLB ,SLD ,SLF ,SG  ,SGH ,SGB ,SGD ,SGF ,"                     // store
                "SX  ,SXH ,SXB ,SXD ,SXF ,"
                "ADDF,SUBF,MULF,DIVF,"                                                   // arithmetic
                "ADD ,ADDI,ADDL,SUB ,SUBI,SUBL,MUL ,MULI,MULL,DIV ,DIVI,DIVL,"
                "DVU ,DVUI,DVUL,MOD ,MODI,MODL,MDU ,MDUI,MDUL,AND ,ANDI,ANDL,"
                "OR  ,ORI ,ORL ,XOR ,XORI,XORL,SHL ,SHLI,SHLL,SHR ,SHRI,SHRL,"
                "SRU ,SRUI,SRUL,EQ  ,EQF ,NE  ,NEF ,LT  ,LTU ,LTF ,GE  ,GEU ,GEF ,"      // logical
                "BZ  ,BZF ,BNZ ,BNZF,BE  ,BEF ,BNE ,BNEF,BLT ,BLTU,BLTF,BGE ,BGEU,BGEF," // conditional
                "CID ,CUD ,CDI ,CDU ,"                                                   // conversion
                "CLI ,STI ,RTI ,BIN ,BOUT,NOP ,SSP ,PSHA,PSHI,PSHF,PSHB,POPB,POPF,POPA," // misc
                "IVEC,PDIR,SPAG,TIME,LVAD,TRAP,LUSP,SUSP,LCL ,LCA ,PSHC,POPC,MSIZ,"
                "PSHG,POPG,NET1,NET2,NET3,NET4,NET5,NET6,NET7,NET8,NET9,"
                "POW ,ATN2,FABS,ATAN,LOG ,LOGT,EXP ,FLOR,CEIL,HYPO,SIN ,COS ,TAN ,ASIN," // math
                "ACOS,SINH,COSH,TANH,SQRT,FMOD,"
                "IDLE,";

uint bp[256];
int bpn;             // breakpoints
char *dml;                         // DML file path
char dmlt[1 << 20];                 // DML file text
char sct[65536];
int sctL = 0;     // source code text
char _sct[65536];                 // copy above
int labelJ[65536];                 // label for JSR
int td_sz;                         // TEXT & DATA segment size
char smem[65536];
int smemL = 0; // string memory
int ret_ct;                         // return count

int _strcmp(char *s1, char *s2, int len) {
  char tmp = s1[len];
  s1[len] = '\0';
  int ret = strcmp(s1, s2);
  s1[len] = tmp;
  return ret;
}

struct var_struct {
    char *name;
    char *type;
};
typedef struct var_struct var_t;
struct func_struct {
    uint nameL;
    char *nameS;
    uint addr, line, end;
    uint regN, regL;
    var_t regs[8];
    uint locN, locL;
    var_t locs[32];
};
typedef struct func_struct func_t;
struct const_struct {
    char *name;
    int value;
};
typedef struct const_struct const_t;
struct stu_struct {
    char *name;
    int size;
    int numN;
    var_t nums[256];
};
typedef struct stu_struct stu_t;
struct stmt_struct {
    int start, end;
    char *text;
};
typedef struct stmt_struct stmt_t;
struct backtrace_struct {
    int func, stmt;
    uint sp;
};
typedef struct backtrace_struct btrace_t;
struct watchpoint_struct {
    int ltype;
    uint lvalue;
    int rtype;
    uint rvalue;
    int optype;
};
typedef struct watchpoint_struct wpoint_t;

func_t func[256];
int funcN = 0;
var_t glbs[256];
int glbN = 0;
stu_t stus[256];
int stuN = 0;
stmt_t stmts[65536];
int stmtN = 0;
btrace_t bts[1024];
int btN = 0;
wpoint_t wps[1024];
int wpN = 0, wpR = 1023;
const_t cts[1024];
int ctN = 0;
int oprank[256];

void initialize() {
  int i = strlen(ops) - 1;
  for (; i >= 0; i--)
    if (ops[i] == ',') ops[i] = '\0';
  oprank['!'] = 1;
  oprank['*'] = oprank['/'] = 2;
  oprank['+'] = oprank['-'] = 3;
  oprank['>'] = oprank['<'] = oprank['='] = 4;
  oprank['&'] = oprank['|'] = oprank['^'] = 5;
}

void analysis_dml() {
  int i = 0, j = 0, k = 0;
  int cmt = 0;
  int dml_len = strlen(dmlt);
  for (; i < dml_len; i++) {
    if (!_strcmp(dmlt + i, "start=", 6)) {
      sscanf(dmlt + i, "start=%d end=%d", &stmts[stmtN].start, &stmts[stmtN].end);
      stmts[stmtN].start <<= 2;
      stmts[stmtN].end <<= 2;
    }
    if (!_strcmp(dmlt + i, "<code>", 6)) {
      j = i + 6;
      k = 0x3f3f3f3f;
    }
    if (!_strcmp(dmlt + i, "//", 2)) if (k == 0x3f3f3f3f) k = i;
    if (!_strcmp(dmlt + i, "/*", 2))
      cmt = 1;
    if (!_strcmp(dmlt + i, "*/", 2))
      cmt = 0;
    if (!_strcmp(dmlt + i, "</code>", 7) && !cmt) {
      k = (k < i ? k : i);
      memcpy(sct + sctL, dmlt + j, k - j);
      stmts[stmtN++].text = sct + sctL;
      sctL += k - j + 1;
    }
  }
}

char *strgen(char *s, int len) {
  while (s[len - 1] == ' ') len--;
  s[len] = '\0';
  return s;
}

char *strclone(char *s) {
  int len = strlen(s);
  memcpy(smem + smemL, s, len);
  smemL += len;
  smem[smemL] = '\0';
  smemL += 8;
  return smem + smemL - len - 8;
}

int typesize(char *tp) {
  //printf("typesize=%s\n", tp);
  int i = 0;
  if (tp[strlen(tp) - 1] == ']') {
    for (i = 0; i < strlen(tp); i++)
      if (tp[i] == '[') {
        tp[i] = '\0';
        int sz = typesize(tp);
        tp[i] = '[';
        sscanf(tp + i + 1, "%d", &i);
        return sz * i;
      }
    return -1;
  }
  if (tp[strlen(tp) - 1] == '*') {
    tp[strlen(tp) - 1] = '\0';
    if (typesize(tp) == -1) {
      tp[strlen(tp)] = '*';
      return -1;
    }
    tp[strlen(tp)] = '*';
    return 4;
  }
  if (!strcmp(tp, "float")) return 4;
  if (!strcmp(tp, "double")) return 8;
  if (!strcmp(tp, "int")) return 4;
  if (!strcmp(tp, "uint")) return 4;
  if (!strcmp(tp, "short")) return 2;
  if (!strcmp(tp, "ushort")) return 2;
  if (!strcmp(tp, "char")) return 1;
  if (!strcmp(tp, "uchar")) return 1;
  if (!strcmp(tp, "void")) return 0;
  for (i = 0; i < stuN; i++)
    if (!strcmp(tp, stus[i].name)) {
      return stus[i].size;
    }
  return -1;
}

void stmt_entry_match(uint *insts, int instN) {
  int i = 0;
  for (; i < stmtN; i++) {
    //printf("i=%d stmt=%s start=%d inst=%s\n",
    //	i, stmts[i].text, stmts[i].start, ops+(insts[stmts[i].start]&255)*5);
    while ((insts[stmts[i].start >> 2] & 255) == LEV && stmts[i].start < stmts[i].end - 1) {
      if (i > 0) stmts[i - 1].end++;
      stmts[i].start++;
    }
  }
}

void func_entry_match(uint *insts, int instN, char *codes, int codeL) {
  //codes[codeL] = '\0';
  int i = 0, j = 0, k = 0, i1 = 0, i2 = 0, i3 = 0;
  int o_stu = 0, stu = 0;
  char *fn = NULL;
  int fns = -1;
  char p_code;
  int o_line = 0, line = 0;
  int array_sz = 0;
  for (i = 0; i < instN; i++) {
    if ((insts[i] & 255) == JSR)
      labelJ[i + (((int) (insts[i])) >> 10) + 1] = 1;
    if (!i || (insts[i - 1] & 255) == LEV)
      labelJ[i] = 1;
  }
  int jsr_ct = 0;
  for (i = 0; i < codeL; o_stu = stu, i++) {
    switch (codes[i]) {
      case '(' :
      case '{' :
        stu += 0x0001;
        break;
      case ')' :
      case '}' :
        stu -= 0x0001;
        break;
      case '\"' :
        stu ^= 0x0100;
        break;
      case '\'' :
        stu ^= 0x0100;
        break;
      case '\0' :
        line++;
        break;
      default :
        break;
    }
    //printf("%c", codes[i] == '\0' ? '\n' : codes[i]);
    //printf("o_stu=%08x stu=%08x codes[%d]=%c line=%d\n", o_stu, stu, i, codes[i], line);
    if ((o_stu & 255) == 0 && (stu & 255) == 1) {
      //printf("o_stu=%08x stu=%08x codes[%d]=%c line=%d\n", o_stu, stu, i, codes[i], line);
      if (codes[i] == '(') {
        func[funcN].regN = 0;
        stu |= 0x0200;
        for (fn = codes + i - 1; *fn == ' '; fn--);                    // get func name
        for (fns = 0; isalpha(*fn) || isdigit(*fn); fn--, fns++);
        fn++;
      } else {
        if (codes[i] == '{') {
          ret_ct = 0;
          if (stu & 0x0200) stu |= 0x0400;
          else stu |= 0x0800;
        }
        if (stu & 0x0800) {
          for (fn = codes + i - 1; *fn == ' '; fn--);
          for (fns = 0; isalpha(*fn) || isdigit(*fn) || *fn == '_'; fn--, fns++);
          if (fns == 0) continue;
          (++fn)[fns] = '\0';
          if (!strcmp(fn, "enum")) {
            stu |= 0x1000;
            stu &= ~(0x0800);
            continue;
          }
          stus[stuN].name = fn;
          stus[stuN].size = 0;
          stus[stuN++].numN = 0;
        }
        stu &= (~0x1000);
        stu &= (~0x0200);
      }
    }
    if ((o_stu & 255) == 1 && (stu & 255) == 0) {
      //printf("o_stu=%08x stu=%08x codes[%d]=%c\n", o_stu, stu, i, codes[i]);
      if ((stu & 0x0200) && codes[i] == ')') {
        o_line = line;
        for (i1 = i - 1; codes[i1] == ' '; i1--);
        if (codes[i1] == '(') continue;
        for (; codes[i1] == '*' || isalpha(codes[i1]) || isdigit(codes[i1]) || codes[i1] == '_'; i1--);
        for (i2 = i1; codes[i2] == ' '; i2--);
        for (; codes[i2] == '*' || isalpha(codes[i2]) || isdigit(codes[i2]) || codes[i2] == '_'; i2--);
        func[funcN].regs[func[funcN].regN].name = strgen(codes + i1 + 1, i - i1 - 1);
        func[funcN].regs[func[funcN].regN++].type = (i1 == ++i2 || codes[i1] != ' ' ? "int" : strgen(codes + i2,
                                                                                                     i1 - i2));
        while (func[funcN].regs[func[funcN].regN - 1].name[0] == '*') {
          func[funcN].regs[func[funcN].regN - 1].name++;
          codes[i1] = '*';
          codes[++i1] = '\0';
        }
      }
      if ((stu & 0x0400) && codes[i] == '}') {
        func[funcN].nameS = strgen(fn, fns);
        func[funcN].nameL = fns;
        func[funcN].line = o_line;
        func[funcN].end = line;
        func[funcN].regL = func[funcN].regN << 3;
        func[funcN].locL = 0;
        for (k = 0; k < func[funcN].locN; k++)
          func[funcN].locL += typesize(func[funcN].locs[k].type);
        while (!labelJ[j]) j++;
        func[funcN++].addr = (j++) << 2;
        while (ret_ct--) {
          while (!labelJ[j]) j++;
          j++;
        }
        printf("%d %s 0x%08x line=%d\n", funcN - 1, func[funcN - 1].nameS, func[funcN - 1].addr, func[funcN - 1].line);
        stu &= (~0x0400);
      }
      if ((stu & 0x0800) && codes[i] == '}')
        stu &= (~0x0800);
    }
    if (o_stu == stu && (stu & 255) == 0) {
      if (codes[i] == '=' || codes[i] == ',' || codes[i] == ';') {
        for (i1 = i - 1; codes[i1] == ' '; i1--);
        array_sz = 1;
        if (codes[i1] == ']') {
          codes[i1] = '\0';
          for (i1--; codes[i1] != '['; i1--);
          codes[i1] = '\0';
          for (k = 0; k < ctN; k++)
            if (!strcmp(codes + i1 + 1, cts[k].name))
              array_sz = cts[k].value;
          if (array_sz == 1)
            sscanf(codes + i1 + 1, "%d", &array_sz);
          for (i1--; codes[i1] == ' '; i1--);
        }
        for (; codes[i1] == '*' || isalpha(codes[i1]) || isdigit(codes[i1]) || codes[i1] == '_'; i1--);
        for (i2 = i1; codes[i2] == ' '; i2--);
        for (; codes[i2] == '*' || isalpha(codes[i2]) || isdigit(codes[i2]) || codes[i2] == '_'; i2--);
        for (i3 = i2; codes[i3] == ' '; i3--);
        for (; isalpha(codes[i3]); i3--);
        if (!_strcmp(codes + i3 + 1, "unsigned", 8)) {
          for (; codes[i3] == ' '; i3--);
          for (; isalpha(codes[i3]); i3--);
        }
        if (!_strcmp(codes + i3 + 1, "typedef", 7))
          continue;
        //printf("o_stu=%08x stu=%08x codes[%d]=%c ", o_stu, stu, i, codes[i]);
        //printf("i1=%d i2=%d i3=%d\n", i1, i2, i3);
        //printf("code[i3]=%c%c%c%c%c%c%c%c\n",
        //	codes[i3+1], codes[i3+2],codes[i3+3],codes[i3+4],codes[i3+5],codes[i3+6],codes[i3+7],codes[i3+8]);
        if (i1 == ++i2) continue;
        p_code = codes[i];
        glbs[glbN].name = strgen(codes + i1 + 1, i - i1 - 1);
        glbs[glbN].type = strgen(codes + i2, i1 - i2);
        while (glbs[glbN].name[0] == '*') {
          glbs[glbN].name++;
          codes[i1] = '*';
          codes[++i1] = '\0';
        }
        if (!isalpha(glbs[glbN].name[0])) continue;
        if (typesize(glbs[glbN].type) == -1) continue;
        if (array_sz > 1) {
          glbs[glbN].type = strclone(glbs[glbN].type);
          sprintf(glbs[glbN].type + strlen(glbs[glbN].type), "[%d]", array_sz);
          glbN++;
          continue;
        }
        //printf("global name=%s type=%s\n", glbs[glbN].name, glbs[glbN].type);
        int o_glbN = ++glbN;
        while (p_code == ',' || p_code == '=') {
          for (i++; (stu & 0x01ff) != 0x0000 || (codes[i] != '=' && codes[i] != ',' && codes[i] != ';'); i++) {
            switch (codes[i]) {
              case '(' :
              case '{' :
                stu += 0x0001;
                break;
              case ')' :
              case '}' :
                stu -= 0x0001;
                break;
              case '\"' :
                stu ^= 0x0100;
                break;
              case '\'' :
                stu ^= 0x0100;
                break;
              default :
                break;
            }
          }
          if (p_code == '=') {
            p_code = codes[i];
            continue;
          }
          for (i1 = i - 1; codes[i1] == ' '; i1--);
          for (; codes[i1] == '*' || isalpha(codes[i1]) || isdigit(codes[i1]) || codes[i1] == '_'; i1--);
          p_code = codes[i];
          if (i1 == i - 1) break;
          glbs[glbN].name = strgen(codes + i1 + 1, i - i1 - 1);
          glbs[glbN].type = strclone(glbs[glbN - 1].type);
          glbN++;
        }
        for (k = o_glbN; k < glbN; k++) {
          while (glbs[k].type[strlen(glbs[k].type) - 1] == '*')
            glbs[k].type[strlen(glbs[k].type) - 1] = '\0';
          while (glbs[k].name[0] == '*') {
            glbs[k].name++;
            *(glbs[k].type + strlen(glbs[k].type) + 1) = '\0';
            *(glbs[k].type + strlen(glbs[k].type)) = '*';
          }
          //printf("global name=%s type=%s\n", glbs[k].name, glbs[k].type);
        }
      }
    }
    if (o_stu == stu) if (!(stu & 0x0100) && !_strcmp(codes + i, "return", 6))
      ret_ct++;
    if (o_stu == stu && (stu & 255) == 1) {
      if ((stu & 0x0200) && codes[i] == ',') {
        for (i1 = i - 1; codes[i1] == ' '; i1--);
        for (; codes[i1] == '*' || isalpha(codes[i1]) || isdigit(codes[i1]) || codes[i1] == '_'; i1--);
        for (i2 = i1; codes[i2] == ' '; i2--);
        for (; codes[i2] == '*' || isalpha(codes[i2]) || isdigit(codes[i2]) || codes[i2] == '_'; i2--);
        func[funcN].regs[func[funcN].regN].name = strgen(codes + i1 + 1, i - i1 - 1);
        func[funcN].regs[func[funcN].regN++].type = (i1 == ++i2 || codes[i1] != ' ' ? "int" : strgen(codes + i2,
                                                                                                     i1 - i2));
        while (func[funcN].regs[func[funcN].regN - 1].name[0] == '*') {
          func[funcN].regs[func[funcN].regN - 1].name++;
          codes[i1] = '*';
          codes[++i1] = '\0';
        }
      }
      if ((stu & 0x0400) && (codes[i] == '=' || codes[i] == ',' || codes[i] == ';')) {
        for (i1 = i - 1; codes[i1] == ' '; i1--);
        array_sz = 1;
        if (codes[i1] == ']') {
          codes[i1] = '\0';
          for (i1--; codes[i1] != '['; i1--);
          codes[i1] = '\0';
          for (k = 0; k < ctN; k++)
            if (!strcmp(codes + i1 + 1, cts[k].name))
              array_sz = cts[k].value;
          if (array_sz == 1)
            sscanf(codes + i1 + 1, "%d", &array_sz);
          for (i1--; codes[i1] == ' '; i1--);
        }
        for (; codes[i1] == '*' || isalpha(codes[i1]) || isdigit(codes[i1]) || codes[i1] == '_'; i1--);
        for (i2 = i1; codes[i2] == ' '; i2--);
        for (; codes[i2] == '*' || isalpha(codes[i2]) || isdigit(codes[i2]) || codes[i2] == '_'; i2--);
        if (i1 == ++i2) continue;
        for (i3 = i2 - 1; codes[i3] == ' '; i3--);
        for (; isalpha(codes[i3]); i3--);    // check 'static'
        if (!_strcmp(codes + i3 + 1, "static", 6)) {
          glbs[glbN].name = strgen(codes + i1 + 1, (i++) - i1 - 1);
          glbs[glbN].type = strgen(codes + i2, i1 - i2);
          while (glbs[glbN].name[0] == '*') {
            glbs[glbN].name++;
            codes[i1] = '*';
            codes[++i1] = '\0';
          }
          if (typesize(glbs[glbN].type) != -1) glbN++;
          if (array_sz > 1) {
            glbs[glbN - 1].type = strclone(glbs[glbN - 1].type);
            sprintf(glbs[glbN - 1].type + strlen(glbs[glbN - 1].type), "[%d]", array_sz);
          }
          continue;
        }
        p_code = codes[i];
        func[funcN].locs[func[funcN].locN].name = strgen(codes + i1 + 1, i - i1 - 1);
        func[funcN].locs[func[funcN].locN].type = strgen(codes + i2, i1 - i2);
        while (func[funcN].locs[func[funcN].locN].name[0] == '*') {
          func[funcN].locs[func[funcN].locN].name++;
          codes[i1] = '*';
          codes[++i1] = '\0';
        }
        if (!isalpha(func[funcN].locs[func[funcN].locN].name[0])) continue;
        if (typesize(func[funcN].locs[func[funcN].locN].type) == -1) continue;
        if (array_sz > 1) {
          func[funcN].locs[func[funcN].locN].type = strclone(func[funcN].locs[func[funcN].locN].type);
          sprintf(func[funcN].locs[func[funcN].locN].type + strlen(func[funcN].locs[func[funcN].locN].type), "[%d]",
                  array_sz);
          func[funcN].locN++;
          continue;
        }
        int o_locN = ++func[funcN].locN;
        while (p_code == ',' || p_code == '=') {
          for (i++; (stu & 0x01ff) != 0x0001 || (codes[i] != '=' && codes[i] != ',' && codes[i] != ';'); i++) {
            switch (codes[i]) {
              case '(' :
              case '{' :
                stu += 0x0001;
                break;
              case ')' :
              case '}' :
                stu -= 0x0001;
                break;
              case '\"' :
                stu ^= 0x0100;
                break;
              case '\'' :
                stu ^= 0x0100;
                break;
              default :
                break;
            }
          }
          if (p_code == '=') {
            p_code = codes[i];
            continue;
          }
          for (i1 = i - 1; codes[i1] == ' '; i1--);
          for (; codes[i1] == '*' || isalpha(codes[i1]) || isdigit(codes[i1]) || codes[i1] == '_'; i1--);
          p_code = codes[i];
          if (i1 == i - 1) break;
          func[funcN].locs[func[funcN].locN].name = strgen(codes + i1 + 1, i - i1 - 1);
          func[funcN].locs[func[funcN].locN].type = strclone(func[funcN].locs[func[funcN].locN - 1].type);
          func[funcN].locN++;
        }
        for (k = o_locN; k < func[funcN].locN; k++) {
          while (func[funcN].locs[k].type[strlen(func[funcN].locs[k].type) - 1] == '*')
            func[funcN].locs[k].type[strlen(func[funcN].locs[k].type) - 1] = '\0';
          while (func[funcN].locs[k].name[0] == '*') {
            func[funcN].locs[k].name++;
            *(func[funcN].locs[k].type + strlen(func[funcN].locs[k].type) + 1) = '\0';
            *(func[funcN].locs[k].type + strlen(func[funcN].locs[k].type)) = '*';
          }
        }
      }
      if ((stu & 0x0800) && (codes[i] == ',' || codes[i] == ';')) {
        for (i1 = i - 1; codes[i1] == ' '; i1--);
        array_sz = 1;
        if (codes[i1] == ']') {
          codes[i1] = '\0';
          for (i1--; codes[i1] != '['; i1--);
          codes[i1] = '\0';
          for (k = 0; k < ctN; k++)
            if (!strcmp(codes + i1 + 1, cts[k].name))
              array_sz = cts[k].value;
          if (array_sz == 1)
            sscanf(codes + i1 + 1, "%d", &array_sz);
          for (i1--; codes[i1] == ' '; i1--);
        }
        for (; codes[i1] == '*' || isalpha(codes[i1]) || isdigit(codes[i1]) || codes[i1] == '_'; i1--);
        for (i2 = i1; codes[i2] == ' '; i2--);
        for (; codes[i2] == '*' || isalpha(codes[i2]) || isdigit(codes[i2]) || codes[i2] == '_'; i2--);
        if (i1 == ++i2 && codes[i2 - 1] != '\0') continue;
        p_code = codes[i];
        stus[stuN - 1].nums[stus[stuN - 1].numN].name = strgen(codes + i1 + 1, i - i1 - 1);
        stus[stuN - 1].nums[stus[stuN - 1].numN].type = (i1 == i2 ? stus[stuN - 1].nums[stus[stuN - 1].numN - 1].type
                                                                  : strgen(codes + i2, i1 - i2));
        while (stus[stuN - 1].nums[stus[stuN - 1].numN].name[0] == '*') {
          stus[stuN - 1].nums[stus[stuN - 1].numN].name++;
          codes[i1] = '*';
          codes[++i1] = '\0';
        }
        int sz = typesize(stus[stuN - 1].nums[stus[stuN - 1].numN].type);
        if (sz != -1) {
          if (array_sz > 1) {
            stus[stuN - 1].nums[stus[stuN - 1].numN].type = strclone(stus[stuN - 1].nums[stus[stuN - 1].numN].type);
            sprintf(stus[stuN - 1].nums[stus[stuN - 1].numN].type +
                    strlen(stus[stuN - 1].nums[stus[stuN - 1].numN].type), "[%d]", array_sz);
            stus[stuN - 1].numN++;
            stus[stuN - 1].size += sz * array_sz;
            continue;
          }
          stus[stuN - 1].size += sz;
          int o_numN = ++stus[stuN - 1].numN;
          while (p_code == ',') {
            for (i++; (stu & 0x01ff) != 0x0001 || (codes[i] != ',' && codes[i] != ';'); i++) {
              switch (codes[i]) {
                case '(' :
                case '{' :
                  stu += 0x0001;
                  break;
                case ')' :
                case '}' :
                  stu -= 0x0001;
                  break;
                case '\"' :
                  stu ^= 0x0100;
                  break;
                case '\'' :
                  stu ^= 0x0100;
                  break;
                default :
                  break;
              }
            }
            for (i1 = i - 1; codes[i1] == ' '; i1--);
            for (; codes[i1] == '*' || isalpha(codes[i1]) || isdigit(codes[i1]) || codes[i1] == '_'; i1--);
            p_code = codes[i];
            if (i1 == i - 1) break;
            stus[stuN - 1].nums[stus[stuN - 1].numN].name = strgen(codes + i1 + 1, i - i1 - 1);
            stus[stuN - 1].nums[stus[stuN - 1].numN].type = strclone(stus[stuN - 1].nums[stus[stuN - 1].numN - 1].type);
            stus[stuN - 1].numN++;
          }
          for (k = o_numN; k < stus[stuN - 1].numN; k++) {
            while (stus[stuN - 1].nums[k].type[strlen(stus[stuN - 1].nums[k].type) - 1] == '*')
              stus[stuN - 1].nums[k].type[strlen(stus[stuN - 1].nums[k].type) - 1] = '\0';
            while (stus[stuN - 1].nums[k].name[0] == '*') {
              stus[stuN - 1].nums[k].name++;
              *(stus[stuN - 1].nums[k].type + strlen(stus[stuN - 1].nums[k].type) + 1) = '\0';
              *(stus[stuN - 1].nums[k].type + strlen(stus[stuN - 1].nums[k].type)) = '*';
            }
            stus[stuN - 1].size += typesize(stus[stuN - 1].nums[k].type);
          }
        }
      }
      if ((stu & 0x1000) && (codes[i] == '=')) {
        for (i1 = i - 1; codes[i1] == ' '; i1--);
        for (; isalpha(codes[i1]) || isdigit(codes[i1]) || codes[i1] == '_'; i1--);
        for (i2 = i + 1; codes[i2] == ' '; i2++);
        cts[ctN].name = strgen(codes + i1 + 1, i - i1 - 1);
        sscanf(codes + i2, "%d", &cts[ctN++].value);
        //printf("const name=%s value=%d\n", cts[ctN-1].name, cts[ctN-1].value);
      }
    }
  }
//for show more info
//  for (i = 0; i < stuN; i++) {
//    printf("%s size=%d\n", stus[i].name, stus[i].size);
//    for (j = 0; j < stus[i].numN; j++)
//      printf("%s %s %s\n", stus[i].name, stus[i].nums[j].type, stus[i].nums[j].name);
//  }
}

void printi(int addr, int tpc) {
  uint *xpc = (uint * )(addr + tpc);
  int i = 0, j = 0;
  for (; i < funcN - 1; i++)
    if (func[i].addr <= (addr & 0x00ffffff) && (addr & 0x00ffffff) < func[i + 1].addr) break;
  for (; j < func[i].nameL; j++)
    printf("%c", func[i].nameS[j]);
  printf("+%d : ", (addr & 0x00ffffff) - func[i].addr);
  printf("[%8.8x] %s 0x%06x\n", addr, ops + ((*xpc) & 255) * 5, (*xpc) >> 8);
}

void printc(int start, int end, int pc) {
  int j = 0;
  for (; j < funcN - 1 && func[j].end < start; j++);
  int i = start;
  for (; i < end && i < stmtN; i++) {
    if (i == pc) printf(">> ");
    else printf("   ");
    for (; j < funcN - 1 && func[j].end < i; j++);
    printf("%8s+%3d : %s\n", func[j].nameS, i - func[j].line, stmts[i].text);
  }
}

uint rlook(uint v);

void printv(int u, var_t *var) {
  uint t = 0;
  int i = 0, j = 0;
  if (*(var->type + strlen(var->type) - 1) == ']') {
    printf("%s %s=0x%08x, ", var->type, var->name, u);
    return;
  }
  for (; i < stuN; i++)
    if (!strcmp(var->type, stus[i].name)) {
      printf("%s %s={", var->type, var->name);
      for (; j < stus[i].numN; j++) {
        printv(u, &stus[i].nums[j]);
        u += typesize(stus[i].nums[j].type);
      }
      printf("}, ");
      return;
    }
  if (!(t = tr[u >> 12]) && !(t = rlook(u))) {
    printf("\ninvalid address: 0x%08x.\n", u);
    return;
  }
  if (*(var->type + strlen(var->type) - 1) == '*')
    printf("%s %s=0x%08x, ", var->type, var->name, *(uint * )(u ^ (t & -4)));
  if (!strcmp(var->type, "float"))
    printf("%s %s=%.2f, ", var->type, var->name, *((float *) (u ^ (t & -4))));
  if (!strcmp(var->type, "double"))
    printf("%s %s=%.2f, ", var->type, var->name, (float) (*((double *) (u ^ (t & -8)))));
  if (!strcmp(var->type, "int"))
    printf("%s %s=%d, ", var->type, var->name, *((int *) (u ^ (t & -4))));
  if (!strcmp(var->type, "uint"))
    printf("%s %s=%d, ", var->type, var->name, *((uint * )(u ^ (t & -4))));
  if (!strcmp(var->type, "short"))
    printf("%s %s=%d, ", var->type, var->name, *((short *) (u ^ (t & -2))));
  if (!strcmp(var->type, "ushort"))
    printf("%s %s=%d, ", var->type, var->name, *((ushort * )(u ^ (t & -2))));
  if (!strcmp(var->type, "char"))
    printf("%s %s=%d, ", var->type, var->name, *((char *) (u ^ (t & -2))));
  if (!strcmp(var->type, "uchar"))
    printf("%s %s=%d, ", var->type, var->name, *((uchar * )(u ^ (t & -2))));
}

void backtrace() {
  int i = 0, j = 0;
  uint sp = 0;
  int cpos = 0;
  for (; i < btN; i++) {
    sp = bts[i].sp;
    printf(" ===> %s + %d (", func[bts[i].func].nameS, bts[i].stmt);
    for (j = 0; j < func[bts[i].func].regN; j++) {
      sp += 8;
      printv(sp, &func[bts[i].func].regs[j]);
    }
    printf(") <=====\n");
    cpos = func[bts[i].func].line + bts[i].stmt;
    printc(cpos < 2 ? 0 : cpos - 2, cpos + 2, -1);
  }
}

void create_watchpoint(char *expr, int id) {
  //printf("create_watchpoint expr=%s id=%d\n", expr, id);
  int i = 0, j = 0, stu = 0x0000, len = strlen(expr);
  char *b1, *b2;
  int opl, opr, opt = -1;
  int _opl, _opr, _opt;
  int ifun_id = -1;
  for (; i < len; i++) {
    switch (expr[i]) {
      case '(' :
        stu++;
        break;
      case ')' :
        stu--;
        break;
    }
    if (!stu) {
      switch (expr[i]) {
        case '<' :
        case '>' :
        case '!' :
        case '=' :
          _opt = (uint)(expr[i]);
          _opl = _opr = i;
          if (expr[i + 1] == '=')
            _opt |= 0x100, _opr = ++i;
          goto compare;
        case '&' :
        case '|' :
        case '^' :
          _opt = (uint)(expr[i]);
          _opl = _opr = i;
          if (expr[i + 1] == expr[i])
            _opr = ++i;
          goto compare;
        case '+' :
        case '-' :
        case '*' :
        case '/' :
          _opt = (uint)(expr[i]);
          _opl = _opr = i;
        compare :
          if (opt == -1 || oprank[_opt & 255] > oprank[opt & 255])
            opt = _opt, opl = _opl, opr = _opr;
          break;
        case ':' :
          ifun_id = i;
        default :
          break;
      }
    }
  }
  if (opt != -1) {
    wps[id].optype = opt;
    expr[opl] = '\0';
    create_watchpoint(expr, wpR--);
    if (wps[wpR + 1].optype == -3) {
      wps[id].optype = -3;
      return;
    }
    if (wps[wpR + 1].optype == -1) {
      wps[id].lvalue = wps[++wpR].lvalue;
      wps[id].ltype = 0;
    } else if (wps[wpR + 1].optype == -2) {
      wps[id].lvalue = (uint)(&wps[wpR + 1]);
      wps[id].ltype = 1;
    } else if (wps[wpR + 1].optype == -4) {
      wps[id].lvalue = (uint)(&wps[wpR + 1]);
      wps[id].ltype = 3;
    } else {
      wps[id].lvalue = (uint)(&wps[wpR + 1]);
      wps[id].ltype = 2;
    }
    if (opt == (uint)('!')) {
      wps[id].rvalue = UNDEFINED;
      return;
    }
    create_watchpoint(expr + opr + 1, wpR--);
    if (wps[wpR + 1].optype == -3) {
      wps[id].optype = -3;
      return;
    }
    if (wps[wpR + 1].optype == -1) {
      wps[id].rvalue = wps[++wpR].lvalue;
      wps[id].rtype = 0;
    } else if (wps[wpR + 1].optype == -2) {
      wps[id].rvalue = (uint)(&wps[wpR + 1]);
      wps[id].rtype = 1;
    } else if (wps[wpR + 1].optype == -4) {
      wps[id].rvalue = (uint)(&wps[wpR + 1]);
      wps[id].rtype = 3;
    } else {
      wps[id].rvalue = (uint)(&wps[wpR + 1]);
      wps[id].rtype = 2;
    }
    return;
  }
  if (expr[0] == '(' && expr[len - 1] == ')') {
    expr[len - 1] = '\0';
    create_watchpoint(expr + 1, id);
    return;
  }
  if (ifun_id != -1) {
    expr[ifun_id] = '\0';
    int loc_ct = 0;
    for (i = 0; i < funcN; i++)
      if (!strcmp(func[i].nameS, expr)) {
        for (j = 0; j < func[i].locN; j++) {
          loc_ct -= typesize(func[i].locs[j].type);
          if (!strcmp(func[i].locs[j].name, expr + ifun_id + 1)) {
            wps[id].optype = -4;
            wps[id].ltype = loc_ct;
            wps[id].lvalue = (uint)(&func[i].locs[j]);
            wps[id].rvalue = (uint)(&func[i]);
            return;
          }
        }
        loc_ct = 8;
        for (j = 0; j < func[i].regN; j++) {
          if (!strcmp(func[i].regs[j].name, expr + ifun_id + 1)) {
            wps[id].optype = -4;
            wps[id].ltype = loc_ct;
            wps[id].lvalue = (uint)(&func[i].regs[j]);
            wps[id].rvalue = (uint)(&func[i]);
            return;
          }
          loc_ct += 8;
        }
        wps[id].optype = -3;
        return;
      }
    wps[id].optype = -3;
    return;
  }
  if (isdigit(expr[0])) {
    wps[id].optype = -1;
    sscanf(expr, "%d", &wps[id].lvalue);
    return;
  }
  wps[id].optype = -2;
  int bss_ct = 0;
  for (i = 0; i < glbN; i++) {
    if (!strcmp(glbs[i].name, expr)) {
      wps[id].optype = -2;
      wps[id].lvalue = (uint)(&glbs[i]);
      wps[id].rvalue = td_sz + bss_ct;
      return;
    }
    bss_ct += typesize(glbs[i].type);
  }
  wps[id].optype = -3;
}

void print_watchpoint(wpoint_t *wp) {
  if (wp->optype == -2) {
    printf("%s", ((var_t *) (wp->lvalue))->name);
    return;
  }
  if (wp->optype == -4) {
    printf("%s:%s", ((func_t *) (wp->rvalue))->nameS, ((var_t *) (wp->lvalue))->name);
    return;
  }
  printf("(");
  switch (wp->ltype) {
    case 0 :
      printf("%d", wp->lvalue);
      break;
    case 1 :
      printf("%s", ((var_t *) (((wpoint_t *) (wp->lvalue))->lvalue))->name);
      break;
    case 2 :
      print_watchpoint((wpoint_t *) (wp->lvalue));
      break;
    case 3 :
      printf("%s:%s",
             ((func_t *) (((wpoint_t *) (wp->lvalue))->rvalue))->nameS,
             ((var_t *) (((wpoint_t *) (wp->lvalue))->lvalue))->name);
      break;
  }
  switch (wp->optype & 255) {
    case '<' :
    case '>' :
    case '=' :
      printf("%c", (char) (wp->optype & 255));
      if (wp->optype >> 8) printf("=");
      break;
    case '&' :
    case '|' :
    case '^' :
      printf("%c", (char) (wp->optype & 255));
      if (wp->optype >> 8) printf("%c", (char) (wp->optype & 255));
      break;
    default :
      printf("%c", (char) (wp->optype & 255));
      break;
  }
  if (wp->optype == (uint)('!')) {
    printf(")");
    return;
  }
  switch (wp->rtype) {
    case 0 :
      printf("%d", wp->rvalue);
      break;
    case 1 :
      printf("%s", ((var_t *) (((wpoint_t *) (wp->rvalue))->lvalue))->name);
      break;
    case 2 :
      print_watchpoint((wpoint_t *) (wp->rvalue));
      break;
    case 3 :
      printf("%s:%s",
             ((func_t *) (((wpoint_t *) (wp->rvalue))->rvalue))->nameS,
             ((var_t *) (((wpoint_t *) (wp->rvalue))->lvalue))->name);
      break;
  }
  printf(")");
}

int calc_watchpoint(wpoint_t *wp) {
  int lv = 0, rv = 0;
  uint t = 0, u = 0;
  char *type = NULL;
  switch (wp->ltype) {
    case 0 :
      lv = wp->lvalue;
      break;
    case 1 :
      u = ((wpoint_t *) (wp->lvalue))->rvalue;
      type = ((var_t *) (((wpoint_t *) (wp->lvalue))->lvalue))->type;
      goto calclv;
    case 2 :
      lv = calc_watchpoint((wpoint_t *) (wp->lvalue));
      break;
    case 3 :
      if (((wpoint_t *) (wp->lvalue))->rvalue == (uint)(&func[bts[btN - 1].func])) {
        u = bts[btN - 1].sp + ((wpoint_t *) (wp->lvalue))->ltype;
        type = ((var_t *) (((wpoint_t *) (wp->lvalue))->lvalue))->type;
        goto calclv;
      }
      lv = UNDEFINED;
      break;
    calclv :
      if (!(t = tr[u >> 12]) && !(t = rlook(u))) {
        printf("\ninvalid address: 0x%08x.\n", u);
        return 0;
      }
      if (*(type + strlen(type) - 1) == '*') lv = *((uint * )(u ^ (t & -4)));
      if (!strcmp(type, "int")) lv = *((int *) (u ^ (t & -4)));
      if (!strcmp(type, "uint")) lv = *((uint * )(u ^ (t & -4)));
      if (!strcmp(type, "short")) lv = *((short *) (u ^ (t & -2)));
      if (!strcmp(type, "ushort")) lv = *((ushort * )(u ^ (t & -2)));
      if (!strcmp(type, "char")) lv = *((char *) (u ^ (t & -2)));
      if (!strcmp(type, "uchar")) lv = *((uchar * )(u ^ (t & -2)));
      break;
  }
  if (wp->optype == (uint)('!')) {
    //printf(" %d => %d\n", wp->rvalue, lv);
    if (lv != UNDEFINED && wp->rvalue != UNDEFINED && lv != wp->rvalue) {
      print_watchpoint((wpoint_t *) (wp->lvalue));
      printf(" %d => %d\n", wp->rvalue, lv);
      wp->rvalue = lv;
      return 1;
    }
    wp->rvalue = lv;
    return 0;
  }
  switch (wp->rtype) {
    case 0 :
      rv = wp->rvalue;
      break;
    case 1 :
      u = ((wpoint_t *) (wp->rvalue))->rvalue;
      type = ((var_t *) (((wpoint_t *) (wp->rvalue))->lvalue))->type;
      goto calcrv;
    case 2 :
      rv = calc_watchpoint((wpoint_t *) (wp->rvalue));
      break;
    case 3 :
      if (((wpoint_t *) (wp->rvalue))->rvalue == (uint)(&func[bts[btN - 1].func])) {
        u = bts[btN - 1].sp + ((wpoint_t *) (wp->rvalue))->ltype;
        type = ((var_t *) (((wpoint_t *) (wp->rvalue))->lvalue))->type;
        goto calcrv;
      }
      rv = UNDEFINED;
      break;
    calcrv :
      if (!(t = tr[u >> 12]) && !(t = rlook(u))) {
        printf("\ninvalid address: 0x%08x.\n", u);
        return 0;
      }
      if (*(type + strlen(type) - 1) == '*') rv = *((uint * )(u ^ (t & -4)));
      if (!strcmp(type, "int")) rv = *((int *) (u ^ (t & -4)));
      if (!strcmp(type, "uint")) rv = *((uint * )(u ^ (t & -4)));
      if (!strcmp(type, "short")) rv = *((short *) (u ^ (t & -2)));
      if (!strcmp(type, "ushort")) rv = *((ushort * )(u ^ (t & -2)));
      if (!strcmp(type, "char")) rv = *((char *) (u ^ (t & -2)));
      if (!strcmp(type, "uchar")) rv = *((uchar * )(u ^ (t & -2)));
      break;
  }
  //printf("calcwp lv=%d rv=%d op=%d%c\n",
  //	lv, rv, wp->optype>>8, (char)(wp->optype&255));
  if (lv == UNDEFINED || rv == UNDEFINED)
    return UNDEFINED;
  switch (wp->optype & 255) {
    case '<' :
      return (wp->optype >> 8) ? (lv <= rv) : (lv < rv);
    case '>' :
      return (wp->optype >> 8) ? (lv >= rv) : (lv > rv);
    case '=' :
      return (lv == rv);
    case '!' :
      return (wp->optype >> 8) ? (!lv) : (lv != rv);
    case '+' :
      return (lv + rv);
    case '-' :
      return (lv - rv);
    case '*' :
      return (lv * rv);
    case '/' :
      return (lv / rv);
    case '&' :
      return (lv & rv);
    case '|' :
      return (lv | rv);
    case '^' :
      return (lv ^ rv);
    default :
      return 0;
  }
}

// ===================================================================================================================


void *new(int size) {
  void *p;
  if ((p = sbrk((size + 7) & -8)) == (void *) -1) {
    dprintf(2, "%s : fatal: unable to sbrk(%d)\n", cmd, size);
    exit(-1);
  }
  return (void *) (((int) p + 7) & -8);
}

void flush() {
  uint v;
//  static int xx; if (tpages >= xx) { xx = tpages; dprintf(2,"****** flush(%d)\n",tpages); }
//  if (verbose) printf("F(%d)",tpages);
  while (tpages) {
    v = tpage[--tpages];
    trk[v] = twk[v] = tru[v] = twu[v] = 0;
  }
}

uint setpage(uint v, uint p, uint writable, uint userable) {
  if (p >= memsz) {
    trap = FMEM;
    vadr = v;
    return 0;
  }
  p = ((v ^ (mem + p)) & -4096) + 1;
  if (!trk[v >>= 12]) {
    if (tpages >= TPAGES) flush();
    tpage[tpages++] = v;
  }
//  if (verbose) printf(".");
  trk[v] = p;
  twk[v] = writable ? p : 0;
  tru[v] = userable ? p : 0;
  twu[v] = (userable && writable) ? p : 0;
  return p;
}

uint rlook(uint v) {
  uint pde, *ppde, pte, *ppte, q, userable;
//  dprintf(2,"rlook(%08x)\n",v);
  if (!paging) return setpage(v, v, 1, 1);
  pde = *(ppde = (uint * )(pdir + (v >> 22 << 2))); // page directory entry
  if (pde & PTE_P) {
    if (!(pde & PTE_A)) *ppde = pde | PTE_A;
    if (pde >= memsz) {
      trap = FMEM;
      vadr = v;
      return 0;
    }
    pte = *(ppte = (uint * )(mem + (pde & -4096) + ((v >> 10) & 0xffc))); // page table entry
    if ((pte & PTE_P) && ((userable = (q = pte & pde) & PTE_U) || !user)) {
      if (!(pte & PTE_A)) *ppte = pte | PTE_A;
      return setpage(v, pte, (pte & PTE_D) && (q & PTE_W),
                     userable); // set writable after first write so dirty gets set
    }
  }
  trap = FRPAGE;
  vadr = v;
  return 0;
}

uint wlook(uint v) {
  uint pde, *ppde, pte, *ppte, q, userable;
//  dprintf(2,"wlook(%08x)\n",v);
  if (!paging) return setpage(v, v, 1, 1);
  pde = *(ppde = (uint * )(pdir + (v >> 22 << 2))); // page directory entry
  if (pde & PTE_P) {
    if (!(pde & PTE_A)) *ppde = pde | PTE_A;
    if (pde >= memsz) {
      trap = FMEM;
      vadr = v;
      return 0;
    }
    pte = *(ppte = (uint * )(mem + (pde & -4096) + ((v >> 10) & 0xffc)));  // page table entry
    if ((pte & PTE_P) && (((userable = (q = pte & pde) & PTE_U) || !user) && (q & PTE_W))) {
      if ((pte & (PTE_D | PTE_A)) != (PTE_D | PTE_A)) *ppte = pte | (PTE_D | PTE_A);
      return setpage(v, pte, q & PTE_W, userable);
    }
  }
  trap = FWPAGE;
  vadr = v;
  return 0;
}

static char dbg_getcmd(char *buf) {
  char c;
  char *pos = buf;

  printf("\ndbg => ");
  fflush(stdout);
  fflush(stdin);
  fflush(stderr);

  do {
    c = getchar();
    putchar(c);
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != EOF)
      *pos++ = c;
  } while (c != EOF && c != '\n' && c != '\r');
  *pos = '\0';
  return buf[0];
}

static char *DBG_HELP_STRING = "\n"
        "h:\tprint help commands.\n"
        "q:\tquit.\n"
        "c:\tcontinue.\n"
        "s:\tsingle step for one program statement.\n"
        "\tsi:single step for one instruction.\n"
        "i:\tdisplay infomation.\n"
        "\tir:\tdisplay register.\n"
        "\til:\tdisplay arguments / local variable.\n"
        "\tic:\tdisplay sourcecode corrosponds to current PC.\n"
        "x:\tdisplay memory, the input address is hex number (e.g x 10000)\n"
        "\tx /(d)i pc:\t display instruction emitter";

static char *DBG_REG_CONTEX = "\n"
        "ra:\t%x\n"
        "rb:\t%x\n"
        "rc:\t%x\n"
        "rd:\t%8.8x\t[cur sp]\n"
        "re:\t%8.8x\t[next pc]\n"
        "ff:\t%f\n"
        "fg:\t%f\n\n"
        "tsp:\t%8.8x\t[top sp]\n"
        "user:\t%x\t\t[user mode or not]\n"
        "iena:\t%x\t\t[interrupt flag]\n"
        "trap:\t%x\t\t[current trap]\n"
        "vmem:\t%x\t\t[virtual memory enabled or not]\n\n"
        "ipend:\t%8.8x\t[interrupted pending or not]\n\n";

void cpu(uint pc, uint sp) {
  uint a, b, c, ssp, usp, t, p, v, u, delta, cycle, xcycle, timer, timeout, fpc, tpc, xsp, tsp, fsp;
  double f, g;
  int ir, *xpc, kbchar;
  char ch;
  struct pollfd pfd;
  int i, j, k;
  int addr, end;
  int sz;
  char tp;
  static char rn[256];
  static char fname[256];
  static char rbuf[4096]; // XXX

  int cbp = -1, ipos, cpos;

// ===== init backtrace	==================
  for (i = 0; i < funcN; i++)
    if (!strcmp(func[i].nameS, "main")) break;
  ipos = pc;
  cpos = 0;
  while (cpos < stmtN && (stmts[cpos].start == stmts[cpos].end || ipos >= stmts[cpos].end)) cpos++;

  bts[0].func = i;
  bts[0].stmt = cpos - func[i].line;
  bts[0].sp = sp;
  btN = 1;

// =======================================

  a = b = c = timer = timeout = fpc = tsp = fsp = 0;
  cycle = delta = 4096;
  xcycle = delta * 4;
  kbchar = -1;
  xpc = 0;
  tpc = -pc;
  xsp = sp;
  goto fixpc;

  fixsp:
  if ((p = tw[(v = xsp - tsp) >> 12])) {
    tsp = (xsp = v ^ (p - 1)) - v;
    fsp = (4096 - (xsp & 4095)) << 8;
  }

  for (; ;) {
    if ((uint) xpc == fpc) {
      fixpc:
      //printf("b1\n");
      //printf("fixpc\n");
      if (!(p = tr[(v = (uint) xpc - tpc) >> 12]) && !(p = rlook(v))) {
        trap = FIPAGE;
        goto exception;
      }
      xcycle -= tpc;
      xcycle += (tpc = (uint)(xpc = (int *) (v ^ (p - 1))) - v);
      fpc = ((uint) xpc + 4096) & -4096;
      next:
      //printf("next\n");
      if ((uint) xpc > xcycle) {
        cycle += delta;
        xcycle += delta * 4;
        if (iena || !(ipend & FKEYBD)) { // XXX dont do this, use a small queue instead
          pfd.fd = 0;
          pfd.events = POLLIN;
          if (poll(&pfd, 1, 0) == 1 && read(0, &ch, 1) == 1) {
            kbchar = ch;
            if (kbchar == '~') {
              dprintf(2, "ungraceful exit. cycle = %u\n", cycle + (int) ((uint) xpc - xcycle) / 4);
              return;
            }
            if (kbchar == '`') {
              dbg = 1;
              goto runir;
            }
            if (iena) {
              trap = FKEYBD;
              iena = 0;
              goto interrupt;
            }
            ipend |= FKEYBD;
          }
        }
        if (timeout) {
          timer += delta;
          if (timer >= timeout) { // XXX  // any interrupt actually!
//          dprintf(2,"timeout! timer=%d, timeout=%d\n",timer,timeout);
            timer = 0;
            if (iena) {
              trap = FTIMER;
              iena = 0;
              goto interrupt;
            }
            ipend |= FTIMER;
          }
        }
      }
    }

    runir:
    // =====> new feature : backtrace maintenance (push when JSR(A) / pop when LEV)
    switch ((uchar) ir) {
      case JSR :
      case JSRA :
        ipos = ((uint)(xpc) - tpc) & 0x00ffffff;
        cpos = 0;
        while (cpos < stmtN && (stmts[cpos].start == stmts[cpos].end || ipos >= stmts[cpos].end)) cpos++;
        for (i = 0; i < funcN - 1 && func[i].end < cpos; i++);
        bts[btN].func = i;
        bts[btN].stmt = cpos - func[i].line;
        bts[btN++].sp = xsp - tsp;
        //printf("btN=%d func=%d stmt=%d sp=%d\n",
        //	btN, bts[btN-1].func, bts[btN-1].stmt, bts[btN-1].sp);
        break;
      case LEV :
        if (!--btN) {
          for (i = 0; i < funcN; i++)
            if (!strcmp(func[i].nameS, "mainc")) break;
          bts[0].func = i;
          bts[0].stmt = 0;
          bts[0].sp = xsp - tsp;
          btN = 1;
        }
        break;
    }
// <=====
    ir = *xpc++;
    //printi((uint)(xpc-1)-tpc, tpc);
    //printf("pc = %x\n", (uint)(xpc-1)-tpc);
    if (cbp == -2) {
      ipos = ((uint)(xpc - 1) - tpc) & 0x00ffffff;
      for (cpos = 0; cpos < stmtN; cpos++)
        if (stmts[cpos].start != stmts[cpos].end && ipos == stmts[cpos].start) {
          printf("break at : \n");
          i = (cpos > 10 ? cpos - 10 : 0);
          printc(i, cpos + 5, cpos);
          cbp = -1;
          dbg = 1;
          break;
        }
    }
    if ((uint)(xpc - 1) - tpc == cbp) {
      printf("break at : \n");
      ipos = (uint)(xpc - 1) - tpc;
      cpos = 0;
      while (cpos < stmtN && (stmts[cpos].start == stmts[cpos].end || ipos >= stmts[cpos].end)) cpos++;
      i = (cpos > 10 ? cpos - 10 : 0);
      printc(i, cpos + 5, cpos);
      cbp = -1;
      dbg = 1;
    }

    for (i = 0; i < bpn; i++)
      if ((uint)(xpc - 1) - tpc == bp[i]) {
        printf("break at breakpoint %d : 0x%08x\n", i, bp[i]);
        dbg = 1;
      }
    for (i = 0; i < wpN; i++)
      if ((j = calc_watchpoint(&wps[i])) && j != UNDEFINED) {
        printf("break at watchpoint %d : ", i);
        print_watchpoint(&wps[i]);
        printf("\n");
        dbg = 1;
      }
    if (dbg) {
      again:
      memset(fname, 0, sizeof(fname));
      switch (dbg_getcmd(dbgbuf)) {
        case 'c':
          dbg = 0;
          break;
        case 's':
          if (!strcmp(dbgbuf + 1, "c")) {
            ipos = (uint)(xpc - 1) - tpc;
            cpos = 0;
            while (cpos < stmtN && (stmts[cpos].start == stmts[cpos].end || ipos >= stmts[cpos].end)) cpos++;
            cbp = stmts[cpos].end;
            dbg = 0;
            break;
          }
          if (!strcmp(dbgbuf + 1, "i")) {
            printi((uint)(xpc - 1) - tpc, tpc);
            break;
          }
          ipos = (uint)(xpc - 1) - tpc;
          cpos = 0;
          while (cpos < stmtN && ipos >= stmts[cpos].end) cpos++;
          cbp = -2;
          dbg = 0;
          break;
        case 'q':
          exit(0);
          // ===============================================================================================
          // new feature
        case 'i':
          if (!strcmp(dbgbuf + 1, "r")) {
            printf(DBG_REG_CONTEX,
                   a, b, c, xsp - tsp, (uint)(xpc - 1) - tpc, f, g,
                   (user ? usp : ssp) - tsp, user, iena, trap, paging, ipend);
          }
          if (!strcmp(dbgbuf + 1, "l")) {
            for (i = 0; i < funcN - 1; i++)
              if (func[i].addr <= (uint)(xpc - 1) - tpc && (uint)(xpc - 1) - tpc < func[i + 1].addr)
                break;
            int loc_sz = func[i].locL, reg_sz = 0;
            if (func[i].addr == (uint)(xpc - 1) - tpc)            // perhaps ENT
              loc_sz = 0;
            printi((uint)(xpc - 1) - tpc, tpc);
            printf("===== args =====\n");
            for (j = 0; j < func[i].regN; j++) {
              printv(xsp - tsp + loc_sz + 8 + reg_sz, &func[i].regs[j]);
              reg_sz += 8;
            }
            printf("\n===== locs =====\n");
            for (j = 0; j < func[i].locN; j++) {
              printv(xsp - tsp + loc_sz, &func[i].locs[j]);
              loc_sz -= typesize(func[i].locs[j].type);
            }
            printf("\n");
          }
          if (!strcmp(dbgbuf + 1, "f")) {
            for (i = 0; i < funcN; i++)
              printf("%02d %-10s 0x%08x\n",
                     i, func[i].nameS, func[i].addr);
          }
          if (!strcmp(dbgbuf + 1, "g")) {
            int bss_ct = 0;
            for (i = 0; i < glbN; i++) {
              printv(td_sz + bss_ct, &glbs[i]);
              bss_ct += typesize(glbs[i].type);
            }
          }
          if (!strcmp(dbgbuf + 1, "c")) {
            ipos = ((uint)(xpc - 1) - tpc) & 0x00ffffff;
            cpos = 0;
            while (cpos < stmtN && (stmts[cpos].start == stmts[cpos].end || ipos >= stmts[cpos].end)) cpos++;
            i = (cpos > 10 ? cpos - 10 : 0);
            printc(i, cpos + 5, cpos);
          } else if (dbgbuf[1] == 'c') {
            addr = -1;
            end = -1;
            sscanf(dbgbuf + 2, "%d", &addr);
            if (addr == -1) {
              for (i = 2; dbgbuf[i] != '\0'; i++)
                if (dbgbuf[i] == '+') break;
              if (dbgbuf[i] == '\0') {
                sscanf(dbgbuf + 2, "%s", fname);
                for (i = 0; i < funcN; i++)
                  if (!strcmp(fname, func[i].nameS)) break;
                if (i < funcN) {
                  addr = func[i].line;
                  end = func[i].end;
                }
              } else {
                memcpy(fname, dbgbuf + 2, i - 2);
                sscanf(dbgbuf + i + 1, "%d", &addr);
                for (i = 0; i < funcN; i++)
                  if (!strcmp(fname, func[i].nameS)) break;
                if (i < funcN) {
                  addr += func[i].line;
                  end = func[i].end;
                }
              }
            }
            if (addr != -1) {
              ipos = (uint)(xpc - 1) - tpc;
              cpos = addr;
              if (end == -1) end = addr + 20;
              printc(addr, end, cpos);
            }
          }
          goto again;
        case 'x':
          if (dbgbuf[1] == '/') {
            sscanf(dbgbuf + 2, "%d%c%s", &sz, &tp, rn);
            addr = -1;
            if (!strcmp(rn, "pc")) addr = (uint)(xpc - 1) - tpc;
            if (!strcmp(rn, "sp")) addr = xsp - tsp - 8;
            if (addr == -1)
              sscanf(dbgbuf + 2, "%d%c%x", &sz, &tp, &addr);
            if (tp == 'i')
              for (i = 0; i < sz; i += 4)
                printi(addr + i, tpc);
            if (tp == 'x')
              for (i = 0; i < sz; i += 8) {
                if (!(t = tr[(addr + i) >> 12]) && !(t = rlook(addr + i)))
                  printf("\ninvalid address: 0x%08x.\n", addr + i);
                else
                  printf("0x%08x%c", *(uint * )((addr + i) ^ (t & -2)), ((i >> 6) & 1) == 1 ? '\n' : ' ');
              }
          } else if ((sscanf(dbgbuf + 1, "%x", &u) != 1)
                     || (!(t = tr[u >> 12]) && !(t = rlook(u))))
            printf("\ninvalid address: %s.\n", dbgbuf + 1);
          else
            printf("\n[%8.8x]: %2.2x\n", u, *((unsigned char *) (u ^ (t & -2))));
          goto again;
        case 'b':
          addr = -1;
          if (!_strcmp(dbgbuf + 1, "il", 2)) {
            for (i = 0; i < wpN; i++) {
              printf("watchpoint %d : ", i);
              print_watchpoint(&wps[i]);
              printf("\n");
            }
            goto again;
          }
          if (!_strcmp(dbgbuf + 1, "if", 2)) {
            create_watchpoint(dbgbuf + 3, wpN);
            if (wps[wpN].optype == -3)
              printf("error : illegal expression.\n");
            else
              wpN++;
            goto again;
          }
          if (dbgbuf[1] == 't') {
            backtrace((uint)(xpc - 1) - tpc, xsp, tsp);
            goto again;
          }
          if (dbgbuf[1] == 'l') {
            if (bpn == 0)
              printf("the breakpoints list is empty.\n");
            else
              for (i = 0; i < bpn; i++)
                printf("breakpoint %d : 0x%08x\n", i, bp[i]);
            goto again;
          }
          if (dbgbuf[1] == 'd') {
            if (!_strcmp(dbgbuf + 2, "ifall", 5)) {
              printf("the watchpoints list is empty.\n");
              bpn = 0;
            } else if (!_strcmp(dbgbuf + 2, "if", 2)) {
              sscanf(dbgbuf + 4, "%d", &i);
              for (; i < wpN; i++) wps[i] = wps[i + 1];
              if (--wpN == 0)
                printf("the watchpoints list is empty.\n");
              else {
                for (i = 0; i < wpN; i++) {
                  printf("watchpoint %d : ", i);
                  print_watchpoint(&wps[i]);
                  printf("\n");
                }
              }
            } else if (!_strcmp(dbgbuf + 2, "all", 3)) {
              printf("the breakpoints list is empty.\n");
              bpn = 0;
            } else {
              sscanf(dbgbuf + 2, "%d", &i);
              for (; i < bpn; i++) bp[i] = bp[i + 1];
              if (--bpn == 0)
                printf("the breakpoints list is empty.\n");
              else
                for (i = 0; i < bpn; i++)
                  printf("breakpoint %d : 0x%08x\n", i, bp[i]);
            }
            goto again;
          }
          sscanf(dbgbuf + 1, "%x", &addr);
          if (addr == -1) {
            for (i = 1; dbgbuf[i] != '\0'; i++)
              if (dbgbuf[i] == '+') break;
            if (dbgbuf[i] == '\0') {
              sscanf(dbgbuf + 1, "%s", fname);
              for (i = 0; i < funcN; i++)
                if (!strcmp(func[i].nameS, fname)) break;
              if (i < funcN) addr = func[i].addr;
            } else {
              memcpy(fname, dbgbuf + 1, i - 1);
              sscanf(dbgbuf + i + 1, "%d", &addr);
              for (i = 0; i < funcN; i++)
                if (!strcmp(func[i].nameS, fname)) break;
              if (i < funcN) addr = stmts[func[i].line + addr].start;
            }
          }
          if (addr != -1) {
            for (i = 0; i < bpn; i++)
              if (bp[i] == addr) {
                printf("breakpoint 0x%08x has been already existed.\n", addr);
                goto again;
              }
            printf("add breakpoint %d 0x%08x.\n", bpn, addr);
          }
          bp[bpn++] = addr;
          goto again;

          // ===============================================================================================
        case 'h':
        default:
          printf(DBG_HELP_STRING);
          goto again;
      }
    }

    ipos = ((uint)(xpc - 1) - tpc) & 0x00ffffff;
    cpos = func[bts[btN - 1].func].line + bts[btN - 1].stmt;
    while (cpos < stmtN && (stmts[cpos].start == stmts[cpos].end || ipos >= stmts[cpos].end)) cpos++;
    bts[btN - 1].stmt = cpos - func[bts[btN - 1].func].line;

    switch ((uchar) ir) {
      case HALT:
        if (user || verbose) dprintf(2, "halt(%d) cycle = %u\n", a, cycle + (int) ((uint) xpc - xcycle) / 4);
        return; // XXX should be supervisor!
      case IDLE:
        if (user) {
          trap = FPRIV;
          break;
        }
        if (!iena) {
          trap = FINST;
          break;
        } // XXX this will be fatal !!!
        for (; ;) {
          pfd.fd = 0;
          pfd.events = POLLIN;
          if (poll(&pfd, 1, 0) == 1 && read(0, &ch, 1) == 1) {
            kbchar = ch;
            if (kbchar == '~') {
              dprintf(2, "ungraceful exit. cycle = %u\n", cycle + (int) ((uint) xpc - xcycle) / 4);
              return;
            }
            if (kbchar == '`') {
              dbg = 1;
              goto runir;
            }
            trap = FKEYBD;
            iena = 0;
            goto interrupt;
          }
          cycle += delta;
          if (timeout) {
            timer += delta;
            if (timer >= timeout) { // XXX  // any interrupt actually!
//        dprintf(2,"IDLE timeout! timer=%d, timeout=%d\n",timer,timeout);
              timer = 0;
              trap = FTIMER;
              iena = 0;
              goto interrupt;
            }
          }
        }

        // memory -- designed to be restartable/continuable after exception/interrupt
      case MCPY: // while (c) { *a = *b; a++; b++; c--; }
        while (c) {
          if (!(t = tr[b >> 12]) && !(t = rlook(b))) goto exception;
          if (!(p = tw[a >> 12]) && !(p = wlook(a))) goto exception;
          if ((v = 4096 - (a & 4095)) > c) v = c;
          if ((u = 4096 - (b & 4095)) > v) u = v;
          memcpy((char *) (a ^ (p & -2)), (char *) (b ^ (t & -2)), u);
          a += u;
          b += u;
          c -= u;
//        if (!(++cycle % DELTA)) { pc -= 4; break; } XXX
        }
        continue;

      case MCMP: // for (;;) { if (!c) { a = 0; break; } if (*b != *a) { a = *b - *a; b += c; c = 0; break; } a++; b++; c--; }
        for (; ;) {
          if (!c) {
            a = 0;
            break;
          }
          if (!(t = tr[b >> 12]) && !(t = rlook(b))) goto exception;
          if (!(p = tr[a >> 12]) && !(p = rlook(a))) goto exception;
          if ((v = 4096 - (a & 4095)) > c) v = c;
          if ((u = 4096 - (b & 4095)) > v) u = v;
          if ((t = memcmp((char *) (a ^ (p & -2)), (char *) (b ^ (t & -2)), u))) {
            a = t;
            b += c;
            c = 0;
            break;
          }
          a += u;
          b += u;
          c -= u;
//        if (!(++cycle % DELTA)) { pc -= 4; break; } XXX
        }
        continue;

      case MCHR: // for (;;) { if (!c) { a = 0; break; } if (*a == b) { c = 0; break; } a++; c--; }
        for (; ;) {
          if (!c) {
            a = 0;
            break;
          }
          if (!(p = tr[a >> 12]) && !(p = rlook(a))) goto exception;
          if ((u = 4096 - (a & 4095)) > c) u = c;
          if ((t = (uint) memchr((char *) (v = a ^ (p & -2)), b, u))) {
            a += t - v;
            c = 0;
            break;
          }
          a += u;
          c -= u;
//        if (!(++cycle % DELTA)) { pc -= 4; break; } XXX
        }
        continue;

      case MSET: // while (c) { *a = b; a++; c--; }
        while (c) {
          if (!(p = tw[a >> 12]) && !(p = wlook(a))) goto exception;
          if ((u = 4096 - (a & 4095)) > c) u = c;
          memset((char *) (a ^ (p & -2)), b, u);
          a += u;
          c -= u;
//        if (!(++cycle % DELTA)) { pc -= 4; break; } XXX
        }
        continue;

        // math
      case POW:
        f = pow(f, g);
        continue;
      case ATN2:
        f = atan2(f, g);
        continue;
      case FABS:
        f = fabs(f);
        continue;
      case ATAN:
        f = atan(f);
        continue;
      case LOG:
        if (f) f = log(f);
        continue; // XXX others?
      case LOGT:
        if (f) f = log10(f);
        continue; // XXX
      case EXP:
        f = exp(f);
        continue;
      case FLOR:
        f = floor(f);
        continue;
      case CEIL:
        f = ceil(f);
        continue;
      case HYPO:
        f = hypot(f, g);
        continue;
      case SIN:
        f = sin(f);
        continue;
      case COS:
        f = cos(f);
        continue;
      case TAN:
        f = tan(f);
        continue;
      case ASIN:
        f = asin(f);
        continue;
      case ACOS:
        f = acos(f);
        continue;
      case SINH:
        f = sinh(f);
        continue;
      case COSH:
        f = cosh(f);
        continue;
      case TANH:
        f = tanh(f);
        continue;
      case SQRT:
        f = sqrt(f);
        continue;
      case FMOD:
        f = fmod(f, g);
        continue;

      case ENT:
        if (fsp && (fsp -= ir & -256) > 4096 << 8) fsp = 0;
        xsp += ir >> 8;
        if (fsp) continue;
        goto fixsp;
      case LEV:
        if (ir < fsp) {
          t = *(uint * )(xsp + (ir >> 8)) + tpc;
          fsp -= (ir + 0x800) & -256;
        } // XXX revisit this mess
        else {
          if (!(p = tr[(v = xsp - tsp + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
          t = *(uint * )((v ^ p) & -8) + tpc;
          fsp = 0;
        }
        xsp += (ir >> 8) + 8;
        xcycle += t - (uint) xpc;
        if ((uint)(xpc = (int *) t) - fpc < -4096) goto fixpc;
        goto next;

        // jump
      case JMP:
        xcycle += ir >> 8;
        if ((uint)(xpc += ir >> 10) - fpc < -4096) goto fixpc;
        goto next;
      case JMPI:
        if (!(p = tr[(v = (uint) xpc - tpc + (ir >> 8) + (a << 2)) >> 12]) && !(p = rlook(v))) break;
        xcycle += (t = *(uint * )((v ^ p) & -4));
        if ((uint)(xpc = (int *) ((uint) xpc + t)) - fpc < -4096) goto fixpc;
        goto next;
      case JSR:
        if (fsp & (4095 << 8)) {
          xsp -= 8;
          fsp += 8 << 8;
          *(uint *) xsp = (uint) xpc - tpc;
        }
        else {
          if (!(p = tw[(v = xsp - tsp - 8) >> 12]) && !(p = wlook(v))) break;
          *(uint * )((v ^ p) & -8) = (uint) xpc - tpc;
          fsp = 0;
          xsp -= 8;
        }
        xcycle += ir >> 8;
        if ((uint)(xpc += ir >> 10) - fpc < -4096) goto fixpc;
        goto next;
      case JSRA:
        if (fsp & (4095 << 8)) {
          xsp -= 8;
          fsp += 8 << 8;
          *(uint *) xsp = (uint) xpc - tpc;
        }
        else {
          if (!(p = tw[(v = xsp - tsp - 8) >> 12]) && !(p = wlook(v))) break;
          *(uint * )((v ^ p) & -8) = (uint) xpc - tpc;
          fsp = 0;
          xsp -= 8;
        }
        xcycle += a + tpc - (uint) xpc;
        if ((uint)(xpc = (int *) (a + tpc)) - fpc < -4096) goto fixpc;
        goto next;

        // stack
      case PSHA:
        if (fsp & (4095 << 8)) {
          xsp -= 8;
          fsp += 8 << 8;
          *(uint *) xsp = a;
          continue;
        }
        if (!(p = tw[(v = xsp - tsp - 8) >> 12]) && !(p = wlook(v))) break;
        *(uint * )((v ^ p) & -8) = a;
        xsp -= 8;
        fsp = 0;
        goto fixsp;
      case PSHB:
        if (fsp & (4095 << 8)) {
          xsp -= 8;
          fsp += 8 << 8;
          *(uint *) xsp = b;
          continue;
        }
        if (!(p = tw[(v = xsp - tsp - 8) >> 12]) && !(p = wlook(v))) break;
        *(uint * )((v ^ p) & -8) = b;
        xsp -= 8;
        fsp = 0;
        goto fixsp;
      case PSHC:
        if (fsp & (4095 << 8)) {
          xsp -= 8;
          fsp += 8 << 8;
          *(uint *) xsp = c;
          continue;
        }
        if (!(p = tw[(v = xsp - tsp - 8) >> 12]) && !(p = wlook(v))) break;
        *(uint * )((v ^ p) & -8) = c;
        xsp -= 8;
        fsp = 0;
        goto fixsp;
      case PSHF:
        if (fsp & (4095 << 8)) {
          xsp -= 8;
          fsp += 8 << 8;
          *(double *) xsp = f;
          continue;
        }
        if (!(p = tw[(v = xsp - tsp - 8) >> 12]) && !(p = wlook(v))) break;
        *(double *) ((v ^ p) & -8) = f;
        xsp -= 8;
        fsp = 0;
        goto fixsp;
      case PSHG:
        if (fsp & (4095 << 8)) {
          xsp -= 8;
          fsp += 8 << 8;
          *(double *) xsp = g;
          continue;
        }
        if (!(p = tw[(v = xsp - tsp - 8) >> 12]) && !(p = wlook(v))) break;
        *(double *) ((v ^ p) & -8) = g;
        xsp -= 8;
        fsp = 0;
        goto fixsp;
      case PSHI:
        if (fsp & (4095 << 8)) {
          xsp -= 8;
          fsp += 8 << 8;
          *(int *) xsp = ir >> 8;
          continue;
        }
        if (!(p = tw[(v = xsp - tsp - 8) >> 12]) && !(p = wlook(v))) break;
        *(int *) ((v ^ p) & -8) = ir >> 8;
        xsp -= 8;
        fsp = 0;
        goto fixsp;

      case POPA:
        if (fsp) {
          a = *(uint *) xsp;
          xsp += 8;
          fsp -= 8 << 8;
          continue;
        }
        if (!(p = tr[(v = xsp - tsp) >> 12]) && !(p = rlook(v))) break;
        a = *(uint * )((v ^ p) & -8);
        xsp += 8;
        goto fixsp;
      case POPB:
        if (fsp) {
          b = *(uint *) xsp;
          xsp += 8;
          fsp -= 8 << 8;
          continue;
        }
        if (!(p = tr[(v = xsp - tsp) >> 12]) && !(p = rlook(v))) break;
        b = *(uint * )((v ^ p) & -8);
        xsp += 8;
        goto fixsp;
      case POPC:
        if (fsp) {
          c = *(uint *) xsp;
          xsp += 8;
          fsp -= 8 << 8;
          continue;
        }
        if (!(p = tr[(v = xsp - tsp) >> 12]) && !(p = rlook(v))) break;
        c = *(uint * )((v ^ p) & -8);
        xsp += 8;
        goto fixsp;
      case POPF:
        if (fsp) {
          f = *(double *) xsp;
          xsp += 8;
          fsp -= 8 << 8;
          continue;
        }
        if (!(p = tr[(v = xsp - tsp) >> 12]) && !(p = rlook(v))) break;
        f = *(double *) ((v ^ p) & -8);
        xsp += 8;
        goto fixsp;
      case POPG:
        if (fsp) {
          g = *(double *) xsp;
          xsp += 8;
          fsp -= 8 << 8;
          continue;
        }
        if (!(p = tr[(v = xsp - tsp) >> 12]) && !(p = rlook(v))) break;
        g = *(double *) ((v ^ p) & -8);
        xsp += 8;
        goto fixsp;

        // load effective address
      case LEA:
        a = xsp - tsp + (ir >> 8);
        continue;
      case LEAG:
        a = (uint) xpc - tpc + (ir >> 8);
        continue;

        // load a local
      case LL:
        if (ir < fsp) {
          a = *(uint * )(xsp + (ir >> 8));
          continue;
        }
        if (!(p = tr[(v = xsp - tsp + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        a = *(uint * )((v ^ p) & -4);
        if (fsp || (v ^ (xsp - tsp)) & -4096) continue;
        goto fixsp;
      case LLS:
        if (ir < fsp) {
          a = *(short *) (xsp + (ir >> 8));
          continue;
        }
        if (!(p = tr[(v = xsp - tsp + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        a = *(short *) ((v ^ p) & -2);
        if (fsp || (v ^ (xsp - tsp)) & -4096) continue;
        goto fixsp;
      case LLH:
        if (ir < fsp) {
          a = *(ushort * )(xsp + (ir >> 8));
          continue;
        }
        if (!(p = tr[(v = xsp - tsp + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        a = *(ushort * )((v ^ p) & -2);
        if (fsp || (v ^ (xsp - tsp)) & -4096) continue;
        goto fixsp;
      case LLC:
        if (ir < fsp) {
          a = *(char *) (xsp + (ir >> 8));
          continue;
        }
        if (!(p = tr[(v = xsp - tsp + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        a = *(char *) (v ^ p & -2);
        if (fsp || (v ^ (xsp - tsp)) & -4096) continue;
        goto fixsp;
      case LLB:
        if (ir < fsp) {
          a = *(uchar * )(xsp + (ir >> 8));
          continue;
        }
        if (!(p = tr[(v = xsp - tsp + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        a = *(uchar * )(v ^ p & -2);
        if (fsp || (v ^ (xsp - tsp)) & -4096) continue;
        goto fixsp;
      case LLD:
        if (ir < fsp) {
          f = *(double *) (xsp + (ir >> 8));
          continue;
        }
        if (!(p = tr[(v = xsp - tsp + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        f = *(double *) ((v ^ p) & -8);
        if (fsp || (v ^ (xsp - tsp)) & -4096) continue;
        goto fixsp;
      case LLF:
        if (ir < fsp) {
          f = *(float *) (xsp + (ir >> 8));
          continue;
        }
        if (!(p = tr[(v = xsp - tsp + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        f = *(float *) ((v ^ p) & -4);
        if (fsp || (v ^ (xsp - tsp)) & -4096) continue;
        goto fixsp;

        // load a global
      case LG:
        if (!(p = tr[(v = (uint) xpc - tpc + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        a = *(uint * )((v ^ p) & -4);
        continue;
      case LGS:
        if (!(p = tr[(v = (uint) xpc - tpc + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        a = *(short *) ((v ^ p) & -2);
        continue;
      case LGH:
        if (!(p = tr[(v = (uint) xpc - tpc + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        a = *(ushort * )((v ^ p) & -2);
        continue;
      case LGC:
        if (!(p = tr[(v = (uint) xpc - tpc + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        a = *(char *) (v ^ p & -2);
        continue;
      case LGB:
        if (!(p = tr[(v = (uint) xpc - tpc + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        a = *(uchar * )(v ^ p & -2);
        continue;
      case LGD:
        if (!(p = tr[(v = (uint) xpc - tpc + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        f = *(double *) ((v ^ p) & -8);
        continue;
      case LGF:
        if (!(p = tr[(v = (uint) xpc - tpc + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        f = *(float *) ((v ^ p) & -4);
        continue;

        // load a indexed
      case LX:
        if (!(p = tr[(v = a + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        a = *(uint * )((v ^ p) & -4);
        continue;
      case LXS:
        if (!(p = tr[(v = a + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        a = *(short *) ((v ^ p) & -2);
        continue;
      case LXH:
        if (!(p = tr[(v = a + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        a = *(ushort * )((v ^ p) & -2);
        continue;
      case LXC:
        if (!(p = tr[(v = a + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        a = *(char *) (v ^ p & -2);
        continue;
      case LXB:
        if (!(p = tr[(v = a + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        a = *(uchar * )(v ^ p & -2);
        continue;
      case LXD:
        if (!(p = tr[(v = a + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        f = *(double *) ((v ^ p) & -8);
        continue;
      case LXF:
        if (!(p = tr[(v = a + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        f = *(float *) ((v ^ p) & -4);
        continue;

        // load a immediate
      case LI:
        a = ir >> 8;
        continue;
      case LHI:
        a = a << 24 | (uint) ir >> 8;
        continue;
      case LIF:
        f = (ir >> 8) / 256.0;
        continue;

        // load b local
      case LBL:
        if (ir < fsp) {
          b = *(uint * )(xsp + (ir >> 8));
          continue;
        }
        if (!(p = tr[(v = xsp - tsp + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        b = *(uint * )((v ^ p) & -4);
        if (fsp || (v ^ (xsp - tsp)) & -4096) continue;
        goto fixsp;
      case LBLS:
        if (ir < fsp) {
          b = *(short *) (xsp + (ir >> 8));
          continue;
        }
        if (!(p = tr[(v = xsp - tsp + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        b = *(short *) ((v ^ p) & -2);
        if (fsp || (v ^ (xsp - tsp)) & -4096) continue;
        goto fixsp;
      case LBLH:
        if (ir < fsp) {
          b = *(ushort * )(xsp + (ir >> 8));
          continue;
        }
        if (!(p = tr[(v = xsp - tsp + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        b = *(ushort * )((v ^ p) & -2);
        if (fsp || (v ^ (xsp - tsp)) & -4096) continue;
        goto fixsp;
      case LBLC:
        if (ir < fsp) {
          b = *(char *) (xsp + (ir >> 8));
          continue;
        }
        if (!(p = tr[(v = xsp - tsp + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        b = *(char *) (v ^ p & -2);
        if (fsp || (v ^ (xsp - tsp)) & -4096) continue;
        goto fixsp;
      case LBLB:
        if (ir < fsp) {
          b = *(uchar * )(xsp + (ir >> 8));
          continue;
        }
        if (!(p = tr[(v = xsp - tsp + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        b = *(uchar * )(v ^ p & -2);
        if (fsp || (v ^ (xsp - tsp)) & -4096) continue;
        goto fixsp;
      case LBLD:
        if (ir < fsp) {
          g = *(double *) (xsp + (ir >> 8));
          continue;
        }
        if (!(p = tr[(v = xsp - tsp + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        g = *(double *) ((v ^ p) & -8);
        if (fsp || (v ^ (xsp - tsp)) & -4096) continue;
        goto fixsp;
      case LBLF:
        if (ir < fsp) {
          g = *(float *) (xsp + (ir >> 8));
          continue;
        }
        if (!(p = tr[(v = xsp - tsp + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        g = *(float *) ((v ^ p) & -4);
        if (fsp || (v ^ (xsp - tsp)) & -4096) continue;
        goto fixsp;

        // load b global
      case LBG:
        if (!(p = tr[(v = (uint) xpc - tpc + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        b = *(uint * )((v ^ p) & -4);
        continue;
      case LBGS:
        if (!(p = tr[(v = (uint) xpc - tpc + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        b = *(short *) ((v ^ p) & -2);
        continue;
      case LBGH:
        if (!(p = tr[(v = (uint) xpc - tpc + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        b = *(ushort * )((v ^ p) & -2);
        continue;
      case LBGC:
        if (!(p = tr[(v = (uint) xpc - tpc + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        b = *(char *) (v ^ p & -2);
        continue;
      case LBGB:
        if (!(p = tr[(v = (uint) xpc - tpc + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        b = *(uchar * )(v ^ p & -2);
        continue;
      case LBGD:
        if (!(p = tr[(v = (uint) xpc - tpc + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        g = *(double *) ((v ^ p) & -8);
        continue;
      case LBGF:
        if (!(p = tr[(v = (uint) xpc - tpc + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        g = *(float *) ((v ^ p) & -4);
        continue;

        // load b indexed
      case LBX:
        if (!(p = tr[(v = b + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        b = *(uint * )((v ^ p) & -4);
        continue;
      case LBXS:
        if (!(p = tr[(v = b + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        b = *(short *) ((v ^ p) & -2);
        continue;
      case LBXH:
        if (!(p = tr[(v = b + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        b = *(ushort * )((v ^ p) & -2);
        continue;
      case LBXC:
        if (!(p = tr[(v = b + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        b = *(char *) (v ^ p & -2);
        continue;
      case LBXB:
        if (!(p = tr[(v = b + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        b = *(uchar * )(v ^ p & -2);
        continue;
      case LBXD:
        if (!(p = tr[(v = b + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        g = *(double *) ((v ^ p) & -8);
        continue;
      case LBXF:
        if (!(p = tr[(v = b + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        g = *(float *) ((v ^ p) & -4);
        continue;

        // load b immediate
      case LBI:
        b = ir >> 8;
        continue;
      case LBHI:
        b = b << 24 | (uint) ir >> 8;
        continue;
      case LBIF:
        g = (ir >> 8) / 256.0;
        continue;

        // misc transfer
      case LCL:
        if (ir < fsp) {
          c = *(uint * )(xsp + (ir >> 8));
          continue;
        }
        if (!(p = tr[(v = xsp - tsp + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        c = *(uint * )((v ^ p) & -4);
        if (fsp || (v ^ (xsp - tsp)) & -4096) continue;
        goto fixsp;

      case LBA:
        b = a;
        continue;  // XXX need LAB, LAC to improve k.c  // or maybe a = a * imm + b ?  or b = b * imm + a ?
      case LCA:
        c = a;
        continue;
      case LBAD:
        g = f;
        continue;

        // store a local
      case SL:
        if (ir < fsp) {
          *(uint * )(xsp + (ir >> 8)) = a;
          continue;
        }
        if (!(p = tw[(v = xsp - tsp + (ir >> 8)) >> 12]) && !(p = wlook(v))) break;
        *(uint * )((v ^ p) & -4) = a;
        if (fsp || (v ^ (xsp - tsp)) & -4096) continue;
        goto fixsp;
      case SLH:
        if (ir < fsp) {
          *(ushort * )(xsp + (ir >> 8)) = a;
          continue;
        }
        if (!(p = tw[(v = xsp - tsp + (ir >> 8)) >> 12]) && !(p = wlook(v))) break;
        *(ushort * )((v ^ p) & -2) = a;
        if (fsp || (v ^ (xsp - tsp)) & -4096) continue;
        goto fixsp;
      case SLB:
        if (ir < fsp) {
          *(uchar * )(xsp + (ir >> 8)) = a;
          continue;
        }
        if (!(p = tw[(v = xsp - tsp + (ir >> 8)) >> 12]) && !(p = wlook(v))) break;
        *(uchar * )(v ^ p & -2) = a;
        if (fsp || (v ^ (xsp - tsp)) & -4096) continue;
        goto fixsp;
      case SLD:
        if (ir < fsp) {
          *(double *) (xsp + (ir >> 8)) = f;
          continue;
        }
        if (!(p = tw[(v = xsp - tsp + (ir >> 8)) >> 12]) && !(p = wlook(v))) break;
        *(double *) ((v ^ p) & -8) = f;
        if (fsp || (v ^ (xsp - tsp)) & -4096) continue;
        goto fixsp;
      case SLF:
        if (ir < fsp) {
          *(float *) (xsp + (ir >> 8)) = f;
          continue;
        }
        if (!(p = tw[(v = xsp - tsp + (ir >> 8)) >> 12]) && !(p = wlook(v))) break;
        *(float *) ((v ^ p) & -4) = f;
        if (fsp || (v ^ (xsp - tsp)) & -4096) continue;
        goto fixsp;

        // store a global
      case SG:
        if (!(p = tw[(v = (uint) xpc - tpc + (ir >> 8)) >> 12]) && !(p = wlook(v))) break;
        *(uint * )((v ^ p) & -4) = a;
        continue;
      case SGH:
        if (!(p = tw[(v = (uint) xpc - tpc + (ir >> 8)) >> 12]) && !(p = wlook(v))) break;
        *(ushort * )((v ^ p) & -2) = a;
        continue;
      case SGB:
        if (!(p = tw[(v = (uint) xpc - tpc + (ir >> 8)) >> 12]) && !(p = wlook(v))) break;
        *(uchar * )(v ^ p & -2) = a;
        continue;
      case SGD:
        if (!(p = tw[(v = (uint) xpc - tpc + (ir >> 8)) >> 12]) && !(p = wlook(v))) break;
        *(double *) ((v ^ p) & -8) = f;
        continue;
      case SGF:
        if (!(p = tw[(v = (uint) xpc - tpc + (ir >> 8)) >> 12]) && !(p = wlook(v))) break;
        *(float *) ((v ^ p) & -4) = f;
        continue;

        // store a indexed
      case SX:
        if (!(p = tw[(v = b + (ir >> 8)) >> 12]) && !(p = wlook(v))) break;
        *(uint * )((v ^ p) & -4) = a;
        continue;
      case SXH:
        if (!(p = tw[(v = b + (ir >> 8)) >> 12]) && !(p = wlook(v))) break;
        *(ushort * )((v ^ p) & -2) = a;
        continue;
      case SXB:
        if (!(p = tw[(v = b + (ir >> 8)) >> 12]) && !(p = wlook(v))) break;
        *(uchar * )(v ^ p & -2) = a;
        continue;
      case SXD:
        if (!(p = tw[(v = b + (ir >> 8)) >> 12]) && !(p = wlook(v))) break;
        *(double *) ((v ^ p) & -8) = f;
        continue;
      case SXF:
        if (!(p = tw[(v = b + (ir >> 8)) >> 12]) && !(p = wlook(v))) break;
        *(float *) ((v ^ p) & -4) = f;
        continue;

        // arithmetic
      case ADDF:
        f += g;
        continue;
      case SUBF:
        f -= g;
        continue;
      case MULF:
        f *= g;
        continue;
      case DIVF:
        if (g == 0.0) {
          trap = FARITH;
          break;
        }
        f /= g;
        continue; // XXX

      case ADD:
        a += b;
        continue;
      case ADDI:
        a += ir >> 8;
        continue;
      case ADDL:
        if (ir < fsp) {
          a += *(uint * )(xsp + (ir >> 8));
          continue;
        }
        if (!(p = tr[(v = xsp - tsp + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        a += *(uint * )((v ^ p) & -4);
        if (fsp || (v ^ (xsp - tsp)) & -4096) continue;
        goto fixsp;

      case SUB:
        a -= b;
        continue;
      case SUBI:
        a -= ir >> 8;
        continue;
      case SUBL:
        if (ir < fsp) {
          a -= *(uint * )(xsp + (ir >> 8));
          continue;
        }
        if (!(p = tr[(v = xsp - tsp + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        a -= *(uint * )((v ^ p) & -4);
        if (fsp || (v ^ (xsp - tsp)) & -4096) continue;
        goto fixsp;

      case MUL:
        a = (int) a * (int) b;
        continue; // XXX MLU ???
      case MULI:
        a = (int) a * (ir >> 8);
        continue;
      case MULL:
        if (ir < fsp) {
          a = (int) a * *(int *) (xsp + (ir >> 8));
          continue;
        }
        if (!(p = tr[(v = xsp - tsp + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        a = (int) a * *(int *) ((v ^ p) & -4);
        if (fsp || (v ^ (xsp - tsp)) & -4096) continue;
        goto fixsp;

      case DIV:
        if (!b) {
          trap = FARITH;
          break;
        }
        a = (int) a / (int) b;
        continue;
      case DIVI:
        if (!(t = ir >> 8)) {
          trap = FARITH;
          break;
        }
        a = (int) a / (int) t;
        continue;
      case DIVL:
        if (ir < fsp) {
          if (!(t = *(uint * )(xsp + (ir >> 8)))) {
            trap = FARITH;
            break;
          }
          a = (int) a / (int) t;
          continue;
        }
        if (!(p = tr[(v = xsp - tsp + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        if (!(t = *(uint * )((v ^ p) & -4))) {
          trap = FARITH;
          break;
        }
        a = (int) a / (int) t;
        if (fsp || (v ^ (xsp - tsp)) & -4096) continue;
        goto fixsp;

      case DVU:
        if (!b) {
          trap = FARITH;
          break;
        }
        a /= b;
        continue;
      case DVUI:
        if (!(t = ir >> 8)) {
          trap = FARITH;
          break;
        }
        a /= t;
        continue;
      case DVUL:
        if (ir < fsp) {
          if (!(t = *(int *) (xsp + (ir >> 8)))) {
            trap = FARITH;
            break;
          }
          a /= t;
          continue;
        }
        if (!(p = tr[(v = xsp - tsp + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        if (!(t = *(uint * )((v ^ p) & -4))) {
          trap = FARITH;
          break;
        }
        a /= t;
        if (fsp || (v ^ (xsp - tsp)) & -4096) continue;
        goto fixsp;

      case MOD:
        a = (int) a % (int) b;
        continue;
      case MODI:
        a = (int) a % (ir >> 8);
        continue;
      case MODL:
        if (ir < fsp) {
          a = (int) a % *(int *) (xsp + (ir >> 8));
          continue;
        }
        if (!(p = tr[(v = xsp - tsp + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        a = (int) a % *(int *) ((v ^ p) & -4);
        if (fsp || (v ^ (xsp - tsp)) & -4096) continue;
        goto fixsp;

      case MDU:
        a %= b;
        continue;
      case MDUI:
        a %= (ir >> 8);
        continue;
      case MDUL:
        if (ir < fsp) {
          a %= *(uint * )(xsp + (ir >> 8));
          continue;
        }
        if (!(p = tr[(v = xsp - tsp + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        a %= *(uint * )((v ^ p) & -4);
        if (fsp || (v ^ (xsp - tsp)) & -4096) continue;
        goto fixsp;

      case AND:
        a &= b;
        continue;
      case ANDI:
        a &= ir >> 8;
        continue;
      case ANDL:
        if (ir < fsp) {
          a &= *(uint * )(xsp + (ir >> 8));
          continue;
        }
        if (!(p = tr[(v = xsp - tsp + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        a &= *(uint * )((v ^ p) & -4);
        if (fsp || (v ^ (xsp - tsp)) & -4096) continue;
        goto fixsp;

      case OR:
        a |= b;
        continue;
      case ORI:
        a |= ir >> 8;
        continue;
      case ORL:
        if (ir < fsp) {
          a |= *(uint * )(xsp + (ir >> 8));
          continue;
        }
        if (!(p = tr[(v = xsp - tsp + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        a |= *(uint * )((v ^ p) & -4);
        if (fsp || (v ^ (xsp - tsp)) & -4096) continue;
        goto fixsp;

      case XOR:
        a ^= b;
        continue;
      case XORI:
        a ^= ir >> 8;
        continue;
      case XORL:
        if (ir < fsp) {
          a ^= *(uint * )(xsp + (ir >> 8));
          continue;
        }
        if (!(p = tr[(v = xsp - tsp + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        a ^= *(uint * )((v ^ p) & -4);
        if ((fsp || (v ^ (xsp - tsp)) & -4096)) continue;
        goto fixsp;

      case SHL:
        a <<= b;
        continue;
      case SHLI:
        a <<= ir >> 8;
        continue;
      case SHLL:
        if (ir < fsp) {
          a <<= *(uint * )(xsp + (ir >> 8));
          continue;
        }
        if (!(p = tr[(v = xsp - tsp + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        a <<= *(uint * )((v ^ p) & -4);
        if (fsp || (v ^ (xsp - tsp)) & -4096) continue;
        goto fixsp;

      case SHR:
        a = (int) a >> (int) b;
        continue;
      case SHRI:
        a = (int) a >> (ir >> 8);
        continue;
      case SHRL:
        if (ir < fsp) {
          a = (int) a >> *(int *) (xsp + (ir >> 8));
          continue;
        }
        if (!(p = tr[(v = xsp - tsp + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        a = (int) a >> *(int *) ((v ^ p) & -4);
        if (fsp || (v ^ (xsp - tsp)) & -4096) continue;
        goto fixsp;

      case SRU:
        a >>= b;
        continue;
      case SRUI:
        a >>= ir >> 8;
        continue;
      case SRUL:
        if (ir < fsp) {
          a >>= *(uint * )(xsp + (ir >> 8));
          continue;
        }
        if (!(p = tr[(v = xsp - tsp + (ir >> 8)) >> 12]) && !(p = rlook(v))) break;
        a >>= *(uint * )((v ^ p) & -4);
        if (fsp || (v ^ (xsp - tsp)) & -4096) continue;
        goto fixsp;

        // logical
      case EQ:
        a = a == b;
        continue;
      case EQF:
        a = f == g;
        continue;
      case NE:
        a = a != b;
        continue;
      case NEF:
        a = f != g;
        continue;
      case LT:
        a = (int) a < (int) b;
        continue;
      case LTU:
        a = a < b;
        continue;
      case LTF:
        a = f < g;
        continue;
      case GE:
        a = (int) a >= (int) b;
        continue;
      case GEU:
        a = a >= b;
        continue;
      case GEF:
        a = f >= g;
        continue;

        // branch
      case BZ:
        if (!a) {
          xcycle += ir >> 8;
          if ((uint)(xpc += ir >> 10) - fpc < -4096) goto fixpc;
          goto next;
        }
        continue;
      case BZF:
        if (!f) {
          xcycle += ir >> 8;
          if ((uint)(xpc += ir >> 10) - fpc < -4096) goto fixpc;
          goto next;
        }
        continue;
      case BNZ:
        if (a) {
          xcycle += ir >> 8;
          if ((uint)(xpc += ir >> 10) - fpc < -4096) goto fixpc;
          goto next;
        }
        continue;
      case BNZF:
        if (f) {
          xcycle += ir >> 8;
          if ((uint)(xpc += ir >> 10) - fpc < -4096) goto fixpc;
          goto next;
        }
        continue;
      case BE:
        if (a == b) {
          xcycle += ir >> 8;
          if ((uint)(xpc += ir >> 10) - fpc < -4096) goto fixpc;
          goto next;
        }
        continue;
      case BEF:
        if (f == g) {
          xcycle += ir >> 8;
          if ((uint)(xpc += ir >> 10) - fpc < -4096) goto fixpc;
          goto next;
        }
        continue;
      case BNE:
        if (a != b) {
          xcycle += ir >> 8;
          if ((uint)(xpc += ir >> 10) - fpc < -4096) goto fixpc;
          goto next;
        }
        continue;
      case BNEF:
        if (f != g) {
          xcycle += ir >> 8;
          if ((uint)(xpc += ir >> 10) - fpc < -4096) goto fixpc;
          goto next;
        }
        continue;
      case BLT:
        if ((int) a < (int) b) {
          xcycle += ir >> 8;
          if ((uint)(xpc += ir >> 10) - fpc < -4096) goto fixpc;
          goto next;
        }
        continue;
      case BLTU:
        if (a < b) {
          xcycle += ir >> 8;
          if ((uint)(xpc += ir >> 10) - fpc < -4096) goto fixpc;
          goto next;
        }
        continue;
      case BLTF:
        if (f < g) {
          xcycle += ir >> 8;
          if ((uint)(xpc += ir >> 10) - fpc < -4096) goto fixpc;
          goto next;
        }
        continue;
      case BGE:
        if ((int) a >= (int) b) {
          xcycle += ir >> 8;
          if ((uint)(xpc += ir >> 10) - fpc < -4096) goto fixpc;
          goto next;
        }
        continue;
      case BGEU:
        if (a >= b) {
          xcycle += ir >> 8;
          if ((uint)(xpc += ir >> 10) - fpc < -4096) goto fixpc;
          goto next;
        }
        continue;
      case BGEF:
        if (f >= g) {
          xcycle += ir >> 8;
          if ((uint)(xpc += ir >> 10) - fpc < -4096) goto fixpc;
          goto next;
        }
        continue;

        // conversion
      case CID:
        f = (int) a;
        continue;
      case CUD:
        f = a;
        continue;
      case CDI:
        a = (int) f;
        continue;
      case CDU:
        a = f;
        continue;

        // misc
      case BIN:
        if (user) {
          trap = FPRIV;
          break;
        }
        a = kbchar;
        kbchar = -1;
        continue;  // XXX
      case BOUT:
        if (user) {
          trap = FPRIV;
          break;
        }
        if (a != 1) {
          dprintf(2, "bad write a=%d\n", a);
          return;
        }
        ch = b;
        a = write(a, &ch, 1);
        continue;
      case SSP:
        xsp = a;
        tsp = fsp = 0;
        goto fixsp;

      case NOP:
        continue;
      case CYC:
        a = cycle + (int) ((uint) xpc - xcycle) / 4;
        continue; // XXX protected?  XXX also need wall clock time instruction
      case MSIZ:
        if (user) {
          trap = FPRIV;
          break;
        }
        a = memsz;
        continue;

      case CLI:
        if (user) {
          trap = FPRIV;
          break;
        }
        a = iena;
        iena = 0;
        continue;
      case STI:
        if (user) {
          trap = FPRIV;
          break;
        }
        if (ipend) {
          trap = ipend & -ipend;
          ipend ^= trap;
          iena = 0;
          goto interrupt;
        }
        iena = 1;
        continue;

      case RTI:
        if (user) {
          trap = FPRIV;
          break;
        }
        xsp -= tsp;
        tsp = fsp = 0;
        if (!(p = tr[xsp >> 12]) && !(p = rlook(xsp))) {
          dprintf(2, "RTI kstack fault\n");
          goto fatal;
        }
        t = *(uint * )((xsp ^ p) & -8);
        xsp += 8;
        if (!(p = tr[xsp >> 12]) && !(p = rlook(xsp))) {
          dprintf(2, "RTI kstack fault\n");
          goto fatal;
        }
        xcycle += (pc = *(uint * )((xsp ^ p) & -8) + tpc) - (uint) xpc;
        xsp += 8;
        xpc = (int *) pc;
        if (t & USER) {
          ssp = xsp;
          xsp = usp;
          user = 1;
          tr = tru;
          tw = twu;
        }
        if (!iena) {
          if (ipend) {
            trap = ipend & -ipend;
            ipend ^= trap;
            goto interrupt;
          }
          iena = 1;
        }
        goto fixpc; // page may be invalid

      case IVEC:
        if (user) {
          trap = FPRIV;
          break;
        }
        ivec = a;
        continue;
      case PDIR:
        if (user) {
          trap = FPRIV;
          break;
        }
        if (a > memsz) {
          trap = FMEM;
          break;
        }
        pdir = (mem + a) & -4096;
        flush();
        fsp = 0;
        goto fixpc; // set page directory
      case SPAG:
        if (user) {
          trap = FPRIV;
          break;
        }
        if (a && !pdir) {
          trap = FMEM;
          break;
        }
        paging = a;
        flush();
        fsp = 0;
        goto fixpc; // enable paging

      case TIME:
        if (user) {
          trap = FPRIV;
          break;
        }
        if (ir >> 8) {
          dprintf(2, "timer%d=%u timeout=%u\n", ir >> 8, timer, timeout);
          continue;
        }    // XXX undocumented feature!
        timeout = a;
        continue; // XXX cancel pending interrupts if disabled?

        // XXX need some sort of user mode thread locking functions to support user mode semaphores, etc.  atomic test/set?

      case LVAD:
        if (user) {
          trap = FPRIV;
          break;
        }
        a = vadr;
        continue;

      case TRAP:
        trap = FSYS;
        break;

      case LUSP:
        if (user) {
          trap = FPRIV;
          break;
        }
        a = usp;
        continue;
      case SUSP:
        if (user) {
          trap = FPRIV;
          break;
        }
        usp = a;
        continue;

      default:
        trap = FINST;
        break;
    }
    exception:
    if (!iena) {
      dprintf(2, "exception in interrupt handler\n");
      goto fatal;
    }
    interrupt:
    xsp -= tsp;
    tsp = fsp = 0;
    if (user) {
      usp = xsp;
      xsp = ssp;
      user = 0;
      tr = trk;
      tw = twk;
      trap |= USER;
    }
    xsp -= 8;
    if (!(p = tw[xsp >> 12]) && !(p = wlook(xsp))) {
      dprintf(2, "kstack fault!\n");
      goto fatal;
    }
    *(uint * )((xsp ^ p) & -8) = (uint) xpc - tpc;
    xsp -= 8;
    if (!(p = tw[xsp >> 12]) && !(p = wlook(xsp))) {
      dprintf(2, "kstack fault\n");
      goto fatal;
    }
    *(uint * )((xsp ^ p) & -8) = trap;
    xcycle += ivec + tpc - (uint) xpc;
    xpc = (int *) (ivec + tpc);
    goto fixpc;
  }
  fatal:
  dprintf(2, "processor halted! cycle = %u pc = %08x ir = %08x sp = %08x a = %d b = %d c = %d trap = %u\n",
          cycle + (int) ((uint) xpc - xcycle) / 4, (uint) xpc - tpc, ir, xsp - tsp, a, b, c, trap);
}

void usage() {
  dprintf(2, "%s : usage: %s [-s sourcecode] [-g] [-v] [-m memsize] [-f filesys] file\n", cmd, cmd);
  exit(-1);
}

int main(int argc, char *argv[]) {
  initialize();
  int i, f;
  char *file, *fs;
  struct stat st;

  cmd = *argv++;
  if (argc < 2) usage();
  file = *argv;
  memsz = MEM_SZ;
  fs = 0;
  dbg = 0;
  verbose = 0;
  while (--argc && *file == '-') {
    switch (file[1]) {
      case 'g':
        dbg = 1;
        dml = *++argv;
        argc--;
        break;
      case 'v':
        verbose = 1;
        break;
      case 'm':
        memsz = atoi(*++argv) * (1024 * 1024);
        argc--;
        break;
      case 'f':
        fs = *++argv;
        argc--;
        break;
      default:
        usage();
    }
    file = *++argv;
  }

  if (dbg) dprintf(2, "in debuger mode\n");
  if (verbose) dprintf(2, "mem size = %u\n", memsz);
  mem = (((int) new(memsz + 4096)) + 4095) & -4096;

  if (fs) {
    if (verbose) dprintf(2, "%s : loading ram file system %s\n", cmd, fs);
    if ((f = open(fs, O_RDONLY)) < 0) {
      dprintf(2, "%s : couldn't open file system %s\n", cmd, fs);
      return -1;
    }
    if (fstat(f, &st)) {
      dprintf(2, "%s : couldn't stat file system %s\n", cmd, fs);
      return -1;
    }
    if ((i = read(f, (void *) (mem + memsz - FS_SZ), st.st_size)) != st.st_size) {
      dprintf(2, "%s : failed to read filesystem size %d returned %d\n", cmd, st.st_size, i);
      return -1;
    }
    close(f);
  }

  if ((f = open(file, O_RDONLY)) < 0) {
    dprintf(2, "%s : couldn't open %s\n", cmd, file);
    return -1;
  }
  if (fstat(f, &st)) {
    dprintf(2, "%s : couldn't stat file %s\n", cmd, file);
    return -1;
  }

  read(f, &hdr, sizeof(hdr));
  if (hdr.magic != 0xC0DEF00D) {
    dprintf(2, "%s : bad hdr.magic\n", cmd);
    return -1;
  }
  labelJ[hdr.entry >> 2] = 1;
  td_sz = st.st_size - sizeof(hdr);
  //printf("Text Data size = %d\n", td_sz);
  int isz = (st.st_size - sizeof(hdr)) >> 2;
  if (read(f, (void *) mem, st.st_size - sizeof(hdr)) != st.st_size - sizeof(hdr)) {
    dprintf(2, "%s : failed to read file %sn", cmd, file);
    return -1;
  }
  close(f);

  if (dml) {
    if ((f = open(dml, O_RDONLY)) < 0) {
      dprintf(2, "%s : couldn't open %s\n", cmd, dml);
      return -1;
    }
    if (fstat(f, &st)) {
      dprintf(2, "%s : couldn't stat %s\n", cmd, dml);
      return -1;
    }
    read(f, &dmlt, st.st_size);
    close(f);
    analysis_dml();
    memcpy(_sct, sct, sctL);
    func_entry_match((uint *) mem, isz, _sct, sctL);
    stmt_entry_match((uint *) mem, isz);
  }

//  if (verbose) dprintf(2,"entry = %u text = %u data = %u bss = %u\n", hdr.entry, hdr.text, hdr.data, hdr.bss);

  // setup virtual memory
  trk = (uint *) new(TB_SZ * sizeof(uint)); // kernel read table
  twk = (uint *) new(TB_SZ * sizeof(uint)); // kernel write table
  tru = (uint *) new(TB_SZ * sizeof(uint)); // user read table
  twu = (uint *) new(TB_SZ * sizeof(uint)); // user write table
  tr = trk;
  tw = twk;

  if (verbose) dprintf(2, "%s : emulating %s\n", cmd, file);
  cpu(hdr.entry, memsz - FS_SZ);
  return 0;
}
