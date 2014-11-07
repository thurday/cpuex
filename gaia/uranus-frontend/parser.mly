%{
open Syntax
%}

/* tokens */
%token <bool>   BOOL
%token <int>    INT
%token <float>  FLOAT
%token <string> IDENT
%token LPAR RPAR
%token PLUS MINUS AST SLASH
%token PLUSDOT MINUSDOT ASTDOT SLASHDOT FABS
%token NOT EQUAL NOTEQ LESS LESSEQ GREATER GREATEREQ
%token IF THEN ELSE LET IN REC COMMA SEMI
%token DOT ASSIGN MAKEARRAY
%token READ WRITE ITOF FTOI FLOOR CASTINT CASTFLT
%token EOF

/* priority (from lower to higher) */
%nonassoc below_SEMI
%nonassoc SEMI
%nonassoc ELSE
%nonassoc ASSIGN
%nonassoc below_COMMA
%left     COMMA
%left     EQUAL NOTEQ LESS LESSEQ GREATER GREATEREQ
%left     PLUS MINUS PLUSDOT MINUSDOT
%left     AST SLASH ASTDOT SLASHDOT
%nonassoc unary_minus

/* start symbol */
%type <Syntax.ast> top
%start top

%%

top:
    seq_expr EOF            { $1 }

seq_expr:
    expr %prec below_SEMI   { $1 }
  | expr SEMI               { $1 }
  | expr SEMI seq_expr      { Seq ($1, $3) }

expr:
    simple_expr             { $1 }
  | NOT simple_expr         { Not $2 }
  | MINUS expr %prec unary_minus
      { match $2 with
          | Int i -> Int (-i)
          | Float f -> Float (-.f)
          | _ -> IUnOp (Neg, $2) }
  | expr PLUS expr      { IBinOp (Add, $1, $3) }
  | expr MINUS expr     { IBinOp (Sub, $1, $3) }
  | expr AST expr       { IBinOp (Mul, $1, $3) }
  | expr SLASH expr     { IBinOp (Div, $1, $3) }
  | MINUSDOT expr %prec unary_minus { FUnOp (FNeg, $2) }
  | FABS simple_expr    { FUnOp (FAbs, $2) }
  | expr PLUSDOT expr   { FBinOp (FAdd, $1, $3) }
  | expr MINUSDOT expr  { FBinOp (FSub, $1, $3) }
  | expr ASTDOT expr    { FBinOp (FMul, $1, $3) }
  | expr SLASHDOT expr  { FBinOp (FDiv, $1, $3) }
  | expr EQUAL expr     { Cmp (EQ, $1, $3) }
  | expr NOTEQ expr     { Cmp (NE, $1, $3) }
  | expr LESS expr      { Cmp (LT, $1, $3) }
  | expr LESSEQ expr    { Cmp (LE, $1, $3) }
  | expr GREATER expr   { Cmp (GT, $1, $3) }
  | expr GREATEREQ expr { Cmp (GE, $1, $3) }
  | simple_expr args    { App ($1, $2) }
  | elems %prec below_COMMA { Tuple $1 }
  | MAKEARRAY simple_expr simple_expr               { MakeAry ($2, $3) }
  | simple_expr DOT LPAR seq_expr RPAR ASSIGN expr  { Put ($1, $4, $7) }
  | IF expr THEN expr ELSE expr                     { If ($2, $4, $6) }
  | LET IDENT EQUAL seq_expr IN seq_expr            { Let ($2, $4, $6) }
  | LET REC IDENT params EQUAL seq_expr IN seq_expr { LetRec ($3, $4, $6, $8) }
  | LET pat EQUAL seq_expr IN seq_expr              { LetTpl ($2, $4, $6) }
  | READ                { Read }
  | WRITE simple_expr   { Write $2 }
  | ITOF simple_expr    { ItoF $2 }
  | FTOI simple_expr    { FtoI $2 }
  | FLOOR simple_expr   { Floor $2 }
  | CASTINT simple_expr { CastInt $2 }
  | CASTFLT simple_expr { CastFlt $2 }

simple_expr:
    LPAR seq_expr RPAR { $2 }
  | LPAR RPAR { Unit }
  | BOOL      { Bool $1 }
  | INT       { Int $1 }
  | FLOAT     { Float $1 }
  | IDENT     { Var $1 }
  | simple_expr DOT LPAR seq_expr RPAR { Get ($1, $4) }

args:
    args simple_expr  { $1 @ [$2] }
  | simple_expr       { [$1] }

params:
    IDENT params      { $1 :: $2 }
  | IDENT             { [$1] }

elems:
    elems COMMA expr  { $1 @ [$3] }
  | expr COMMA expr   { [$1; $3] }

pat:
    vars              { $1 }
  | LPAR vars RPAR    { $2 }

vars:
    vars COMMA IDENT  { $1 @ [$3] }
  | IDENT COMMA IDENT { [$1; $3] }
