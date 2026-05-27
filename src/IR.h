#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include "AST.h"  // for ExprType

namespace nylang {

// ─── IR Types ───────────────────────────────────────────────────────────────
// ExprType is re-used as IRType for now.
using IRType = ExprType;

// ─── Virtual Register ────────────────────────────────────────────────────────
// A virtual register (VReg) is an unlimited SSA-like slot.
// Negative IDs indicate named variables (e.g. function params).
// Zero/positive IDs are temporaries produced by instructions.
struct IRValue {
    std::string name;   // e.g. "%t0", "%x", "%a"
    IRType       type;

    IRValue() : name("(none)"), type(IRType::Unknown) {}
    IRValue(std::string n, IRType t) : name(std::move(n)), type(t) {}

    bool valid() const { return !name.empty() && name != "(none)"; }

    bool operator==(const IRValue& o) const { return name == o.name; }
    bool operator!=(const IRValue& o) const { return name != o.name; }
};

inline IRValue IRVoid() { return {"(void)", IRType::Void}; }

// ─── IR Opcodes ──────────────────────────────────────────────────────────────
enum class IROp {
    // Constants
    CONST_INT,       // result = <imm int>
    CONST_STR,       // result = <string label>
    CONST_BOOL,      // result = 0|1

    // Stack slots
    ALLOCA,          // dest = alloca <type>  (declares a named stack slot)
    STORE,           // store <val> -> <dest alloca>
    LOAD,            // result = load <src alloca>

    // Pointer operations
    ADDR_OF,         // result = addr_of <alloca>  (like C &x)
    DEREF_LOAD,      // result = deref_load <ptr>   (like C *ptr read)
    DEREF_STORE,     // deref_store <ptr>, <val>    (like C *ptr = val)

    // Binary arithmetic
    ADD,
    SUB,
    MUL,
    DIV,

    // Comparison (yields Bool)
    EQ,
    NE,
    LT,
    GT,

    // Calls
    CALL,            // result = call <funcName> [args...]
    CALL_MALLOC,     // result = malloc <size>
    CALL_FREE,       // free <ptr>
    CALL_PRINT,      // print <val>
    CALL_GETINPUT,   // result = getinput

    // Control flow
    LABEL,           // defines a branch target label (name stored in strVal)
    JUMP,            // unconditional jump  (strVal = target label)
    JUMP_IF_ZERO,    // if result == 0 jump strVal
    RET,             // return <val> (or void)
};

// ─── IR Instruction ──────────────────────────────────────────────────────────
struct IRInstr {
    IROp     op;
    IRValue  result;    // destination vReg (may be void/unused)
    IRValue  left;      // primary operand
    IRValue  right;     // secondary operand
    int64_t  immInt = 0;        // for CONST_INT
    std::string strVal;         // for CONST_STR, LABEL, JUMP, func name in CALL
    std::vector<IRValue> args;  // for CALL

    // Convenience constructors
    static IRInstr make(IROp op) { IRInstr i; i.op = op; return i; }
    static IRInstr makeLabel(const std::string& label) {
        IRInstr i; i.op = IROp::LABEL; i.strVal = label; return i;
    }
    static IRInstr makeJump(const std::string& label) {
        IRInstr i; i.op = IROp::JUMP; i.strVal = label; return i;
    }
    static IRInstr makeJumpIfZero(IRValue cond, const std::string& label) {
        IRInstr i; i.op = IROp::JUMP_IF_ZERO; i.left = cond; i.strVal = label; return i;
    }
    static IRInstr makeRet(IRValue val) {
        IRInstr i; i.op = IROp::RET; i.left = val; return i;
    }
    static IRInstr makeConst(IRValue dest, int64_t imm) {
        IRInstr i; i.op = IROp::CONST_INT; i.result = dest; i.immInt = imm; return i;
    }
    static IRInstr makeConstStr(IRValue dest, const std::string& str) {
        IRInstr i; i.op = IROp::CONST_STR; i.result = dest; i.strVal = str; return i;
    }
    static IRInstr makeAlloca(IRValue dest) {
        IRInstr i; i.op = IROp::ALLOCA; i.result = dest; return i;
    }
    static IRInstr makeStore(IRValue val, IRValue dest) {
        IRInstr i; i.op = IROp::STORE; i.left = val; i.result = dest; return i;
    }
    static IRInstr makeLoad(IRValue dest, IRValue src) {
        IRInstr i; i.op = IROp::LOAD; i.result = dest; i.left = src; return i;
    }
    static IRInstr makeBinop(IROp op, IRValue dest, IRValue l, IRValue r) {
        IRInstr i; i.op = op; i.result = dest; i.left = l; i.right = r; return i;
    }
    static IRInstr makeAddrOf(IRValue dest, IRValue src) {
        IRInstr i; i.op = IROp::ADDR_OF; i.result = dest; i.left = src; return i;
    }
    static IRInstr makeDerefLoad(IRValue dest, IRValue ptr) {
        IRInstr i; i.op = IROp::DEREF_LOAD; i.result = dest; i.left = ptr; return i;
    }
    static IRInstr makeDerefStore(IRValue ptr, IRValue val) {
        IRInstr i; i.op = IROp::DEREF_STORE; i.left = ptr; i.right = val; return i;
    }
    static IRInstr makeCall(IRValue dest, const std::string& func, std::vector<IRValue> argList) {
        IRInstr i; i.op = IROp::CALL; i.result = dest; i.strVal = func;
        i.args = std::move(argList); return i;
    }
    static IRInstr makePrint(IRValue val) {
        IRInstr i; i.op = IROp::CALL_PRINT; i.left = val; return i;
    }
    static IRInstr makeMalloc(IRValue dest, IRValue size) {
        IRInstr i; i.op = IROp::CALL_MALLOC; i.result = dest; i.left = size; return i;
    }
    static IRInstr makeFree(IRValue ptr) {
        IRInstr i; i.op = IROp::CALL_FREE; i.left = ptr; return i;
    }
    static IRInstr makeGetInput(IRValue dest) {
        IRInstr i; i.op = IROp::CALL_GETINPUT; i.result = dest; return i;
    }
};

// ─── IR Function ─────────────────────────────────────────────────────────────
struct IRParam {
    std::string name;
    IRType type;
};

struct IRFunction {
    std::string name;
    IRType returnType;
    std::vector<IRParam> params;
    std::vector<IRInstr> instructions;  // flat SSA-like list
};

// ─── IR Program ──────────────────────────────────────────────────────────────
struct IRProgram {
    std::vector<IRFunction> functions;
};

} // namespace nylang
