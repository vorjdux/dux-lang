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

#ifndef Dux_PARSER_H
#define Dux_PARSER_H

extern "C" {

	/* Parser interface */

#define MAXSTACK 1500

	typedef struct {
		int		 s_state;	/* State in current DFA */
		dfa		*s_dfa;		/* Current DFA */
		struct _node	*s_parent;	/* Where to add next node */
	} stackentry;

	typedef struct {
		stackentry	*s_top;		/* Top entry */
		stackentry	 s_base[MAXSTACK];/* Array of stack entries */
	} stack;

	typedef struct {
		stack	 	p_stack;	/* Stack of parser states */
		grammar		*p_grammar;	/* Grammar to use */
		node		*p_tree;	/* Top of parse tree */
	} parser_state;

	parser_state *DuxParser_New(grammar *g, int start);
	void DuxParser_Delete(parser_state *ps);
	int DuxParser_AddToken(parser_state *ps, int type, char *str, int lineno, int col_offset, int *expected_ret);
	void DuxGrammar_AddAccelerators(grammar *g);

}
#endif /* !Dux_PARSER_H */
