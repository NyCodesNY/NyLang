#include "CodeGen.h"
#include "IRGen.h"
#include "IRCodeGen.h"
#include "Assembler.h"
#include "ElfWriter.h"
#include "PeWriter.h"

namespace nylang {

IRProgram CodeGen::generateIR(const Program& program) {
    IRGen irGen;
    return irGen.generate(program);
}

std::vector<uint8_t> CodeGen::generate(const Program& program) {
    // ── Step 1: AST → IR ──────────────────────────────────────────────────
    IRProgram ir = generateIR(program);

    // ── Step 2: [IR Optimization Passes would run here] ───────────────────
    // e.g. ConstantFoldingPass().run(ir);
    //      DeadCodeEliminationPass().run(ir);

    // ── Step 3: IR → Machine Code ─────────────────────────────────────────
    Assembler asmObj;

    // Add format strings used by CALL_PRINT
    size_t fmtInt = asmObj.addString("%d\n");
    size_t fmtStr = asmObj.addString("%s\n");

    IRCodeGen backend(m_target);
    // Give the backend the fmt string offsets
    backend.m_fmtIntOffset = fmtInt;
    backend.m_fmtStrOffset = fmtStr;
    backend.generate(ir, asmObj);

    // ── Step 4: Resolve label fixups ──────────────────────────────────────
    asmObj.resolve();

    // ── Step 5: Emit binary ───────────────────────────────────────────────
    if (m_target == Target::Windows) {
        return PeWriter::write(asmObj);
    } else {
        return ElfWriter::write(asmObj);
    }
}

} // namespace nylang
