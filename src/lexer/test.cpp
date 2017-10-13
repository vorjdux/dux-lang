#include <iostream>
#include "Lexer.h"

using namespace std;

int main()
{
    dux::Lexer scanner;        // define a Scanner object

    while (int token = scanner.lex())   // get all tokens
    {
        string const &text = scanner.matched();
        switch (token)
        {
            case dux::Lexer::IDENTIFIER:
                cout << "identifier: " << text << '\n';
            break;

            case dux::Lexer::NUMBER:
                cout << "number: " << text << '\n';
            break;

            default:
                cout << "char. token: `" << text << "'\n";
            break;
        }
    }
}
