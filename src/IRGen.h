#pragma once

#include "IR.h"
#include "AST.h"
#include <string>
#include <map>

namespace nylang {

/// Walks an AST Program and emits an IRProgram.
/// No machine code is produced here — only virtual registers and IR instructions.
class IRGen {
public:
    IRProgram generate(const Program& program);

private:
    IRProgram    m_ir;
    IRFunction*  m_currentFunc = nullptr;  // active function being built
    int          m_tempCounter = 0;
    int          m_labelCounter = 0;

    // Maps variable name → its alloca IRValue (i.e. the ALLOCA slot)
    std::map<std::string, IRValue> m_locals;

    // Maps function name → return type (for call type inference)
    std::map<std::string, IRType> m_funcReturnTypes;

    // ── Helpers ──────────────────────────────────────────────────────────
    IRValue newTemp(IRType t);      // allocate %t0, %t1, ...
    std::string newLabel(const std::string& prefix);
    void emit(IRInstr instr);

    // ── Generation ───────────────────────────────────────────────────────
    void genFunction(const FunctionDeclaration& func);
    void genStatement(const Statement& stmt);
    void genBlock(const BlockStatement& block);
    void genVarDecl(const VariableDeclaration& decl);
    void genAssignment(const AssignmentStatement& assign);
    void genIf(const IfStatement& ifStmt);
    void genWhile(const WhileStatement& whileStmt);
    void genPrint(const PrintStatement& print);
    void genFree(const FreeStatement& freeStmt);
    void genReturn(const ReturnStatement& ret);

    IRValue genExpr(const Expression& expr);
};

} // namespace nylang
