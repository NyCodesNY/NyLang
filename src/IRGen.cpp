#include "IRGen.h"
#include <stdexcept>

namespace nylang {

// ─── Public API ──────────────────────────────────────────────────────────────

IRProgram IRGen::generate(const Program& program) {
    m_ir = IRProgram();
    m_tempCounter = 0;
    m_labelCounter = 0;

    // First pass: collect all function return types so calls can be type-checked.
    for (const auto& func : program.functions) {
        m_funcReturnTypes[func->name] = func->returnType;
    }

    // Second pass: generate IR for each function.
    for (const auto& func : program.functions) {
        genFunction(*func);
    }

    return m_ir;
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

IRValue IRGen::newTemp(IRType t) {
    return {"%t" + std::to_string(m_tempCounter++), t};
}

std::string IRGen::newLabel(const std::string& prefix) {
    return prefix + "_" + std::to_string(m_labelCounter++);
}

void IRGen::emit(IRInstr instr) {
    m_currentFunc->instructions.push_back(std::move(instr));
}

// ─── Function ────────────────────────────────────────────────────────────────

void IRGen::genFunction(const FunctionDeclaration& func) {
    m_ir.functions.push_back(IRFunction{});
    m_currentFunc = &m_ir.functions.back();
    m_currentFunc->name = func.name;
    m_currentFunc->returnType = func.returnType;
    m_locals.clear();
    m_tempCounter = 0;   // reset per-function so dumps are readable
    m_labelCounter = 0;

    // Parameters: create an alloca slot for each param, then store arg into it.
    for (const auto& [paramType, paramName] : func.parameters) {
        IRValue paramReg{"%p_" + paramName, paramType};
        m_currentFunc->params.push_back({paramName, paramType});

        // Create the alloca slot and store the incoming parameter value.
        IRValue slot{"%v_" + paramName, paramType};
        m_locals[paramName] = slot;
        emit(IRInstr::makeAlloca(slot));
        emit(IRInstr::makeStore(paramReg, slot));
    }

    // Body
    for (const auto& stmt : func.body) {
        genStatement(*stmt);
    }

    // Implicit void return at end if not already present.
    if (m_currentFunc->instructions.empty() ||
        m_currentFunc->instructions.back().op != IROp::RET) {
        emit(IRInstr::makeRet(IRVoid()));
    }
}

// ─── Statement Dispatch ──────────────────────────────────────────────────────

void IRGen::genStatement(const Statement& stmt) {
    if (auto* s = dynamic_cast<const VariableDeclaration*>(&stmt)) { genVarDecl(*s); return; }
    if (auto* s = dynamic_cast<const AssignmentStatement*>(&stmt)) { genAssignment(*s); return; }
    if (auto* s = dynamic_cast<const PrintStatement*>(&stmt))      { genPrint(*s); return; }
    if (auto* s = dynamic_cast<const FreeStatement*>(&stmt))       { genFree(*s); return; }
    if (auto* s = dynamic_cast<const ReturnStatement*>(&stmt))     { genReturn(*s); return; }
    if (auto* s = dynamic_cast<const BlockStatement*>(&stmt))      { genBlock(*s); return; }
    if (auto* s = dynamic_cast<const IfStatement*>(&stmt))         { genIf(*s); return; }
    if (auto* s = dynamic_cast<const WhileStatement*>(&stmt))      { genWhile(*s); return; }
    throw std::runtime_error("IRGen: unknown statement type");
}

void IRGen::genBlock(const BlockStatement& block) {
    for (const auto& s : block.statements) {
        genStatement(*s);
    }
}

// ─── Variable Declaration ────────────────────────────────────────────────────

void IRGen::genVarDecl(const VariableDeclaration& decl) {
    IRValue val = genExpr(*decl.initializer);

    // Determine final type: prefer declared type, fall back to inferred type (for `init`).
    IRType finalType = (decl.varType != IRType::Unknown) ? decl.varType : val.type;
    val.type = finalType;

    IRValue slot{"%v_" + decl.name, finalType};
    m_locals[decl.name] = slot;
    emit(IRInstr::makeAlloca(slot));
    emit(IRInstr::makeStore(val, slot));
}

// ─── Assignment ──────────────────────────────────────────────────────────────

void IRGen::genAssignment(const AssignmentStatement& assign) {
    IRValue val = genExpr(*assign.right);

    if (auto* varExpr = dynamic_cast<const VariableExpression*>(assign.left.get())) {
        auto it = m_locals.find(varExpr->name);
        if (it == m_locals.end())
            throw std::runtime_error("IRGen: undefined variable in assignment: " + varExpr->name);
        emit(IRInstr::makeStore(val, it->second));

    } else if (auto* deref = dynamic_cast<const DereferenceExpression*>(assign.left.get())) {
        IRValue ptr = genExpr(*deref->operand);
        emit(IRInstr::makeDerefStore(ptr, val));

    } else {
        throw std::runtime_error("IRGen: invalid left-hand side in assignment");
    }
}

// ─── Control Flow ────────────────────────────────────────────────────────────

void IRGen::genIf(const IfStatement& ifStmt) {
    IRValue cond = genExpr(*ifStmt.condition);

    if (ifStmt.elseBranch) {
        std::string elseLabel = newLabel(".L_else");
        std::string endLabel  = newLabel(".L_end");

        emit(IRInstr::makeJumpIfZero(cond, elseLabel));
        genStatement(*ifStmt.thenBranch);
        emit(IRInstr::makeJump(endLabel));
        emit(IRInstr::makeLabel(elseLabel));
        genStatement(*ifStmt.elseBranch);
        emit(IRInstr::makeLabel(endLabel));
    } else {
        std::string endLabel = newLabel(".L_end");
        emit(IRInstr::makeJumpIfZero(cond, endLabel));
        genStatement(*ifStmt.thenBranch);
        emit(IRInstr::makeLabel(endLabel));
    }
}

void IRGen::genWhile(const WhileStatement& whileStmt) {
    std::string startLabel = newLabel(".L_while_start");
    std::string endLabel   = newLabel(".L_while_end");

    emit(IRInstr::makeLabel(startLabel));
    IRValue cond = genExpr(*whileStmt.condition);
    emit(IRInstr::makeJumpIfZero(cond, endLabel));
    genStatement(*whileStmt.body);
    emit(IRInstr::makeJump(startLabel));
    emit(IRInstr::makeLabel(endLabel));
}

// ─── Print / Free / Return ───────────────────────────────────────────────────

void IRGen::genPrint(const PrintStatement& print) {
    IRValue val = genExpr(*print.expr);
    emit(IRInstr::makePrint(val));
}

void IRGen::genFree(const FreeStatement& freeStmt) {
    IRValue ptr = genExpr(*freeStmt.ptr);
    emit(IRInstr::makeFree(ptr));
}

void IRGen::genReturn(const ReturnStatement& ret) {
    IRValue val = genExpr(*ret.value);
    emit(IRInstr::makeRet(val));
}

// ─── Expression Evaluation ───────────────────────────────────────────────────

IRValue IRGen::genExpr(const Expression& expr) {
    // Integer literal
    if (auto* lit = dynamic_cast<const IntegerLiteral*>(&expr)) {
        IRValue dest = newTemp(IRType::Int);
        emit(IRInstr::makeConst(dest, lit->value));
        return dest;
    }

    // String literal
    if (auto* lit = dynamic_cast<const StringLiteral*>(&expr)) {
        IRValue dest = newTemp(IRType::String);
        emit(IRInstr::makeConstStr(dest, lit->value));
        return dest;
    }

    // Boolean literal
    if (auto* lit = dynamic_cast<const BooleanLiteral*>(&expr)) {
        IRValue dest = newTemp(IRType::Bool);
        IRInstr i; i.op = IROp::CONST_BOOL; i.result = dest; i.immInt = lit->value ? 1 : 0;
        emit(i);
        return dest;
    }

    // Variable reference → load from alloca slot
    if (auto* var = dynamic_cast<const VariableExpression*>(&expr)) {
        auto it = m_locals.find(var->name);
        if (it == m_locals.end())
            throw std::runtime_error("IRGen: undefined variable '" + var->name + "'");
        IRValue dest = newTemp(it->second.type);
        emit(IRInstr::makeLoad(dest, it->second));
        return dest;
    }

    // Address-of (&x) → produce pointer to the alloca slot
    if (auto* addrOf = dynamic_cast<const AddressOfExpression*>(&expr)) {
        auto* varExpr = dynamic_cast<const VariableExpression*>(addrOf->operand.get());
        if (!varExpr)
            throw std::runtime_error("IRGen: address-of requires a variable");
        auto it = m_locals.find(varExpr->name);
        if (it == m_locals.end())
            throw std::runtime_error("IRGen: undefined variable in address-of: " + varExpr->name);

        IRType ptrType = IRType::VoidPtr;
        if (it->second.type == IRType::Int)    ptrType = IRType::IntPtr;
        if (it->second.type == IRType::String) ptrType = IRType::StringPtr;
        if (it->second.type == IRType::Bool)   ptrType = IRType::BoolPtr;

        IRValue dest = newTemp(ptrType);
        emit(IRInstr::makeAddrOf(dest, it->second));
        return dest;
    }

    // Dereference (*ptr) → load from pointer
    if (auto* deref = dynamic_cast<const DereferenceExpression*>(&expr)) {
        IRValue ptr = genExpr(*deref->operand);

        IRType elemType = IRType::Unknown;
        if (ptr.type == IRType::IntPtr)    elemType = IRType::Int;
        if (ptr.type == IRType::StringPtr) elemType = IRType::String;
        if (ptr.type == IRType::BoolPtr)   elemType = IRType::Bool;

        IRValue dest = newTemp(elemType);
        emit(IRInstr::makeDerefLoad(dest, ptr));
        return dest;
    }

    // System.Alloc
    if (auto* alloc = dynamic_cast<const AllocExpression*>(&expr)) {
        IRValue size = genExpr(*alloc->size);
        IRValue dest = newTemp(IRType::Unknown); // raw pointer
        emit(IRInstr::makeMalloc(dest, size));
        return dest;
    }

    // System.GetInput
    if (dynamic_cast<const GetInputExpression*>(&expr)) {
        IRValue dest = newTemp(IRType::String);
        emit(IRInstr::makeGetInput(dest));
        return dest;
    }

    // Function call
    if (auto* call = dynamic_cast<const CallExpression*>(&expr)) {
        std::vector<IRValue> argVals;
        for (const auto& arg : call->arguments) {
            argVals.push_back(genExpr(*arg));
        }

        IRType retType = IRType::Unknown;
        auto it = m_funcReturnTypes.find(call->callee);
        if (it != m_funcReturnTypes.end()) retType = it->second;

        IRValue dest = newTemp(retType);
        emit(IRInstr::makeCall(dest, call->callee, std::move(argVals)));
        return dest;
    }

    // Binary expression
    if (auto* bin = dynamic_cast<const BinaryExpression*>(&expr)) {
        IRValue lhs = genExpr(*bin->left);
        IRValue rhs = genExpr(*bin->right);

        static const std::map<TokenType, IROp> cmpOps = {
            {TokenType::EqEq,   IROp::EQ},
            {TokenType::NotEq,  IROp::NE},
            {TokenType::Less,   IROp::LT},
            {TokenType::Greater,IROp::GT},
        };
        static const std::map<TokenType, IROp> arithOps = {
            {TokenType::Plus,  IROp::ADD},
            {TokenType::Minus, IROp::SUB},
            {TokenType::Star,  IROp::MUL},
            {TokenType::Slash, IROp::DIV},
        };

        auto cmpIt = cmpOps.find(bin->op);
        if (cmpIt != cmpOps.end()) {
            IRValue dest = newTemp(IRType::Bool);
            emit(IRInstr::makeBinop(cmpIt->second, dest, lhs, rhs));
            return dest;
        }

        auto arithIt = arithOps.find(bin->op);
        if (arithIt != arithOps.end()) {
            // Pointer arithmetic: if left is a pointer and right is int, result is pointer
            bool isPtrArith = (lhs.type >= IRType::IntPtr && lhs.type <= IRType::VoidPtr)
                           && (rhs.type == IRType::Int);
            IRType resultType = isPtrArith ? lhs.type : IRType::Int;
            IRValue dest = newTemp(resultType);
            emit(IRInstr::makeBinop(arithIt->second, dest, lhs, rhs));
            return dest;
        }

        throw std::runtime_error("IRGen: unsupported binary operator");
    }

    throw std::runtime_error("IRGen: unknown expression type");
}

} // namespace nylang
