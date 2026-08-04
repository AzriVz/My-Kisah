#include "AnalysisSession.hpp"

#include "BasicBlockBuilder.hpp"
#include "FunctionDiscovery.hpp"
#include "IRLifter.hpp"
#include "PseudocodeGenerator.hpp"

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
    controlFlowGraphCache_.reserve(functions_.size());
    abiAnalysisCache_.reserve(functions_.size());
    irCache_.reserve(functions_.size());
    dataFlowCache_.reserve(functions_.size());
    pseudocodeCache_.reserve(functions_.size());
    const BasicBlockBuilder basicBlockBuilder;
    const AbiAnalyzer abiAnalyzer;
    const IRLifter irLifter;
    for(const auto& function : functions_) {
        const auto sectionOffset = function.address - text->address;
        if(sectionOffset > textBytes.size() || function.size > textBytes.size() - sectionOffset) {
            errorMessage_ = "A discovered function points outside the .text section.";
            functions_.clear();
            instructionCache_.clear();
            controlFlowGraphCache_.clear();
            abiAnalysisCache_.clear();
            irCache_.clear();
            dataFlowCache_.clear();
            pseudocodeCache_.clear();
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
            controlFlowGraphCache_.clear();
            abiAnalysisCache_.clear();
            irCache_.clear();
            dataFlowCache_.clear();
            pseudocodeCache_.clear();
            return false;
        }

        ControlFlowGraph controlFlowGraph;
        auto blocks = basicBlockBuilder.build(result.instructions);
        if(!controlFlowGraph.build(std::move(blocks))) {
            errorMessage_ = std::string(controlFlowGraph.errorMessage());
            functions_.clear();
            instructionCache_.clear();
            controlFlowGraphCache_.clear();
            abiAnalysisCache_.clear();
            irCache_.clear();
            dataFlowCache_.clear();
            pseudocodeCache_.clear();
            return false;
        }

        auto abiAnalysis = abiAnalyzer.analyze(result.instructions);
        auto ir = irLifter.lift(function, result.instructions, abiAnalysis);
        controlFlowGraphCache_.emplace(function.address, std::move(controlFlowGraph));
        abiAnalysisCache_.emplace(function.address, std::move(abiAnalysis));
        irCache_.emplace(function.address, std::move(ir));
        instructionCache_.emplace(function.address, std::move(result.instructions));
    }

    std::vector<FunctionPrototype> prototypes;
    prototypes.reserve(functions_.size());
    for(const auto& function : functions_) {
        const auto& abiAnalysis = abiAnalysisCache_.at(function.address);
        prototypes.push_back(FunctionPrototype {
            .address = function.address,
            .name = function.name,
            .parameterCount = abiAnalysis.parameters.size(),
            .returnsValue = abiAnalysis.returnsValue,
            .returnType = abiAnalysis.returnType,
            .returnBitWidth = abiAnalysis.returnBitWidth,
        });
    }

    const DataFlowAnalyzer dataFlowAnalyzer;
    const PseudocodeGenerator pseudocodeGenerator;
    for(const auto& function : functions_) {
        const auto& instructions = instructionCache_.at(function.address);
        const auto& controlFlowGraph = controlFlowGraphCache_.at(function.address);
        const auto& abiAnalysis = abiAnalysisCache_.at(function.address);
        const auto& ir = irCache_.at(function.address);
        auto dataFlow =
            dataFlowAnalyzer.analyze(ir, instructions, controlFlowGraph, abiAnalysis);
        auto pseudocode = pseudocodeGenerator.generate(
            function, abiAnalysis, controlFlowGraph, dataFlow, prototypes);
        dataFlowCache_.emplace(function.address, std::move(dataFlow));
        pseudocodeCache_.emplace(function.address, std::move(pseudocode));
    }

    valid_ = true;
    return true;
}

void AnalysisSession::reset() noexcept {
    elfLoader_.reset();
    functions_.clear();
    instructionCache_.clear();
    controlFlowGraphCache_.clear();
    abiAnalysisCache_.clear();
    irCache_.clear();
    dataFlowCache_.clear();
    pseudocodeCache_.clear();
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

const ControlFlowGraph*
AnalysisSession::controlFlowGraphFor(std::uint64_t functionAddress) const noexcept {
    const auto graph = controlFlowGraphCache_.find(functionAddress);
    if(graph == controlFlowGraphCache_.end()) {
        return nullptr;
    }
    return &graph->second;
}

const AbiAnalysisResult*
AnalysisSession::abiAnalysisFor(std::uint64_t functionAddress) const noexcept {
    const auto analysis = abiAnalysisCache_.find(functionAddress);
    if(analysis == abiAnalysisCache_.end()) {
        return nullptr;
    }
    return &analysis->second;
}

const IRFunction* AnalysisSession::irFor(std::uint64_t functionAddress) const noexcept {
    const auto ir = irCache_.find(functionAddress);
    if(ir == irCache_.end()) {
        return nullptr;
    }
    return &ir->second;
}

const DataFlowAnalysis*
AnalysisSession::dataFlowFor(std::uint64_t functionAddress) const noexcept {
    const auto analysis = dataFlowCache_.find(functionAddress);
    if(analysis == dataFlowCache_.end()) {
        return nullptr;
    }
    return &analysis->second;
}

const std::string*
AnalysisSession::pseudocodeFor(std::uint64_t functionAddress) const noexcept {
    const auto pseudocode = pseudocodeCache_.find(functionAddress);
    if(pseudocode == pseudocodeCache_.end()) {
        return nullptr;
    }
    return &pseudocode->second;
}

} // namespace decompiler
