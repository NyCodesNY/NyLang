#include "IRCodeGen.h"
#include <stdexcept>

namespace nylang {

// ─── Public API ──────────────────────────────────────────────────────────────

void IRCodeGen::generate(const IRProgram& ir, Assembler& asmOut) {
    m_asm = &asmOut;

    // Collect function return types
    for (const auto& func : ir.functions) {
        m_funcRetTypes[func.name] = func.returnType;
    }

    for (const auto& func : ir.functions) {
        genFunction(func);
    }
}

// ─── Utilities ───────────────────────────────────────────────────────────────

int IRCodeGen::allocSlot(const std::string& name, IRType type) {
    m_stackOffset += 8;
    m_slots[name] = {m_stackOffset, type};
    return m_stackOffset;
}

int IRCodeGen::getSlot(const std::string& name) const {
    auto it = m_slots.find(name);
    if (it == m_slots.end())
        throw std::runtime_error("IRCodeGen: unknown vReg '" + name + "'");
    return it->second.stackOffset;
}

IRType IRCodeGen::getSlotType(const std::string& name) const {
    auto it = m_slots.find(name);
    if (it == m_slots.end()) return IRType::Unknown;
    return it->second.type;
}

void IRCodeGen::loadToRAX(const IRValue& v) {
    // If it's a void placeholder, do nothing.
    if (v.name == "(void)" || v.name == "(none)") return;

    auto it = m_slots.find(v.name);
    if (it == m_slots.end())
        throw std::runtime_error("IRCodeGen: loadToRAX – unknown vReg '" + v.name + "'");

    m_asm->movRegMem(RAX, RBP, -it->second.stackOffset);
}

// ─── Function ────────────────────────────────────────────────────────────────

void IRCodeGen::genFunction(const IRFunction& func) {
    m_slots.clear();
    m_stackOffset = 0;
    m_labelCounter = 0;
    m_currentFuncName = func.name;

    // ── Pass 1: Pre-allocate ALL named ALLOCA slots first ─────────────────
    // This ensures named variables (used by ADDR_OF) get contiguous addresses
    // at the bottom of the frame, before temps are mixed in.
    for (const auto& instr : func.instructions) {
        if (instr.op == IROp::ALLOCA) {
            allocSlot(instr.result.name, instr.result.type);
        }
    }
    // Reserve alloca region size
    int allocaRegionSize = m_stackOffset;

    // ── Count remaining temp slots ─────────────────────────────────────────
    int tempSlots = 0;
    for (const auto& instr : func.instructions) {
        if (instr.op == IROp::ALLOCA) continue;
        if (instr.result.valid()) tempSlots++;
    }

    int stackSpace = (allocaRegionSize + tempSlots * 8);
    if (m_target == Target::Windows) stackSpace += 32; // shadow space
    int alignedSpace = (stackSpace + 15) & ~15;

    // Map NyLang "Main" → C "main"
    std::string asmName = (func.name == "Main") ? "main" : func.name;
    m_asm->defineGlobal(asmName);

    // Prologue
    m_asm->pushReg(RBP);
    m_asm->movRegReg(RBP, RSP);
    if (alignedSpace > 0) {
        m_asm->subRegImm(RSP, alignedSpace);
    }

    // Spill parameters from ABI registers into their pre-allocated alloca slots.
    Reg linuxArgs[] = {RDI, RSI, RDX, RCX, R8, R9};
    Reg winArgs[]   = {RCX, RDX, R8, R9};

    for (size_t i = 0; i < func.params.size(); ++i) {
        std::string paramReg = "%p_" + func.params[i].name;
        int offset = getSlot(paramReg);  // already pre-allocated
        Reg argReg = (m_target == Target::Windows) ? winArgs[i] : linuxArgs[i];
        m_asm->movMemReg(RBP, -offset, argReg);
    }

    // ── Pass 2: Emit instructions ──────────────────────────────────────────
    for (const auto& instr : func.instructions) {
        genInstr(instr);
    }
}

// ─── Instruction Emission ────────────────────────────────────────────────────

void IRCodeGen::genInstr(const IRInstr& instr) {
    switch (instr.op) {

        // ── Alloca ──────────────────────────────────────────────────────
        case IROp::ALLOCA: {
            // Already pre-allocated in genFunction pass 1 — nothing to do here.
            break;
        }

        // ── Constants ───────────────────────────────────────────────────
        case IROp::CONST_INT:
        case IROp::CONST_BOOL: {
            int offset = allocSlot(instr.result.name, instr.result.type);
            m_asm->movRegImm(RAX, instr.immInt);
            m_asm->movMemReg(RBP, -offset, RAX);
            break;
        }

        case IROp::CONST_STR: {
            int offset = allocSlot(instr.result.name, IRType::String);
            size_t strOffset = m_asm->addString(instr.strVal);
            m_asm->leaRipRel(RAX, static_cast<uint32_t>(strOffset));
            m_asm->movMemReg(RBP, -offset, RAX);
            break;
        }

        // ── Store / Load ─────────────────────────────────────────────────
        case IROp::STORE: {
            // store <val> -> <dest alloca>
            loadToRAX(instr.left);
            int destSlot = getSlot(instr.result.name);
            m_asm->movMemReg(RBP, -destSlot, RAX);
            break;
        }

        case IROp::LOAD: {
            // result = load from src alloca
            int srcSlot  = getSlot(instr.left.name);
            int destSlot = allocSlot(instr.result.name, instr.result.type);
            m_asm->movRegMem(RAX, RBP, -srcSlot);
            m_asm->movMemReg(RBP, -destSlot, RAX);
            break;
        }

        // ── Pointer operations ───────────────────────────────────────────
        case IROp::ADDR_OF: {
            // result = &alloca_slot
            int srcSlot  = getSlot(instr.left.name);
            int destSlot = allocSlot(instr.result.name, instr.result.type);
            m_asm->movRegReg(RAX, RBP);
            m_asm->subRegImm(RAX, srcSlot);
            m_asm->movMemReg(RBP, -destSlot, RAX);
            break;
        }

        case IROp::DEREF_LOAD: {
            // result = *ptr
            loadToRAX(instr.left);               // RAX = address
            m_asm->movRegMem(RAX, RAX, 0);       // RAX = *RAX
            int destSlot = allocSlot(instr.result.name, instr.result.type);
            m_asm->movMemReg(RBP, -destSlot, RAX);
            break;
        }

        case IROp::DEREF_STORE: {
            // *ptr = val
            loadToRAX(instr.right);              // RAX = value
            m_asm->pushReg(RAX);
            loadToRAX(instr.left);               // RAX = ptr address
            m_asm->popReg(RCX);
            m_asm->movMemReg(RAX, 0, RCX);       // *ptr = value
            break;
        }

        // ── Arithmetic / Comparison ──────────────────────────────────────
        case IROp::ADD: case IROp::SUB: case IROp::MUL: case IROp::DIV:
        case IROp::EQ:  case IROp::NE:  case IROp::LT:  case IROp::GT: {
            loadToRAX(instr.left);
            m_asm->pushReg(RAX);
            loadToRAX(instr.right);
            m_asm->movRegReg(RCX, RAX);
            m_asm->popReg(RAX);

            // Pointer arithmetic: scale RCX by 8
            bool isPtrArith = (instr.left.type >= IRType::IntPtr &&
                               instr.left.type <= IRType::VoidPtr) &&
                              (instr.right.type == IRType::Int) &&
                              (instr.op == IROp::ADD || instr.op == IROp::SUB);
            if (isPtrArith) {
                m_asm->pushReg(RAX);
                m_asm->movRegImm(RAX, 8);
                m_asm->imulRegReg(RCX, RAX);
                m_asm->popReg(RAX);
            }

            if (instr.op == IROp::EQ || instr.op == IROp::NE ||
                instr.op == IROp::LT || instr.op == IROp::GT) {
                m_asm->cmpRegReg(RAX, RCX);
                switch (instr.op) {
                    case IROp::EQ: m_asm->sete();  break;
                    case IROp::NE: m_asm->setne(); break;
                    case IROp::LT: m_asm->setl();  break;
                    case IROp::GT: m_asm->setg();  break;
                    default: break;
                }
                m_asm->movzxRaxAl();
            } else {
                switch (instr.op) {
                    case IROp::ADD: m_asm->addRegReg(RAX, RCX); break;
                    case IROp::SUB: m_asm->subRegReg(RAX, RCX); break;
                    case IROp::MUL: m_asm->imulRegReg(RAX, RCX); break;
                    case IROp::DIV: m_asm->cqo(); m_asm->idivReg(RCX); break;
                    default: break;
                }
            }

            int destSlot = allocSlot(instr.result.name, instr.result.type);
            m_asm->movMemReg(RBP, -destSlot, RAX);
            break;
        }

        // ── Print ────────────────────────────────────────────────────────
        case IROp::CALL_PRINT: {
            loadToRAX(instr.left);
            IRType type = instr.left.type;

            if (m_target == Target::Windows) {
                m_asm->movRegReg(RDX, RAX);
                if (type == IRType::Int || type == IRType::Bool) {
                    m_asm->leaRipRel(RCX, static_cast<uint32_t>(m_fmtIntOffset));
                } else {
                    m_asm->leaRipRel(RCX, static_cast<uint32_t>(m_fmtStrOffset));
                }
                m_asm->xorReg32(RAX, RAX);
                m_asm->callImport("printf");
            } else {
                m_asm->movRegReg(RSI, RAX);
                if (type == IRType::Int || type == IRType::Bool) {
                    m_asm->leaRipRel(RDI, static_cast<uint32_t>(m_fmtIntOffset));
                } else {
                    m_asm->leaRipRel(RDI, static_cast<uint32_t>(m_fmtStrOffset));
                }
                m_asm->xorReg32(RAX, RAX);
                m_asm->callExtern("printf");
            }
            break;
        }

        // ── Malloc / Free ────────────────────────────────────────────────
        case IROp::CALL_MALLOC: {
            loadToRAX(instr.left);
            if (m_target == Target::Windows) {
                m_asm->movRegReg(RCX, RAX);
                m_asm->callImport("malloc");
            } else {
                m_asm->movRegReg(RDI, RAX);
                m_asm->callExtern("malloc");
            }
            int destSlot = allocSlot(instr.result.name, instr.result.type);
            m_asm->movMemReg(RBP, -destSlot, RAX);
            break;
        }

        case IROp::CALL_FREE: {
            loadToRAX(instr.left);
            if (m_target == Target::Windows) {
                m_asm->movRegReg(RCX, RAX);
                m_asm->callImport("free");
            } else {
                m_asm->movRegReg(RDI, RAX);
                m_asm->callExtern("free");
            }
            break;
        }

        // ── GetInput ─────────────────────────────────────────────────────
        case IROp::CALL_GETINPUT: {
            size_t bufOffset = m_asm->addZeros(256);

            if (m_target == Target::Windows) {
                m_asm->movRegImm(RCX, 0);
                m_asm->leaRipRel(RDX, static_cast<uint32_t>(bufOffset));
                m_asm->movRegImm(R8, 255);
                m_asm->callImport("_read");
            } else {
                m_asm->movRegImm(RAX, 0);
                m_asm->movRegImm(RDI, 0);
                m_asm->leaRipRel(RSI, static_cast<uint32_t>(bufOffset));
                m_asm->movRegImm(RDX, 255);
                m_asm->syscall();
            }

            m_asm->leaRipRel(RSI, static_cast<uint32_t>(bufOffset));
            m_asm->addRegReg(RSI, RAX);
            m_asm->movByteMemImm(RSI, 0, 0);

            std::string skipLabel = ".L_skip_nl_" + std::to_string(m_labelCounter++);
            m_asm->cmpRegImm(RAX, 0);
            m_asm->je(skipLabel);
            m_asm->cmpByteMemImm(RSI, -1, '\n');
            m_asm->jne(skipLabel);
            m_asm->movByteMemImm(RSI, -1, 0);
            m_asm->defineLabel(skipLabel);

            m_asm->leaRipRel(RAX, static_cast<uint32_t>(bufOffset));
            int destSlot = allocSlot(instr.result.name, IRType::String);
            m_asm->movMemReg(RBP, -destSlot, RAX);
            break;
        }

        // ── User-defined function call ────────────────────────────────────
        case IROp::CALL: {
            Reg linuxArgs[] = {RDI, RSI, RDX, RCX, R8, R9};
            Reg winArgs[]   = {RCX, RDX, R8, R9};

            // Push all args
            for (const auto& arg : instr.args) {
                loadToRAX(arg);
                m_asm->pushReg(RAX);
            }
            // Pop into ABI registers in reverse
            for (int i = (int)instr.args.size() - 1; i >= 0; --i) {
                Reg argReg = (m_target == Target::Windows) ? winArgs[i] : linuxArgs[i];
                m_asm->popReg(argReg);
            }

            m_asm->call(instr.strVal);

            // Store return value
            if (instr.result.valid()) {
                int destSlot = allocSlot(instr.result.name, instr.result.type);
                m_asm->movMemReg(RBP, -destSlot, RAX);
            }
            break;
        }

        // ── Control flow ─────────────────────────────────────────────────
        case IROp::LABEL: {
            m_asm->defineLabel(instr.strVal);
            break;
        }

        case IROp::JUMP: {
            m_asm->jmp(instr.strVal);
            break;
        }

        case IROp::JUMP_IF_ZERO: {
            loadToRAX(instr.left);
            m_asm->cmpRegImm(RAX, 0);
            m_asm->je(instr.strVal);
            break;
        }

        case IROp::RET: {
            if (instr.left.name != "(void)" && instr.left.name != "(none)") {
                loadToRAX(instr.left);
            }
            
            if (m_currentFuncName == "Main") {
                if (m_target == Target::Windows) {
                    m_asm->xorReg32(RCX, RCX);
                    m_asm->callImport("ExitProcess");
                } else {
                    // Linux exit syscall
                    m_asm->movRegImm(RAX, 60); // sys_exit
                    m_asm->movRegImm(RDI, 0);  // exit code
                    m_asm->syscall();
                }
            } else {
                m_asm->movRegReg(RSP, RBP);
                m_asm->popReg(RBP);
                m_asm->ret();
            }
            break;
        }

        default:
            throw std::runtime_error("IRCodeGen: unhandled IROp");
    }
}

} // namespace nylang
