#include "Parser.h"
#include <stdexcept>
#include <sstream>

namespace nylang {

Parser::Parser(const std::vector<Token>& tokens)
    : m_tokens(tokens), m_pos(0) {}

const Token& Parser::current() const {
    return m_tokens[m_pos];
}

const Token& Parser::peek() const {
    if (m_pos + 1 < m_tokens.size()) {
        return m_tokens[m_pos + 1];
    }
    return m_tokens.back(); // EOF
}

void Parser::advance() {
    if (m_pos < m_tokens.size() - 1) {
        m_pos++;
    }
}

void Parser::expect(TokenType type, const std::string& context) {
    if (current().type != type) {
        std::ostringstream oss;
        oss << "Parse error at " << current().line << ":" << current().column
            << ": expected " << tokenTypeName(type)
            << " in " << context
            << ", but got " << tokenTypeName(current().type)
            << " (\"" << current().value << "\")";
        throw std::runtime_error(oss.str());
    }
}

// ─── Program ────────────────────────────────────────────────────────────────

std::unique_ptr<Program> Parser::parseProgram() {
    auto program = std::make_unique<Program>();

    while (current().type != TokenType::EndOfFile) {
        if (current().type == TokenType::Function) {
            program->functions.push_back(parseFunctionDeclaration());
        } else {
            std::ostringstream oss;
            oss << "Parse error at " << current().line << ":" << current().column
                << ": unexpected token " << tokenTypeName(current().type)
                << " (\"" << current().value << "\") at top level";
            throw std::runtime_error(oss.str());
        }
    }

    return program;
}

// ─── Function Declaration ───────────────────────────────────────────────────
// Grammar: "function" IDENTIFIER "(" ")" "{" statement* "}"

std::unique_ptr<FunctionDeclaration> Parser::parseFunctionDeclaration() {
    expect(TokenType::Function, "function declaration");
    advance(); // consume "function"

    ExprType returnType = ExprType::Void;
    if (current().type == TokenType::IntKeyword || current().type == TokenType::StringKeyword || current().type == TokenType::BoolKeyword || current().type == TokenType::VoidKeyword) {
        returnType = parseType();
    } else {
        // Assume Void if omitted for backwards compatibility, or maybe we enforce it. 
        // Let's allow omitting for backwards compat with `function Main()`.
        if (current().type == TokenType::Identifier) {
            returnType = ExprType::Void;
        } else {
            throw std::runtime_error("Expected return type or function name.");
        }
    }

    expect(TokenType::Identifier, "function name");
    std::string name = current().value;
    advance(); // consume function name

    expect(TokenType::LParen, "function parameter list");
    advance(); // consume "("

    std::vector<std::pair<ExprType, std::string>> parameters;
    if (current().type != TokenType::RParen) {
        while (true) {
            ExprType paramType = parseType();
            expect(TokenType::Identifier, "parameter name");
            parameters.push_back({paramType, current().value});
            advance();
            if (current().type == TokenType::Comma) {
                advance();
            } else {
                break;
            }
        }
    }

    expect(TokenType::RParen, "function parameter list");
    advance(); // consume ")"

    expect(TokenType::LBrace, "function body");
    advance(); // consume "{"

    std::vector<std::unique_ptr<Statement>> body;
    while (current().type != TokenType::RBrace &&
           current().type != TokenType::EndOfFile) {
        body.push_back(parseStatement());
    }

    expect(TokenType::RBrace, "end of function body");
    advance(); // consume "}"

    return std::make_unique<FunctionDeclaration>(std::move(name), returnType, std::move(parameters), std::move(body));
}

// ─── Types ──────────────────────────────────────────────────────────────────

ExprType Parser::parseType() {
    ExprType baseType = ExprType::Unknown;
    if (current().type == TokenType::IntKeyword) {
        advance();
        baseType = ExprType::Int;
    } else if (current().type == TokenType::StringKeyword) {
        advance();
        baseType = ExprType::String;
    } else if (current().type == TokenType::BoolKeyword) {
        advance();
        baseType = ExprType::Bool;
    } else if (current().type == TokenType::VoidKeyword) {
        advance();
        baseType = ExprType::Void;
    } else if (current().type == TokenType::Init) {
        advance();
        baseType = ExprType::Unknown;
    } else {
        throw std::runtime_error("Expected type (int, string, bool, void, init)");
    }
    
    if (current().type == TokenType::Star) {
        advance();
        if (baseType == ExprType::Int) return ExprType::IntPtr;
        if (baseType == ExprType::String) return ExprType::StringPtr;
        if (baseType == ExprType::Bool) return ExprType::BoolPtr;
        if (baseType == ExprType::Void) return ExprType::VoidPtr;
        throw std::runtime_error("Cannot create pointer to init");
    }
    return baseType;
}

// ─── Statement ──────────────────────────────────────────────────────────────

std::unique_ptr<Statement> Parser::parseStatement() {
    if (current().type == TokenType::IntKeyword || 
        current().type == TokenType::StringKeyword || 
        current().type == TokenType::BoolKeyword ||
        current().type == TokenType::Init) {
        return parseVariableDeclaration();
    }
    if (current().type == TokenType::If) {
        return parseIfStatement();
    }
    if (current().type == TokenType::While) {
        return parseWhileStatement();
    }
    if (current().type == TokenType::Return) {
        return parseReturnStatement();
    }
    if (current().type == TokenType::LBrace) {
        return parseBlock();
    }
    // Currently only: System.Print("...") and System.Free(...)
    if (current().type == TokenType::Identifier && current().value == "System") {
        if (peek().type == TokenType::Dot && m_tokens[m_pos+2].type == TokenType::Identifier) {
            if (m_tokens[m_pos+2].value == "Print") return parsePrintStatement();
            if (m_tokens[m_pos+2].value == "Free") return parseFreeStatement();
        }
    }
    
    // Assignment or expression statement
    if (current().type == TokenType::Identifier || current().type == TokenType::Star) {
        auto expr = parseExpression();
        if (current().type == TokenType::Eq) {
            return parseAssignmentStatement(std::move(expr));
        }
        throw std::runtime_error("Expected assignment");
    }

    std::ostringstream oss;
    oss << "Parse error at " << current().line << ":" << current().column
        << ": unexpected token " << tokenTypeName(current().type)
        << " (\"" << current().value << "\") in statement";
    throw std::runtime_error(oss.str());
}

// ─── Variable Declaration ───────────────────────────────────────────────────
std::unique_ptr<Statement> Parser::parseAssignmentStatement(std::unique_ptr<Expression> left) {
    expect(TokenType::Eq, "assignment");
    advance(); // consume "="
    
    auto right = parseExpression();
    
    expect(TokenType::Semicolon, "end of assignment");
    advance(); // consume ";"
    
    return std::make_unique<AssignmentStatement>(std::move(left), std::move(right));
}

std::unique_ptr<VariableDeclaration> Parser::parseVariableDeclaration() {
    ExprType varType = parseType();

    expect(TokenType::Identifier, "variable name");
    std::string name = current().value;
    advance();

    expect(TokenType::Eq, "variable initialization");
    advance();

    auto initializer = parseExpression();

    expect(TokenType::Semicolon, "end of variable declaration");
    advance();

    return std::make_unique<VariableDeclaration>(std::move(name), varType, std::move(initializer));
}

// ─── Block Statement ────────────────────────────────────────────────────────
std::unique_ptr<BlockStatement> Parser::parseBlock() {
    expect(TokenType::LBrace, "block start");
    advance();

    std::vector<std::unique_ptr<Statement>> stmts;
    while (current().type != TokenType::RBrace && current().type != TokenType::EndOfFile) {
        stmts.push_back(parseStatement());
    }

    expect(TokenType::RBrace, "block end");
    advance();

    return std::make_unique<BlockStatement>(std::move(stmts));
}

// ─── If Statement ───────────────────────────────────────────────────────────
std::unique_ptr<IfStatement> Parser::parseIfStatement() {
    expect(TokenType::If, "if statement");
    advance();

    expect(TokenType::LParen, "if condition");
    advance();

    auto condition = parseExpression();

    expect(TokenType::RParen, "if condition end");
    advance();

    auto thenBranch = parseStatement();
    std::unique_ptr<Statement> elseBranch = nullptr;

    if (current().type == TokenType::Else) {
        advance();
        elseBranch = parseStatement();
    }

    return std::make_unique<IfStatement>(std::move(condition), std::move(thenBranch), std::move(elseBranch));
}

// ─── While Statement ────────────────────────────────────────────────────────
std::unique_ptr<WhileStatement> Parser::parseWhileStatement() {
    expect(TokenType::While, "while statement");
    advance();

    expect(TokenType::LParen, "while condition");
    advance();

    auto condition = parseExpression();

    expect(TokenType::RParen, "while condition end");
    advance();

    auto body = parseStatement();

    return std::make_unique<WhileStatement>(std::move(condition), std::move(body));
}

// ─── Print Statement ────────────────────────────────────────────────────────
// Grammar: "System" "." "Print" "(" expression ")" ";"

std::unique_ptr<PrintStatement> Parser::parsePrintStatement() {
    expect(TokenType::Identifier, "System.Print");
    advance(); // consume "System"

    expect(TokenType::Dot, "System.Print");
    advance(); // consume "."

    expect(TokenType::Identifier, "System.Print");
    advance(); // consume "Print"

    expect(TokenType::LParen, "System.Print arguments");
    advance(); // consume "("

    auto expr = parseExpression();

    expect(TokenType::RParen, "System.Print arguments");
    advance(); // consume ")"

    expect(TokenType::Semicolon, "end of statement");
    advance(); // consume ";"

    return std::make_unique<PrintStatement>(std::move(expr));
}

// ─── Free Statement ───────────────────────────────────────────────────────────
// Grammar: "System" "." "Free" "(" expression ")" ";"

std::unique_ptr<FreeStatement> Parser::parseFreeStatement() {
    expect(TokenType::Identifier, "System.Free");
    advance(); // consume "System"

    expect(TokenType::Dot, "System.Free");
    advance(); // consume "."

    expect(TokenType::Identifier, "System.Free");
    advance(); // consume "Free"

    expect(TokenType::LParen, "System.Free arguments");
    advance(); // consume "("

    auto expr = parseExpression();

    expect(TokenType::RParen, "System.Free arguments");
    advance(); // consume ")"

    expect(TokenType::Semicolon, "end of statement");
    advance(); // consume ";"

    return std::make_unique<FreeStatement>(std::move(expr));
}

// ─── Return Statement ───────────────────────────────────────────────────────

std::unique_ptr<ReturnStatement> Parser::parseReturnStatement() {
    expect(TokenType::Return, "return statement");
    advance(); // consume "return"

    auto expr = parseExpression();

    expect(TokenType::Semicolon, "end of statement");
    advance(); // consume ";"

    return std::make_unique<ReturnStatement>(std::move(expr));
}

// ─── Expressions ────────────────────────────────────────────────────────────

std::unique_ptr<Expression> Parser::parseExpression() {
    auto left = parseTerm();

    // Parse optional binary operator for comparisons (lowest precedence)
    while (current().type == TokenType::EqEq ||
           current().type == TokenType::NotEq ||
           current().type == TokenType::Less ||
           current().type == TokenType::Greater) {
        TokenType op = current().type;
        advance();
        auto right = parseTerm();
        left = std::make_unique<BinaryExpression>(std::move(left), op, std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parseTerm() {
    auto left = parseFactor();

    // Parse + and - (medium precedence)
    while (current().type == TokenType::Plus ||
           current().type == TokenType::Minus) {
        TokenType op = current().type;
        advance();
        auto right = parseFactor();
        left = std::make_unique<BinaryExpression>(std::move(left), op, std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parseFactor() {
    auto left = parseUnary();

    // Parse * and / (highest precedence)
    while (current().type == TokenType::Star ||
           current().type == TokenType::Slash) {
        TokenType op = current().type;
        advance();
        auto right = parseUnary();
        left = std::make_unique<BinaryExpression>(std::move(left), op, std::move(right));
    }

    return left;
}

std::unique_ptr<Expression> Parser::parseUnary() {
    if (current().type == TokenType::Ampersand) {
        advance();
        return std::make_unique<AddressOfExpression>(parseUnary());
    }
    if (current().type == TokenType::Star) {
        advance();
        return std::make_unique<DereferenceExpression>(parseUnary());
    }
    return parsePrimary();
}

std::unique_ptr<Expression> Parser::parsePrimary() {
    if (current().type == TokenType::Number) {
        int val = std::stoi(current().value);
        advance();
        return std::make_unique<IntegerLiteral>(val);
    }
    if (current().type == TokenType::TrueKeyword) {
        advance();
        return std::make_unique<BooleanLiteral>(true);
    }
    if (current().type == TokenType::FalseKeyword) {
        advance();
        return std::make_unique<BooleanLiteral>(false);
    }
    if (current().type == TokenType::StringLiteral) {
        std::string val = current().value;
        advance();
        return std::make_unique<StringLiteral>(std::move(val));
    }
    if (current().type == TokenType::Identifier) {
        std::string name = current().value;
        advance();
        
        if (name == "System") {
            if (current().type == TokenType::Dot) {
                advance(); // consume '.'
                
                if (current().type == TokenType::Identifier && current().value == "GetInput") {
                    advance(); // consume 'GetInput'
                    
                    expect(TokenType::LParen, "GetInput args");
                    advance();
                    expect(TokenType::RParen, "GetInput args");
                    advance();
                    
                    return std::make_unique<GetInputExpression>();
                }
                
                if (current().type == TokenType::Identifier && current().value == "Alloc") {
                    advance(); // consume 'Alloc'
                    
                    expect(TokenType::LParen, "Alloc args");
                    advance();
                    auto sizeExpr = parseExpression();
                    expect(TokenType::RParen, "Alloc args");
                    advance();
                    
                    return std::make_unique<AllocExpression>(std::move(sizeExpr));
                }
                
                throw std::runtime_error("Parser: unknown System method in expression.");
            }
        }
        
        if (current().type == TokenType::LParen) {
            advance(); // consume "("
            std::vector<std::unique_ptr<Expression>> args;
            if (current().type != TokenType::RParen) {
                while (true) {
                    args.push_back(parseExpression());
                    if (current().type == TokenType::Comma) {
                        advance();
                    } else {
                        break;
                    }
                }
            }
            expect(TokenType::RParen, "function arguments");
            advance(); // consume ")"
            return std::make_unique<CallExpression>(name, std::move(args));
        }
        
        return std::make_unique<VariableExpression>(name);
    }
    if (current().type == TokenType::LParen) {
        advance();
        auto expr = parseExpression();
        expect(TokenType::RParen, "expression");
        advance();
        return expr;
    }

    std::ostringstream oss;
    oss << "Parse error at " << current().line << ":" << current().column
        << ": expected expression, got " << tokenTypeName(current().type)
        << " (\"" << current().value << "\")";
    throw std::runtime_error(oss.str());
}

} // namespace nylang
