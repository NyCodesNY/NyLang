#pragma once
#include "Assembler.h"
#include <vector>
#include <cstdint>

namespace nylang {

class PeWriter {
public:
    static std::vector<uint8_t> write(const Assembler& asmObj);
};

} // namespace nylang
