#include <iostream>
#include "lexer/Scanner.h"
#include "grammar/Parser.h"

using namespace std;

int main(int argc, char **argv)
{

	/*dux::Scanner scanner;        // define a Scanner object

	while (int token = scanner.lex())   // get all tokens
	{
		string const &text = scanner.matched();
		switch (token)
		{
			case dux::Parser::IDENTIFIER:
				cout << "identifier: " << text << endl;
				break;
				
			case dux::Parser::INT:
				cout << "integer: " << text << endl;
				break;

			case dux::Parser::DOUBLE:
				cout << "double: " << text << endl;
				break;

			case dux::Parser::NEW_LINE:
				cout << "new linw: " << text << endl;
				break;

			case dux::Parser::SPACE:
				cout << "space: " << text << endl;
				break;

			case dux::Parser::TAB:
				cout << "tab: " << text << endl;
				break;
				
			default:
				cout << "hey char. token: `" << text << endl;
				break;
		}
		}*/

	dux::Parser parser;
	parser.parse();
}
