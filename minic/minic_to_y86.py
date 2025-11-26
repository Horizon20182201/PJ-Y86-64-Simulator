#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Mini-C -> Y86-64 .yo compiler
# language subset described in the docs above.

import sys, re
from collections import namedtuple

# ---------- Lexer ----------
Token = namedtuple('Token', 'typ val pos')

KEYWORDS = {'int','return','if','else','while','main'}

TOKEN_SPEC = [
    ('NUMBER',  r'-?\d+'),
    ('ID',      r'[A-Za-z_][A-Za-z0-9_]*'),
    ('EQ',      r'=='),
    ('NE',      r'!='),
    ('LE',      r'<='),
    ('GE',      r'>='),
    ('LT',      r'<'),
    ('GT',      r'>'),
    ('PLUS',    r'\+'),
    ('MINUS',   r'-'),
    ('AMP',     r'&'),
    ('XOR',     r'\^'),
    ('ASSIGN',  r'='),
    ('LP',      r'\('),
    ('RP',      r'\)'),
    ('LB',      r'\{'),
    ('RB',      r'\}'),
    ('SEMI',    r';'),
    ('COMMA',   r','),
    ('SKIP',    r'[ \t\r\n]+'),
    ('MISMATCH',r'.'),
]
TOK_REGEX = '|'.join('(?P<%s>%s)'%(n,p) for n,p in TOKEN_SPEC)
def lex(s):
    for m in re.finditer(TOK_REGEX, s):
        typ = m.lastgroup; val = m.group()
        if typ == 'SKIP': continue
        if typ == 'ID' and val in KEYWORDS: typ = val.upper()
        if typ == 'MISMATCH':
            raise SyntaxError(f'Illegal char {val!r} at {m.start()}')
        yield Token(typ, val, m.start())
    yield Token('EOF','',len(s))

# ---------- AST ----------
class Node: pass
class Program(Node):
    def __init__(self, func): self.func = func
class Func(Node):
    def __init__(self, name, body, decls): self.name=name; self.body=body; self.decls=decls
class Block(Node):
    def __init__(self, stmts): self.stmts=stmts
class Decl(Node):
    def __init__(self, name, init): self.name=name; self.init=init
class Assign(Node):
    def __init__(self, name, expr): self.name=name; self.expr=expr
class If(Node):
    def __init__(self, cond, then, els): self.cond=cond; self.then=then; self.els=els
class While(Node):
    def __init__(self, cond, body): self.cond=cond; self.body=body
class Return(Node):
    def __init__(self, expr): self.expr=expr
# expr
class Num(Node):
    def __init__(self, v): self.v=int(v)
class Var(Node):
    def __init__(self, n): self.n=n
class Unary(Node):
    def __init__(self, op, e): self.op=op; self.e=e
class Bin(Node):
    def __init__(self, op, l, r): self.op=op; self.l=l; self.r=r

# ---------- Parser (recursive descent) ----------
class Parser:
    def __init__(self, tokens):
        self.toks = list(tokens); self.i=0
    def cur(self): return self.toks[self.i]
    def eat(self,typ):
        t=self.cur()
        if t.typ!=typ: raise SyntaxError(f'Expected {typ}, got {t.typ}')
        self.i+=1; return t
    def accept(self,typ):
        if self.cur().typ==typ:
            t=self.cur(); self.i+=1; return t
        return None

    def parse(self):
        # program := 'int' 'main' '(' ')' block
        self.eat('INT')
        name = self.eat('MAIN')
        self.eat('LP'); self.eat('RP')
        decls, body = self.parse_block_with_decls()
        return Program(Func('main', body, decls))

    def parse_block_with_decls(self):
        self.eat('LB')
        decls=[]
        while self.cur().typ=='INT':
            self.eat('INT')
            ident=self.eat('ID').val
            init=None
            if self.accept('ASSIGN'):
                init=self.parse_expr()
            self.eat('SEMI')
            decls.append(Decl(ident, init))
        stmts=[]
        while self.cur().typ!='RB':
            stmts.append(self.parse_stmt())
        self.eat('RB')
        return decls, Block(stmts)

    def parse_stmt(self):
        t=self.cur()
        if t.typ=='LB':
            decls, b = self.parse_block_with_decls()
            if decls: # 把块内声明翻成赋值（本编译器只支持函数级局部）
                raise SyntaxError('Only function-scope declarations supported for simplicity.')
            return b
        if t.typ=='IF':
            self.eat('IF'); self.eat('LP'); c=self.parse_expr(); self.eat('RP')
            th=self.parse_stmt(); el=None
            if self.accept('ELSE'): el=self.parse_stmt()
            return If(c, th, el)
        if t.typ=='WHILE':
            self.eat('WHILE'); self.eat('LP'); c=self.parse_expr(); self.eat('RP')
            b=self.parse_stmt(); return While(c,b)
        if t.typ=='RETURN':
            self.eat('RETURN'); e=self.parse_expr(); self.eat('SEMI'); return Return(e)
        # assignment
        if t.typ=='ID' and self.toks[self.i+1].typ=='ASSIGN':
            name=self.eat('ID').val; self.eat('ASSIGN'); e=self.parse_expr(); self.eat('SEMI')
            return Assign(name,e)
        raise SyntaxError(f'Bad statement at {t.pos}: {t.typ}')

    # expr precedence:  unary > (&,^) > (+,-) > (==,!=,<,<=,>,>=)
    def parse_expr(self): return self.parse_cmp()
    def parse_cmp(self):
        e=self.parse_add()
        while self.cur().typ in ('EQ','NE','LT','LE','GT','GE'):
            op=self.eat(self.cur().typ).typ
            r=self.parse_add()
            e=Bin(op,e,r)
        return e
    def parse_add(self):
        e=self.parse_term()
        while self.cur().typ in ('PLUS','MINUS'):
            op=self.eat(self.cur().typ).typ
            r=self.parse_term()
            e=Bin(op,e,r)
        return e
    def parse_term(self):
        e=self.parse_factor()
        while self.cur().typ in ('AMP','XOR'):
            op=self.eat(self.cur().typ).typ
            r=self.parse_factor()
            e=Bin(op,e,r)
        return e
    def parse_factor(self):
        t=self.cur()
        if t.typ=='NUMBER':
            self.eat('NUMBER'); return Num(t.val)
        if t.typ=='ID':
            self.eat('ID'); return Var(t.val)
        if t.typ=='LP':
            self.eat('LP'); e=self.parse_expr(); self.eat('RP'); return e
        if t.typ=='MINUS':
            self.eat('MINUS'); return Unary('NEG', self.parse_factor())
        raise SyntaxError(f'Bad expr at {t.pos}: {t.typ}')

# ---------- Y86 Assembler (two-pass) ----------
# register map
REG = {
 'rax':0,'rcx':1,'rdx':2,'rbx':3,'rsp':4,'rbp':5,'rsi':6,'rdi':7,
 'r8':8,'r9':9,'r10':10,'r11':11,'r12':12,'r13':13,'r14':14
}
def le64(x): return [(x>> (8*i)) & 0xFF for i in range(8)]
def i64(x): return x & 0xFFFFFFFFFFFFFFFF

class Ins:
    def __init__(self, op, **kw): self.op=op; self.kw=kw; self.addr=None; self.text=kw.get('text','')
    def size(self):
        o=self.op
        if o in ('halt','nop','ret'): return 1
        if o in ('rrmov','cmov','opq','push','pop'): return 2
        if o in ('irmov','rmmov','mrmov'): return 10
        if o in ('jxx','call'): return 9
        raise ValueError(o)
    def encode(self, labels):
        o=self.op; K=self.kw; B=[]
        if o=='halt': return [0x00]
        if o=='nop':  return [0x10]
        if o=='ret':  return [0x90]
        if o=='rrmov' or o=='cmov':
            ifun=K['ifun']; rA=K['rA']; rB=K['rB']
            B=[0x20|ifun, ((rA&0xF)<<4)|(rB&0xF)]
            return B
        if o=='irmov':
            rB=K['rB']; imm = i64(K['imm'])
            B=[0x30, (0xF<<4)|(rB&0xF)] + le64(imm); return B
        if o=='rmmov':
            rA=K['rA']; rB=K['rB']; D=i64(K['D'])
            B=[0x40, ((rA&0xF)<<4)|(rB&0xF)] + le64(D); return B
        if o=='mrmov':
            rA=K['rA']; rB=K['rB']; D=i64(K['D'])
            B=[0x50, ((rA&0xF)<<4)|(rB&0xF)] + le64(D); return B
        if o=='opq':
            ifun=K['ifun']; rA=K['rA']; rB=K['rB']
            B=[0x60|ifun, ((rA&0xF)<<4)|(rB&0xF)]; return B
        if o=='jxx':
            ifun=K['ifun']; dst = K['dst']
            addr = dst if isinstance(dst,int) else labels[dst]
            B=[0x70|ifun] + le64(i64(addr)); return B
        if o=='call':
            dst = K['dst']
            addr = dst if isinstance(dst,int) else labels[dst]
            B=[0x80] + le64(i64(addr)); return B
        if o=='push':
            rA=K['rA']; B=[0xA0, ((rA&0xF)<<4)|0xF]; return B
        if o=='pop':
            rA=K['rA']; B=[0xB0, ((rA&0xF)<<4)|0xF]; return B
        raise ValueError(o)

class Asm:
    def __init__(self): self.ins=[]; self.labels={}
    def label(self, name):
        i=Ins('nop', text=f'{name}: (label)')  # placeholder for address
        self.ins.append(i); self.labels[name]=None
    def emit(self, ins): self.ins.append(ins)
    def layout(self, base=0):
        addr=base
        for i in self.ins:
            if i.text.endswith('(label)'): # label placeholder
                self.labels[i.text.split(':')[0]] = addr
            else:
                i.addr=addr; addr += i.size()
        # second pass to set labels correctly (some labels may coincide with next ins)
        addr=base
        cur_label=None
        for i in self.ins:
            if i.text.endswith('(label)'):
                name = i.text.split(':')[0]
                self.labels[name]=addr
            else:
                if i.addr is None: i.addr=addr
                addr += i.size()
    def assemble(self, base=0):
        self.layout(base)
        out=[]
        for i in self.ins:
            if i.text.endswith('(label)'): continue
            bytes_ = i.encode(self.labels)
            txt = i.text or ''
            out.append((i.addr, bytes_, txt))
        return out

# helpers to create instructions with readable text
def I_irmov(imm, rB, text=None): return Ins('irmov', imm=imm, rB=rB, text=text or f'irmovq ${imm},%{REG_INV[rB]}')
def I_rrmov(rA,rB,ifun=0, text=None): return Ins('rrmov', rA=rA,rB=rB,ifun=ifun, text=text or ('rrmovq' if ifun==0 else f'cmov{IFUN_NAME[ifun]}') )
def I_rmmov(rA,rB,D,text=None): return Ins('rmmov', rA=rA,rB=rB,D=D, text=text or f'rmmovq %{REG_INV[rA]},{D}(%{REG_INV[rB]})')
def I_mrmov(rA,rB,D,text=None): return Ins('mrmov', rA=rA,rB=rB,D=D, text=text or f'mrmovq {D}(%{REG_INV[rB]}),%{REG_INV[rA]}')
def I_opq(ifun,rA,rB,text=None): return Ins('opq', ifun=ifun,rA=rA,rB=rB, text=text or f'{OPQ_NAME[ifun]} %{REG_INV[rA]},%{REG_INV[rB]}')
def I_jxx(ifun,dst,text=None):    return Ins('jxx', ifun=ifun,dst=dst, text=text or f'j{IFUN_NAME[ifun]} {dst}')
def I_call(dst,text=None): return Ins('call', dst=dst, text=text or f'call {dst}')
def I_ret(): return Ins('ret', text='ret')
def I_push(rA): return Ins('push', rA=rA, text=f'pushq %{REG_INV[rA]}')
def I_pop(rA):  return Ins('pop',  rA=rA, text=f'popq %{REG_INV[rA]}')
def I_halt(): return Ins('halt', text='halt')
def I_nop():  return Ins('nop', text='nop')

IFUN_NAME = {0:'',1:'le',2:'l',3:'e',4:'ne',5:'ge',6:'g'}
OPQ_NAME  = {0:'addq',1:'subq',2:'andq',3:'xorq'}
REG_INV = {v:k for k,v in REG.items()}

# ---------- Codegen ----------
class Codegen:
    def __init__(self, prog:Program):
        self.p = prog
        self.asm = Asm()
        self.var_off = {}   # name -> negative offset from rbp
        self.next_off = -8
        self.lbl_id = 0

    def L(self,pfx='L'):
        self.lbl_id += 1; return f'.{pfx}{self.lbl_id}'

    def compile(self):
        # runtime entry: set a safe stack, call main, halt
        self.asm.emit(I_irmov(0x1000, REG['rsp'], 'irmovq $4096,%rsp'))
        self.asm.emit(I_call('main', 'call main'))
        self.asm.emit(I_halt())

        # function 'main'
        self.asm.label('main')
        # prologue
        self.asm.emit(I_push(REG['rbp']))
        self.asm.emit(I_rrmov(REG['rsp'], REG['rbp'], 0, 'rrmovq %rsp,%rbp'))

        # allocate locals (known at compile-time)
        for d in self.p.func.decls:
            self.var_off[d.name] = self.next_off
            self.next_off -= 8
        frame_size = -self.next_off - 8  # total positive bytes to reserve
        if frame_size > 0:
            self.asm.emit(I_irmov(frame_size, REG['rcx'], f'irmovq ${frame_size},%rcx'))
            self.asm.emit(I_opq(1, REG['rcx'], REG['rsp'], 'subq %rcx,%rsp'))

        # init decls
        for d in self.p.func.decls:
            if d.init is not None:
                self.gen_expr(d.init)      # -> %rax
                self.store_local(d.name)   # rmmovq %rax, off(%rbp)

        # body
        self.gen_block(self.p.func.body)

        # implicit return 0 (if not returned)
        self.asm.emit(I_irmov(0, REG['rax'], 'irmovq $0,%rax'))
        self.func_epilogue()
        return self.asm

    def func_epilogue(self):
        # epilogue: mov %rbp->%rsp; pop %rbp; ret
        self.asm.emit(I_rrmov(REG['rbp'], REG['rsp'], 0, 'rrmovq %rbp,%rsp'))
        self.asm.emit(I_pop(REG['rbp']))
        self.asm.emit(I_ret())

    def store_local(self, name):
        off = self.var_off[name]
        self.asm.emit(I_rmmov(REG['rax'], REG['rbp'], off))

    def load_local(self, name):
        off = self.var_off[name]
        self.asm.emit(I_mrmov(REG['rax'], REG['rbp'], off))

    # Expressions: leave result in %rax. Use stack to save left operand; %rcx/%rdx scratch.
    def gen_expr(self, e:Node):
        if isinstance(e, Num):
            self.asm.emit(I_irmov(e.v, REG['rax'], f'irmovq ${e.v},%rax')); return
        if isinstance(e, Var):
            self.load_local(e.n); return
        if isinstance(e, Unary) and e.op=='NEG':
            # rax = 0 - rax
            self.gen_expr(e.e)
            self.asm.emit(I_irmov(0, REG['rcx'], 'irmovq $0,%rcx'))
            self.asm.emit(I_opq(1, REG['rax'], REG['rcx'], 'subq %rax,%rcx')) # rcx = 0 - rax
            self.asm.emit(I_rrmov(REG['rcx'], REG['rax'], 0, 'rrmovq %rcx,%rax'))
            return
        if isinstance(e, Bin):
            # left -> push; right -> %rax; pop -> %rcx
            self.gen_expr(e.l)
            self.asm.emit(I_push(REG['rax']))
            self.gen_expr(e.r)
            self.asm.emit(I_pop(REG['rcx']))  # rcx = left, rax = right
            op = e.op
            if op=='PLUS':
                self.asm.emit(I_opq(0, REG['rax'], REG['rcx'], 'addq %rax,%rcx'))
                self.asm.emit(I_rrmov(REG['rcx'], REG['rax'], 0, 'rrmovq %rcx,%rax'))
            elif op=='MINUS':
                self.asm.emit(I_opq(1, REG['rax'], REG['rcx'], 'subq %rax,%rcx')) # rcx-left minus rax-right
                self.asm.emit(I_rrmov(REG['rcx'], REG['rax'], 0, 'rrmovq %rcx,%rax'))
            elif op=='AMP':
                self.asm.emit(I_opq(2, REG['rax'], REG['rcx'], 'andq %rax,%rcx'))
                self.asm.emit(I_rrmov(REG['rcx'], REG['rax'], 0, 'rrmovq %rcx,%rax'))
            elif op=='XOR':
                self.asm.emit(I_opq(3, REG['rax'], REG['rcx'], 'xorq %rax,%rcx'))
                self.asm.emit(I_rrmov(REG['rcx'], REG['rax'], 0, 'rrmovq %rcx,%rax'))
            elif op in ('EQ','NE'):
                # xor -> ZF set if equal
                self.asm.emit(I_opq(3, REG['rax'], REG['rcx'], 'xorq %rax,%rcx'))  # rcx ^= rax
                self.bool_from_cc(3 if op=='EQ' else 4)  # e / ne
            elif op in ('LT','LE','GT','GE'):
                # want CC of (left - right) => do rcx - rax into rcx
                self.asm.emit(I_opq(1, REG['rax'], REG['rcx'], 'subq %rax,%rcx'))
                ifun = {'LT':2,'LE':1,'GT':6,'GE':5}[op]
                self.bool_from_cc(ifun)
            else:
                raise NotImplementedError(op)
            return
        raise NotImplementedError(f'expr {type(e)}')

    def bool_from_cc(self, ifun):
        # ZF/SF/OF already set by previous op.
        # rax = 0; rdx = 1; cmovXX rdx -> rax
        self.asm.emit(I_irmov(0, REG['rax'], 'irmovq $0,%rax'))
        self.asm.emit(I_irmov(1, REG['rdx'], 'irmovq $1,%rdx'))
        self.asm.emit(I_rrmov(REG['rdx'], REG['rax'], ifun, f'cmov{IFUN_NAME[ifun]} %rdx,%rax'))

    # statements
    def gen_block(self, b:Block):
        for s in b.stmts: self.gen_stmt(s)

    def gen_stmt(self, s:Node):
        if isinstance(s, Assign):
            self.gen_expr(s.expr); self.store_local(s.name); return
        if isinstance(s, Return):
            self.gen_expr(s.expr); self.func_epilogue(); return
        if isinstance(s, If):
            L_else = self.L('ELSE'); L_end = self.L('ENDIF')
            self.gen_expr(s.cond)
            # test rax == 0 -> je else
            self.asm.emit(I_opq(2, REG['rax'], REG['rax'], 'andq %rax,%rax'))  # set ZF/SF
            self.asm.emit(I_jxx(3, L_else, 'je ELSE'))
            self.gen_stmt(s.then)
            self.asm.emit(I_jxx(0, L_end, 'jmp ENDIF'))
            self.asm.label(L_else)
            if s.els: self.gen_stmt(s.els)
            self.asm.label(L_end)
            return
        if isinstance(s, While):
            L_top = self.L('LOOP'); L_end = self.L('ENDL')
            self.asm.label(L_top)
            self.gen_expr(s.cond)
            self.asm.emit(I_opq(2, REG['rax'], REG['rax'], 'andq %rax,%rax'))
            self.asm.emit(I_jxx(3, L_end, 'je ENDL'))
            self.gen_stmt(s.body)
            self.asm.emit(I_jxx(0, L_top, 'jmp LOOP'))
            self.asm.label(L_end)
            return
        if isinstance(s, Block):
            self.gen_block(s); return
        raise NotImplementedError(type(s))

# ---------- .yo writer ----------
def write_yo(records, out=sys.stdout):
    # records: list of (addr:int, bytes:list[int], asm:str)
    for addr, bs, txt in records:
        hx = ''.join(f'{b:02x}' for b in bs)
        out.write(f'0x{addr:03x}: {hx} |   {txt}\n')

# ---------- driver ----------
def main():
    source = sys.stdin.read() if len(sys.argv)<2 else open(sys.argv[1],'r',encoding='utf-8').read()
    parser = Parser(lex(source))
    prog = parser.parse()
    cg = Codegen(prog)
    asm = cg.compile()
    recs = asm.assemble(base=0)
    write_yo(recs)

if __name__ == '__main__':
    main()
