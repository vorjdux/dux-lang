%namespace dux

%baseclass-preinclude DUXinc.h
%baseclass-header DUXTokenizerBase.h
%class-header DUXTokenizer.h
%implementation-header DUXTokenizerimpl.h
%class-name DUXTokenizer
%parsefun-source DUXTokenizer.cpp

%scanner DUXLexer.h

//%debug
%no-lines

%token STRING BOOLEAN INTEGER DOUBLE NIL LAMBDA
%left LCB RCB
%left LB RB
%left COMMA
%left COLON

%%