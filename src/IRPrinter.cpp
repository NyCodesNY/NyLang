#include "IRPrinter.h"
#include <ostream>

namespace nylang {

// ─── Type / Op name helpers ──────────────────────────────────────────────────

const char* IRPrinter::typeName(IRType t) {
    switch (t) {
        case IRType::Int:       return "int";
        case IRType::String:    return "string";
        case IRType::Bool:      return "bool";
        case IRType::Void:      return "void";
        case IRType::IntPtr:    return "int*";
        case IRType::StringPtr: return "string*";
        case IRType::BoolPtr:   return "bool*";
        case IRType::VoidPtr:   return "void*";
        default:                return "unknown";
    }
}

const char* IRPrinter::opName(IROp op) {
    switch (op) {
        case IROp::CONST_INT:    return "const_int";
        case IROp::CONST_STR:    return "const_str";
        case IROp::CONST_BOOL:   return "const_bool";
        case IROp::ALLOCA:       return "alloca";
        case IROp::STORE:        return "store";
        case IROp::LOAD:         return "load";
        case IROp::ADDR_OF:      return "addr_of";
        case IROp::DEREF_LOAD:   return "deref_load";
        case IROp::DEREF_STORE:  return "deref_store";
        case IROp::ADD:          return "add";
        case IROp::SUB:          return "sub";
        case IROp::MUL:          return "mul";
        case IROp::DIV:          return "div";
        case IROp::EQ:           return "eq";
        case IROp::NE:           return "ne";
        case IROp::LT:           return "lt";
        case IROp::GT:           return "gt";
        case IROp::CALL:         return "call";
        case IROp::CALL_MALLOC:  return "call_malloc";
        case IROp::CALL_FREE:    return "call_free";
        case IROp::CALL_PRINT:   return "call_print";
        case IROp::CALL_GETINPUT:return "call_getinput";
        case IROp::LABEL:        return "label";
        case IROp::JUMP:         return "jump";
        case IROp::JUMP_IF_ZERO: return "jump_if_zero";
        case IROp::RET:          return "ret";
        default:                 return "???";
    }
}

// ─── Printing ────────────────────────────────────────────────────────────────

void IRPrinter::print(const IRProgram& prog) {
    m_out << "; ═══════════════════════════════════════\n";
    m_out << "; NyLang IR Dump\n";
    m_out << "; ═══════════════════════════════════════\n\n";
    for (const auto& func : prog.functions) {
        printFunction(func);
        m_out << "\n";
    }
}

void IRPrinter::printFunction(const IRFunction& func) {
    m_out << "define " << typeName(func.returnType) << " @" << func.name << "(";
    for (size_t i = 0; i < func.params.size(); ++i) {
        if (i > 0) m_out << ", ";
        m_out << typeName(func.params[i].type) << " %p_" << func.params[i].name;
    }
    m_out << ") {\n";

    for (const auto& instr : func.instructions) {
        printInstr(instr);
    }

    m_out << "}\n";
}

void IRPrinter::printInstr(const IRInstr& instr) {
    // Labels get their own formatting
    if (instr.op == IROp::LABEL) {
        m_out << instr.strVal << ":\n";
        return;
    }

    m_out << "    ";

    switch (instr.op) {
        case IROp::CONST_INT:
            m_out << instr.result.name << " = const_int " << typeName(instr.result.type)
                  << " " << instr.immInt;
            break;

        case IROp::CONST_BOOL:
            m_out << instr.result.name << " = const_bool " << (instr.immInt ? "true" : "false");
            break;

        case IROp::CONST_STR:
            m_out << instr.result.name << " = const_str \"" << instr.strVal << "\"";
            break;

        case IROp::ALLOCA:
            m_out << instr.result.name << " = alloca " << typeName(instr.result.type);
            break;

        case IROp::STORE:
            m_out << "store " << instr.left.name << " -> " << instr.result.name;
            break;

        case IROp::LOAD:
            m_out << instr.result.name << " = load " << typeName(instr.result.type)
                  << " from " << instr.left.name;
            break;

        case IROp::ADDR_OF:
            m_out << instr.result.name << " = addr_of " << instr.left.name;
            break;

        case IROp::DEREF_LOAD:
            m_out << instr.result.name << " = deref_load *" << instr.left.name;
            break;

        case IROp::DEREF_STORE:
            m_out << "deref_store *" << instr.left.name << " <- " << instr.right.name;
            break;

        case IROp::ADD: case IROp::SUB: case IROp::MUL: case IROp::DIV:
        case IROp::EQ:  case IROp::NE:  case IROp::LT:  case IROp::GT:
            m_out << instr.result.name << " = " << opName(instr.op)
                  << " " << typeName(instr.result.type)
                  << " " << instr.left.name << ", " << instr.right.name;
            break;

        case IROp::CALL:
            m_out << instr.result.name << " = call @" << instr.strVal << "(";
            for (size_t i = 0; i < instr.args.size(); ++i) {
                if (i > 0) m_out << ", ";
                m_out << instr.args[i].name;
            }
            m_out << ")";
            break;

        case IROp::CALL_MALLOC:
            m_out << instr.result.name << " = malloc(" << instr.left.name << ")";
            break;

        case IROp::CALL_FREE:
            m_out << "free(" << instr.left.name << ")";
            break;

        case IROp::CALL_PRINT:
            m_out << "call_print(" << instr.left.name << " : " << typeName(instr.left.type) << ")";
            break;

        case IROp::CALL_GETINPUT:
            m_out << instr.result.name << " = call_getinput()";
            break;

        case IROp::JUMP:
            m_out << "jump " << instr.strVal;
            break;

        case IROp::JUMP_IF_ZERO:
            m_out << "jump_if_zero " << instr.left.name << " -> " << instr.strVal;
            break;

        case IROp::RET:
            m_out << "ret " << instr.left.name;
            break;

        default:
            m_out << opName(instr.op);
            break;
    }

    m_out << "\n";
}

} // namespace nylang
