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

#ifndef Dux_TOKENIZER_H
#define Dux_TOKENIZER_H

extern "C" {

	/* Tokenizer interface */

        #include "token.h"      /* For token types */

        #define MAXINDENT 100   /* Max indentation level */

	enum decoding_state {
		STATE_INIT,
		STATE_RAW,
		STATE_NORMAL        /* have a codec associated with input */
	};

	/* Tokenizer state */
	struct tok_state {
		/* Input state; buf <= cur <= inp <= end */
		/* NB an entire line is held in the buffer */
		char *buf;          /* Input buffer, or NULL; malloc'ed if fp != NULL */
		char *cur;          /* Next character in buffer */
		char *inp;          /* End of data in buffer */
		char *end;          /* End of input buffer if buf != NULL */
		char *start;        /* Start of current token if not NULL */
		int done;           /* E_OK normally, E_EOF at EOF, otherwise error code */
		/* NB If done != E_OK, cur must be == inp!!! */
		FILE *fp;           /* Rest of input; NULL if tokenizing a string */
		int tabsize;        /* Tab spacing */
		int indent;         /* Current indentation index */
		int indstack[MAXINDENT];            /* Stack of indents */
		int atbol;          /* Nonzero if at begin of new line */
		int pendin;         /* Pending indents (if > 0) or dedents (if < 0) */
		const char *prompt, *nextprompt;          /* For interactive prompting */
		int lineno;         /* Current line number */
		int level;          /* () [] {} Parentheses nesting level */

		int altwarning;     /* Issue warning if alternate tabs don't match */
		int alterror;       /* Issue error if alternate tabs don't match */
		int alttabsize;     /* Alternate tab spacing */
		int altindstack[MAXINDENT];         /* Stack of alternate indents */

		enum decoding_state decoding_state;
		int decoding_erred;         /* whether erred in decoding  */
		int read_coding_spec;       /* whether 'coding:...' has been read  */
		char *encoding;         /* Source encoding. */
		int cont_line;          /* whether we are in a continuation line. */
		const char* line_start;     /* pointer to start of current line */

		const char* enc;        /* Encoding for the current str. */
		const char* str;
		const char* input; /* Tokenizer's newline translated copy of the string. */

	};

	extern struct tok_state *DuxTokenizer_FromString(const char *, int);
	extern struct tok_state *DuxTokenizer_FromUTF8(const char *, int);
	extern struct tok_state *DuxTokenizer_FromFile(FILE *, const char*, const char *, const char *);
	extern void DuxTokenizer_Free(struct tok_state *);
	extern int DuxTokenizer_Get(struct tok_state *, char **, char **);
	extern char * DuxTokenizer_RestoreEncoding(struct tok_state* tok, int len, int *offset);

}
#endif /* !Dux_TOKENIZER_H */
