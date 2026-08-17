#include "AnalysisSession.hpp"
#include "BinaryPatcher.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <system_error>

static int failures = 0;

static void expect(bool condition, std::string_view message) {
    if(!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

static const decompiler::Instruction* findPatchableCall(
    const decompiler::AnalysisSession& session,
    std::uint64_t& callerAddress,
    std::uint64_t& calleeAddress) {
    for(const auto& function : session.functions()) {
        const auto* instructions = session.instructionsFor(function.address);
        if(instructions == nullptr) {
            continue;
        }
        for(const auto& instruction : *instructions) {
            if(instruction.kind == decompiler::InstructionKind::Call
               && instruction.directTarget
               && session.functionAt(*instruction.directTarget) != nullptr) {
                callerAddress = function.address;
                calleeAddress = *instruction.directTarget;
                return &instruction;
            }
        }
    }
    return nullptr;
}

int main(int argc, char* argv[]) {
    if(argc != 2) {
        std::cerr << "expected patch sample path\n";
        return 2;
    }

    const auto parsed = decompiler::BinaryPatcher::parseHexBytes("90 0a FF");
    expect(parsed.succeeded(), "spaced hexadecimal bytes should parse");
    expect(
        parsed.bytes == std::vector<std::uint8_t>({0x90, 0x0A, 0xFF}),
        "parsed hexadecimal values mismatch");
    expect(
        !decompiler::BinaryPatcher::parseHexBytes("9x").succeeded(),
        "non-hex characters should be rejected");
    expect(
        !decompiler::BinaryPatcher::parseHexBytes("999").succeeded(),
        "incomplete bytes should be rejected");

    decompiler::AnalysisSession session;
    expect(session.analyze(argv[1]), "patch sample should analyze");
    if(!session.isValid()) {
        std::cerr << "analysis error: " << session.errorMessage() << '\n';
        return 1;
    }

    std::uint64_t callerAddress = 0;
    std::uint64_t calleeAddress = 0;
    const auto* call = findPatchableCall(session, callerAddress, calleeAddress);
    expect(call != nullptr, "sample should contain a patchable internal direct call");
    if(call == nullptr) {
        return 1;
    }

    const auto uniqueValue = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path()
                           / ("decompiler-patch-tests-" + std::to_string(uniqueValue));
    std::filesystem::create_directories(directory);
    const auto outputPath = directory / "patched-sample";
    const decompiler::BinaryPatcher patcher;
    const auto nops = decompiler::BinaryPatcher::nopBytes(call->bytes.size());

    const auto mismatch = patcher.patchInstruction(
        session.elfLoader(), call->address, call->bytes, std::span(nops).first(nops.size() - 1), outputPath);
    expect(
        mismatch.error == decompiler::PatchError::SizeMismatch,
        "different replacement size should be rejected");

    const std::vector<std::uint8_t> wrongOriginal(call->bytes.size(), 0xCC);
    const auto wrongBytes = patcher.patchInstruction(
        session.elfLoader(), call->address, wrongOriginal, nops, outputPath);
    expect(
        wrongBytes.error == decompiler::PatchError::OriginalBytesMismatch,
        "stale original bytes should be rejected");

    const auto originalOverwrite = patcher.patchInstruction(
        session.elfLoader(),
        call->address,
        call->bytes,
        nops,
        session.elfLoader().metadata().filePath);
    expect(
        originalOverwrite.error == decompiler::PatchError::OriginalOverwriteDenied,
        "original file overwrite should require confirmation");

    const std::vector<std::uint8_t> invalidInstructions(call->bytes.size(), 0x0F);
    const auto invalidPatch = patcher.patchInstruction(
        session.elfLoader(), call->address, call->bytes, invalidInstructions, outputPath);
    expect(
        invalidPatch.error == decompiler::PatchError::InvalidInstructionBytes,
        "undecodable replacement bytes should be rejected");

    const auto dataSection = session.elfLoader().findSection(".rodata");
    if(dataSection && dataSection->size > 0) {
        const std::vector<std::uint8_t> oneByte = {0x90};
        const auto nonExecutable = patcher.patchInstruction(
            session.elfLoader(), dataSection->address, oneByte, oneByte, outputPath);
        expect(
            nonExecutable.error == decompiler::PatchError::NonExecutableAddress,
            "non-executable section patch should be rejected");
    }

    const auto patched = patcher.patchInstruction(
        session.elfLoader(), call->address, call->bytes, nops, outputPath);
    expect(patched.succeeded(), "same-size NOP patch should succeed");
    expect(std::filesystem::exists(outputPath), "patched output should be created");

    std::error_code filesystemError;
    const auto permissions = std::filesystem::status(outputPath, filesystemError).permissions();
    expect(!filesystemError, "patched output permissions should be readable");
    expect(
        (permissions
         & (std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec
            | std::filesystem::perms::others_exec))
            != std::filesystem::perms::none,
        "patched output should remain executable");

    decompiler::AnalysisSession patchedSession;
    expect(patchedSession.analyze(outputPath), "patched output should re-analyze");
    if(patchedSession.isValid()) {
        const auto* instructions = patchedSession.instructionsFor(callerAddress);
        bool foundNopRange = false;
        if(instructions != nullptr) {
            std::size_t nopCount = 0;
            for(const auto& instruction : *instructions) {
                if(instruction.address >= call->address
                   && instruction.address < call->address + call->bytes.size()
                   && instruction.mnemonic == "nop") {
                    ++nopCount;
                }
            }
            foundNopRange = nopCount == call->bytes.size();
        }
        expect(foundNopRange, "patched call should redisassemble as NOP instructions");

        bool edgeStillPresent = false;
        for(const auto& edge : patchedSession.callGraph().edges()) {
            edgeStillPresent = edgeStillPresent
                               || (edge.callerAddress == callerAddress
                                   && edge.calleeAddress == calleeAddress);
        }
        expect(!edgeStillPresent, "call graph should refresh after removing the call");
        const auto* pseudocode = patchedSession.pseudocodeFor(callerAddress);
        expect(pseudocode != nullptr && !pseudocode->empty(), "pseudocode should refresh after patch");
    }

    const auto existingOutput = patcher.patchInstruction(
        session.elfLoader(), call->address, call->bytes, nops, outputPath);
    expect(
        existingOutput.error == decompiler::PatchError::OutputExists,
        "existing output should require overwrite confirmation");

    std::filesystem::remove_all(directory, filesystemError);
    return failures == 0 ? 0 : 1;
}
