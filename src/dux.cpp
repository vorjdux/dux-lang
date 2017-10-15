#include <iostream>
#include "lexer/Scanner.h"
#include "grammar/Parser.h"

using namespace std;

int main(int argc, char **argv)
{

	dux::Scanner scanner;        // define a Scanner object

	while (int token = scanner.lex())   // get all tokens
	{
		string const &text = scanner.matched();
		switch (token)
		{
			case dux::Scanner::IDENTIFIER:
				cout << "identifier: " << text << '\n';
				break;
				
			case dux::Scanner::NUMBER:
				cout << "number: " << text << '\n';
				break;
				
			default:
				cout << "char. token: `" << text << "'\n";
				break;
		}
	}

	dux::Parser parser;
	parser.parse();
}
