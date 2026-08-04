#include "AnalysisSession.hpp"

#include "FunctionDiscovery.hpp"

#include <algorithm>
#include <string>

namespace decompiler {

bool AnalysisSession::analyze(const std::filesystem::path& path) {
    reset();

    if(!elfLoader_.load(path)) {
        errorMessage_ = std::string(elfLoader_.errorMessage());
        return false;
    }

    if(!disassembler_.isAvailable()) {
        errorMessage_ = std::string(disassembler_.errorMessage());
        return false;
    }

    FunctionDiscovery discovery;
    functions_ = discovery.discover(elfLoader_, disassembler_);
    if(functions_.empty()) {
        errorMessage_ = discovery.errorMessage().empty()
                            ? "No functions were found in the binary."
                            : std::string(discovery.errorMessage());
        return false;
    }

    const auto text = elfLoader_.findSection(".text");
    const auto textBytes = elfLoader_.bytesForSection(".text");
    if(!text || textBytes.empty()) {
        errorMessage_ = "The executable .text section is unavailable.";
        functions_.clear();
        return false;
    }

    instructionCache_.reserve(functions_.size());
    for(const auto& function : functions_) {
        const auto sectionOffset = function.address - text->address;
        if(sectionOffset > textBytes.size() || function.size > textBytes.size() - sectionOffset) {
            errorMessage_ = "A discovered function points outside the .text section.";
            functions_.clear();
            instructionCache_.clear();
            return false;
        }

        auto result = disassembler_.disassemble(
            textBytes.subspan(
                static_cast<std::size_t>(sectionOffset),
                static_cast<std::size_t>(function.size)),
            function.address);
        if(!result.succeeded()) {
            errorMessage_ = result.errorMessage;
            functions_.clear();
            instructionCache_.clear();
            return false;
        }

        instructionCache_.emplace(function.address, std::move(result.instructions));
    }

    valid_ = true;
    return true;
}

void AnalysisSession::reset() noexcept {
    elfLoader_.reset();
    functions_.clear();
    instructionCache_.clear();
    errorMessage_.clear();
    valid_ = false;
}

bool AnalysisSession::isValid() const noexcept {
    return valid_;
}

std::string_view AnalysisSession::errorMessage() const noexcept {
    return errorMessage_;
}

const ElfLoader& AnalysisSession::elfLoader() const noexcept {
    return elfLoader_;
}

const std::vector<FunctionInfo>& AnalysisSession::functions() const noexcept {
    return functions_;
}

const FunctionInfo* AnalysisSession::functionAt(std::uint64_t address) const noexcept {
    const auto function = std::lower_bound(
        functions_.begin(),
        functions_.end(),
        address,
        [](const FunctionInfo& candidate, std::uint64_t requestedAddress) {
            return candidate.address < requestedAddress;
        });

    if(function == functions_.end() || function->address != address) {
        return nullptr;
    }
    return &*function;
}

const std::vector<Instruction>*
AnalysisSession::instructionsFor(std::uint64_t functionAddress) const noexcept {
    const auto instructions = instructionCache_.find(functionAddress);
    if(instructions == instructionCache_.end()) {
        return nullptr;
    }
    return &instructions->second;
}

} // namespace decompiler
