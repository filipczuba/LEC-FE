%skeleton "lalr1.cc" /* -*- C++ -*- */
%require "3.2"
%defines

%define api.token.constructor
%define api.location.file none
%define api.value.type variant
%define parse.assert

%code requires {
  # include <string>
  #include <exception>
  class driver;
  class RootAST;
  class ExprAST;
  class NumberExprAST;
  class VariableExprAST;
  class CallExprAST;
  class FunctionAST;
  class SeqAST;
  class PrototypeAST;
  class BlockExprAST;
  class VarBindingAST;
  class GlobalAST;
  class AssignmentExprAST;
  class ForStmtAST;
  class IfExprAST;
  class BooleanExprAST;
  class IfStmtAST;
  class InitAST;
  class StatementAST;
}

// The parsing context.
%param { driver& drv }

%locations

%define parse.trace
%define parse.error verbose

%code {
# include "driver.hpp"
}

%define api.token.prefix {TOK_}
%token
  END  0  "end of file"
  SEMICOLON  ";"
  COMMA      ","
  MINUS      "-"
  PLUS       "+"
  STAR       "*"
  SLASH      "/"
  LPAREN     "("
  RPAREN     ")"
  QMARK	     "?"
  COLON      ":"
  LT         "<"
  EQ         "=="
  ASSIGN     "="
  LBRACE     "{"
  RBRACE     "}"
  EXTERN     "extern"
  DEF        "def"
  VAR        "var"
  GLOBAL     "global"
  IF         "if"
  ELSE       "else"
  FOR        "for"
  AND        "and"
  OR         "or"
  NOT        "not" 
  LSQUARE    "["
  RSQUARE    "]"
;

%token <std::string> IDENTIFIER "id"
%token <double> NUMBER "number"
%type <ExprAST*> exp
%type <ExprAST*> idexp
%type <ExprAST*> expif
%type <ExprAST*> relexp
%type <std::vector<StatementAST*>> stmts
%type <StatementAST*> stmt
%type <std::vector<ExprAST*>> optexp
%type <std::vector<ExprAST*>> explist
%type <RootAST*> program
%type <RootAST*> top
%type <FunctionAST*> definition
%type <PrototypeAST*> external
%type <PrototypeAST*> proto
%type <std::vector<std::string>> idseq
%type <BlockExprAST*> block
%type <std::vector<InitAST*>> vardefs
%type <InitAST*> binding
%type <GlobalAST*> globalvar
%type <AssignmentExprAST*> assignment
%type <ExprAST*> initexp
%type <InitAST*> init
%type <IfStmtAST*> ifstmt
%type <ForStmtAST*> forstmt
%type <ExprAST*> condexp


%%
%start startsymb;

startsymb:
program                 { drv.root = $1; }

program:
  %empty                { $$ = new SeqAST(nullptr,nullptr); }
|  top ";" program      { $$ = new SeqAST($1,$3); };

top:
%empty                  { $$ = nullptr; }
| definition            { $$ = $1; }
| external              { $$ = $1; }
| globalvar             { $$ = $1; };

definition:
  "def" proto block       { $$ = new FunctionAST($2,$3); $2->noemit(); };  
  

external:
  "extern" proto        { $$ = $2; };

proto:
  "id" "(" idseq ")"    { $$ = new PrototypeAST($1,$3);  };

globalvar:
  "global" "id"         { $$ = new GlobalAST($2);}
| "global" "id" "[" "number" "]" {$$ = new GlobalAST($2,$4,true);};

idseq:
  %empty                { std::vector<std::string> args; $$ = args; }
| "id" idseq            { $2.insert($2.begin(),$1); $$ = $2; };

%left ":";
%left "<" "==";
%left "+" "-";
%left "not";
%left "and" "or";
%left "*" "/";

stmts:
  stmt                  { $$ = std::vector<StatementAST*>{$1}; }
| stmt ";" stmts        { $3.insert($3.begin(),$1); $$ = $3;};

stmt:
  assignment            { $$ = $1; }
| block                 { $$ = $1; }
| ifstmt                { $$ = $1; }
| forstmt               { $$ = $1; }
| exp                   { $$ = $1; };

ifstmt:
  "if" "(" condexp ")" stmt { $$ = new IfStmtAST($3,$5);}
| "if" "(" condexp ")" stmt "else" stmt { $$ = new IfStmtAST($3,$5,$7);};

forstmt:
  "for" "(" init ";" condexp ";" assignment ")" stmt { $$ = new ForStmtAST($3,$5,$7,$9);};

init:
  binding { $$ = $1; }
| assignment { $$ = $1; };

assignment:
  "id" "=" exp              { $$ = new AssignmentExprAST($1,$3);}
| "+" "+" "id"              {$$ = new AssignmentExprAST($3,new BinaryExprAST('+',new VariableExprAST($3),new NumberExprAST(1.0)));}
| "-" "-" "id"              {$$ = new AssignmentExprAST($3,new BinaryExprAST('-',new VariableExprAST($3),new NumberExprAST(1.0)));};
| "id" "[" exp "]" "=" exp   {$$ = new AssignmentExprAST($1,$6,$3,true);};

block:
  "{" stmts "}"             {std::vector<InitAST*> definitions; $$ = new BlockExprAST(definitions,$2);}
| "{" vardefs ";" stmts "}" {$$ = new BlockExprAST($2,$4);};

vardefs:
  binding                 { $$ = std::vector<InitAST*>{$1};}
| vardefs ";" binding     { $1.push_back($3); $$ = $1; };

binding:
  "var" "id" initexp           { $$ = new VarBindingAST($2,$3); }
| "var" "id" "[" "number" "]"  { $$ = new VarBindingAST($2,$4);}
| "var" "id" "[" "number" "]" "=" "{" explist "}"  { $$ = new VarBindingAST($2,$4,$8);};

exp:
  exp "+" exp           { $$ = new BinaryExprAST('+',$1,$3); }
| exp "-" exp           { $$ = new BinaryExprAST('-',$1,$3); }
| exp "*" exp           { $$ = new BinaryExprAST('*',$1,$3); }
| exp "/" exp           { $$ = new BinaryExprAST('/',$1,$3); }
| idexp                 { $$ = $1; }
| "(" exp ")"           { $$ = $2; }
| "number"              { $$ = new NumberExprAST($1); }
| "-" "number"          { $$ = new NumberExprAST(-$2); }
| expif                 { $$ = $1; };               

initexp:
  %empty                { $$ = new NumberExprAST(0.0); }
| "=" exp               { $$ = $2;};

expif:
  condexp "?" exp ":" exp { $$ = new IfExprAST($1,$3,$5); };

condexp:
  relexp               { $$ = $1; }
| relexp "and" condexp { $$ = new BooleanExprAST('A',$1,$3); }
| relexp "or" condexp  { $$ = new BooleanExprAST('O',$1,$3); }
| "not" condexp        { $$ = new BooleanExprAST('N',$2); }
| "(" condexp ")"      { $$ = $2; };

relexp:
  exp "<" exp           { $$ = new BinaryExprAST('<',$1,$3); }
| exp "==" exp          { $$ = new BinaryExprAST('=',$1,$3); };

idexp:
  "id"                  { $$ = new VariableExprAST($1); }
| "-" "id"              { $$ = new BinaryExprAST('*',new VariableExprAST($2),new NumberExprAST(-1.0));}
| "id" "(" optexp ")"   { $$ = new CallExprAST($1,$3); }
| "id" "[" exp "]"      { $$ = new VariableExprAST($1,$3,true);};

optexp:
  %empty                { std::vector<ExprAST*> args; $$ = args; }
| explist               { $$ = $1; };

explist:
  exp                   { std::vector<ExprAST*> args; args.push_back($1); $$ = args;}
| exp "," explist       { $3.insert($3.begin(), $1); $$ = $3; };
 
%%

void
yy::parser::error (const location_type& l, const std::string& m)
{
  std::cerr << l << ": " << m << '\n';
}

