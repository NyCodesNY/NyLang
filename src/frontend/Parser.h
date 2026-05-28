#pragma once

#include "Token.h"
#include "AST.h"
#include <vector>
#include <memory>

namespace nylang {

class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens);

    /// Parse the full token stream into a Program AST.
    std::unique_ptr<Program> parseProgram();

private:
    std::vector<Token> m_tokens;
    size_t m_pos;

    const Token& current() const;
    const Token& peek() const;
    void advance();
    void expect(TokenType type, const std::string& context);

    std::unique_ptr<FunctionDeclaration> parseFunctionDeclaration();
    ExprType parseType();
    std::unique_ptr<Statement> parseStatement();
    std::unique_ptr<Statement> parseAssignmentStatement(std::unique_ptr<Expression> left);
    std::unique_ptr<VariableDeclaration> parseVariableDeclaration();
    std::unique_ptr<BlockStatement> parseBlock();
    std::unique_ptr<IfStatement> parseIfStatement();
    std::unique_ptr<WhileStatement> parseWhileStatement();
    std::unique_ptr<ForStatement> parseForStatement();
    std::unique_ptr<PrintStatement> parsePrintStatement();
    std::unique_ptr<FreeStatement> parseFreeStatement();
    std::unique_ptr<ReturnStatement> parseReturnStatement();

    std::unique_ptr<Expression> parseExpression();
    std::unique_ptr<Expression> parseTerm();
    std::unique_ptr<Expression> parseFactor();
    std::unique_ptr<Expression> parseUnary();
    std::unique_ptr<Expression> parsePrimary();
};

} // namespace nylang
