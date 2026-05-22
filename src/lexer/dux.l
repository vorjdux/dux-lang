%{
/*
 * Dux language lexer
 *
 * Automatic semicolon insertion (Go-style):
 * After: identifier, literal, this/super/true/false/null, ), ], }, ++, --
 * a SEMI token is injected on the next newline.
 */
#include "driver/driver.hpp"
#include "parser.hpp"

/*
 * YY_DECL — the signature of the yylex function bison will call.
 * 'loc' is threaded in as a reference so lexer and parser share it.
 */
#define YY_DECL \
    yy::parser::symbol_type yylex(Driver& driver, yy::location& loc)

/*
 * Called before every matched rule's action.
 * Advances the end-of-token column position.
 */
#define YY_USER_ACTION  loc.columns(yyleng);

static bool need_semi = false;
static int  nesting   = 0;   /* depth of ( [ — suppress SEMI when > 0 */
%}

%option noyywrap nounput noinput batch
%option yylineno

%x ML_COMMENT
%x STR_SQ
%x STR_DQ

DIGIT   [0-9]
ALPHA   [a-zA-Z_]
ALNUM   [a-zA-Z0-9_]

%%

[ \t\r]+    { loc.step(); }

\n+         {
                loc.lines(yyleng);
                loc.step();
                if (need_semi && nesting == 0) {
                    need_semi = false;
                    return yy::parser::make_SEMI(loc);
                }
                need_semi = false;
            }

"#"[^\n]*   { loc.step(); }

"\"\"\""    { BEGIN(ML_COMMENT); }
<ML_COMMENT>{
    "\"\"\""    { BEGIN(INITIAL); }
    \n          { loc.lines(1); }
    .           { }
    <<EOF>>     { throw yy::parser::syntax_error(loc, "unterminated multi-line comment"); }
}

"\""        { driver.string_buf().clear(); BEGIN(STR_DQ); }
<STR_DQ>{
    "\""    { BEGIN(INITIAL); need_semi = true; return yy::parser::make_STRING(driver.string_buf(), loc); }
    "\\n"   { driver.string_buf() += '\n'; }
    "\\t"   { driver.string_buf() += '\t'; }
    "\\r"   { driver.string_buf() += '\r'; }
    "\\\\"  { driver.string_buf() += '\\'; }
    "\\\""  { driver.string_buf() += '"';  }
    "\\'"   { driver.string_buf() += '\''; }
    "\\{"   { driver.string_buf() += '{';  }
    \n      { driver.string_buf() += '\n'; loc.lines(1); }
    .       { driver.string_buf() += yytext[0]; }
    <<EOF>> { throw yy::parser::syntax_error(loc, "unterminated string"); }
}

"'"         { driver.string_buf().clear(); BEGIN(STR_SQ); }
<STR_SQ>{
    "'"     { BEGIN(INITIAL); need_semi = true; return yy::parser::make_STRING(driver.string_buf(), loc); }
    "\\n"   { driver.string_buf() += '\n'; }
    "\\t"   { driver.string_buf() += '\t'; }
    "\\r"   { driver.string_buf() += '\r'; }
    "\\\\"  { driver.string_buf() += '\\'; }
    "\\\""  { driver.string_buf() += '"';  }
    "\\'"   { driver.string_buf() += '\''; }
    "\\{"   { driver.string_buf() += '{';  }
    \n      { driver.string_buf() += '\n'; loc.lines(1); }
    .       { driver.string_buf() += yytext[0]; }
    <<EOF>> { throw yy::parser::syntax_error(loc, "unterminated string"); }
}

"namespace"  { need_semi = false; return yy::parser::make_KW_NAMESPACE(loc); }
"import"     { need_semi = false; return yy::parser::make_KW_IMPORT(loc);    }
"from"       { need_semi = false; return yy::parser::make_KW_FROM(loc);      }
"as"         { need_semi = false; return yy::parser::make_KW_AS(loc);        }
"class"      { need_semi = false; return yy::parser::make_KW_CLASS(loc);     }
"interface"  { need_semi = false; return yy::parser::make_KW_INTERFACE(loc); }
"public"     { need_semi = false; return yy::parser::make_KW_PUBLIC(loc);    }
"private"    { need_semi = false; return yy::parser::make_KW_PRIVATE(loc);   }
"protected"  { need_semi = false; return yy::parser::make_KW_PROTECTED(loc); }
"new"        { need_semi = false; return yy::parser::make_KW_NEW(loc);       }
"delete"     { need_semi = false; return yy::parser::make_KW_DELETE(loc);    }
"this"       { need_semi = true;  return yy::parser::make_KW_THIS(loc);      }
"super"      { need_semi = true;  return yy::parser::make_KW_SUPER(loc);     }
"return"     { need_semi = true;  return yy::parser::make_KW_RETURN(loc);    }
"break"      { need_semi = true;  return yy::parser::make_KW_BREAK(loc);     }
"continue"   { need_semi = true;  return yy::parser::make_KW_CONTINUE(loc);  }
"if"         { need_semi = false; return yy::parser::make_KW_IF(loc);        }
"else"       { need_semi = false; return yy::parser::make_KW_ELSE(loc);      }
"for"        { need_semi = false; return yy::parser::make_KW_FOR(loc);       }
"while"      { need_semi = false; return yy::parser::make_KW_WHILE(loc);     }
"do"         { need_semi = false; return yy::parser::make_KW_DO(loc);        }
"switch"     { need_semi = false; return yy::parser::make_KW_SWITCH(loc);    }
"case"       { need_semi = false; return yy::parser::make_KW_CASE(loc);      }
"default"    { need_semi = false; return yy::parser::make_KW_DEFAULT(loc);   }
"try"        { need_semi = false; return yy::parser::make_KW_TRY(loc);       }
"catch"      { need_semi = false; return yy::parser::make_KW_CATCH(loc);     }
"in"         { need_semi = false; return yy::parser::make_KW_IN(loc);        }
"const"      { need_semi = false; return yy::parser::make_KW_CONST(loc);     }
"static"     { need_semi = false; return yy::parser::make_KW_STATIC(loc);    }
"get"        { need_semi = true;  return yy::parser::make_KW_GET(loc);       }
"set"        { need_semi = true;  return yy::parser::make_KW_SET(loc);       }
"assert"     { need_semi = false; return yy::parser::make_KW_ASSERT(loc);    }
"fn"         { need_semi = false; return yy::parser::make_KW_FN(loc);        }
"defer"      { need_semi = false; return yy::parser::make_KW_DEFER(loc);     }
"throw"      { need_semi = false; return yy::parser::make_KW_THROW(loc);     }
"unsafe"     { need_semi = false; return yy::parser::make_KW_UNSAFE(loc);    }
"extern"     { need_semi = false; return yy::parser::make_KW_EXTERN(loc);    }
"and"        { need_semi = false; return yy::parser::make_KW_AND(loc);       }
"or"         { need_semi = false; return yy::parser::make_KW_OR(loc);        }
"not"        { need_semi = false; return yy::parser::make_KW_NOT(loc);       }
"true"       { need_semi = true;  return yy::parser::make_BOOL_LIT(true,  loc); }
"false"      { need_semi = true;  return yy::parser::make_BOOL_LIT(false, loc); }
"null"       { need_semi = true;  return yy::parser::make_KW_NULL(loc);      }

"void"       { need_semi = false; return yy::parser::make_KW_VOID(loc);   }
"int"        { need_semi = false; return yy::parser::make_KW_INT(loc);    }
"long"       { need_semi = false; return yy::parser::make_KW_LONG(loc);   }
"real"       { need_semi = false; return yy::parser::make_KW_REAL(loc);   }
"double"     { need_semi = false; return yy::parser::make_KW_DOUBLE(loc); }
"str"        { need_semi = false; return yy::parser::make_KW_STR(loc);    }
"bool"       { need_semi = false; return yy::parser::make_KW_BOOL(loc);   }
"list"       { need_semi = false; return yy::parser::make_KW_LIST(loc);   }
"dict"       { need_semi = false; return yy::parser::make_KW_DICT(loc);   }
"tuple"      { need_semi = false; return yy::parser::make_KW_TUPLE(loc);  }
"object"     { need_semi = false; return yy::parser::make_KW_OBJECT(loc); }
"ptr"        { need_semi = false; return yy::parser::make_KW_PTR(loc);    }

{DIGIT}+"."{DIGIT}+|"."{DIGIT}+  {
    need_semi = true;
    return yy::parser::make_FLOAT_LIT(std::stod(yytext), loc);
}

{DIGIT}+  {
    need_semi = true;
    return yy::parser::make_INT_LIT(std::stoll(yytext), loc);
}

{ALPHA}{ALNUM}*  {
    need_semi = true;
    return yy::parser::make_IDENT(std::string(yytext), loc);
}

"..."  { need_semi = false; return yy::parser::make_ELLIPSIS(loc);       }
"..<"  { need_semi = false; return yy::parser::make_RANGE_EXCL(loc);     }
"..="  { need_semi = false; return yy::parser::make_RANGE_INCL(loc);     }
"::"   { need_semi = false; return yy::parser::make_DCOLON(loc);         }
"->"   { need_semi = false; return yy::parser::make_ARROW(loc);          }
"=>"   { need_semi = false; return yy::parser::make_FAT_ARROW(loc);      }
"++"   { need_semi = true;  return yy::parser::make_PLUSPLUS(loc);       }
"--"   { need_semi = true;  return yy::parser::make_MINUSMINUS(loc);     }
"=="   { need_semi = false; return yy::parser::make_EQEQ(loc);           }
"!="   { need_semi = false; return yy::parser::make_NEQ(loc);            }
"<="   { need_semi = false; return yy::parser::make_LEQ(loc);            }
">="   { need_semi = false; return yy::parser::make_GEQ(loc);            }
"&&"   { need_semi = false; return yy::parser::make_AMPAMP(loc);         }
"||"   { need_semi = false; return yy::parser::make_PIPEPIPE(loc);       }
"+="   { need_semi = false; return yy::parser::make_PLUS_ASSIGN(loc);    }
"-="   { need_semi = false; return yy::parser::make_MINUS_ASSIGN(loc);   }
"*="   { need_semi = false; return yy::parser::make_STAR_ASSIGN(loc);    }
"/="   { need_semi = false; return yy::parser::make_SLASH_ASSIGN(loc);   }
"%="   { need_semi = false; return yy::parser::make_PERCENT_ASSIGN(loc); }

"+"  { need_semi = false; return yy::parser::make_PLUS(loc);     }
"-"  { need_semi = false; return yy::parser::make_MINUS(loc);    }
"*"  { need_semi = false; return yy::parser::make_STAR(loc);     }
"/"  { need_semi = false; return yy::parser::make_SLASH(loc);    }
"%"  { need_semi = false; return yy::parser::make_PERCENT(loc);  }
"<"  { need_semi = false; return yy::parser::make_LT(loc);       }
">"  { need_semi = false; return yy::parser::make_GT(loc);       }
"="  { need_semi = false; return yy::parser::make_ASSIGN(loc);   }
"!"  { need_semi = false; return yy::parser::make_BANG(loc);     }
"~"  { need_semi = false; return yy::parser::make_TILDE(loc);    }
"@"  { need_semi = false; return yy::parser::make_AT(loc);       }
"&"  { need_semi = false; return yy::parser::make_AMP(loc);      }
"."  { need_semi = false; return yy::parser::make_DOT(loc);      }
","  { need_semi = false; return yy::parser::make_COMMA(loc);    }
";"  { need_semi = false; return yy::parser::make_SEMI(loc);     }
":"  { need_semi = false; return yy::parser::make_COLON(loc);    }
"("  { need_semi = false; nesting++; return yy::parser::make_LPAREN(loc);   }
")"  { need_semi = true;  nesting--; return yy::parser::make_RPAREN(loc);   }
"{"  { need_semi = false;            return yy::parser::make_LBRACE(loc);   }
"}"  { need_semi = true;             return yy::parser::make_RBRACE(loc);   }
"["  { need_semi = false; nesting++; return yy::parser::make_LBRACKET(loc); }
"]"  { need_semi = true;  nesting--; return yy::parser::make_RBRACKET(loc); }

<<EOF>>  {
    if (need_semi && nesting == 0) {
        need_semi = false;
        return yy::parser::make_SEMI(loc);
    }
    return yy::parser::make_YYEOF(loc);
}

.  {
    throw yy::parser::syntax_error(loc,
        std::string("unexpected character: '") + yytext[0] + '\'');
}

%%
