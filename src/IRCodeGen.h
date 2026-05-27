#pragma once

#include "IR.h"
#include "AST.h"
#include "Assembler.h"
#include "Target.h"
#include <map>
#include <string>

namespace nylang {

/// Translates a flat IRProgram into Assembler instructions.
/// This is the machine-code backend; it knows about physical registers,
/// calling conventions, and the target ABI.
class IRCodeGen {
public:
    explicit IRCodeGen(Target target) : m_target(target) {}

    void generate(const IRProgram& ir, Assembler& asmOut);

    // Set by CodeGen before generate() is called (format strings live in .data)
    size_t m_fmtIntOffset = 0;
    size_t m_fmtStrOffset = 0;

private:
    Target    m_target;
    Assembler* m_asm = nullptr;

    // Per-function state
    struct VarSlot {
        int      stackOffset;  // bytes from RBP (positive = above RBP, so we use -stackOffset)
        IRType   type;
    };
    std::map<std::string, VarSlot> m_slots;  // vReg name → stack slot
    int m_stackOffset = 0;                   // bytes allocated so far
    int m_labelCounter = 0;

    // Function table for call type resolution
    std::map<std::string, IRType> m_funcRetTypes;

    // ── Function level ───────────────────────────────────────────────────
    void genFunction(const IRFunction& func);

    // ── Instruction level ────────────────────────────────────────────────
    void genInstr(const IRInstr& instr);

    // ── Utilities ────────────────────────────────────────────────────────
    int  allocSlot(const std::string& name, IRType type);   // returns stack offset
    int  getSlot(const std::string& name) const;             // throws if not found
    IRType getSlotType(const std::string& name) const;

    // Load a vReg value into RAX. Handles both alloca slots and cached temps.
    void loadToRAX(const IRValue& v);
};

} // namespace nylang
