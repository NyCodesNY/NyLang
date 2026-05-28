#include "Lexer.h"
#include <stdexcept>

namespace nylang {

Lexer::Lexer(const std::string& source)
    : m_source(source), m_pos(0), m_line(1), m_column(1) {}

char Lexer::current() const {
    if (m_pos >= m_source.size()) return '\0';
    return m_source[m_pos];
}

char Lexer::peek() const {
    if (m_pos + 1 >= m_source.size()) return '\0';
    return m_source[m_pos + 1];
}

void Lexer::advance() {
    if (current() == '\n') {
        m_line++;
        m_column = 1;
    } else {
        m_column++;
    }
    m_pos++;
}

void Lexer::skipWhitespace() {
    while (m_pos < m_source.size()) {
        char c = current();
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            advance();
        }
        // Single-line comments: // ...
        else if (c == '/' && peek() == '/') {
            while (m_pos < m_source.size() && current() != '\n') {
                advance();
            }
        }
        else {
            break;
        }
    }
}

Token Lexer::readString() {
    int startLine = m_line;
    int startCol = m_column;

    advance(); // skip opening "

    std::string value;
    while (m_pos < m_source.size() && current() != '"') {
        if (current() == '\\') {
            advance();
            switch (current()) {
                case 'n':  value += '\n'; break;
                case 't':  value += '\t'; break;
                case '\\': value += '\\'; break;
                case '"':  value += '"';  break;
                default:
                    throw std::runtime_error(
                        "Unknown escape sequence at " +
                        std::to_string(m_line) + ":" + std::to_string(m_column));
            }
        } else {
            value += current();
        }
        advance();
    }

    if (m_pos >= m_source.size()) {
        throw std::runtime_error(
            "Unterminated string literal starting at " +
            std::to_string(startLine) + ":" + std::to_string(startCol));
    }

    advance(); // skip closing "
    return Token(TokenType::StringLiteral, value, startLine, startCol);
}

Token Lexer::readNumber() {
    int startLine = m_line;
    int startCol = m_column;
    std::string value;

    while (m_pos < m_source.size() && std::isdigit(current())) {
        value += current();
        advance();
    }

    return Token(TokenType::Number, value, startLine, startCol);
}

Token Lexer::readIdentifierOrKeyword() {
    int startLine = m_line;
    int startCol = m_column;
    std::string value;

    while (m_pos < m_source.size() && (std::isalnum(current()) || current() == '_')) {
        value += current();
        advance();
    }

    // Check for keywords
    if (value == "function") {
        return Token(TokenType::Function, value, startLine, startCol);
    } else if (value == "if") {
        return Token(TokenType::If, value, startLine, startCol);
    } else if (value == "else") {
        return Token(TokenType::Else, value, startLine, startCol);
    } else if (value == "for") {
        return Token(TokenType::For, value, startLine, startCol);
    } else if (value == "while") {
        return Token(TokenType::While, value, startLine, startCol);
    } else if (value == "return") {
        return Token(TokenType::Return, value, startLine, startCol);
    } else if (value == "init") {
        return Token(TokenType::Init, value, startLine, startCol);
    } else if (value == "int") {
        return Token(TokenType::IntKeyword, value, startLine, startCol);
    } else if (value == "string") {
        return Token(TokenType::StringKeyword, value, startLine, startCol);
    } else if (value == "bool") {
        return Token(TokenType::BoolKeyword, value, startLine, startCol);
    } else if (value == "void") {
        return Token(TokenType::VoidKeyword, value, startLine, startCol);
    } else if (value == "true") {
        return Token(TokenType::TrueKeyword, value, startLine, startCol);
    } else if (value == "false") {
        return Token(TokenType::FalseKeyword, value, startLine, startCol);
    }

    return Token(TokenType::Identifier, value, startLine, startCol);
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;

    while (m_pos < m_source.size()) {
        skipWhitespace();
        if (m_pos >= m_source.size()) break;

        int line = m_line;
        int col  = m_column;
        char c   = current();

        switch (c) {
            case '(':
                tokens.emplace_back(TokenType::LParen, "(", line, col);
                advance();
                break;
            case ')':
                tokens.emplace_back(TokenType::RParen, ")", line, col);
                advance();
                break;
            case '{':
                tokens.emplace_back(TokenType::LBrace, "{", line, col);
                advance();
                break;
            case '}':
                tokens.emplace_back(TokenType::RBrace, "}", line, col);
                advance();
                break;
            case ';':
                tokens.emplace_back(TokenType::Semicolon, ";", line, col);
                advance();
                break;
            case ',':
                tokens.emplace_back(TokenType::Comma, ",", line, col);
                advance();
                break;
            case '.':
                tokens.emplace_back(TokenType::Dot, ".", line, col);
                advance();
                break;
            case '=':
                if (peek() == '=') {
                    tokens.emplace_back(TokenType::EqEq, "==", line, col);
                    advance(); advance();
                } else {
                    tokens.emplace_back(TokenType::Eq, "=", line, col);
                    advance();
                }
                break;
            case '!':
                if (peek() == '=') {
                    tokens.emplace_back(TokenType::NotEq, "!=", line, col);
                    advance(); advance();
                } else {
                    tokens.emplace_back(TokenType::Unknown, "!", line, col);
                    advance();
                }
                break;
            case '<':
                tokens.emplace_back(TokenType::Less, "<", line, col);
                advance();
                break;
            case '>':
                tokens.emplace_back(TokenType::Greater, ">", line, col);
                advance();
                break;
            case '&':
                tokens.emplace_back(TokenType::Ampersand, "&", line, col);
                advance();
                break;
            case '+':
                tokens.emplace_back(TokenType::Plus, "+", line, col);
                advance();
                break;
            case '-':
                tokens.emplace_back(TokenType::Minus, "-", line, col);
                advance();
                break;
            case '*':
                tokens.emplace_back(TokenType::Star, "*", line, col);
                advance();
                break;
            case '/':
                // Handle single-line comments in skipWhitespace, so a single '/' is a division token here.
                tokens.emplace_back(TokenType::Slash, "/", line, col);
                advance();
                break;
            case '"':
                tokens.push_back(readString());
                break;
            default:
                if (std::isalpha(c) || c == '_') {
                    tokens.push_back(readIdentifierOrKeyword());
                } else if (std::isdigit(c)) {
                    tokens.push_back(readNumber());
                } else {
                    tokens.emplace_back(TokenType::Unknown, std::string(1, c), line, col);
                    advance();
                }
                break;
        }
    }

    tokens.emplace_back(TokenType::EndOfFile, "", m_line, m_column);
    return tokens;
}

} // namespace nylang
