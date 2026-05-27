#include "PeWriter.h"
#include <iostream>
#include <cstring>
#include <map>
#include <set>

namespace nylang {

static void write8(std::vector<uint8_t>& buf, uint8_t val) {
    buf.push_back(val);
}

static void write32(std::vector<uint8_t>& buf, uint32_t val) {
    buf.push_back(val & 0xFF);
    buf.push_back((val >> 8) & 0xFF);
    buf.push_back((val >> 16) & 0xFF);
    buf.push_back((val >> 24) & 0xFF);
}

static void write16(std::vector<uint8_t>& buf, uint16_t val) {
    buf.push_back(val & 0xFF);
    buf.push_back((val >> 8) & 0xFF);
}

static void write64(std::vector<uint8_t>& buf, uint64_t val) {
    write32(buf, val & 0xFFFFFFFF);
    write32(buf, val >> 32);
}

static void writeBytes(std::vector<uint8_t>& buf, const uint8_t* data, size_t len) {
    buf.insert(buf.end(), data, data + len);
}

static void writeZeros(std::vector<uint8_t>& buf, size_t len) {
    buf.resize(buf.size() + len, 0);
}

static uint32_t alignUp(uint32_t val, uint32_t alignment) {
    return (val + alignment - 1) & ~(alignment - 1);
}

static void writeSectionHeader(std::vector<uint8_t>& buf, const char* name, uint32_t virtualSize, uint32_t virtualAddr, uint32_t rawSize, uint32_t rawPtr, uint32_t characteristics) {
    char nameBuf[8] = {0};
    strncpy(nameBuf, name, 8);
    writeBytes(buf, reinterpret_cast<const uint8_t*>(nameBuf), 8);
    write32(buf, virtualSize);
    write32(buf, virtualAddr);
    write32(buf, rawSize);
    write32(buf, rawPtr);
    write32(buf, 0); // PointerToRelocations
    write32(buf, 0); // PointerToLinenumbers
    write16(buf, 0); // NumberOfRelocations
    write16(buf, 0); // NumberOfLinenumbers
    write32(buf, characteristics);
}

std::vector<uint8_t> PeWriter::write(const Assembler& asmObj) {
    std::vector<uint8_t> pe;

    // We will hardcode 3 sections: .text, .rdata (imports), .data
    uint32_t fileAlign = 0x200;
    uint32_t secAlign = 0x1000;
    uint32_t imageBase = 0x400000;

    // Calculate sizes
    uint32_t textVSize = asmObj.textBytes().size();
    uint32_t textRSize = alignUp(textVSize, fileAlign);

    // Build .rdata (Import Directory)
    std::vector<uint8_t> rdata;
    
    // We import from "msvcrt.dll" and "kernel32.dll"
    std::map<std::string, std::vector<std::string>> imports = {
        {"msvcrt.dll", {"printf", "_read", "malloc", "free"}},
        {"kernel32.dll", {"ExitProcess"}}
    };

    uint32_t rdataRva = alignUp(0x1000 + textVSize, secAlign);

    // Maps to track offsets for IAT and INT
    std::map<std::string, uint32_t> iatOffsets; 
    std::map<std::string, uint32_t> intOffsets;
    std::map<std::string, uint32_t> dllNameOffsets;
    std::map<std::string, uint32_t> hintNameOffsets;

    // 1. Calculate space for IAT (Import Address Table)
    uint32_t currentOffset = 0;
    for (const auto& [dll, funcs] : imports) {
        for (const auto& func : funcs) {
            iatOffsets[func] = currentOffset;
            currentOffset += 8;
        }
        currentOffset += 8; // Null terminator for IAT
    }
    
    // 2. Calculate space for INT (Import Name Table)
    for (const auto& [dll, funcs] : imports) {
        for (const auto& func : funcs) {
            intOffsets[func] = currentOffset;
            currentOffset += 8;
        }
        currentOffset += 8; // Null terminator for INT
    }

    // 3. Import Directory Table (IDT)
    uint32_t idtOffset = currentOffset;
    currentOffset += 20 * (imports.size() + 1); // +1 for null terminator

    // 4. DLL Names and Hint/Name tables
    for (const auto& [dll, funcs] : imports) {
        dllNameOffsets[dll] = currentOffset;
        currentOffset += dll.length() + 1;
        for (const auto& func : funcs) {
            hintNameOffsets[func] = currentOffset;
            currentOffset += 2 + func.length() + 1; // 2 for Hint
        }
    }

    // Now write .rdata
    rdata.resize(currentOffset, 0);
    
    // Fill Hint/Name and DLL names
    for (const auto& [dll, funcs] : imports) {
        strcpy(reinterpret_cast<char*>(&rdata[dllNameOffsets[dll]]), dll.c_str());
        for (const auto& func : funcs) {
            uint32_t hno = hintNameOffsets[func];
            rdata[hno] = 0; rdata[hno+1] = 0; // Hint = 0
            strcpy(reinterpret_cast<char*>(&rdata[hno+2]), func.c_str());
        }
    }

    // Fill IAT and INT
    for (const auto& [dll, funcs] : imports) {
        for (const auto& func : funcs) {
            uint64_t rva = rdataRva + hintNameOffsets[func];
            // IAT
            rdata[iatOffsets[func]] = rva & 0xFF;
            rdata[iatOffsets[func]+1] = (rva >> 8) & 0xFF;
            rdata[iatOffsets[func]+2] = (rva >> 16) & 0xFF;
            rdata[iatOffsets[func]+3] = (rva >> 24) & 0xFF;
            // INT
            rdata[intOffsets[func]] = rva & 0xFF;
            rdata[intOffsets[func]+1] = (rva >> 8) & 0xFF;
            rdata[intOffsets[func]+2] = (rva >> 16) & 0xFF;
            rdata[intOffsets[func]+3] = (rva >> 24) & 0xFF;
        }
    }

    // Fill IDT
    uint32_t idtIdx = idtOffset;
    for (const auto& [dll, funcs] : imports) {
        // OriginalFirstThunk (INT)
        uint32_t intRva = rdataRva + intOffsets[funcs[0]];
        rdata[idtIdx] = intRva & 0xFF; rdata[idtIdx+1] = (intRva >> 8) & 0xFF; rdata[idtIdx+2] = (intRva >> 16) & 0xFF; rdata[idtIdx+3] = (intRva >> 24) & 0xFF;
        // TimeDateStamp
        rdata[idtIdx+4] = 0; rdata[idtIdx+5] = 0; rdata[idtIdx+6] = 0; rdata[idtIdx+7] = 0;
        // ForwarderChain
        rdata[idtIdx+8] = 0; rdata[idtIdx+9] = 0; rdata[idtIdx+10] = 0; rdata[idtIdx+11] = 0;
        // Name (DLL Name)
        uint32_t nameRva = rdataRva + dllNameOffsets[dll];
        rdata[idtIdx+12] = nameRva & 0xFF; rdata[idtIdx+13] = (nameRva >> 8) & 0xFF; rdata[idtIdx+14] = (nameRva >> 16) & 0xFF; rdata[idtIdx+15] = (nameRva >> 24) & 0xFF;
        // FirstThunk (IAT)
        uint32_t iatRva = rdataRva + iatOffsets[funcs[0]];
        rdata[idtIdx+16] = iatRva & 0xFF; rdata[idtIdx+17] = (iatRva >> 8) & 0xFF; rdata[idtIdx+18] = (iatRva >> 16) & 0xFF; rdata[idtIdx+19] = (iatRva >> 24) & 0xFF;
        
        idtIdx += 20;
    }

    uint32_t rdataVSize = rdata.size();
    uint32_t rdataRSize = alignUp(rdataVSize, fileAlign);

    // .data
    uint32_t dataVSize = asmObj.dataBytes().size();
    uint32_t dataRSize = alignUp(dataVSize, fileAlign);
    uint32_t dataRva = alignUp(rdataRva + rdataVSize, secAlign);

    // Patch .text relocations
    std::vector<uint8_t> textBytes = asmObj.textBytes();

    // DataRelocs (rip-relative to .data)
    for (const auto& rel : asmObj.dataRelocs()) {
        uint32_t targetRva = dataRva + rel.dataOffset;
        uint32_t instrRva = 0x1000 + rel.textOffset + 4; // +4 because RIP is instruction end
        int32_t disp = static_cast<int32_t>(targetRva - instrRva);
        textBytes[rel.textOffset] = disp & 0xFF;
        textBytes[rel.textOffset+1] = (disp >> 8) & 0xFF;
        textBytes[rel.textOffset+2] = (disp >> 16) & 0xFF;
        textBytes[rel.textOffset+3] = (disp >> 24) & 0xFF;
    }

    // ImportRelocs (rip-relative to IAT in .rdata)
    for (const auto& rel : asmObj.importRelocs()) {
        if (iatOffsets.find(rel.symbol) == iatOffsets.end()) {
            throw std::runtime_error("PeWriter: Unresolved import '" + rel.symbol + "'");
        }
        uint32_t targetRva = rdataRva + iatOffsets[rel.symbol];
        uint32_t instrRva = 0x1000 + rel.textOffset + 4;
        int32_t disp = static_cast<int32_t>(targetRva - instrRva);
        textBytes[rel.textOffset] = disp & 0xFF;
        textBytes[rel.textOffset+1] = (disp >> 8) & 0xFF;
        textBytes[rel.textOffset+2] = (disp >> 16) & 0xFF;
        textBytes[rel.textOffset+3] = (disp >> 24) & 0xFF;
    }

    // Find main text offset
    uint32_t entryOffset = 0;
    for (const auto& sym : asmObj.globalSymbols()) {
        if (sym.name == "main") {
            entryOffset = sym.textOffset;
            break;
        }
    }

    // ── Headers ─────────────────────────────────────────────────────────────
    
    // DOS Header
    writeBytes(pe, reinterpret_cast<const uint8_t*>("MZ"), 2);
    writeZeros(pe, 58);
    write32(pe, 0x80); // e_lfanew
    
    // DOS Stub
    writeZeros(pe, 64);
    
    // PE Signature
    writeBytes(pe, reinterpret_cast<const uint8_t*>("PE\0\0"), 4);

    // COFF Header
    write16(pe, 0x8664); // Machine (AMD64)
    write16(pe, 3);      // NumberOfSections (.text, .rdata, .data)
    write32(pe, 0);      // TimeDateStamp
    write32(pe, 0);      // PointerToSymbolTable
    write32(pe, 0);      // NumberOfSymbols
    write16(pe, 240);    // SizeOfOptionalHeader
    write16(pe, 0x0022); // Characteristics (Executable, LargeAddressAware)

    // Optional Header (PE32+)
    write16(pe, 0x20B);  // Magic
    write8(pe, 2); write8(pe, 14); // LMajor, LMinor (arbitrary linker ver)
    write32(pe, textRSize); // SizeOfCode
    write32(pe, rdataRSize + dataRSize); // SizeOfInitData
    write32(pe, 0); // SizeOfUninitData
    write32(pe, 0x1000 + entryOffset); // AddressOfEntryPoint
    write32(pe, 0x1000); // BaseOfCode

    write64(pe, imageBase); // ImageBase
    write32(pe, secAlign); // SectionAlignment
    write32(pe, fileAlign); // FileAlignment
    write16(pe, 5); write16(pe, 2); // OSVersion
    write16(pe, 0); write16(pe, 0); // ImageVersion
    write16(pe, 5); write16(pe, 2); // SubsystemVersion
    write32(pe, 0); // Win32VersionValue
    uint32_t sizeOfImage = alignUp(dataRva + dataVSize, secAlign);
    write32(pe, sizeOfImage); // SizeOfImage
    write32(pe, 0x200); // SizeOfHeaders
    write32(pe, 0); // CheckSum
    write16(pe, 3); // Subsystem (Console)
    write16(pe, 0x8140); // DllCharacteristics (DynamicBase, NXCompat, TerminalServerAware)
    write64(pe, 0x100000); // SizeOfStackReserve
    write64(pe, 0x1000); // SizeOfStackCommit
    write64(pe, 0x100000); // SizeOfHeapReserve
    write64(pe, 0x1000); // SizeOfHeapCommit
    write32(pe, 0); // LoaderFlags
    write32(pe, 16); // NumberOfRvaAndSizes

    // Data Directories
    write32(pe, 0); write32(pe, 0); // Export
    write32(pe, rdataRva + idtOffset); write32(pe, 20 * (imports.size() + 1)); // Import
    write32(pe, 0); write32(pe, 0); // Resource
    write32(pe, 0); write32(pe, 0); // Exception
    write32(pe, 0); write32(pe, 0); // Certificate
    write32(pe, 0); write32(pe, 0); // BaseReloc
    write32(pe, 0); write32(pe, 0); // Debug
    write32(pe, 0); write32(pe, 0); // Architecture
    write32(pe, 0); write32(pe, 0); // GlobalPtr
    write32(pe, 0); write32(pe, 0); // TLS
    write32(pe, 0); write32(pe, 0); // LoadConfig
    write32(pe, 0); write32(pe, 0); // BoundImport
    write32(pe, rdataRva); write32(pe, iatOffsets["ExitProcess"] + 8); // IAT (start of .rdata, size = total IAT size approximately)
    write32(pe, 0); write32(pe, 0); // DelayImport
    write32(pe, 0); write32(pe, 0); // COM
    write32(pe, 0); write32(pe, 0); // Reserved

    // Section Headers
    uint32_t headerSize = pe.size() + 3 * 40;
    uint32_t firstSectionRaw = alignUp(headerSize, fileAlign);

    uint32_t textRaw = firstSectionRaw;
    uint32_t rdataRaw = textRaw + textRSize;
    uint32_t dataRaw = rdataRaw + rdataRSize;

    // .text
    writeSectionHeader(pe, ".text", textVSize, 0x1000, textRSize, textRaw, 0x60000020); // Execute | Read | Code
    // .rdata
    writeSectionHeader(pe, ".rdata", rdataVSize, rdataRva, rdataRSize, rdataRaw, 0x40000040); // Read | InitData
    // .data
    writeSectionHeader(pe, ".data", dataVSize, dataRva, dataRSize, dataRaw, 0xC0000040); // Read | Write | InitData

    // Padding to first section
    writeZeros(pe, firstSectionRaw - pe.size());

    // Write sections
    writeBytes(pe, textBytes.data(), textBytes.size());
    writeZeros(pe, textRSize - textBytes.size());

    writeBytes(pe, rdata.data(), rdata.size());
    writeZeros(pe, rdataRSize - rdata.size());

    writeBytes(pe, asmObj.dataBytes().data(), asmObj.dataBytes().size());
    writeZeros(pe, dataRSize - asmObj.dataBytes().size());

    return pe;
}

} // namespace nylang
