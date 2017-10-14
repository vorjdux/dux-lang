/**
** This header file is generated in gen/lexer.
** If you need to change it, please change there.
**/
// Generated by Flexc++ V2.03.04 on Sat, 14 Oct 2017 19:33:49 +0100

#ifndef duxScanner_H_INCLUDED_
#define duxScanner_H_INCLUDED_

// $insert baseclass_h
#include "ScannerBase.h"

// $insert namespace-open
namespace dux
{

// $insert classHead
class Scanner: public ScannerBase
{
    public:
        explicit Scanner(std::istream &in = std::cin,
                                std::ostream &out = std::cout);

        Scanner(std::string const &infile, std::string const &outfile);
        
        // $insert lexFunctionDecl
        int lex();

	enum Tokens
        {
            IDENTIFIER = 0x100,
            NUMBER
        };

    private:
        int lex__();
        int executeAction__(size_t ruleNr);

        void print();
        void preCode();     // re-implement this function for code that must 
                            // be exec'ed before the patternmatching starts

        void postCode(PostEnum__ type);    
                            // re-implement this function for code that must 
                            // be exec'ed after the rules's actions.
};

// $insert scannerConstructors
inline Scanner::Scanner(std::istream &in, std::ostream &out)
:
    ScannerBase(in, out)
{}

inline Scanner::Scanner(std::string const &infile, std::string const &outfile)
:
    ScannerBase(infile, outfile)
{}

// $insert inlineLexFunction
inline int Scanner::lex()
{
    return lex__();
}

inline void Scanner::preCode() 
{
    // optionally replace by your own code
}

inline void Scanner::postCode(PostEnum__ type) 
{
    // optionally replace by your own code
}

inline void Scanner::print() 
{
    print__();
}

// $insert namespace-close
}

#endif // Scanner_H_INCLUDED_
