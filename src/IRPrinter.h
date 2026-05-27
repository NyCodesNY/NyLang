#pragma once

#include "IR.h"
#include <ostream>

namespace nylang {

/// Prints a human-readable dump of an IRProgram to any output stream.
class IRPrinter {
public:
    explicit IRPrinter(std::ostream& out) : m_out(out) {}

    void print(const IRProgram& prog);
    void printFunction(const IRFunction& func);
    void printInstr(const IRInstr& instr);

private:
    std::ostream& m_out;

    static const char* typeName(IRType t);
    static const char* opName(IROp op);
};

} // namespace nylang
