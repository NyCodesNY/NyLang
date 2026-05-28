#pragma once

#include "Assembler.h"
#include <vector>
#include <cstdint>

namespace nylang {

/// Generates a valid ELF64 relocatable object file (.o) from assembled
/// machine code, data, symbols, and relocations.
class ElfWriter {
public:
    static std::vector<uint8_t> write(const Assembler& as);
};

} // namespace nylang
