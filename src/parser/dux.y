/* Dux language parser — bison 3.8 */
%skeleton "lalr1.cc"
%require  "3.8"

%define api.token.constructor
%define api.value.type variant
%define parse.assert
%define parse.error detailed
%define parse.lac    full
%define parse.trace

%locations

/* =====================================================================
   Code visible in generated parser.hpp
   ===================================================================== */
%code requires {
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "ast/ast.hpp"

class Driver;

/* Bring AST types into scope for %type declarations below */
using namespace dux::ast;

/* Type aliases needed for %type — std::pair can't appear in bison %type
   arguments because the angle-brackets confuse the parser. */
using PairList    = std::vector<std::pair<ExprPtr, ExprPtr>>;
using VarItem     = std::pair<std::string, ExprPtr>;
using VarItemList = std::vector<VarItem>;
/* Generic type parameter: (param_name, bound_name). bound_name is empty if no bound. */
using TypeParamEntry    = std::pair<std::string, std::string>;
using TypeParamEntryList = std::vector<TypeParamEntry>;
}

/* Thread both driver and loc through parser and yylex */
%param { Driver& driver }
%param { yy::location& loc }

/* =====================================================================
   Code visible only in parser.cpp
   ===================================================================== */
%code {
#include "driver/driver.hpp"

/* yylex declaration — matches YY_DECL in dux.l */
yy::parser::symbol_type yylex(Driver& driver, yy::location& loc);

/* Convert bison location to AST source location */
static dux::ast::SourceLoc sl(const yy::location& l, Driver& d) {
    return d.make_loc(l);
}

/* Smart-pointer factory */
template<typename T, typename... Args>
static std::unique_ptr<T> mk(Args&&... a) {
    return std::make_unique<T>(std::forward<Args>(a)...);
}
}

/* =====================================================================
   Token declarations
   ===================================================================== */

/* Literals */
%token <long long>   INT_LIT    "integer literal"
%token <long long>   LONG_LIT   "long literal"
%token <double>      FLOAT_LIT  "float literal"
%token <double>      REAL_LIT   "real literal"
%token <std::string> STRING     "string literal"
%token <std::string> IDENT      "identifier"
%token <bool>        BOOL_LIT   "bool literal"

/* Keywords */
%token KW_NAMESPACE KW_IMPORT KW_FROM KW_AS KW_CLASS KW_INTERFACE KW_ENUM
%token KW_PUBLIC KW_PRIVATE KW_PROTECTED
%token KW_NEW KW_DELETE KW_THIS KW_SUPER KW_NULL
%token KW_RETURN KW_BREAK KW_CONTINUE
%token KW_IF KW_ELSE KW_FOR KW_WHILE KW_DO
%token KW_SWITCH KW_CASE KW_DEFAULT
%token KW_MATCH
%token KW_TRY KW_CATCH KW_IN
%token KW_CONST KW_STATIC
%token KW_GET KW_SET
%token KW_ASSERT KW_DEFER KW_THROW
%token KW_FN "fn"
%token KW_AUTO "auto"
%token KW_AND KW_OR KW_NOT
%token KW_UNSAFE KW_EXTERN
%token KW_ASYNC KW_AWAIT

/* Type keywords */
%token KW_VOID KW_INT KW_LONG KW_REAL KW_DOUBLE KW_STR
%token KW_BOOL KW_LIST KW_DICT KW_TUPLE KW_OBJECT KW_PTR

/* Operators & punctuation */
%token SEMI ";"
%token COMMA "," COLON ":" DCOLON "::" DOT "." ELLIPSIS "..."
%token RANGE_EXCL "..<" RANGE_INCL "..="
%token ASSIGN "=" PLUS_ASSIGN "+=" MINUS_ASSIGN "-=" STAR_ASSIGN "*="
%token SLASH_ASSIGN "/=" PERCENT_ASSIGN "%="
%token EQEQ "==" NEQ "!=" LT "<" GT ">" LEQ "<=" GEQ ">="
%token AMPAMP "&&" PIPEPIPE "||"
%token PLUS "+" MINUS "-" STAR "*" SLASH "/" PERCENT "%"
%token PLUSPLUS "++" MINUSMINUS "--"
%token BANG "!" TILDE "~"
%token ARROW "->" FAT_ARROW "=>"
%token LPAREN "(" RPAREN ")" LBRACE "{" RBRACE "}" LBRACKET "[" RBRACKET "]"
%token AT "@" AMP "&"

/* =====================================================================
   Nonterminal types  — ALL declared here before %%
   ===================================================================== */

/* Program */
%type <DeclList>                         top_decl_list
%type <DeclPtr>                          top_decl decl extern_decl
/* Declarations */
%type <DeclPtr>  namespace_decl import_decl class_decl interface_decl enum_decl
%type <DeclPtr>  func_decl field_decl ctor_decl dtor_decl
/* Enum */
%type <std::vector<EnumVariant>>  enum_variants
%type <EnumVariant>               enum_variant
%type <std::vector<TypeExpr>>     enum_payload opt_enum_payload
/* Namespace body accumulates into a temporary Program */
%type <std::unique_ptr<Program>>         namespace_body
/* Class / interface */
%type <std::vector<BaseClass>>           opt_base_list base_list base_list_ne
%type <BaseClass>                        base_entry
%type <std::string>                      class_ref
%type <std::vector<ClassMember>>         class_body_or_semi class_body class_members
%type <std::vector<ClassMember>>         interface_members
%type <ClassMember>                      class_member interface_member
/* Function */
%type <std::optional<std::string>>       opt_func_modifier
/* Constructor */
%type <std::vector<InitEntry>>           opt_init_list init_list
%type <InitEntry>                        init_entry
/* Decorators */
%type <std::vector<Decorator>>           decorators
%type <Decorator>                        decorator
%type <std::string>                      decorator_name
/* Parameters */
%type <std::vector<Param>>               param_list param_list_ne
%type <Param>                            param
/* Types */
%type <TypeExpr>                         type_expr
%type <std::vector<TypeExpr>>            fn_type_params fn_type_params_ne type_arg_list_ne
%type <TypeParamEntryList>               opt_type_params type_params_ne
/* Access */
%type <AccessMod>                        access_mod
%type <std::string>                      dotted_name
%type <std::vector<std::string>>         ident_list
/* Statements */
%type <StmtList>                         stmt_list block_body
%type <StmtPtr>  stmt simple_stmt
%type <StmtPtr>  if_stmt while_stmt do_while_stmt for_stmt
%type <StmtPtr>  switch_stmt match_stmt try_stmt return_stmt break_stmt
%type <StmtPtr>  continue_stmt assert_stmt delete_stmt defer_stmt throw_stmt unsafe_stmt
%type <ExprPtr>  defer_expr defer_primary
%type <std::unique_ptr<BlockStmt>>       block
%type <std::vector<SwitchCase>>          switch_cases
%type <SwitchCase>                       switch_case
%type <std::optional<std::string>>       opt_label
/* Match statement */
%type <std::vector<MatchArm>>            match_arms
%type <MatchArm>                         match_arm
%type <MatchPattern>                     match_pattern
/* Variable declarations */
%type <StmtPtr>                          var_decl_stmt
%type <VarItemList>                      var_decl_items
%type <VarItem>                          var_decl_item
/* Expressions */
%type <ExprPtr>  expr assign_expr or_expr and_expr eq_expr rel_expr
%type <ExprPtr>  add_expr mul_expr unary_expr postfix_expr primary_expr
%type <ExprList>                         arg_list arg_list_ne expr_list
%type <ExprPtr>                          list_lit dict_lit
%type <PairList>                         dict_pairs

/* =====================================================================
   Operator precedence  (lowest to highest)
   ===================================================================== */
%right ASSIGN PLUS_ASSIGN MINUS_ASSIGN STAR_ASSIGN SLASH_ASSIGN PERCENT_ASSIGN
%left  PIPEPIPE KW_OR
%left  AMPAMP KW_AND
%left  EQEQ NEQ
%left  LT GT LEQ GEQ RANGE_EXCL RANGE_INCL
%left  PLUS MINUS
%left  STAR SLASH PERCENT
%right BANG TILDE UMINUS UPLUS
%left  PLUSPLUS MINUSMINUS DOT LBRACKET LPAREN

%nonassoc KW_ELSE   /* resolve dangling-else */

%start program

%%

/* =====================================================================
   Program — top level
   ===================================================================== */

program
    : top_decl_list
        {
            auto p   = mk<Program>();
            p->loc   = sl(@$, driver);
            p->decls = std::move($1);
            driver.result = std::move(p);
        }
    ;

top_decl_list
    : %empty                          { $$ = DeclList{}; }
    | top_decl_list SEMI              { $$ = std::move($1); }
    | top_decl_list top_decl          { $1.push_back(std::move($2)); $$ = std::move($1); }
    | top_decl_list error SEMI        { $$ = std::move($1); yyerrok; }
    ;

top_decl : decl { $$ = std::move($1); } ;

decl
    : namespace_decl  { $$ = std::move($1); }
    | import_decl     { $$ = std::move($1); }
    | class_decl      { $$ = std::move($1); }
    | interface_decl  { $$ = std::move($1); }
    | enum_decl       { $$ = std::move($1); }
    | func_decl       { $$ = std::move($1); }
    | extern_decl     { $$ = std::move($1); }
    ;

/* =====================================================================
   Namespace
   ===================================================================== */

namespace_decl
    : KW_NAMESPACE dotted_name LBRACE namespace_body RBRACE
        {
            auto n    = mk<NamespaceDecl>();
            n->loc    = sl(@$, driver);
            n->name   = $2;
            n->decls  = std::move($4->decls);
            n->stmts  = std::move($4->stmts);
            $$ = std::move(n);
        }
    | KW_NAMESPACE dotted_name SEMI
        {
            auto n              = mk<NamespaceDecl>();
            n->loc              = sl(@$, driver);
            n->name             = $2;
            n->is_package_decl  = true;
            $$ = std::move(n);
        }
    ;

/* Accumulate namespace body items into a temporary Program node */
namespace_body
    : %empty
        { $$ = mk<Program>(); }
    | namespace_body SEMI
        { $$ = std::move($1); }
    | namespace_body decl
        { $1->decls.push_back(std::move($2)); $$ = std::move($1); }
    | namespace_body var_decl_stmt
        { $1->stmts.push_back(std::move($2)); $$ = std::move($1); }
    | namespace_body simple_stmt SEMI
        { $1->stmts.push_back(std::move($2)); $$ = std::move($1); }
    | namespace_body error SEMI
        { $$ = std::move($1); yyerrok; }
    ;

dotted_name
    : IDENT                     { $$ = $1; }
    | KW_STR                    { $$ = "str"; }
    | KW_LIST                   { $$ = "list"; }
    | KW_DICT                   { $$ = "dict"; }
    | KW_ASYNC                  { $$ = "async"; }
    | dotted_name DOT IDENT     { $$ = $1 + '.' + $3; }
    | dotted_name DOT KW_ASYNC  { $$ = $1 + ".async"; }
    ;

/* =====================================================================
   Import
   ===================================================================== */

import_decl
    : KW_IMPORT dotted_name SEMI
        {
            auto n  = mk<ImportDecl>();
            n->loc  = sl(@$, driver);
            n->path = $2;
            $$ = std::move(n);
        }
    | KW_IMPORT dotted_name KW_AS IDENT SEMI
        {
            auto n    = mk<ImportDecl>();
            n->loc    = sl(@$, driver);
            n->path   = $2;
            n->alias  = $4;
            $$ = std::move(n);
        }
    | KW_IMPORT dotted_name DCOLON LBRACE ident_list RBRACE SEMI
        {
            auto n     = mk<ImportDecl>();
            n->loc     = sl(@$, driver);
            n->path    = $2;
            n->symbols = std::move($5);
            $$ = std::move(n);
        }
    | KW_IMPORT dotted_name DCOLON LBRACE ident_list RBRACE KW_AS IDENT SEMI
        {
            auto n     = mk<ImportDecl>();
            n->loc     = sl(@$, driver);
            n->path    = $2;
            n->symbols = std::move($5);
            n->alias   = $8;
            $$ = std::move(n);
        }
    | KW_IMPORT dotted_name DCOLON STAR SEMI
        {
            auto n  = mk<ImportDecl>();
            n->loc  = sl(@$, driver);
            n->path = $2;
            n->symbols = {"*"};
            $$ = std::move(n);
        }
    | KW_IMPORT LBRACE ident_list RBRACE KW_FROM dotted_name SEMI
        {
            auto n              = mk<ImportDecl>();
            n->loc              = sl(@$, driver);
            n->path             = $6;
            n->symbols          = std::move($3);
            n->global_scope     = true;
            $$ = std::move(n);
        }
    ;

ident_list
    : IDENT                        { $$.push_back($1); }
    | ident_list COMMA IDENT       { $1.push_back($3); $$ = std::move($1); }
    ;

/* =====================================================================
   Extern declaration
   ===================================================================== */

extern_decl
    : KW_EXTERN STRING type_expr IDENT LPAREN param_list RPAREN SEMI
        {
            auto e    = mk<ExternDecl>(); e->loc = sl(@$, driver);
            e->abi    = $2;
            e->ret    = $3;
            e->name   = $4;
            e->params = std::move($6);
            $$ = std::move(e);
        }
    ;

/* =====================================================================
   Class
   ===================================================================== */

class_decl
    : decorators KW_CLASS IDENT opt_type_params opt_base_list class_body_or_semi
        {
            auto n           = mk<ClassDecl>();
            n->loc           = sl(@$, driver);
            n->decorators    = std::move($1);
            n->name          = $3;
            for (const auto& [pname, bname] : $4) {
                n->type_params.push_back(pname);
                if (!bname.empty()) n->type_bounds.push_back(TypeBound{pname, bname});
            }
            n->bases         = std::move($5);
            n->members       = std::move($6);
            $$ = std::move(n);
        }
    ;

opt_base_list
    : %empty                             { $$ = std::vector<BaseClass>(); }
    | LPAREN base_list RPAREN            { $$ = std::move($2); }
    ;

class_body_or_semi
    : SEMI                               { $$ = std::vector<ClassMember>(); }
    | LBRACE class_body RBRACE           { $$ = std::move($2); }
    ;

base_list
    : %empty                             { $$ = std::vector<BaseClass>(); }
    | base_list_ne                       { $$ = std::move($1); }
    ;

base_list_ne
    : base_entry
        { $$.push_back(std::move($1)); }
    | base_list_ne COMMA base_entry
        { $1.push_back(std::move($3)); $$ = std::move($1); }
    ;

base_entry
    : class_ref              { $$ = BaseClass{AccessMod::None, $1}; }
    | access_mod class_ref   { $$ = BaseClass{$1, $2};              }
    ;

/* Allow 'object' (and similar built-in type names) as base class names */
class_ref
    : IDENT      { $$ = $1;       }
    | KW_OBJECT  { $$ = "object"; }
    ;

class_body
    : %empty          { $$ = std::vector<ClassMember>(); }
    | class_members   { $$ = std::move($1); }
    ;

class_members
    : class_member
        { $$.push_back(std::move($1)); }
    | class_members class_member
        { $1.push_back(std::move($2)); $$ = std::move($1); }
    | class_members SEMI
        { $$ = std::move($1); }
    | class_members error SEMI
        { $$ = std::move($1); yyerrok; }
    ;

class_member
    : access_mod COLON
        {
            ClassMember cm; cm.access = $1; $$ = std::move(cm);
        }
    | decorators func_decl
        {
            ClassMember cm; cm.decorators = std::move($1); cm.decl = std::move($2);
            $$ = std::move(cm);
        }
    | decorators field_decl
        {
            ClassMember cm; cm.decorators = std::move($1); cm.decl = std::move($2);
            $$ = std::move(cm);
        }
    | decorators ctor_decl
        {
            ClassMember cm; cm.decorators = std::move($1); cm.decl = std::move($2);
            $$ = std::move(cm);
        }
    | decorators dtor_decl
        {
            ClassMember cm; cm.decorators = std::move($1); cm.decl = std::move($2);
            $$ = std::move(cm);
        }
    ;

/* =====================================================================
   Interface
   ===================================================================== */

interface_decl
    : KW_INTERFACE IDENT LPAREN RPAREN LBRACE interface_members RBRACE
        {
            auto n     = mk<InterfaceDecl>();
            n->loc     = sl(@$, driver);
            n->name    = $2;
            n->members = std::move($6);
            $$ = std::move(n);
        }
    ;

interface_members
    : %empty                              { $$ = std::vector<ClassMember>(); }
    | interface_members SEMI              { $$ = std::move($1); }
    | interface_members interface_member
        { $1.push_back(std::move($2)); $$ = std::move($1); }
    | interface_members error SEMI
        { $$ = std::move($1); yyerrok; }
    ;

interface_member
    : access_mod COLON
        { ClassMember cm; cm.access = $1; $$ = std::move(cm); }
    | type_expr IDENT LPAREN param_list RPAREN SEMI
        {
            auto f        = mk<FunctionDecl>();
            f->loc        = sl(@$, driver);
            f->return_type = $1;
            f->name       = $2;
            f->params     = std::move($4);
            ClassMember cm; cm.decl = std::move(f);
            $$ = std::move(cm);
        }
    ;

/* =====================================================================
   Enum
   ===================================================================== */

enum_decl
    : KW_ENUM IDENT LBRACE enum_variants RBRACE
        {
            auto n        = mk<EnumDecl>();
            n->loc        = sl(@$, driver);
            n->name       = $2;
            n->variants   = std::move($4);
            $$ = std::move(n);
        }
    | KW_ENUM IDENT LBRACE enum_variants RBRACE SEMI
        {
            auto n        = mk<EnumDecl>();
            n->loc        = sl(@$, driver);
            n->name       = $2;
            n->variants   = std::move($4);
            $$ = std::move(n);
        }
    ;

enum_variants
    : %empty               { $$ = std::vector<EnumVariant>{}; }
    | enum_variant         { $$.push_back(std::move($1)); }
    | enum_variants COMMA enum_variant
        { $1.push_back(std::move($3)); $$ = std::move($1); }
    | enum_variants COMMA  { $$ = std::move($1); }  /* trailing comma */
    | enum_variants SEMI   { $$ = std::move($1); }  /* absorb auto-semicolons */
    ;

enum_variant
    : IDENT opt_enum_payload
        {
            EnumVariant v;
            v.loc     = sl(@$, driver);
            v.name    = $1;
            v.payload = std::move($2);
            $$ = std::move(v);
        }
    ;

opt_enum_payload
    : %empty                           { $$ = std::vector<TypeExpr>{}; }
    | LPAREN enum_payload RPAREN       { $$ = std::move($2); }
    ;

enum_payload
    : type_expr                        { $$.push_back(std::move($1)); }
    | enum_payload COMMA type_expr     { $1.push_back(std::move($3)); $$ = std::move($1); }
    ;

/* =====================================================================
   Function / method
   ===================================================================== */

func_decl
    : type_expr IDENT opt_type_params LPAREN param_list RPAREN opt_func_modifier block
        {
            auto f          = mk<FunctionDecl>();
            f->loc          = sl(@$, driver);
            f->return_type  = $1;
            f->name         = $2;
            for (const auto& [pname, bname] : $3) {
                f->type_params.push_back(pname);
                if (!bname.empty()) f->type_bounds.push_back(TypeBound{pname, bname});
            }
            f->params       = std::move($5);
            f->modifier     = $7;
            f->body         = std::move(*$8);
            $$ = std::move(f);
        }
    | KW_ASYNC type_expr IDENT opt_type_params LPAREN param_list RPAREN block
        {
            auto f          = mk<FunctionDecl>();
            f->loc          = sl(@$, driver);
            f->return_type  = $2;
            f->name         = $3;
            for (const auto& [pname, bname] : $4) {
                f->type_params.push_back(pname);
                if (!bname.empty()) f->type_bounds.push_back(TypeBound{pname, bname});
            }
            f->params       = std::move($6);
            f->body         = std::move(*$8);
            f->is_async     = true;
            $$ = std::move(f);
        }
    | KW_STATIC type_expr IDENT LPAREN param_list RPAREN opt_func_modifier block
        {
            auto f          = mk<FunctionDecl>();
            f->loc          = sl(@$, driver);
            f->return_type  = $2;
            f->name         = $3;
            f->params       = std::move($5);
            f->modifier     = $7;
            f->body         = std::move(*$8);
            f->is_static    = true;
            $$ = std::move(f);
        }
    | KW_STATIC type_expr IDENT LPAREN param_list RPAREN opt_func_modifier block SEMI
        {
            auto f          = mk<FunctionDecl>();
            f->loc          = sl(@$, driver);
            f->return_type  = $2;
            f->name         = $3;
            f->params       = std::move($5);
            f->modifier     = $7;
            f->body         = std::move(*$8);
            f->is_static    = true;
            $$ = std::move(f);
        }
    ;

opt_type_params
    : %empty                        { $$ = TypeParamEntryList{}; }
    | LT type_params_ne GT          { $$ = std::move($2); }
    ;

type_params_ne
    : IDENT                               { $$.push_back({$1, ""}); }
    | IDENT COLON IDENT                   { $$.push_back({$1, $3}); }
    | type_params_ne COMMA IDENT          { $1.push_back({$3, ""}); $$ = std::move($1); }
    | type_params_ne COMMA IDENT COLON IDENT { $1.push_back({$3, $5}); $$ = std::move($1); }
    ;

opt_func_modifier
    : %empty          { $$ = std::nullopt; }
    | KW_GET          { $$ = "get"; }
    | KW_SET          { $$ = "set"; }
    | ARROW KW_GET    { $$ = "get"; }
    | ARROW KW_SET    { $$ = "set"; }
    ;

/* =====================================================================
   Constructor / Destructor
   ===================================================================== */

ctor_decl
    : IDENT LPAREN param_list RPAREN opt_init_list block
        {
            auto f       = mk<FunctionDecl>();
            f->loc       = sl(@$, driver);
            f->name      = $1;
            f->params    = std::move($3);
            f->init_list = std::move($5);
            f->body      = std::move(*$6);
            f->is_ctor   = true;
            $$ = std::move(f);
        }
    ;

opt_init_list
    : %empty          { $$ = std::vector<InitEntry>(); }
    | COLON init_list { $$ = std::move($2); }
    ;

init_list
    : init_entry
        { $$.push_back(std::move($1)); }
    | init_list COMMA init_entry
        { $1.push_back(std::move($3)); $$ = std::move($1); }
    ;

init_entry
    : IDENT LPAREN arg_list RPAREN
        { InitEntry e; e.field = $1; e.args = std::move($3); $$ = std::move(e); }
    ;

dtor_decl
    : TILDE IDENT LPAREN RPAREN block
        {
            auto f     = mk<FunctionDecl>();
            f->loc     = sl(@$, driver);
            f->name    = $2;
            f->body    = std::move(*$5);
            f->is_dtor = true;
            $$ = std::move(f);
        }
    ;

/* =====================================================================
   Field declaration (inside class)
   ===================================================================== */

field_decl
    : type_expr IDENT SEMI
        {
            auto f  = mk<FieldDecl>(); f->loc = sl(@$, driver);
            f->type = $1; f->name = $2;
            $$ = std::move(f);
        }
    | type_expr IDENT ASSIGN expr SEMI
        {
            auto f  = mk<FieldDecl>(); f->loc = sl(@$, driver);
            f->type = $1; f->name = $2; f->init = std::move($4);
            $$ = std::move(f);
        }
    | KW_STATIC type_expr IDENT SEMI
        {
            auto f  = mk<FieldDecl>(); f->loc = sl(@$, driver);
            f->type = $2; f->name = $3; f->is_static = true;
            $$ = std::move(f);
        }
    | KW_STATIC type_expr IDENT ASSIGN expr SEMI
        {
            auto f  = mk<FieldDecl>(); f->loc = sl(@$, driver);
            f->type = $2; f->name = $3; f->init = std::move($5); f->is_static = true;
            $$ = std::move(f);
        }
    ;

/* =====================================================================
   Decorators
   ===================================================================== */

decorators
    : %empty                { $$ = std::vector<Decorator>(); }
    | decorators decorator  { $1.push_back(std::move($2)); $$ = std::move($1); }
    ;

decorator
    : AT decorator_name LPAREN arg_list RPAREN SEMI
        {
            Decorator d; d.loc = sl(@$, driver); d.name = $2; d.args = std::move($4);
            $$ = std::move(d);
        }
    | AT decorator_name SEMI
        {
            Decorator d; d.loc = sl(@$, driver); d.name = $2;
            $$ = std::move(d);
        }
    ;

decorator_name
    : IDENT                  { $$ = $1; }
    | IDENT DCOLON IDENT     { $$ = $1 + "::" + $3; }
    ;

/* =====================================================================
   Parameters
   ===================================================================== */

param_list
    : %empty          { $$ = std::vector<Param>(); }
    | param_list_ne   { $$ = std::move($1); }
    ;

param_list_ne
    : param
        { $$.push_back(std::move($1)); }
    | param_list_ne COMMA param
        { $1.push_back(std::move($3)); $$ = std::move($1); }
    ;

param : type_expr IDENT { $$ = Param{$1, $2}; } ;

fn_type_params
    : %empty                              { $$ = std::vector<TypeExpr>{}; }
    | fn_type_params_ne                   { $$ = std::move($1); }
    ;

fn_type_params_ne
    : type_expr
        { $$ = std::vector<TypeExpr>{}; $$.push_back(std::move($1)); }
    | fn_type_params_ne COMMA type_expr
        { $1.push_back(std::move($3)); $$ = std::move($1); }
    ;

/* =====================================================================
   Access modifier
   ===================================================================== */

access_mod
    : KW_PUBLIC     { $$ = AccessMod::Public;    }
    | KW_PRIVATE    { $$ = AccessMod::Private;   }
    | KW_PROTECTED  { $$ = AccessMod::Protected; }
    ;

/* =====================================================================
   Type expressions
   ===================================================================== */

type_expr
    : KW_VOID    { $$.name = "void";   $$.is_const = false; }
    | KW_INT     { $$.name = "int";    $$.is_const = false; }
    | KW_LONG    { $$.name = "long";   $$.is_const = false; }
    | KW_REAL    { $$.name = "real";   $$.is_const = false; }
    | KW_DOUBLE  { $$.name = "double"; $$.is_const = false; }
    | KW_STR     { $$.name = "str";    $$.is_const = false; }
    | KW_BOOL    { $$.name = "bool";   $$.is_const = false; }
    | KW_LIST    { $$.name = "list";   $$.is_const = false; }
    | KW_DICT    { $$.name = "dict";   $$.is_const = false; }
    | KW_TUPLE   { $$.name = "tuple";  $$.is_const = false; }
    | KW_OBJECT  { $$.name = "object"; $$.is_const = false; }
    | KW_PTR     { $$.name = "ptr";    $$.is_const = false; }
    | KW_CONST type_expr  { $$ = $2; $$.is_const = true; }
    | IDENT      { $$.name = $1;      $$.is_const = false; }
    | IDENT LT type_arg_list_ne GT
        { $$.name = $1; $$.is_const = false; $$.type_args = std::move($3); }
    | KW_FN LPAREN fn_type_params RPAREN ARROW type_expr
        { $$.name = "__fn"; $$.is_const = false; $$.fn_params = std::move($3); $$.fn_ret = $6.name; }
    ;

type_arg_list_ne
    : type_expr
        { $$ = std::vector<TypeExpr>{}; $$.push_back(std::move($1)); }
    | type_arg_list_ne COMMA type_expr
        { $1.push_back(std::move($3)); $$ = std::move($1); }
    ;

/* =====================================================================
   Statements
   ===================================================================== */

stmt_list
    : %empty                    { $$ = StmtList{}; }
    | stmt_list SEMI            { $$ = std::move($1); }
    | stmt_list stmt            { $1.push_back(std::move($2)); $$ = std::move($1); }
    | stmt_list error SEMI      { $$ = std::move($1); yyerrok; }
    ;

stmt
    : if_stmt             { $$ = std::move($1); }
    | while_stmt          { $$ = std::move($1); }
    | do_while_stmt       { $$ = std::move($1); }
    | for_stmt            { $$ = std::move($1); }
    | switch_stmt         { $$ = std::move($1); }
    | match_stmt          { $$ = std::move($1); }
    | try_stmt            { $$ = std::move($1); }
    | return_stmt         { $$ = std::move($1); }
    | break_stmt          { $$ = std::move($1); }
    | continue_stmt       { $$ = std::move($1); }
    | assert_stmt         { $$ = std::move($1); }
    | delete_stmt         { $$ = std::move($1); }
    | defer_stmt          { $$ = std::move($1); }
    | throw_stmt          { $$ = std::move($1); }
    | unsafe_stmt         { $$ = std::move($1); }
    | var_decl_stmt       { $$ = std::move($1); }
    | simple_stmt SEMI    { $$ = std::move($1); }
    ;

simple_stmt
    : expr
        {
            auto s = mk<ExprStmt>(); s->loc = sl(@$, driver); s->expr = std::move($1);
            $$ = std::move(s);
        }
    ;

block
    : LBRACE block_body RBRACE
        {
            auto b  = mk<BlockStmt>(); b->loc = sl(@$, driver); b->body = std::move($2);
            $$ = std::move(b);
        }
    ;

block_body
    : stmt_list   { $$ = std::move($1); }
    ;

/* -- If ---------------------------------------------------------------- */
if_stmt
    : KW_IF expr block %prec KW_ELSE
        {
            auto s = mk<IfStmt>(); s->loc = sl(@$, driver);
            s->cond = std::move($2); s->then_br = std::move($3);
            $$ = std::move(s);
        }
    | KW_IF expr block KW_ELSE block
        {
            auto s = mk<IfStmt>(); s->loc = sl(@$, driver);
            s->cond = std::move($2); s->then_br = std::move($3); s->else_br = std::move($5);
            $$ = std::move(s);
        }
    | KW_IF expr block KW_ELSE if_stmt
        {
            auto s = mk<IfStmt>(); s->loc = sl(@$, driver);
            s->cond = std::move($2); s->then_br = std::move($3); s->else_br = std::move($5);
            $$ = std::move(s);
        }
    ;

/* -- While ------------------------------------------------------------- */
while_stmt
    : opt_label KW_WHILE expr block
        {
            auto s = mk<WhileStmt>(); s->loc = sl(@$, driver);
            s->label = $1; s->cond = std::move($3); s->body = std::move($4);
            $$ = std::move(s);
        }
    ;

opt_label
    : %empty          { $$ = std::nullopt; }
    | AMP IDENT       { $$ = $2; }
    ;

/* -- Do / while -------------------------------------------------------- */
do_while_stmt
    : KW_DO block KW_WHILE expr SEMI
        {
            auto s = mk<DoWhileStmt>(); s->loc = sl(@$, driver);
            s->body = std::move($2); s->cond = std::move($4);
            $$ = std::move(s);
        }
    ;

/* -- For --------------------------------------------------------------- */
for_stmt
    : KW_FOR type_expr IDENT KW_IN expr block
        {
            auto s = mk<ForInStmt>(); s->loc = sl(@$, driver);
            s->var_type = $2; s->var_name = $3; s->iterable = std::move($5); s->body = std::move($6);
            $$ = std::move(s);
        }
    | KW_FOR type_expr IDENT ASSIGN expr COMMA expr COMMA expr block
        {
            auto s = mk<ForCStmt>(); s->loc = sl(@$, driver);
            s->var_type = $2; s->var_name = $3;
            s->init = std::move($5); s->cond = std::move($7); s->incr = std::move($9);
            s->body = std::move($10);
            $$ = std::move(s);
        }
    ;

/* -- Switch ------------------------------------------------------------ */
switch_stmt
    : KW_SWITCH expr LBRACE switch_cases RBRACE
        {
            auto s = mk<SwitchStmt>(); s->loc = sl(@$, driver);
            s->expr = std::move($2); s->cases = std::move($4);
            $$ = std::move(s);
        }
    ;

switch_cases
    : %empty                          { $$ = std::vector<SwitchCase>(); }
    | switch_cases switch_case        { $1.push_back(std::move($2)); $$ = std::move($1); }
    ;

switch_case
    : KW_CASE expr COLON stmt_list
        { SwitchCase c; c.value = std::move($2); c.body = std::move($4); $$ = std::move(c); }
    | KW_DEFAULT COLON stmt_list
        { SwitchCase c; c.body = std::move($3); $$ = std::move(c); }
    ;

/* -- Match statement --------------------------------------------------- */

match_stmt
    : KW_MATCH expr LBRACE match_arms RBRACE
        {
            auto s = mk<MatchStmt>(); s->loc = sl(@$, driver);
            s->expr = std::move($2); s->arms = std::move($4);
            $$ = std::move(s);
        }
    ;

match_arms
    : %empty              { $$ = std::vector<MatchArm>{}; }
    | match_arms match_arm
        { $1.push_back(std::move($2)); $$ = std::move($1); }
    | match_arms SEMI     { $$ = std::move($1); }  /* absorb auto-semicolons */
    ;

match_arm
    : match_pattern FAT_ARROW block
        {
            MatchArm arm; arm.loc = sl(@$, driver);
            arm.pattern = std::move($1); arm.body = std::move($3->body);
            $$ = std::move(arm);
        }
    | match_pattern FAT_ARROW block SEMI
        {
            MatchArm arm; arm.loc = sl(@$, driver);
            arm.pattern = std::move($1); arm.body = std::move($3->body);
            $$ = std::move(arm);
        }
    ;

match_pattern
    : IDENT DOT IDENT
        {
            /* EnumName.Variant */
            MatchPattern p; p.loc = sl(@$, driver);
            p.kind = MatchPattern::Kind::EnumVariant;
            p.enum_name    = $1;
            p.variant_name = $3;
            $$ = std::move(p);
        }
    | INT_LIT
        {
            MatchPattern p; p.loc = sl(@$, driver);
            p.kind = MatchPattern::Kind::IntLit;
            p.int_value = $1;
            $$ = std::move(p);
        }
    | KW_NULL
        {
            MatchPattern p; p.loc = sl(@$, driver);
            p.kind = MatchPattern::Kind::IntLit;
            p.int_value = 0;
            $$ = std::move(p);
        }
    | BOOL_LIT
        {
            MatchPattern p; p.loc = sl(@$, driver);
            p.kind = MatchPattern::Kind::BoolLit;
            p.bool_value = $1;
            $$ = std::move(p);
        }
    | IDENT
        {
            /* bare identifier: "_" is wildcard, any other name is also wildcard for now
               (binding variables deferred to a future milestone) */
            MatchPattern p; p.loc = sl(@$, driver);
            p.kind = MatchPattern::Kind::Wildcard;
            $$ = std::move(p);
        }
    ;

/* -- Try / catch ------------------------------------------------------- */
try_stmt
    : KW_TRY block KW_CATCH ELLIPSIS block
        {
            auto s = mk<TryCatchStmt>(); s->loc = sl(@$, driver);
            s->try_body = std::move($2); s->catch_all = true; s->catch_body = std::move($5);
            $$ = std::move(s);
        }
    | KW_TRY block KW_CATCH LPAREN type_expr IDENT RPAREN block
        {
            auto s = mk<TryCatchStmt>(); s->loc = sl(@$, driver);
            s->try_body = std::move($2); s->catch_type = $5; s->catch_var = $6;
            s->catch_body = std::move($8);
            $$ = std::move(s);
        }
    ;

/* -- Return ------------------------------------------------------------ */
return_stmt
    : KW_RETURN SEMI
        { auto s = mk<ReturnStmt>(); s->loc = sl(@$, driver); $$ = std::move(s); }
    | KW_RETURN expr SEMI
        { auto s = mk<ReturnStmt>(); s->loc = sl(@$, driver); s->value = std::move($2); $$ = std::move(s); }
    ;

/* -- Break / Continue -------------------------------------------------- */
break_stmt
    : KW_BREAK SEMI
        { auto s = mk<BreakStmt>(); s->loc = sl(@$, driver); $$ = std::move(s); }
    | KW_BREAK AMP IDENT SEMI
        { auto s = mk<BreakStmt>(); s->loc = sl(@$, driver); s->label = $3; $$ = std::move(s); }
    ;

continue_stmt
    : KW_CONTINUE SEMI
        { auto s = mk<ContinueStmt>(); s->loc = sl(@$, driver); $$ = std::move(s); }
    | KW_CONTINUE AMP IDENT SEMI
        { auto s = mk<ContinueStmt>(); s->loc = sl(@$, driver); s->label = $3; $$ = std::move(s); }
    ;

/* -- Assert ------------------------------------------------------------ */
assert_stmt
    : KW_ASSERT LPAREN expr RPAREN SEMI
        { auto s = mk<AssertStmt>(); s->loc = sl(@$, driver); s->cond = std::move($3); $$ = std::move(s); }
    ;

/* -- Delete ------------------------------------------------------------ */
delete_stmt
    : KW_DELETE expr SEMI
        { auto s = mk<DeleteStmt>(); s->loc = sl(@$, driver); s->expr = std::move($2); $$ = std::move(s); }
    ;

/* -- Defer ------------------------------------------------------------- */
defer_stmt
    : KW_DEFER LBRACE stmt_list RBRACE
        {
            auto s = mk<DeferStmt>(); s->loc = sl(@$, driver);
            s->body = std::move($3);
            $$ = std::move(s);
        }
    | KW_DEFER defer_expr SEMI
        {
            auto s  = mk<DeferStmt>(); s->loc = sl(@$, driver);
            auto es = mk<ExprStmt>();  es->loc = sl(@$, driver);
            es->expr = std::move($2);
            s->body.push_back(std::move(es));
            $$ = std::move(s);
        }
    ;

/* defer_expr is like postfix_expr but must not start with '{' to avoid
   a grammar conflict with the block form 'defer { stmt_list }'. */
defer_expr
    : defer_primary
        { $$ = std::move($1); }
    | defer_expr DOT IDENT
        { auto e = mk<MemberExpr>(); e->loc = sl(@$, driver); e->object = std::move($1); e->member = $3; $$ = std::move(e); }
    | defer_expr LPAREN arg_list RPAREN
        { auto e = mk<CallExpr>(); e->loc = sl(@$, driver); e->callee = std::move($1); e->args = std::move($3); $$ = std::move(e); }
    | defer_expr LBRACKET expr RBRACKET
        { auto e = mk<IndexExpr>(); e->loc = sl(@$, driver); e->object = std::move($1); e->index = std::move($3); $$ = std::move(e); }
    ;

defer_primary
    : IDENT          { auto e = mk<IdentExpr>();  e->loc = sl(@$, driver); e->name = $1; $$ = std::move(e); }
    | KW_THIS        { auto e = mk<ThisExpr>();   e->loc = sl(@$, driver); $$ = std::move(e); }
    | KW_SUPER       { auto e = mk<SuperExpr>();  e->loc = sl(@$, driver); $$ = std::move(e); }
    | LPAREN expr RPAREN  { $$ = std::move($2); }
    ;

/* -- Throw ------------------------------------------------------------- */
throw_stmt
    : KW_THROW expr SEMI
        {
            auto s = mk<ThrowStmt>(); s->loc = sl(@$, driver);
            s->expr = std::move($2);
            $$ = std::move(s);
        }
    ;

/* -- Unsafe block ------------------------------------------------------ */
unsafe_stmt
    : KW_UNSAFE block
        {
            auto s = mk<UnsafeStmt>(); s->loc = sl(@$, driver);
            s->body = std::move($2->body);
            $$ = std::move(s);
        }
    ;

/* -- Variable declaration ---------------------------------------------- */
var_decl_stmt
    : type_expr var_decl_items SEMI
        {
            auto v = mk<VarDeclStmt>(); v->loc = sl(@$, driver);
            v->type = $1; v->is_const = $1.is_const; v->decls = std::move($2);
            $$ = std::move(v);
        }
    | KW_STATIC type_expr var_decl_items SEMI
        {
            auto v = mk<VarDeclStmt>(); v->loc = sl(@$, driver);
            v->type = $2; v->is_static = true; v->decls = std::move($3);
            $$ = std::move(v);
        }
    | KW_STATIC KW_CONST type_expr var_decl_items SEMI
        {
            auto v = mk<VarDeclStmt>(); v->loc = sl(@$, driver);
            v->type = $3; v->is_static = true; v->is_const = true;
            v->decls = std::move($4);
            $$ = std::move(v);
        }
    | KW_CONST KW_STATIC type_expr var_decl_items SEMI
        {
            auto v = mk<VarDeclStmt>(); v->loc = sl(@$, driver);
            v->type = $3; v->is_static = true; v->is_const = true;
            v->decls = std::move($4);
            $$ = std::move(v);
        }
    | KW_AUTO IDENT ASSIGN expr SEMI
        {
            auto v = mk<VarDeclStmt>(); v->loc = sl(@$, driver);
            v->type.name = "__auto";
            v->decls.emplace_back($2, std::move($4));
            $$ = std::move(v);
        }
    ;

var_decl_items
    : var_decl_item
        { $$.push_back(std::move($1)); }
    | var_decl_items COMMA var_decl_item
        { $1.push_back(std::move($3)); $$ = std::move($1); }
    ;

var_decl_item
    : IDENT               { $$ = VarItem{std::move($1), ExprPtr{}}; }
    | IDENT ASSIGN expr   { $$ = VarItem{std::move($1), std::move($3)}; }
    ;

/* =====================================================================
   Expressions
   ===================================================================== */

expr : assign_expr { $$ = std::move($1); } ;

assign_expr
    : postfix_expr ASSIGN         assign_expr
        { auto e = mk<AssignExpr>(); e->loc = sl(@$, driver); e->op = "=";  e->target = std::move($1); e->value = std::move($3); $$ = std::move(e); }
    | postfix_expr PLUS_ASSIGN    assign_expr
        { auto e = mk<AssignExpr>(); e->loc = sl(@$, driver); e->op = "+="; e->target = std::move($1); e->value = std::move($3); $$ = std::move(e); }
    | postfix_expr MINUS_ASSIGN   assign_expr
        { auto e = mk<AssignExpr>(); e->loc = sl(@$, driver); e->op = "-="; e->target = std::move($1); e->value = std::move($3); $$ = std::move(e); }
    | postfix_expr STAR_ASSIGN    assign_expr
        { auto e = mk<AssignExpr>(); e->loc = sl(@$, driver); e->op = "*="; e->target = std::move($1); e->value = std::move($3); $$ = std::move(e); }
    | postfix_expr SLASH_ASSIGN   assign_expr
        { auto e = mk<AssignExpr>(); e->loc = sl(@$, driver); e->op = "/="; e->target = std::move($1); e->value = std::move($3); $$ = std::move(e); }
    | postfix_expr PERCENT_ASSIGN assign_expr
        { auto e = mk<AssignExpr>(); e->loc = sl(@$, driver); e->op = "%="; e->target = std::move($1); e->value = std::move($3); $$ = std::move(e); }
    | or_expr  { $$ = std::move($1); }
    ;

or_expr
    : or_expr PIPEPIPE and_expr
        { auto e = mk<BinaryExpr>(); e->loc = sl(@$, driver); e->op = "||"; e->left = std::move($1); e->right = std::move($3); $$ = std::move(e); }
    | or_expr KW_OR and_expr
        { auto e = mk<BinaryExpr>(); e->loc = sl(@$, driver); e->op = "||"; e->left = std::move($1); e->right = std::move($3); $$ = std::move(e); }
    | and_expr  { $$ = std::move($1); }
    ;

and_expr
    : and_expr AMPAMP eq_expr
        { auto e = mk<BinaryExpr>(); e->loc = sl(@$, driver); e->op = "&&"; e->left = std::move($1); e->right = std::move($3); $$ = std::move(e); }
    | and_expr KW_AND eq_expr
        { auto e = mk<BinaryExpr>(); e->loc = sl(@$, driver); e->op = "&&"; e->left = std::move($1); e->right = std::move($3); $$ = std::move(e); }
    | eq_expr  { $$ = std::move($1); }
    ;

eq_expr
    : eq_expr EQEQ rel_expr
        { auto e = mk<BinaryExpr>(); e->loc = sl(@$, driver); e->op = "=="; e->left = std::move($1); e->right = std::move($3); $$ = std::move(e); }
    | eq_expr NEQ rel_expr
        { auto e = mk<BinaryExpr>(); e->loc = sl(@$, driver); e->op = "!="; e->left = std::move($1); e->right = std::move($3); $$ = std::move(e); }
    | rel_expr  { $$ = std::move($1); }
    ;

rel_expr
    : rel_expr LT         add_expr { auto e = mk<BinaryExpr>(); e->loc = sl(@$, driver); e->op = "<";   e->left = std::move($1); e->right = std::move($3); $$ = std::move(e); }
    | rel_expr GT         add_expr { auto e = mk<BinaryExpr>(); e->loc = sl(@$, driver); e->op = ">";   e->left = std::move($1); e->right = std::move($3); $$ = std::move(e); }
    | rel_expr LEQ        add_expr { auto e = mk<BinaryExpr>(); e->loc = sl(@$, driver); e->op = "<=";  e->left = std::move($1); e->right = std::move($3); $$ = std::move(e); }
    | rel_expr GEQ        add_expr { auto e = mk<BinaryExpr>(); e->loc = sl(@$, driver); e->op = ">=";  e->left = std::move($1); e->right = std::move($3); $$ = std::move(e); }
    | rel_expr RANGE_EXCL add_expr { auto e = mk<BinaryExpr>(); e->loc = sl(@$, driver); e->op = "..<"; e->left = std::move($1); e->right = std::move($3); $$ = std::move(e); }
    | rel_expr RANGE_INCL add_expr { auto e = mk<BinaryExpr>(); e->loc = sl(@$, driver); e->op = "..="; e->left = std::move($1); e->right = std::move($3); $$ = std::move(e); }
    | add_expr  { $$ = std::move($1); }
    ;

add_expr
    : add_expr PLUS  mul_expr { auto e = mk<BinaryExpr>(); e->loc = sl(@$, driver); e->op = "+"; e->left = std::move($1); e->right = std::move($3); $$ = std::move(e); }
    | add_expr MINUS mul_expr { auto e = mk<BinaryExpr>(); e->loc = sl(@$, driver); e->op = "-"; e->left = std::move($1); e->right = std::move($3); $$ = std::move(e); }
    | mul_expr  { $$ = std::move($1); }
    ;

mul_expr
    : mul_expr STAR    unary_expr { auto e = mk<BinaryExpr>(); e->loc = sl(@$, driver); e->op = "*"; e->left = std::move($1); e->right = std::move($3); $$ = std::move(e); }
    | mul_expr SLASH   unary_expr { auto e = mk<BinaryExpr>(); e->loc = sl(@$, driver); e->op = "/"; e->left = std::move($1); e->right = std::move($3); $$ = std::move(e); }
    | mul_expr PERCENT unary_expr { auto e = mk<BinaryExpr>(); e->loc = sl(@$, driver); e->op = "%"; e->left = std::move($1); e->right = std::move($3); $$ = std::move(e); }
    | unary_expr  { $$ = std::move($1); }
    ;

unary_expr
    : BANG       unary_expr  { auto e = mk<UnaryExpr>(); e->loc = sl(@$, driver); e->op = "!"; e->prefix = true; e->operand = std::move($2); $$ = std::move(e); }
    | TILDE      unary_expr  { auto e = mk<UnaryExpr>(); e->loc = sl(@$, driver); e->op = "~"; e->prefix = true; e->operand = std::move($2); $$ = std::move(e); }
    | MINUS      unary_expr %prec UMINUS
                             { auto e = mk<UnaryExpr>(); e->loc = sl(@$, driver); e->op = "-"; e->prefix = true; e->operand = std::move($2); $$ = std::move(e); }
    | PLUS       unary_expr %prec UPLUS
                             { auto e = mk<UnaryExpr>(); e->loc = sl(@$, driver); e->op = "+"; e->prefix = true; e->operand = std::move($2); $$ = std::move(e); }
    | PLUSPLUS   unary_expr  { auto e = mk<UnaryExpr>(); e->loc = sl(@$, driver); e->op = "++"; e->prefix = true; e->operand = std::move($2); $$ = std::move(e); }
    | MINUSMINUS unary_expr  { auto e = mk<UnaryExpr>(); e->loc = sl(@$, driver); e->op = "--"; e->prefix = true; e->operand = std::move($2); $$ = std::move(e); }
    | KW_NOT     unary_expr  { auto e = mk<UnaryExpr>(); e->loc = sl(@$, driver); e->op = "!"; e->prefix = true; e->operand = std::move($2); $$ = std::move(e); }
    | KW_AWAIT   unary_expr
        {
            auto e = mk<AwaitExpr>();
            e->loc = sl(@$, driver);
            e->operand = std::move($2);
            $$ = std::move(e);
        }
    | postfix_expr  { $$ = std::move($1); }
    ;

postfix_expr
    : postfix_expr DOT IDENT
        { auto e = mk<MemberExpr>(); e->loc = sl(@$, driver); e->object = std::move($1); e->member = $3; $$ = std::move(e); }
    | postfix_expr LBRACKET expr RBRACKET
        { auto e = mk<IndexExpr>(); e->loc = sl(@$, driver); e->object = std::move($1); e->index = std::move($3); $$ = std::move(e); }
    | postfix_expr LPAREN arg_list RPAREN
        { auto e = mk<CallExpr>(); e->loc = sl(@$, driver); e->callee = std::move($1); e->args = std::move($3); $$ = std::move(e); }
    | postfix_expr PLUSPLUS
        { auto e = mk<UnaryExpr>(); e->loc = sl(@$, driver); e->op = "++"; e->prefix = false; e->operand = std::move($1); $$ = std::move(e); }
    | postfix_expr MINUSMINUS
        { auto e = mk<UnaryExpr>(); e->loc = sl(@$, driver); e->op = "--"; e->prefix = false; e->operand = std::move($1); $$ = std::move(e); }
    | primary_expr  { $$ = std::move($1); }
    ;

primary_expr
    : INT_LIT
        { auto e = mk<IntLitExpr>(); e->loc = sl(@$, driver); e->value = $1; $$ = std::move(e); }
    | LONG_LIT
        { auto e = mk<LongLitExpr>(); e->loc = sl(@$, driver); e->value = $1; $$ = std::move(e); }
    | FLOAT_LIT
        { auto e = mk<FloatLitExpr>(); e->loc = sl(@$, driver); e->value = $1; $$ = std::move(e); }
    | REAL_LIT
        { auto e = mk<RealLitExpr>(); e->loc = sl(@$, driver); e->value = $1; $$ = std::move(e); }
    | STRING
        { auto e = mk<StringLitExpr>(); e->loc = sl(@$, driver); e->value = $1; $$ = std::move(e); }
    | BOOL_LIT
        { auto e = mk<BoolLitExpr>(); e->loc = sl(@$, driver); e->value = $1; $$ = std::move(e); }
    | KW_NULL
        { auto e = mk<NullLitExpr>(); e->loc = sl(@$, driver); $$ = std::move(e); }
    | IDENT
        { auto e = mk<IdentExpr>(); e->loc = sl(@$, driver); e->name = $1; $$ = std::move(e); }
    | KW_STR
        { auto e = mk<IdentExpr>(); e->loc = sl(@$, driver); e->name = "str"; $$ = std::move(e); }
    | KW_THIS
        { auto e = mk<ThisExpr>(); e->loc = sl(@$, driver); $$ = std::move(e); }
    | KW_SUPER
        { auto e = mk<SuperExpr>(); e->loc = sl(@$, driver); $$ = std::move(e); }
    | LPAREN expr RPAREN          { $$ = std::move($2); }
    | KW_NEW type_expr LPAREN arg_list RPAREN
        { auto e = mk<NewExpr>(); e->loc = sl(@$, driver); e->type = $2; e->args = std::move($4); $$ = std::move(e); }
    | list_lit  { $$ = std::move($1); }
    | dict_lit  { $$ = std::move($1); }
    | KW_FN LPAREN param_list RPAREN FAT_ARROW expr
        {
            auto e = mk<LambdaExpr>(); e->loc = sl(@$, driver);
            e->params = std::move($3);
            auto ret = mk<ReturnStmt>(); ret->loc = sl(@$, driver);
            ret->value = std::move($6);
            auto blk = mk<BlockStmt>(); blk->body.push_back(std::move(ret));
            e->body = std::move(blk);
            $$ = std::move(e);
        }
    | KW_FN LPAREN param_list RPAREN block
        {
            auto e = mk<LambdaExpr>(); e->loc = sl(@$, driver);
            e->params = std::move($3);
            e->body = std::unique_ptr<Stmt>(std::move($5));
            $$ = std::move(e);
        }
    | KW_FN LPAREN param_list RPAREN ARROW type_expr block
        {
            auto e = mk<LambdaExpr>(); e->loc = sl(@$, driver);
            e->params = std::move($3);
            e->explicit_ret = std::move($6);
            e->body = std::unique_ptr<Stmt>(std::move($7));
            $$ = std::move(e);
        }
    | KW_FN LPAREN param_list RPAREN ARROW type_expr FAT_ARROW expr
        {
            auto e = mk<LambdaExpr>(); e->loc = sl(@$, driver);
            e->params = std::move($3);
            e->explicit_ret = std::move($6);
            auto ret = mk<ReturnStmt>(); ret->loc = sl(@$, driver);
            ret->value = std::move($8);
            auto blk = mk<BlockStmt>(); blk->body.push_back(std::move(ret));
            e->body = std::move(blk);
            $$ = std::move(e);
        }
    ;

/* -- List / dict literals ---------------------------------------------- */

list_lit
    : LBRACKET RBRACKET
        { auto e = mk<ListExpr>(); e->loc = sl(@$, driver); $$ = std::move(e); }
    | LBRACKET expr_list RBRACKET
        { auto e = mk<ListExpr>(); e->loc = sl(@$, driver); e->elements = std::move($2); $$ = std::move(e); }
    ;

dict_lit
    : LBRACE dict_pairs RBRACE
        { auto e = mk<DictExpr>(); e->loc = sl(@$, driver); e->pairs = std::move($2); $$ = std::move(e); }
    ;

dict_pairs
    : %empty                              { $$ = PairList(); }
    | expr COLON expr                     { $$.emplace_back(std::move($1), std::move($3)); }
    | dict_pairs COMMA expr COLON expr    { $1.emplace_back(std::move($3), std::move($5)); $$ = std::move($1); }
    | dict_pairs COMMA                    { $$ = std::move($1); }
    ;

expr_list
    : expr
        { $$ = ExprList{}; $$.push_back(std::move($1)); }
    | expr_list COMMA expr
        { $1.push_back(std::move($3)); $$ = std::move($1); }
    ;

arg_list
    : %empty        { $$ = ExprList{}; }
    | arg_list_ne   { $$ = std::move($1); }
    ;

arg_list_ne
    : expr
        { $$ = ExprList{}; $$.push_back(std::move($1)); }
    | arg_list_ne COMMA expr
        { $1.push_back(std::move($3)); $$ = std::move($1); }
    ;

%%

/* ===================================================================== */
void yy::parser::error(const location_type& l, const std::string& msg) {
    driver.error(l, msg);
}
