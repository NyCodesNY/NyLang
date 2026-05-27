#pragma once

#include "AST.h"
#include "IR.h"
#include "Target.h"
#include <vector>
#include <cstdint>

namespace nylang {

/// Top-level compiler driver.
/// Pipeline: AST → IRGen → [IR Passes] → IRCodeGen → ElfWriter/PeWriter
class CodeGen {
public:
    explicit CodeGen(Target target = Target::Linux) : m_target(target) {}

    /// Compile a full program to a native binary image (ELF .o or PE .exe).
    std::vector<uint8_t> generate(const Program& program);

    /// Generate IR from the program (useful for --dump-ir).
    IRProgram generateIR(const Program& program);

private:
    Target m_target;
};

} // namespace nylang
