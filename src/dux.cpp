#include <iostream>
#include "lexer/Scanner.h"

using namespace std;

int main()
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
}
