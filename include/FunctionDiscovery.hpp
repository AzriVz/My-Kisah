#pragma once

#include "FunctionInfo.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace decompiler {

class Disassembler;
class ElfLoader;

class FunctionDiscovery final {
public:
    [[nodiscard]] std::vector<FunctionInfo>
    discover(const ElfLoader& loader, const Disassembler& disassembler);

    [[nodiscard]] std::string_view errorMessage() const noexcept;

private:
    std::string errorMessage_;
};

} // namespace decompiler

