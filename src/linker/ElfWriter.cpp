#include "ElfWriter.h"
#include <elf.h>
#include <cstring>
#include <map>
#include <stdexcept>
#include <iostream>

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

static size_t alignUp(size_t val, size_t alignment) {
    return (val + alignment - 1) & ~(alignment - 1);
}

// ─── ELF64 Standalone Executable Generation ─────────────────────────────────

std::vector<uint8_t> ElfWriter::write(const Assembler& as) {
    uint64_t BASE_ADDR = 0x400000;
    uint64_t PAGE_SIZE = 0x1000;

    // The libc functions we currently use
    std::vector<std::string> imports = {"printf", "malloc", "free"};

    // ─── 1. Build Tables ───────────────────────────────────────────────────
    
    // .interp
    std::string interpStr = "/lib64/ld-linux-x86-64.so.2";
    
    // .dynstr
    std::vector<uint8_t> dynstr;
    dynstr.push_back(0);
    uint32_t libcStrOff = addStr(dynstr, "libc.so.6");
    std::map<std::string, uint32_t> importStrOff;
    for (const auto& imp : imports) {
        importStrOff[imp] = addStr(dynstr, imp);
    }
    
    // .dynsym
    std::vector<Elf64_Sym> dynsym;
    dynsym.push_back({}); // [0] NULL symbol
    
    std::map<std::string, uint32_t> importSymIdx;
    for (const auto& imp : imports) {
        importSymIdx[imp] = static_cast<uint32_t>(dynsym.size());
        Elf64_Sym s = {};
        s.st_name = importStrOff[imp];
        s.st_info = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC);
        s.st_other = 0;
        s.st_shndx = SHN_UNDEF;
        s.st_value = 0;
        s.st_size = 0;
        dynsym.push_back(s);
    }
    
    // .rela.plt
    // Will be filled once we know the .got.plt address.
    std::vector<Elf64_Rela> relaplt(imports.size());
    for (size_t i = 0; i < imports.size(); i++) {
        relaplt[i] = {};
        // R_X86_64_JUMP_SLOT is 7
        relaplt[i].r_info = ELF64_R_INFO(importSymIdx[imports[i]], 7); 
        relaplt[i].r_addend = 0;
    }
    
    // .plt
    std::vector<uint8_t> plt;
    plt.resize(16, 0); 
    plt[0] = 0xff; plt[1] = 0x35; // pushq GOT[1]
    plt[6] = 0xff; plt[7] = 0x25; // jmpq *GOT[2]
    plt[12] = 0x0f; plt[13] = 0x1f; plt[14] = 0x40; plt[15] = 0x00; // nopl
    
    std::map<std::string, uint32_t> pltStubOffsets;
    for (size_t i = 0; i < imports.size(); i++) {
        pltStubOffsets[imports[i]] = static_cast<uint32_t>(plt.size());
        
        // jmpq *GOT[i+3]
        plt.push_back(0xff); plt.push_back(0x25);
        plt.push_back(0); plt.push_back(0); plt.push_back(0); plt.push_back(0);
        
        // pushq index
        plt.push_back(0x68);
        uint32_t idx = static_cast<uint32_t>(i);
        append(plt, &idx, 4);
        
        // jmpq PLT0
        plt.push_back(0xe9);
        plt.push_back(0); plt.push_back(0); plt.push_back(0); plt.push_back(0);
    }
    
    // ─── 2. Layout RVAs ────────────────────────────────────────────────────
    size_t ehdrSize = sizeof(Elf64_Ehdr);
    size_t phdrSize = 4 * sizeof(Elf64_Phdr); 
    
    size_t currentRva = ehdrSize + phdrSize;
    
    size_t rva_interp = currentRva;
    currentRva += interpStr.length() + 1;
    
    size_t rva_dynstr = currentRva;
    currentRva += dynstr.size();
    
    size_t rva_hash = alignUp(currentRva, 8);
    currentRva = rva_hash + 16; 
    
    size_t rva_dynsym = alignUp(currentRva, 8);
    currentRva = rva_dynsym + dynsym.size() * sizeof(Elf64_Sym);
    
    size_t rva_relaplt = alignUp(currentRva, 8);
    currentRva = rva_relaplt + relaplt.size() * sizeof(Elf64_Rela);
    
    size_t rva_plt = alignUp(currentRva, 16);
    currentRva = rva_plt + plt.size();
    
    size_t rva_text = alignUp(currentRva, 16);
    currentRva = rva_text + as.textBytes().size();
    
    // RW segment alignment
    size_t rva_rw = alignUp(currentRva, PAGE_SIZE);
    currentRva = rva_rw;
    
    size_t rva_dynamic = currentRva;
    size_t numDynEntries = 11;
    currentRva += numDynEntries * sizeof(Elf64_Dyn);
    
    size_t rva_gotplt = alignUp(currentRva, 8);
    size_t gotpltSize = (3 + imports.size()) * 8;
    currentRva = rva_gotplt + gotpltSize;
    
    size_t rva_data = alignUp(currentRva, 8);
    currentRva = rva_data + as.dataBytes().size();
    
    size_t file_size = currentRva;
    
    // ─── 3. Patch contents ─────────────────────────────────────────────────
    
    for (size_t i = 0; i < imports.size(); i++) {
        relaplt[i].r_offset = BASE_ADDR + rva_gotplt + (3 + i) * 8;
    }
    
    // PLT Header jumps
    uint32_t got1_disp = (rva_gotplt + 8) - (rva_plt + 6);
    std::memcpy(&plt[2], &got1_disp, 4);
    
    uint32_t got2_disp = (rva_gotplt + 16) - (rva_plt + 12);
    std::memcpy(&plt[8], &got2_disp, 4);
    
    for (size_t i = 0; i < imports.size(); i++) {
        size_t stubOff = pltStubOffsets[imports[i]];
        
        uint32_t gotDisp = (rva_gotplt + (3 + i) * 8) - (rva_plt + stubOff + 6);
        std::memcpy(&plt[stubOff + 2], &gotDisp, 4);
        
        int32_t plt0Disp = static_cast<int32_t>(rva_plt) - static_cast<int32_t>(rva_plt + stubOff + 16);
        std::memcpy(&plt[stubOff + 12], &plt0Disp, 4);
    }
    
    // Text Relocations
    std::vector<uint8_t> textBytes = as.textBytes();
    for (const auto& r : as.dataRelocs()) {
        uint32_t targetRva = rva_data + r.dataOffset;
        uint32_t instRva = rva_text + r.textOffset;
        uint32_t rel = targetRva - (instRva + 4); 
        std::memcpy(&textBytes[r.textOffset], &rel, 4);
    }
    for (const auto& r : as.externRelocs()) {
        if (pltStubOffsets.find(r.symbol) == pltStubOffsets.end()) {
            throw std::runtime_error("ElfWriter: Unknown external symbol: " + r.symbol);
        }
        uint32_t stubRva = rva_plt + pltStubOffsets[r.symbol];
        uint32_t instRva = rva_text + r.textOffset;
        uint32_t rel = stubRva - (instRva + 4);
        std::memcpy(&textBytes[r.textOffset], &rel, 4);
    }
    
    // GOT
    std::vector<uint64_t> gotplt(3 + imports.size(), 0);
    gotplt[0] = BASE_ADDR + rva_dynamic;
    for (size_t i = 0; i < imports.size(); i++) {
        gotplt[3 + i] = BASE_ADDR + rva_plt + pltStubOffsets[imports[i]] + 6; 
    }
    
    // Dynamic Table
    std::vector<Elf64_Dyn> dynamic;
    auto addDyn = [&](int64_t tag, uint64_t val) {
        Elf64_Dyn d;
        d.d_tag = tag;
        d.d_un.d_val = val;
        dynamic.push_back(d);
    };
    addDyn(DT_NEEDED, libcStrOff);
    addDyn(DT_SYMTAB, BASE_ADDR + rva_dynsym);
    addDyn(DT_SYMENT, sizeof(Elf64_Sym));
    addDyn(DT_STRTAB, BASE_ADDR + rva_dynstr);
    addDyn(DT_STRSZ, dynstr.size());
    addDyn(DT_JMPREL, BASE_ADDR + rva_relaplt);
    addDyn(DT_PLTRELSZ, relaplt.size() * sizeof(Elf64_Rela));
    addDyn(DT_PLTGOT, BASE_ADDR + rva_gotplt);
    addDyn(DT_PLTREL, 7); // DT_RELA
    addDyn(DT_HASH, BASE_ADDR + rva_hash);
    addDyn(DT_NULL, 0);
    
    std::vector<uint32_t> hash = {1, static_cast<uint32_t>(dynsym.size()), 0, 0};
    
    uint64_t entryRva = rva_text;
    for (const auto& sym : as.globalSymbols()) {
        if (sym.name == "main") {
            entryRva = rva_text + sym.textOffset;
            break;
        }
    }
    
    // ─── 4. Build Output File ──────────────────────────────────────────────
    std::vector<uint8_t> out(file_size, 0);
    
    Elf64_Ehdr ehdr = {};
    ehdr.e_ident[EI_MAG0]    = ELFMAG0;
    ehdr.e_ident[EI_MAG1]    = ELFMAG1;
    ehdr.e_ident[EI_MAG2]    = ELFMAG2;
    ehdr.e_ident[EI_MAG3]    = ELFMAG3;
    ehdr.e_ident[EI_CLASS]   = ELFCLASS64;
    ehdr.e_ident[EI_DATA]    = ELFDATA2LSB;
    ehdr.e_ident[EI_VERSION] = EV_CURRENT;
    ehdr.e_ident[EI_OSABI]   = ELFOSABI_SYSV;
    ehdr.e_type      = ET_EXEC;
    ehdr.e_machine   = EM_X86_64;
    ehdr.e_version   = EV_CURRENT;
    ehdr.e_entry     = BASE_ADDR + entryRva;
    ehdr.e_phoff     = ehdrSize;
    ehdr.e_shoff     = 0;
    ehdr.e_ehsize    = sizeof(Elf64_Ehdr);
    ehdr.e_phentsize = sizeof(Elf64_Phdr);
    ehdr.e_phnum     = 4;
    ehdr.e_shentsize = 0;
    ehdr.e_shnum     = 0;
    ehdr.e_shstrndx  = 0;
    std::memcpy(out.data(), &ehdr, sizeof(ehdr));
    
    Elf64_Phdr phdrs[4] = {};
    
    phdrs[0].p_type   = PT_INTERP;
    phdrs[0].p_flags  = PF_R;
    phdrs[0].p_offset = rva_interp;
    phdrs[0].p_vaddr  = BASE_ADDR + rva_interp;
    phdrs[0].p_paddr  = BASE_ADDR + rva_interp;
    phdrs[0].p_filesz = interpStr.length() + 1;
    phdrs[0].p_memsz  = interpStr.length() + 1;
    phdrs[0].p_align  = 1;
    
    phdrs[1].p_type   = PT_LOAD;
    phdrs[1].p_flags  = PF_R | PF_X;
    phdrs[1].p_offset = 0;
    phdrs[1].p_vaddr  = BASE_ADDR;
    phdrs[1].p_paddr  = BASE_ADDR;
    phdrs[1].p_filesz = rva_text + textBytes.size();
    phdrs[1].p_memsz  = rva_text + textBytes.size();
    phdrs[1].p_align  = PAGE_SIZE;
    
    phdrs[2].p_type   = PT_LOAD;
    phdrs[2].p_flags  = PF_R | PF_W;
    phdrs[2].p_offset = rva_rw;
    phdrs[2].p_vaddr  = BASE_ADDR + rva_rw;
    phdrs[2].p_paddr  = BASE_ADDR + rva_rw;
    phdrs[2].p_filesz = file_size - rva_rw;
    phdrs[2].p_memsz  = file_size - rva_rw;
    phdrs[2].p_align  = PAGE_SIZE;
    
    phdrs[3].p_type   = PT_DYNAMIC;
    phdrs[3].p_flags  = PF_R | PF_W;
    phdrs[3].p_offset = rva_dynamic;
    phdrs[3].p_vaddr  = BASE_ADDR + rva_dynamic;
    phdrs[3].p_paddr  = BASE_ADDR + rva_dynamic;
    phdrs[3].p_filesz = dynamic.size() * sizeof(Elf64_Dyn);
    phdrs[3].p_memsz  = dynamic.size() * sizeof(Elf64_Dyn);
    phdrs[3].p_align  = 8;
    
    std::memcpy(out.data() + ehdrSize, phdrs, sizeof(phdrs));
    
    std::memcpy(out.data() + rva_interp, interpStr.c_str(), interpStr.length() + 1);
    std::memcpy(out.data() + rva_dynstr, dynstr.data(), dynstr.size());
    std::memcpy(out.data() + rva_hash, hash.data(), hash.size() * sizeof(uint32_t));
    std::memcpy(out.data() + rva_dynsym, dynsym.data(), dynsym.size() * sizeof(Elf64_Sym));
    std::memcpy(out.data() + rva_relaplt, relaplt.data(), relaplt.size() * sizeof(Elf64_Rela));
    std::memcpy(out.data() + rva_plt, plt.data(), plt.size());
    std::memcpy(out.data() + rva_text, textBytes.data(), textBytes.size());
    
    std::memcpy(out.data() + rva_dynamic, dynamic.data(), dynamic.size() * sizeof(Elf64_Dyn));
    std::memcpy(out.data() + rva_gotplt, gotplt.data(), gotplt.size() * sizeof(uint64_t));
    std::memcpy(out.data() + rva_data, as.dataBytes().data(), as.dataBytes().size());
    
    return out;
}

} // namespace nylang
