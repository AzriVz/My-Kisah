#include "FunctionDiscovery.hpp"

#include "Disassembler.hpp"
#include "ElfLoader.hpp"

#include <elf.h>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <unordered_set>

namespace decompiler {

struct FunctionCandidate {
    std::string symbolName;
    std::uint64_t symbolSize = 0;
    FunctionSource source = FunctionSource::Heuristic;
};

static int sourcePriority(FunctionSource source) noexcept {
    switch(source) {
    case FunctionSource::SymbolTable:
        return 5;
    case FunctionSource::DynamicSymbolTable:
        return 4;
    case FunctionSource::EntryPoint:
        return 3;
    case FunctionSource::DirectCallTarget:
        return 2;
    case FunctionSource::Heuristic:
        return 1;
    }
    return 0;
}

static bool isInTextSection(const SectionInfo& text, std::uint64_t address) noexcept {
    return address >= text.address && address - text.address < text.size;
}

static bool addCandidate(
    std::map<std::uint64_t, FunctionCandidate>& candidates,
    std::uint64_t address,
    std::string name,
    std::uint64_t symbolSize,
    FunctionSource source) {
    auto position = candidates.find(address);
    if(position == candidates.end()) {
        candidates.emplace(
            address,
            FunctionCandidate {
                .symbolName = std::move(name),
                .symbolSize = symbolSize,
                .source = source,
            });
        return true;
    }

    auto& existing = position->second;
    if(sourcePriority(source) > sourcePriority(existing.source)) {
        existing.source = source;
        if(!name.empty()) {
            existing.symbolName = std::move(name);
        }
    } else if(existing.symbolName.empty() && !name.empty()) {
        existing.symbolName = std::move(name);
    }

    if(symbolSize > 0
       && (existing.symbolSize == 0 || sourcePriority(source) >= sourcePriority(existing.source))) {
        existing.symbolSize = symbolSize;
    }

    return false;
}

static std::uint64_t candidateRangeSize(
    const std::map<std::uint64_t, FunctionCandidate>& candidates,
    std::map<std::uint64_t, FunctionCandidate>::const_iterator candidate,
    std::uint64_t textEnd) noexcept {
    const auto next = std::next(candidate);
    const auto nextAddress = next == candidates.end() ? textEnd : next->first;
    if(nextAddress <= candidate->first) {
        return 0;
    }

    const auto availableSize = nextAddress - candidate->first;
    if(candidate->second.symbolSize == 0) {
        return availableSize;
    }
    return std::min(candidate->second.symbolSize, availableSize);
}

static std::string fallbackFunctionName(std::uint64_t address) {
    std::ostringstream name;
    name << "sub_" << std::hex << std::nouppercase << address;
    return name.str();
}

static bool isAlignmentPadding(const Instruction& instruction) noexcept {
    return instruction.mnemonic == "nop" || instruction.mnemonic == "int3";
}

static bool looksLikeFramePrologue(
    const std::vector<Instruction>& instructions,
    std::size_t index) {
    if(index + 1 >= instructions.size()) {
        return false;
    }
    const auto& push = instructions[index];
    const auto& move = instructions[index + 1];
    return push.mnemonic == "push" && push.operandText == "rbp"
           && move.mnemonic == "mov" && move.operandText == "rbp, rsp";
}

static bool addStrippedHeuristicCandidates(
    std::map<std::uint64_t, FunctionCandidate>& candidates,
    const SectionInfo& text,
    std::span<const std::uint8_t> textBytes,
    const Disassembler& disassembler,
    std::string& errorMessage) {
    const auto result = disassembler.disassemble(textBytes, text.address);
    if(!result.succeeded()) {
        errorMessage = result.errorMessage;
        return false;
    }

    bool possibleFunctionBoundary = true;
    for(std::size_t index = 0; index < result.instructions.size(); ++index) {
        const auto& instruction = result.instructions[index];
        if(possibleFunctionBoundary) {
            if(isAlignmentPadding(instruction)) {
                continue;
            }

            const bool hasEntryMarker = instruction.mnemonic == "endbr64"
                                        || instruction.mnemonic == "endbr32";
            const bool hasFramePrologue = looksLikeFramePrologue(result.instructions, index);
            const bool isAligned = instruction.address % 16 == 0;
            if(hasEntryMarker || hasFramePrologue || isAligned) {
                addCandidate(
                    candidates,
                    instruction.address,
                    {},
                    0,
                    FunctionSource::Heuristic);
            }
            possibleFunctionBoundary = false;
        }

        possibleFunctionBoundary = instruction.kind == InstructionKind::Return
                                   || instruction.kind == InstructionKind::UnconditionalJump
                                   || instruction.kind == InstructionKind::IndirectJump;
    }
    return true;
}

std::vector<FunctionInfo>
FunctionDiscovery::discover(const ElfLoader& loader, const Disassembler& disassembler) {
    errorMessage_.clear();

    if(!loader.isValid()) {
        errorMessage_ = "Function discovery requires a valid ELF file.";
        return {};
    }
    if(!disassembler.isAvailable()) {
        errorMessage_ = std::string(disassembler.errorMessage());
        return {};
    }

    const auto text = loader.findSection(".text");
    const auto textBytes = loader.bytesForSection(".text");
    if(!text || textBytes.empty()) {
        errorMessage_ = "Function discovery requires a non-empty .text section.";
        return {};
    }

    const auto textEnd = text->address + text->size;
    std::map<std::uint64_t, FunctionCandidate> candidates;

    for(const auto& symbol : loader.symbols()) {
        if(symbol.type != STT_FUNC || symbol.sectionIndex == SHN_UNDEF || symbol.address == 0
           || !isInTextSection(*text, symbol.address)) {
            continue;
        }

        addCandidate(
            candidates,
            symbol.address,
            symbol.name,
            symbol.size,
            symbol.fromDynamicTable ? FunctionSource::DynamicSymbolTable
                                    : FunctionSource::SymbolTable);
    }

    if(isInTextSection(*text, loader.metadata().entryPoint)) {
        addCandidate(
            candidates,
            loader.metadata().entryPoint,
            {},
            0,
            FunctionSource::EntryPoint);
    }

    if(candidates.empty()) {
        errorMessage_ = "No initial function candidates were found.";
        return {};
    }

    if(loader.metadata().isStripped
       && !addStrippedHeuristicCandidates(
           candidates, *text, textBytes, disassembler, errorMessage_)) {
        return {};
    }

    std::deque<std::uint64_t> pendingAddresses;
    for(const auto& [address, candidate] : candidates) {
        static_cast<void>(candidate);
        pendingAddresses.push_back(address);
    }

    constexpr std::size_t maximumFunctionCount = 100'000;
    while(!pendingAddresses.empty()) {
        const auto address = pendingAddresses.front();
        pendingAddresses.pop_front();

        const auto candidate = candidates.find(address);
        if(candidate == candidates.end()) {
            continue;
        }

        const auto size = candidateRangeSize(candidates, candidate, textEnd);
        if(size == 0) {
            continue;
        }

        const auto sectionOffset = address - text->address;
        const auto result = disassembler.disassemble(
            textBytes.subspan(
                static_cast<std::size_t>(sectionOffset),
                static_cast<std::size_t>(size)),
            address);
        if(!result.succeeded()) {
            errorMessage_ = result.errorMessage;
            return {};
        }

        for(const auto& instruction : result.instructions) {
            if(instruction.kind != InstructionKind::Call || !instruction.directTarget
               || !isInTextSection(*text, *instruction.directTarget)) {
                continue;
            }

            if(candidates.contains(*instruction.directTarget)) {
                continue;
            }

            if(candidates.size() >= maximumFunctionCount) {
                errorMessage_ = "Function discovery limit was reached.";
                return {};
            }

            if(addCandidate(
                   candidates,
                   *instruction.directTarget,
                   {},
                   0,
                   FunctionSource::DirectCallTarget)) {
                pendingAddresses.push_back(*instruction.directTarget);
            }
        }
    }

    std::vector<FunctionInfo> functions;
    functions.reserve(candidates.size());
    std::unordered_set<std::string> usedNames;

    for(auto candidate = candidates.cbegin(); candidate != candidates.cend(); ++candidate) {
        const auto maximumSize = candidateRangeSize(candidates, candidate, textEnd);
        if(maximumSize == 0) {
            continue;
        }

        auto name = candidate->second.symbolName.empty()
                        ? fallbackFunctionName(candidate->first)
                        : candidate->second.symbolName;
        if(!usedNames.insert(name).second) {
            name += "_" + fallbackFunctionName(candidate->first).substr(4);
            usedNames.insert(name);
        }

        const bool estimatedSize = candidate->second.symbolSize == 0
                                   || candidate->second.symbolSize > maximumSize;
        functions.push_back(FunctionInfo {
            .name = std::move(name),
            .address = candidate->first,
            .size = maximumSize,
            .source = candidate->second.source,
            .sizeIsEstimated = estimatedSize,
        });
    }

    return functions;
}

std::string_view FunctionDiscovery::errorMessage() const noexcept {
    return errorMessage_;
}

} // namespace decompiler
