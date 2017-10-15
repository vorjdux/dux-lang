// Generated by Bisonc++ V4.13.01 on Sun, 15 Oct 2017 01:04:49 +0100

#ifndef duxParser_h_included
#define duxParser_h_included

// $insert baseclass
#include "ParserBase.h"
// $insert scanner.h
#include "../lexer/Scanner.h"

// $insert namespace-open
namespace dux
{

#undef Parser
class Parser: public ParserBase
{
    // $insert scannerobject
    Scanner d_scanner;
        
    public:
        int parse();

    private:
        void error(char const *msg);    // called on (syntax) errors
        int lex();                      // returns the next token from the
                                        // lexical scanner. 
        void print();                   // use, e.g., d_token, d_loc

    // support functions for parse():
        void executeAction(int ruleNr);
        void errorRecovery();
        int lookup(bool recovery);
        void nextToken();
        void print__();
        void exceptionHandler__(std::exception const &exc);
};

// $insert namespace-close
}

#endif