#pragma once

#include "Token.h"
#include <string>
#include <vector>

namespace nylang {

class Lexer {
public:
    explicit Lexer(const std::string& source);

    /// Tokenize the entire source and return all tokens.
    std::vector<Token> tokenize();

private:
    std::string m_source;
    size_t m_pos;
    int m_line;
    int m_column;

    char current() const;
    char peek() const;
    void advance();
    void skipWhitespace();
    Token readString();
    Token readNumber();
    Token readIdentifierOrKeyword();
};

} // namespace nylang
