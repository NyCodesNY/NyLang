#include "ElfWriter.h"
#include <elf.h>
#include <cstring>
#include <map>
#include <stdexcept>

namespace nylang {

// ─── Helpers ────────────────────────────────────────────────────────────────

static uint32_t addStr(std::vector<uint8_t>& table, const std::string& s) {
    uint32_t offset = static_cast<uint32_t>(table.size());
    table.insert(table.end(), s.begin(), s.end());
    table.push_back(0);
    return offset;
}

static void append(std::vector<uint8_t>& out, const void* data, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    out.insert(out.end(), p, p + len);
}

static void padTo(std::vector<uint8_t>& out, size_t alignment) {
    while (out.size() % alignment != 0)
        out.push_back(0);
}

// ─── ELF64 Object File Generation ───────────────────────────────────────────
//
// Section layout (indices):
//   0: NULL
//   1: .text       (machine code)
//   2: .data       (string literals, format strings)
//   3: .rela.text  (relocations for .text)
//   4: .symtab     (symbol table)
//   5: .strtab     (symbol names)
//   6: .shstrtab   (section names)

static const int SEC_NULL     = 0;
static const int SEC_TEXT     = 1;
static const int SEC_DATA     = 2;
static const int SEC_RELA     = 3;
static const int SEC_SYMTAB   = 4;
static const int SEC_STRTAB   = 5;
static const int SEC_SHSTRTAB = 6;
static const int NUM_SECTIONS = 7;

std::vector<uint8_t> ElfWriter::write(const Assembler& as) {

    // ── 1. Build .shstrtab (section name string table) ───────────────────
    std::vector<uint8_t> shstrtab;
    shstrtab.push_back(0);
    uint32_t sn_text     = addStr(shstrtab, ".text");
    uint32_t sn_data     = addStr(shstrtab, ".data");
    uint32_t sn_rela     = addStr(shstrtab, ".rela.text");
    uint32_t sn_symtab   = addStr(shstrtab, ".symtab");
    uint32_t sn_strtab   = addStr(shstrtab, ".strtab");
    uint32_t sn_shstrtab = addStr(shstrtab, ".shstrtab");

    // ── 2. Build .strtab and .symtab ─────────────────────────────────────
    std::vector<uint8_t> strtab;
    strtab.push_back(0);  // index 0 = empty string

    std::vector<Elf64_Sym> symbols;

    // [0] NULL symbol (required)
    {
        Elf64_Sym s = {};
        symbols.push_back(s);
    }

    // [1] .text section symbol
    {
        Elf64_Sym s = {};
        s.st_info  = ELF64_ST_INFO(STB_LOCAL, STT_SECTION);
        s.st_shndx = SEC_TEXT;
        symbols.push_back(s);
    }

    // [2] .data section symbol
    {
        Elf64_Sym s = {};
        s.st_info  = ELF64_ST_INFO(STB_LOCAL, STT_SECTION);
        s.st_shndx = SEC_DATA;
        symbols.push_back(s);
    }

    // Index of first non-local symbol (for sh_info of .symtab)
    uint32_t firstGlobal = static_cast<uint32_t>(symbols.size());

    // [3..N] Global function symbols (e.g. "main")
    for (const auto& gs : as.globalSymbols()) {
        Elf64_Sym s = {};
        s.st_name  = addStr(strtab, gs.name);
        s.st_info  = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC);
        s.st_shndx = SEC_TEXT;
        s.st_value = gs.textOffset;
        symbols.push_back(s);
    }

    // [N+1..M] External symbols (e.g. "printf")
    std::map<std::string, uint32_t> externIdx;
    for (const auto& er : as.externRelocs()) {
        if (externIdx.count(er.symbol)) continue;
        uint32_t idx = static_cast<uint32_t>(symbols.size());
        externIdx[er.symbol] = idx;

        Elf64_Sym s = {};
        s.st_name  = addStr(strtab, er.symbol);
        s.st_info  = ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE);
        s.st_shndx = SHN_UNDEF;
        symbols.push_back(s);
    }

    // ── 3. Build .rela.text ──────────────────────────────────────────────
    std::vector<Elf64_Rela> relas;

    // Data relocations: lea [rip+disp32] referencing .data
    //   type = R_X86_64_PC32, symbol = .data section (index 2)
    //   addend = dataOffset - 4   (−4 for RIP-relative adjustment)
    for (const auto& dr : as.dataRelocs()) {
        Elf64_Rela r = {};
        r.r_offset = dr.textOffset;
        r.r_info   = ELF64_R_INFO(SEC_DATA, R_X86_64_PC32);
        r.r_addend = static_cast<int64_t>(dr.dataOffset) - 4;
        relas.push_back(r);
    }

    // External call relocations: call via PLT
    //   type = R_X86_64_PLT32
    //   addend = −4
    for (const auto& er : as.externRelocs()) {
        Elf64_Rela r = {};
        r.r_offset = er.textOffset;
        r.r_info   = ELF64_R_INFO(externIdx[er.symbol], R_X86_64_PLT32);
        r.r_addend = -4;
        relas.push_back(r);
    }

    // ── 4. Build the output file linearly ────────────────────────────────
    //
    // We reserve space for the ELF header, then append each section's data
    // and record the actual file offsets as we go.

    std::vector<uint8_t> out;
    out.resize(sizeof(Elf64_Ehdr), 0); // placeholder for ELF header

    // .text
    size_t off_text = out.size();
    append(out, as.textBytes().data(), as.textBytes().size());

    // .data (aligned to 8)
    padTo(out, 8);
    size_t off_data = out.size();
    append(out, as.dataBytes().data(), as.dataBytes().size());

    // .rela.text (aligned to 8)
    padTo(out, 8);
    size_t off_rela = out.size();
    for (const auto& r : relas)
        append(out, &r, sizeof(r));

    // .symtab (aligned to 8)
    padTo(out, 8);
    size_t off_symtab = out.size();
    for (const auto& s : symbols)
        append(out, &s, sizeof(s));

    // .strtab
    size_t off_strtab = out.size();
    append(out, strtab.data(), strtab.size());

    // .shstrtab
    size_t off_shstrtab = out.size();
    append(out, shstrtab.data(), shstrtab.size());

    // Section header table (aligned to 8)
    padTo(out, 8);
    size_t off_shdr = out.size();

    // ── 5. Build section headers ─────────────────────────────────────────
    Elf64_Shdr shdrs[NUM_SECTIONS] = {};

    // [0] NULL — already zeroed

    // [1] .text
    shdrs[SEC_TEXT].sh_name      = sn_text;
    shdrs[SEC_TEXT].sh_type      = SHT_PROGBITS;
    shdrs[SEC_TEXT].sh_flags     = SHF_ALLOC | SHF_EXECINSTR;
    shdrs[SEC_TEXT].sh_offset    = off_text;
    shdrs[SEC_TEXT].sh_size      = as.textBytes().size();
    shdrs[SEC_TEXT].sh_addralign = 16;

    // [2] .data
    shdrs[SEC_DATA].sh_name      = sn_data;
    shdrs[SEC_DATA].sh_type      = SHT_PROGBITS;
    shdrs[SEC_DATA].sh_flags     = SHF_ALLOC | SHF_WRITE;
    shdrs[SEC_DATA].sh_offset    = off_data;
    shdrs[SEC_DATA].sh_size      = as.dataBytes().size();
    shdrs[SEC_DATA].sh_addralign = 8;

    // [3] .rela.text
    shdrs[SEC_RELA].sh_name      = sn_rela;
    shdrs[SEC_RELA].sh_type      = SHT_RELA;
    shdrs[SEC_RELA].sh_flags     = SHF_INFO_LINK;
    shdrs[SEC_RELA].sh_offset    = off_rela;
    shdrs[SEC_RELA].sh_size      = relas.size() * sizeof(Elf64_Rela);
    shdrs[SEC_RELA].sh_link      = SEC_SYMTAB;
    shdrs[SEC_RELA].sh_info      = SEC_TEXT; // relocations apply to .text
    shdrs[SEC_RELA].sh_addralign = 8;
    shdrs[SEC_RELA].sh_entsize   = sizeof(Elf64_Rela);

    // [4] .symtab
    shdrs[SEC_SYMTAB].sh_name      = sn_symtab;
    shdrs[SEC_SYMTAB].sh_type      = SHT_SYMTAB;
    shdrs[SEC_SYMTAB].sh_offset    = off_symtab;
    shdrs[SEC_SYMTAB].sh_size      = symbols.size() * sizeof(Elf64_Sym);
    shdrs[SEC_SYMTAB].sh_link      = SEC_STRTAB;
    shdrs[SEC_SYMTAB].sh_info      = firstGlobal;
    shdrs[SEC_SYMTAB].sh_addralign = 8;
    shdrs[SEC_SYMTAB].sh_entsize   = sizeof(Elf64_Sym);

    // [5] .strtab
    shdrs[SEC_STRTAB].sh_name      = sn_strtab;
    shdrs[SEC_STRTAB].sh_type      = SHT_STRTAB;
    shdrs[SEC_STRTAB].sh_offset    = off_strtab;
    shdrs[SEC_STRTAB].sh_size      = strtab.size();
    shdrs[SEC_STRTAB].sh_addralign = 1;

    // [6] .shstrtab
    shdrs[SEC_SHSTRTAB].sh_name      = sn_shstrtab;
    shdrs[SEC_SHSTRTAB].sh_type      = SHT_STRTAB;
    shdrs[SEC_SHSTRTAB].sh_offset    = off_shstrtab;
    shdrs[SEC_SHSTRTAB].sh_size      = shstrtab.size();
    shdrs[SEC_SHSTRTAB].sh_addralign = 1;

    // Write section headers
    for (int i = 0; i < NUM_SECTIONS; i++)
        append(out, &shdrs[i], sizeof(Elf64_Shdr));

    // ── 6. Fill in ELF header ────────────────────────────────────────────
    Elf64_Ehdr ehdr = {};
    ehdr.e_ident[EI_MAG0]    = ELFMAG0;       // 0x7F
    ehdr.e_ident[EI_MAG1]    = ELFMAG1;       // 'E'
    ehdr.e_ident[EI_MAG2]    = ELFMAG2;       // 'L'
    ehdr.e_ident[EI_MAG3]    = ELFMAG3;       // 'F'
    ehdr.e_ident[EI_CLASS]   = ELFCLASS64;
    ehdr.e_ident[EI_DATA]    = ELFDATA2LSB;
    ehdr.e_ident[EI_VERSION] = EV_CURRENT;
    ehdr.e_ident[EI_OSABI]   = ELFOSABI_SYSV;
    ehdr.e_type      = ET_REL;
    ehdr.e_machine   = EM_X86_64;
    ehdr.e_version   = EV_CURRENT;
    ehdr.e_shoff     = off_shdr;
    ehdr.e_ehsize    = sizeof(Elf64_Ehdr);
    ehdr.e_shentsize = sizeof(Elf64_Shdr);
    ehdr.e_shnum     = NUM_SECTIONS;
    ehdr.e_shstrndx  = SEC_SHSTRTAB;

    // Patch the header at the beginning of the output
    std::memcpy(out.data(), &ehdr, sizeof(ehdr));

    return out;
}

} // namespace nylang
