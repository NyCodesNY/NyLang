#pragma once

#include "Token.h"
#include <string>
#include <vector>
#include <memory>

namespace nylang {

// ─── Base AST Node ──────────────────────────────────────────────────────────

class ASTNode {
public:
    virtual ~ASTNode() = default;
};

// ─── Types ──────────────────────────────────────────────────────────────────

enum class ExprType {
    Unknown,
    Int,
    String,
    Bool,
    Void,
    IntPtr,
    StringPtr,
    BoolPtr,
    VoidPtr
};

// ─── Expressions ────────────────────────────────────────────────────────────

class Expression : public ASTNode {
public:
    ExprType type = ExprType::Unknown;
};

class IntegerLiteral : public Expression {
public:
    int value;
    explicit IntegerLiteral(int value) : value(value) {
        type = ExprType::Int;
    }
};

class StringLiteral : public Expression {
public:
    std::string value;
    explicit StringLiteral(std::string value) : value(std::move(value)) {
        type = ExprType::String;
    }
};

class BooleanLiteral : public Expression {
public:
    bool value;
    explicit BooleanLiteral(bool value) : value(value) {
        type = ExprType::Bool;
    }
};

class VariableExpression : public Expression {
public:
    std::string name;
    explicit VariableExpression(std::string name) : name(std::move(name)) {}
};

class GetInputExpression : public Expression {
public:
    GetInputExpression() {
        type = ExprType::String;
    }
};

class CallExpression : public Expression {
public:
    std::string callee;
    std::vector<std::unique_ptr<Expression>> arguments;

    CallExpression(std::string callee, std::vector<std::unique_ptr<Expression>> arguments)
        : callee(std::move(callee)), arguments(std::move(arguments)) {}
};

class BinaryExpression : public Expression {
public:
    std::unique_ptr<Expression> left;
    TokenType op;
    std::unique_ptr<Expression> right;

    BinaryExpression(std::unique_ptr<Expression> left, TokenType op, std::unique_ptr<Expression> right)
        : left(std::move(left)), op(op), right(std::move(right)) {
        type = ExprType::Int; // Comparisons return an int (0 or 1)
    }
};

class AddressOfExpression : public Expression {
public:
    std::unique_ptr<Expression> operand;
    AddressOfExpression(std::unique_ptr<Expression> operand) : operand(std::move(operand)) {
        // Type will be determined during codegen, or we can just leave it as Unknown in AST
    }
};

class DereferenceExpression : public Expression {
public:
    std::unique_ptr<Expression> operand;
    DereferenceExpression(std::unique_ptr<Expression> operand) : operand(std::move(operand)) {
        // Type will be determined during codegen
    }
};

class AllocExpression : public Expression {
public:
    std::unique_ptr<Expression> size;
    AllocExpression(std::unique_ptr<Expression> size) : size(std::move(size)) {
        type = ExprType::Unknown; // Can be cast to any pointer type implicitly
    }
};

// ─── Statements ─────────────────────────────────────────────────────────────

/// Base class for all statements.
class Statement : public ASTNode {};

class AssignmentStatement : public Statement {
public:
    std::unique_ptr<Expression> left;
    std::unique_ptr<Expression> right;

    AssignmentStatement(std::unique_ptr<Expression> left, std::unique_ptr<Expression> right)
        : left(std::move(left)), right(std::move(right)) {}
};

class VariableDeclaration : public Statement {
public:
    std::string name;
    ExprType varType;
    std::unique_ptr<Expression> initializer;

    VariableDeclaration(std::string name, ExprType varType, std::unique_ptr<Expression> initializer)
        : name(std::move(name)), varType(varType), initializer(std::move(initializer)) {}
};

class BlockStatement : public Statement {
public:
    std::vector<std::unique_ptr<Statement>> statements;
    explicit BlockStatement(std::vector<std::unique_ptr<Statement>> statements)
        : statements(std::move(statements)) {}
};

/// Represents: if (cond) { ... } else { ... }
class IfStatement : public Statement {
public:
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Statement> thenBranch;
    std::unique_ptr<Statement> elseBranch;

    IfStatement(std::unique_ptr<Expression> condition,
                std::unique_ptr<Statement> thenBranch,
                std::unique_ptr<Statement> elseBranch)
        : condition(std::move(condition)),
          thenBranch(std::move(thenBranch)),
          elseBranch(std::move(elseBranch)) {}
};

/// Represents: while (cond) { ... }
class WhileStatement : public Statement {
public:
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Statement> body;

    WhileStatement(std::unique_ptr<Expression> condition,
                   std::unique_ptr<Statement> body)
        : condition(std::move(condition)), body(std::move(body)) {}
};

/// Represents: System.Print(expr);
class PrintStatement : public Statement {
public:
    std::unique_ptr<Expression> expr;

    explicit PrintStatement(std::unique_ptr<Expression> expr)
        : expr(std::move(expr)) {}
};

class FreeStatement : public Statement {
public:
    std::unique_ptr<Expression> ptr;

    explicit FreeStatement(std::unique_ptr<Expression> ptr)
        : ptr(std::move(ptr)) {}
};

class ReturnStatement : public Statement {
public:
    std::unique_ptr<Expression> value;
    explicit ReturnStatement(std::unique_ptr<Expression> value)
        : value(std::move(value)) {}
};

// ─── Declarations ───────────────────────────────────────────────────────────

/// Represents: function Name() { ... }
class FunctionDeclaration : public ASTNode {
public:
    std::string name;
    ExprType returnType;
    std::vector<std::pair<ExprType, std::string>> parameters;
    std::vector<std::unique_ptr<Statement>> body;

    FunctionDeclaration(std::string name, ExprType returnType, std::vector<std::pair<ExprType, std::string>> parameters, std::vector<std::unique_ptr<Statement>> body)
        : name(std::move(name)), returnType(returnType), parameters(std::move(parameters)), body(std::move(body)) {}
};

// ─── Program (top-level) ────────────────────────────────────────────────────

/// Root node: a program is a list of function declarations.
class Program : public ASTNode {
public:
    std::vector<std::unique_ptr<FunctionDeclaration>> functions;
};

} // namespace nylang
