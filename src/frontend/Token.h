#pragma once

#include <string>
#include <ostream>

namespace nylang {

enum class TokenType {
    // Keywords
    Function,       // "function"
    If,             // "if"
    Else,           // "else"
    For,            // "for"
    While,          // "while"
    Return,         // "return"
    Init,           // "init" (auto type inference)
    IntKeyword,     // "int"
    StringKeyword,  // "string"
    BoolKeyword,    // "bool"
    VoidKeyword,    // "void"
    TrueKeyword,    // "true"
    FalseKeyword,   // "false"

    // Identifiers & Literals
    Identifier,     // e.g. myFunction, System, Print
    StringLiteral,  // e.g. "Hello"
    Number,         // e.g. 123

    // Symbols
    LParen,         // (
    RParen,         // )
    LBrace,         // {
    RBrace,         // }
    Semicolon,      // ;
    Comma,          // ,
    Dot,            // .
    Eq,             // =
    EqEq,           // ==
    NotEq,          // !=
    Less,           // <
    Greater,        // >
    Ampersand,      // &
    Plus,           // +
    Minus,          // -
    Star,           // *
    Slash,          // /

    // Special
    EndOfFile,
    Unknown,
};

struct Token {
    TokenType type;
    std::string value;
    int line;
    int column;

    Token(TokenType type, std::string value, int line, int column)
        : type(type), value(std::move(value)), line(line), column(column) {}
};

inline const char* tokenTypeName(TokenType t) {
    switch (t) {
        case TokenType::Function:      return "Function";
        case TokenType::If:            return "If";
        case TokenType::Else:          return "Else";
        case TokenType::For:           return "For";
        case TokenType::While:         return "While";
        case TokenType::Return:        return "Return";
        case TokenType::Init:          return "Init";
        case TokenType::IntKeyword:    return "IntKeyword";
        case TokenType::StringKeyword: return "StringKeyword";
        case TokenType::BoolKeyword:   return "BoolKeyword";
        case TokenType::VoidKeyword:   return "VoidKeyword";
        case TokenType::TrueKeyword:   return "TrueKeyword";
        case TokenType::FalseKeyword:  return "FalseKeyword";
        case TokenType::Identifier:    return "Identifier";
        case TokenType::StringLiteral: return "StringLiteral";
        case TokenType::Number:        return "Number";
        case TokenType::LParen:        return "LParen";
        case TokenType::RParen:        return "RParen";
        case TokenType::LBrace:        return "LBrace";
        case TokenType::RBrace:        return "RBrace";
        case TokenType::Semicolon:     return "Semicolon";
        case TokenType::Comma:         return "Comma";
        case TokenType::Dot:           return "Dot";
        case TokenType::Eq:            return "Eq";
        case TokenType::EqEq:          return "EqEq";
        case TokenType::NotEq:         return "NotEq";
        case TokenType::Less:          return "Less";
        case TokenType::Greater:       return "Greater";
        case TokenType::Ampersand:     return "Ampersand";
        case TokenType::Plus:          return "Plus";
        case TokenType::Minus:         return "Minus";
        case TokenType::Star:          return "Star";
        case TokenType::Slash:         return "Slash";
        case TokenType::EndOfFile:     return "EndOfFile";
        case TokenType::Unknown:       return "Unknown";
        default:                       return "???";
    }
}

inline std::ostream& operator<<(std::ostream& os, const Token& tok) {
    os << tokenTypeName(tok.type) << "(\"" << tok.value << "\") at "
       << tok.line << ":" << tok.column;
    return os;
}

} // namespace nylang
