#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>

namespace nylang {

// ─── Register Encoding ──────────────────────────────────────────────────────

enum Reg : uint8_t {
    RAX = 0, RCX = 1, RDX = 2, RBX = 3,
    RSP = 4, RBP = 5, RSI = 6, RDI = 7,
    R8  = 8, R9  = 9, R10 = 10, R11 = 11,
    R12 = 12, R13 = 13, R14 = 14, R15 = 15,
};

// ─── Relocation / Fixup Structures ──────────────────────────────────────────

/// A reference from .text to a location in .data (for lea [rip+rel32])
struct DataReloc {
    uint32_t textOffset;  // offset of the rel32 field in .text
    uint32_t dataOffset;  // target offset within .data
};

/// A reference from .text to an external symbol (for call via PLT)
struct ExternReloc {
    uint32_t textOffset;  // offset of the rel32 field in .text
    std::string symbol;   // external symbol name (e.g., "printf")
};

/// A reference from .text to a Windows IAT entry (for call [rip+disp32])
struct ImportReloc {
    uint32_t textOffset;  // offset of the disp32 field in .text
    std::string symbol;   // imported function name
};

/// A forward/backward label reference to backpatch within .text
struct LabelFixup {
    uint32_t textOffset;  // offset of the rel32 placeholder in .text
    std::string label;    // label name
};

/// An exported global symbol
struct GlobalSymbol {
    std::string name;      // symbol name (e.g., "main")
    uint32_t textOffset;   // offset in .text where the symbol is defined
};

// ─── Assembler ──────────────────────────────────────────────────────────────

class Assembler {
public:
    // ── Data Section ─────────────────────────────────────────────────────
    size_t addData(const void* bytes, size_t len);
    size_t addString(const std::string& str);  // null-terminated
    size_t addZeros(size_t len);               // zero-initialized buffer

    // ── Labels & Symbols ─────────────────────────────────────────────────
    void defineLabel(const std::string& name);
    void defineGlobal(const std::string& name);

    // ── x86_64 Instructions ──────────────────────────────────────────────

    // Stack & control
    void pushReg(Reg r);
    void popReg(Reg r);
    void ret();
    void syscall();

    // Register-to-register moves
    void movRegReg(Reg dst, Reg src);

    // Load immediate
    void movRegImm(Reg r, int64_t imm);

    // Memory operations (base + displacement)
    void movMemReg(Reg base, int32_t disp, Reg src);   // [base+disp] = src
    void movRegMem(Reg dst, Reg base, int32_t disp);   // dst = [base+disp]
    void movByteMemImm(Reg base, int32_t disp, uint8_t imm); // byte [base+disp] = imm

    // Arithmetic
    void xorReg32(Reg dst, Reg src);
    void addRegReg(Reg dst, Reg src);
    void subRegReg(Reg dst, Reg src);
    void subRegImm(Reg r, int32_t imm);
    void addRegImm(Reg r, int32_t imm);
    void imulRegReg(Reg dst, Reg src);
    void cqo();
    void idivReg(Reg r);

    // Comparisons
    void cmpRegImm(Reg r, int8_t imm);
    void cmpRegReg(Reg r1, Reg r2);
    void cmpByteMemImm(Reg base, int32_t disp, int8_t imm);

    // Conditional set (always operates on AL)
    void sete();
    void setne();
    void setl();
    void setg();

    // Zero-extend AL to RAX
    void movzxRaxAl();

    // LEA with RIP-relative data reference (records relocation)
    void leaRipRel(Reg dst, uint32_t dataOffset);

    // External function call via PLT (records relocation, for Linux/ELF)
    void callExtern(const std::string& symbol);

    // Indirect call through IAT (records import relocation, for Windows/PE)
    void callImport(const std::string& symbol);

    // Conditional/unconditional jumps and calls (uses label fixups)
    void call(const std::string& label);
    void je(const std::string& label);
    void jne(const std::string& label);
    void jmp(const std::string& label);

    // ── Finalization ─────────────────────────────────────────────────────
    void resolve();  // backpatch all label references

    // ── Accessors ────────────────────────────────────────────────────────
    const std::vector<uint8_t>& textBytes() const { return m_text; }
    const std::vector<uint8_t>& dataBytes() const { return m_data; }
    const std::vector<DataReloc>& dataRelocs() const { return m_dataRelocs; }
    const std::vector<ExternReloc>& externRelocs() const { return m_externRelocs; }
    const std::vector<ImportReloc>& importRelocs() const { return m_importRelocs; }
    const std::vector<GlobalSymbol>& globalSymbols() const { return m_globals; }

private:
    std::vector<uint8_t> m_text;
    std::vector<uint8_t> m_data;

    std::map<std::string, uint32_t> m_labels;
    std::vector<LabelFixup> m_fixups;
    std::vector<DataReloc> m_dataRelocs;
    std::vector<ExternReloc> m_externRelocs;
    std::vector<ImportReloc> m_importRelocs;
    std::vector<GlobalSymbol> m_globals;

    // ── Encoding helpers ─────────────────────────────────────────────────
    void emit8(uint8_t b);
    void emit32(uint32_t v);
    void emit64(uint64_t v);
    
    /// Emits a REX prefix.
    /// w: true for 64-bit operand size.
    /// r: Reg used in the ModR/M 'reg' field.
    /// x: Reg used in the SIB 'index' field (pass RAX if unused).
    /// b: Reg used in the ModR/M 'r/m' field, SIB 'base' field, or Opcode 'reg' field.
    void emitRex(bool w, Reg r, Reg x, Reg b);
    void emitRex(bool w, Reg r, Reg b); // Overload for no index

    uint32_t textPos() const;

    /// Emit a ModR/M byte + displacement for [base + disp]
    void emitModRMMemDisp(uint8_t regField, Reg base, int32_t disp);
};

} // namespace nylang
