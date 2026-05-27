#include "Assembler.h"
#include <stdexcept>
#include <cstring>

namespace nylang {

// ─── Encoding Helpers ───────────────────────────────────────────────────────

void Assembler::emit8(uint8_t b) {
    m_text.push_back(b);
}

void Assembler::emit32(uint32_t v) {
    m_text.push_back(v & 0xFF);
    m_text.push_back((v >> 8) & 0xFF);
    m_text.push_back((v >> 16) & 0xFF);
    m_text.push_back((v >> 24) & 0xFF);
}

void Assembler::emit64(uint64_t v) {
    for (int i = 0; i < 8; i++) {
        m_text.push_back((v >> (i * 8)) & 0xFF);
    }
}

void Assembler::emitRex(bool w, Reg r, Reg x, Reg b) {
    uint8_t rex = 0x40;
    if (w) rex |= 0x08;
    if (r >= 8) rex |= 0x04;
    if (x >= 8) rex |= 0x02;
    if (b >= 8) rex |= 0x01;
    // Emit REX prefix if any of the bits are set
    if (rex != 0x40) {
        emit8(rex);
    }
}

void Assembler::emitRex(bool w, Reg r, Reg b) {
    emitRex(w, r, RAX, b);
}

uint32_t Assembler::textPos() const {
    return static_cast<uint32_t>(m_text.size());
}

void Assembler::emitModRMMemDisp(uint8_t regField, Reg base, int32_t disp) {
    // Note: base=RSP/R12 (which have lower 3 bits = 4) would require a SIB byte.
    // For now we only use RBP.
    uint8_t r = regField & 7;
    uint8_t b = base & 7;
    if (disp >= -128 && disp <= 127) {
        // mod=01: [base + disp8]
        emit8(0x40 | (r << 3) | b);
        emit8(static_cast<uint8_t>(static_cast<int8_t>(disp)));
    } else {
        // mod=10: [base + disp32]
        emit8(0x80 | (r << 3) | b);
        emit32(static_cast<uint32_t>(disp));
    }
}

// ─── Data Section ───────────────────────────────────────────────────────────

size_t Assembler::addData(const void* bytes, size_t len) {
    size_t offset = m_data.size();
    const uint8_t* ptr = static_cast<const uint8_t*>(bytes);
    m_data.insert(m_data.end(), ptr, ptr + len);
    return offset;
}

size_t Assembler::addString(const std::string& str) {
    size_t offset = m_data.size();
    m_data.insert(m_data.end(), str.begin(), str.end());
    m_data.push_back(0); // null terminator
    return offset;
}

size_t Assembler::addZeros(size_t len) {
    size_t offset = m_data.size();
    m_data.resize(m_data.size() + len, 0);
    return offset;
}

// ─── Labels & Symbols ───────────────────────────────────────────────────────

void Assembler::defineLabel(const std::string& name) {
    m_labels[name] = textPos();
}

void Assembler::defineGlobal(const std::string& name) {
    m_labels[name] = textPos();
    m_globals.push_back({name, textPos()});
}

// ─── Stack & Control ────────────────────────────────────────────────────────

void Assembler::pushReg(Reg r) {
    emitRex(false, RAX, RAX, r); // Only need REX.B if r >= 8
    // PUSH r64: 50+rd
    emit8(0x50 + (r & 7));
}

void Assembler::popReg(Reg r) {
    emitRex(false, RAX, RAX, r); // Only need REX.B if r >= 8
    // POP r64: 58+rd
    emit8(0x58 + (r & 7));
}

void Assembler::ret() {
    // RET: C3
    emit8(0xC3);
}

void Assembler::syscall() {
    // SYSCALL: 0F 05
    emit8(0x0F);
    emit8(0x05);
}

// ─── Register-to-Register Moves ─────────────────────────────────────────────

void Assembler::movRegReg(Reg dst, Reg src) {
    emitRex(true, src, RAX, dst);
    // MOV r/m64, r64: REX.W(48) + 89 + ModR/M(11, src, dst)
    emit8(0x89);
    emit8(0xC0 | ((src & 7) << 3) | (dst & 7));
}

// ─── Immediate Loads ────────────────────────────────────────────────────────

void Assembler::movRegImm(Reg r, int64_t imm) {
    if (imm >= 0 && imm <= 0x7FFFFFFF) {
        emitRex(false, RAX, RAX, r); // Only need REX.B if r >= 8
        // MOV r32, imm32 (zero-extends to r64): B8+rd + imm32
        emit8(0xB8 + (r & 7));
        emit32(static_cast<uint32_t>(imm));
    } else {
        emitRex(true, RAX, RAX, r); // Need REX.W and possibly REX.B
        // MOV r64, imm64: REX.W(48) + B8+rd + imm64
        emit8(0xB8 + (r & 7));
        emit64(static_cast<uint64_t>(imm));
    }
}

// ─── Memory Operations ──────────────────────────────────────────────────────

void Assembler::movMemReg(Reg base, int32_t disp, Reg src) {
    emitRex(true, src, RAX, base);
    // MOV [base+disp], src: REX.W(48) + 89 + ModR/M
    emit8(0x89);
    emitModRMMemDisp(src, base, disp);
}

void Assembler::movRegMem(Reg dst, Reg base, int32_t disp) {
    emitRex(true, dst, RAX, base);
    // MOV dst, [base+disp]: REX.W(48) + 8B + ModR/M
    emit8(0x8B);
    emitModRMMemDisp(dst, base, disp);
}

void Assembler::movByteMemImm(Reg base, int32_t disp, uint8_t imm) {
    emitRex(false, RAX, RAX, base);
    // MOV byte [base+disp], imm8: C6 + ModR/M(0, base)
    emit8(0xC6);
    emitModRMMemDisp(0, base, disp);
    emit8(imm);
}

// ─── Arithmetic ─────────────────────────────────────────────────────────────

void Assembler::xorReg32(Reg dst, Reg src) {
    emitRex(false, src, RAX, dst);
    // XOR r/m32, r32: 31 + ModR/M(11, src, dst)
    // No REX.W → 32-bit, upper 32 bits zero-extended
    emit8(0x31);
    emit8(0xC0 | ((src & 7) << 3) | (dst & 7));
}

void Assembler::addRegReg(Reg dst, Reg src) {
    emitRex(true, src, RAX, dst);
    // ADD r/m64, r64: REX.W(48) + 01 + ModR/M(11, src, dst)
    emit8(0x01);
    emit8(0xC0 | ((src & 7) << 3) | (dst & 7));
}

void Assembler::addRegImm(Reg r, int32_t imm) {
    emitRex(true, RAX, RAX, r);
    // ADD r/m64, imm32: REX.W(48) + 81 + ModR/M(0, r)
    emit8(0x81);
    emit8(0xC0 | (r & 7));
    emit32(static_cast<uint32_t>(imm));
}

void Assembler::subRegReg(Reg dst, Reg src) {
    emitRex(true, src, RAX, dst);
    // SUB r/m64, r64: REX.W(48) + 29 + ModR/M(11, src, dst)
    emit8(0x29);
    emit8(0xC0 | ((src & 7) << 3) | (dst & 7));
}

void Assembler::subRegImm(Reg r, int32_t imm) {
    emitRex(true, RAX, RAX, r); // REX.W, maybe REX.B
    if (imm >= -128 && imm <= 127) {
        // SUB r/m64, imm8: 83 /5 ib
        emit8(0x83);
        emit8(0xC0 | (5 << 3) | (r & 7));
        emit8(static_cast<uint8_t>(static_cast<int8_t>(imm)));
    } else {
        // SUB r/m64, imm32: 81 /5 id
        emit8(0x81);
        emit8(0xC0 | (5 << 3) | (r & 7));
        emit32(static_cast<uint32_t>(imm));
    }
}

void Assembler::imulRegReg(Reg dst, Reg src) {
    emitRex(true, dst, RAX, src);
    // IMUL r64, r/m64: REX.W(48) + 0F AF + ModR/M(11, dst, src)
    emit8(0x0F);
    emit8(0xAF);
    emit8(0xC0 | ((dst & 7) << 3) | (src & 7));
}

void Assembler::cqo() {
    // CQO: REX.W(48) + 99
    emit8(0x48); // Fixed, doesn't use R8-R15
    emit8(0x99);
}

void Assembler::idivReg(Reg r) {
    emitRex(true, RAX, RAX, r);
    // IDIV r/m64: REX.W(48) + F7 /7
    emit8(0xF7);
    emit8(0xC0 | (7 << 3) | (r & 7));
}

// ─── Comparisons ────────────────────────────────────────────────────────────

void Assembler::cmpRegImm(Reg r, int8_t imm) {
    emitRex(true, RAX, RAX, r);
    // CMP r/m64, imm8: REX.W(48) + 83 /7 ib
    emit8(0x83);
    emit8(0xC0 | (7 << 3) | (r & 7));
    emit8(static_cast<uint8_t>(imm));
}

void Assembler::cmpRegReg(Reg r1, Reg r2) {
    emitRex(true, r2, RAX, r1);
    // CMP r/m64, r64: REX.W(48) + 39 + ModR/M(11, r2, r1)
    emit8(0x39);
    emit8(0xC0 | ((r2 & 7) << 3) | (r1 & 7));
}

void Assembler::cmpByteMemImm(Reg base, int32_t disp, int8_t imm) {
    emitRex(false, RAX, RAX, base);
    // CMP byte [base+disp], imm8: 80 /7 ib
    emit8(0x80);
    emitModRMMemDisp(7, base, disp);
    emit8(static_cast<uint8_t>(imm));
}

// ─── Conditional Set (always on AL) ─────────────────────────────────────────

void Assembler::sete()  { emit8(0x0F); emit8(0x94); emit8(0xC0); }
void Assembler::setne() { emit8(0x0F); emit8(0x95); emit8(0xC0); }
void Assembler::setl()  { emit8(0x0F); emit8(0x9C); emit8(0xC0); }
void Assembler::setg()  { emit8(0x0F); emit8(0x9F); emit8(0xC0); }

void Assembler::movzxRaxAl() {
    // MOVZX rax, al: REX.W(48) + 0F B6 + ModR/M(11, 0, 0)
    emit8(0x48);
    emit8(0x0F);
    emit8(0xB6);
    emit8(0xC0);
}

// ─── RIP-Relative LEA (data reference) ──────────────────────────────────────

void Assembler::leaRipRel(Reg dst, uint32_t dataOffset) {
    emitRex(true, dst, RAX, RAX);
    // LEA dst, [rip+disp32]: REX.W(48) + 8D + ModR/M(00, dst, 101=rip)
    emit8(0x8D);
    emit8(0x00 | ((dst & 7) << 3) | 0x05);
    uint32_t fixupPos = textPos();
    emit32(0x00000000); // placeholder — resolved by linker
    m_dataRelocs.push_back({fixupPos, dataOffset});
}

// ─── External Call (PLT relocation) ─────────────────────────────────────────

void Assembler::callExtern(const std::string& symbol) {
    // CALL rel32: E8 + rel32
    emit8(0xE8);
    uint32_t fixupPos = textPos();
    emit32(0x00000000); // placeholder — resolved by linker
    m_externRelocs.push_back({fixupPos, symbol});
}

void Assembler::callImport(const std::string& symbol) {
    // CALL r/m64: FF /2. With RIP-relative addressing it's FF 15 disp32.
    // FF 15 = call QWORD PTR [rip+disp32]
    emit8(0xFF);
    emit8(0x15);
    uint32_t fixupPos = textPos();
    emit32(0x00000000);
    m_importRelocs.push_back({fixupPos, symbol});
}

// ─── Conditional / Unconditional Jumps and Calls ────────────────────────────

void Assembler::call(const std::string& label) {
    // CALL rel32: E8 + rel32
    emit8(0xE8);
    uint32_t fixupPos = textPos();
    emit32(0x00000000); // placeholder
    m_fixups.push_back({fixupPos, label});
}

void Assembler::je(const std::string& label) {
    // JE rel32: 0F 84 + rel32
    emit8(0x0F);
    emit8(0x84);
    uint32_t fixupPos = textPos();
    emit32(0x00000000); // placeholder — resolved in resolve()
    m_fixups.push_back({fixupPos, label});
}

void Assembler::jne(const std::string& label) {
    // JNE rel32: 0F 85 + rel32
    emit8(0x0F);
    emit8(0x85);
    uint32_t fixupPos = textPos();
    emit32(0x00000000);
    m_fixups.push_back({fixupPos, label});
}

void Assembler::jmp(const std::string& label) {
    // JMP rel32: E9 + rel32
    emit8(0xE9);
    uint32_t fixupPos = textPos();
    emit32(0x00000000);
    m_fixups.push_back({fixupPos, label});
}

// ─── Finalization ───────────────────────────────────────────────────────────

void Assembler::resolve() {
    for (const auto& fixup : m_fixups) {
        auto it = m_labels.find(fixup.label);
        if (it == m_labels.end()) {
            throw std::runtime_error(
                "Assembler: unresolved label '" + fixup.label + "'");
        }
        int32_t target = static_cast<int32_t>(it->second);
        int32_t from = static_cast<int32_t>(fixup.textOffset + 4);
        int32_t rel = target - from;

        m_text[fixup.textOffset + 0] = static_cast<uint8_t>(rel & 0xFF);
        m_text[fixup.textOffset + 1] = static_cast<uint8_t>((rel >> 8) & 0xFF);
        m_text[fixup.textOffset + 2] = static_cast<uint8_t>((rel >> 16) & 0xFF);
        m_text[fixup.textOffset + 3] = static_cast<uint8_t>((rel >> 24) & 0xFF);
    }
}

} // namespace nylang
