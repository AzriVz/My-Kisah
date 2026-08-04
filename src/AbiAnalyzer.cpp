#include "AbiAnalyzer.hpp"

#include <capstone/x86.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <map>
#include <sstream>
#include <unordered_set>

namespace decompiler {

static constexpr std::array systemVParameterRegisters {
    RegisterId::Rdi,
    RegisterId::Rsi,
    RegisterId::Rdx,
    RegisterId::Rcx,
    RegisterId::R8,
    RegisterId::R9,
};

static constexpr std::array systemVCallClobberedRegisters {
    RegisterId::Rax,
    RegisterId::Rcx,
    RegisterId::Rdx,
    RegisterId::Rsi,
    RegisterId::Rdi,
    RegisterId::R8,
    RegisterId::R9,
    RegisterId::R10,
    RegisterId::R11,
};

static bool isSelfXor(const Instruction& instruction) {
    if(instruction.architectureId != X86_INS_XOR || instruction.operands.size() < 2
       || instruction.operands[0].kind != OperandKind::Register
       || instruction.operands[1].kind != OperandKind::Register) {
        return false;
    }

    const auto left = RegisterNormalizer::normalize(instruction.operands[0].registerName);
    const auto right = RegisterNormalizer::normalize(instruction.operands[1].registerName);
    return left && right && left->id == right->id && left->bitWidth == right->bitWidth;
}

static void recordRegisterParameter(
    std::map<std::size_t, AbiParameter>& parameters,
    const NormalizedRegister& registerInfo,
    ValueType type) {
    const auto index = AbiAnalyzer::parameterIndex(registerInfo.id);
    if(!index) {
        return;
    }

    auto [position, inserted] = parameters.try_emplace(
        *index,
        AbiParameter {
            .index = *index,
            .name = "arg" + std::to_string(*index),
            .type = type,
            .bitWidth = registerInfo.bitWidth,
            .registerId = registerInfo.id,
            .stackOffset = std::nullopt,
        });
    if(!inserted) {
        position->second.bitWidth = std::max<std::uint16_t>(
            position->second.bitWidth, registerInfo.bitWidth);
        if(type == ValueType::Pointer) {
            position->second.type = ValueType::Pointer;
        }
    }
}

static void recordStackParameter(
    std::map<std::size_t, AbiParameter>& parameters,
    std::int64_t stackOffset,
    std::uint16_t bitWidth) {
    if(stackOffset < 16) {
        return;
    }

    const auto index = static_cast<std::size_t>(6 + (stackOffset - 16) / 8);
    auto [position, inserted] = parameters.try_emplace(
        index,
        AbiParameter {
            .index = index,
            .name = "arg" + std::to_string(index),
            .type = ValueType::Integer,
            .bitWidth = bitWidth,
            .registerId = std::nullopt,
            .stackOffset = stackOffset,
        });
    if(!inserted) {
        position->second.bitWidth = std::max(position->second.bitWidth, bitWidth);
    }
}

std::optional<std::size_t> AbiAnalyzer::parameterIndex(RegisterId registerId) noexcept {
    const auto position = std::find(
        systemVParameterRegisters.begin(), systemVParameterRegisters.end(), registerId);
    if(position == systemVParameterRegisters.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(systemVParameterRegisters.begin(), position));
}

std::string AbiAnalyzer::stackVariableName(std::int64_t offset) {
    const auto magnitude = offset < 0
                               ? static_cast<std::uint64_t>(-(offset + 1)) + 1
                               : static_cast<std::uint64_t>(offset);
    std::ostringstream name;
    name << "local_" << std::hex << std::nouppercase << magnitude;
    return name.str();
}

AbiAnalysisResult AbiAnalyzer::analyze(std::span<const Instruction> instructions) const {
    AbiAnalysisResult result;
    result.callClobberedRegisters.assign(
        systemVCallClobberedRegisters.begin(), systemVCallClobberedRegisters.end());

    std::map<std::size_t, AbiParameter> parameters;
    std::map<std::int64_t, AbiStackVariable> stackVariables;
    std::unordered_set<RegisterId> writtenRegisters;
    bool sawReturn = false;
    bool returnRegisterWritten = false;
    std::uint16_t returnBitWidth = 0;

    for(const auto& instruction : instructions) {
        std::map<RegisterId, NormalizedRegister> registersRead;
        std::map<RegisterId, NormalizedRegister> registersWritten;

        for(const auto& registerName : instruction.registersRead) {
            const auto normalized = RegisterNormalizer::normalize(registerName);
            if(normalized) {
                registersRead.insert_or_assign(normalized->id, *normalized);
            }
        }
        for(const auto& registerName : instruction.registersWritten) {
            const auto normalized = RegisterNormalizer::normalize(registerName);
            if(normalized) {
                registersWritten.insert_or_assign(normalized->id, *normalized);
            }
        }

        for(const auto& operand : instruction.operands) {
            if(operand.kind == OperandKind::Register) {
                const auto normalized = RegisterNormalizer::normalize(operand.registerName);
                if(!normalized) {
                    continue;
                }
                if(operand.isRead) {
                    registersRead.insert_or_assign(normalized->id, *normalized);
                }
                if(operand.isWritten) {
                    registersWritten.insert_or_assign(normalized->id, *normalized);
                }
                continue;
            }

            if(operand.kind != OperandKind::Memory) {
                continue;
            }

            const auto base = RegisterNormalizer::normalize(operand.memory.baseRegister);
            const auto index = RegisterNormalizer::normalize(operand.memory.indexRegister);
            if(base) {
                auto baseUsage = *base;
                if(instruction.architectureId == X86_INS_LEA
                   && !instruction.operands.empty()
                   && instruction.operands.front().size != 0) {
                    baseUsage.bitWidth = static_cast<std::uint8_t>(
                        instruction.operands.front().size * 8);
                }
                registersRead.insert_or_assign(base->id, baseUsage);
                if(instruction.architectureId != X86_INS_LEA
                   && !writtenRegisters.contains(base->id)) {
                    recordRegisterParameter(parameters, *base, ValueType::Pointer);
                }
            }
            if(index) {
                auto indexUsage = *index;
                if(instruction.architectureId == X86_INS_LEA
                   && !instruction.operands.empty()
                   && instruction.operands.front().size != 0) {
                    indexUsage.bitWidth = static_cast<std::uint8_t>(
                        instruction.operands.front().size * 8);
                }
                registersRead.insert_or_assign(index->id, indexUsage);
            }

            if(base && base->id == RegisterId::Rbp) {
                const auto displacement = operand.memory.displacement;
                const auto bitWidth = static_cast<std::uint16_t>(operand.size * 8);
                if(displacement < 0) {
                    auto [position, inserted] = stackVariables.try_emplace(
                        displacement,
                        AbiStackVariable {
                            .offset = displacement,
                            .name = stackVariableName(displacement),
                            .type = ValueType::Integer,
                            .bitWidth = bitWidth,
                        });
                    if(!inserted) {
                        position->second.bitWidth =
                            std::max(position->second.bitWidth, bitWidth);
                    }
                } else {
                    recordStackParameter(parameters, displacement, bitWidth);
                }
            }
        }

        if(isSelfXor(instruction) && !instruction.operands.empty()) {
            const auto destination =
                RegisterNormalizer::normalize(instruction.operands.front().registerName);
            if(destination) {
                registersRead.erase(destination->id);
            }
        }

        for(const auto& [registerId, registerInfo] : registersRead) {
            if(!writtenRegisters.contains(registerId)) {
                recordRegisterParameter(parameters, registerInfo, ValueType::Integer);
            }
        }

        if(instruction.kind == InstructionKind::Call) {
            registersWritten.insert_or_assign(
                RegisterId::Rax,
                NormalizedRegister {
                    .id = RegisterId::Rax,
                    .bitWidth = 64,
                    .bitOffset = 0,
                    .zeroExtendsOnWrite = false,
                });
            for(const auto registerId : systemVCallClobberedRegisters) {
                writtenRegisters.insert(registerId);
            }
        }

        for(const auto& [registerId, registerInfo] : registersWritten) {
            writtenRegisters.insert(registerId);
            if(registerId == RegisterId::Rax) {
                returnRegisterWritten = true;
                returnBitWidth = std::max<std::uint16_t>(
                    returnBitWidth, registerInfo.bitWidth);
            }
        }

        sawReturn = sawReturn || instruction.kind == InstructionKind::Return;
    }

    for(auto& [index, parameter] : parameters) {
        static_cast<void>(index);
        result.parameters.push_back(std::move(parameter));
    }
    for(auto& [offset, variable] : stackVariables) {
        static_cast<void>(offset);
        result.stackVariables.push_back(std::move(variable));
    }

    result.returnsValue = sawReturn && returnRegisterWritten;
    result.returnType = result.returnsValue ? ValueType::Integer : ValueType::Unknown;
    result.returnBitWidth = result.returnsValue ? returnBitWidth : 0;
    return result;
}

} // namespace decompiler
