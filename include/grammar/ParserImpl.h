/**
** This header file is generated in gen/grammar.
** If you need to change it, please change there.
**/
// Generated by Bisonc++ V4.13.01 on Sun, 15 Oct 2017 01:04:49 +0100

    // Include this file in the sources of the class Parser.

// $insert class.h
#include "grammar/Parser.h"

// $insert namespace-open
namespace dux
{

inline void Parser::error(char const *msg)
{
    std::cerr << msg << '\n';
}

// $insert lex
inline int Parser::lex()
{
    return d_scanner.lex();
}

inline void Parser::print()         
{
    print__();           // displays tokens if --print was specified
}

inline void Parser::exceptionHandler__(std::exception const &exc)         
{
    throw;              // re-implement to handle exceptions thrown by actions
}

// $insert namespace-close
}

    // Add here includes that are only required for the compilation 
    // of Parser's sources.


// $insert namespace-use
    // UN-comment the next using-declaration if you want to use
    // symbols from the namespace dux without specifying dux::
//using namespace dux;

    // UN-comment the next using-declaration if you want to use
    // int Parser's sources symbols from the namespace std without
    // specifying std::

//using namespace std;
