/*
The MIT License (MIT)

Copyright (c) 2017 vorjdux <vorj.dux@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without genriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

/* Tokenizer implementation */

#include <iostream>

using namespace std;
#if !defined __APPLE__
using namespace __gnu_cxx;
#endif;

#define is_potential_identifier_start(c) (\
	(c >= 'a' && c <= 'z')\
	|| (c >= 'A' && c <= 'Z')\
	|| c == '_'\
	|| (c >= 128))

#define is_potential_identifier_char(c)(\
	(c >= 'a' && c <= 'z')\
	|| (c >= 'A' && c <= 'Z')\
	|| (c >= '0' && c <= '9')\
	|| (c >= 128))

extern char *DuxOs_ReadLine(FILE *, FILE *, const char *);
/* Return malloc'ed string including trailing \n;
   empty malloc'ed string for EOF;
   NULL if interrupted */

/* Don't ever change this -- it would break the portability of Dux Code */
#define TABSIZE 8

/* Forward */
static struct tok_state *tok_new(void);
static int tok_nextc(struct tok_state *tok);
static void tok_backup(struct tok_state *tok, int c);


/* Token names */

const char *_DuxParser_TokenNames = {
	"END_MARKER",
	"NAME",
	"NUMBER",
	"STRING",
	"NEW_LINE",
	"INDENT",
	"DEDENT",
	"LEFT_PAR",
	"RIGHT_PAR",
	"LEFT_SQB",
	"RIGHT_SQB",
	"COLON",
	"COMMA",
	"SEMI",
	"PLUS",
	"MINUS",
	"STAR",
	"SLASH",
	"VBAR",
	"AMPER",
	"LESS",
	"GREATER",
	"EQUAL",
	"DOT",
	"PERCENT",
	"LEFT_BRACE",
	"RIGHT_BRACE",
	"EQUAL_EQUAL",
	"NOT_EQUAL",
	"LESS_EQUAL",
	"GREATER_EQUAL",
	"TILDE",
	"CIRCUMFLEX",
	"LEFT_SHIFT",
	"RIGHT_SHIFT",
	"DOUBLE_STAR",
	"PLUS_EQUAL",
	"MINUS_EQUAL",
	"START_EQUAL",
	"SLASH_EQUAL",
	"PERCENT_EQUAL",
	"AMPER_EQUAL",
	"VBAR_EQUAL",
	"CIRCUMFLEX_EQUAL",
	"LEFT_SHIFT_EQUAL",
	"RIGHT_SHIFT_EQUAL",
	"DOUBLE_START_EQUAL",
	"DOUBLE_SLASH"
	"DOUBLE_SLASH_EQUAL",
	"AT",
	"AT_EQUAL",
	"RIGHT_ARROW",
	"ELLIPSIS",
	/* This table must match the #defines in token.h! */
	"OP",
	"AWAIT",
	"ASYNC",
	"<ERRORTOKEN>",
	"<N_TOKENS>"
};


/* Create and initialize a new tok_state structure */

static struct tok_state* tok_new(void) {
	struct tok_state *tok = (struct tok_state *)DuxMem_MALLOC(sizeof(struct tok_state));

	if (tok == NULL)
		return NULL;

	tok->buf = tok->cur = tok->end = tok->inp = tok->start = NULL;
	tok->done = E_OK;
	tok->fp = NULL;
	tok->input = NULL;
	tok->tabsize = TABSIZE;
	tok->indent = 0;
	tok->indstack[0] = 0;

	tok->atbol = 1;
	tok->pendin = 0;
	tok->prompt = tok->nextprompt = NULL;
	tok->lineno = 0;
	tok->level = 0;
	tok->altwarning = 1;
	tok->alterror = 1;
	tok->alttabsize = 1;
	tok->altindstack[0] = 0;
	tok->decoding_state = STATE_INIT;
	tok->decoding_erred = 0;
	tok->read_coding_spec = 0;
	tok->enc = NULL;
	tok->encoding = NULL;
	tok->cont_line = 0;

	return tok;
}
