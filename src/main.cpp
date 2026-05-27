#include "Lexer.h"
#include "Parser.h"
#include "CodeGen.h"
#include "IRPrinter.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

static std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + path);
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

static void writeBinaryFile(const std::string& path,
                            const std::vector<uint8_t>& data) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Could not write to file: " + path);
    }
    file.write(reinterpret_cast<const char*>(data.data()),
               static_cast<std::streamsize>(data.size()));
}

static int runCommand(const std::string& cmd) {
    std::cout << "  > " << cmd << std::endl;
    return std::system(cmd.c_str());
}

static void printUsage(const char* progName) {
    std::cerr << "Usage: " << progName << " <source.ny> [output]" << std::endl;
    std::cerr << std::endl;
    std::cerr << "  Compiles a NyLang source file into a native executable." << std::endl;
    std::cerr << "  Options:" << std::endl;
    std::cerr << "    --target windows    Compiles to a Windows PE (.exe) executable." << std::endl;
    std::cerr << "    --target linux      Compiles to a Linux ELF executable (default)." << std::endl;
    std::cerr << "    --dump-ir           Print the NyIR before code generation, then exit." << std::endl;
    std::cerr << "  If [output] is not given, it defaults to the source filename" << std::endl;
    std::cerr << "  without extension." << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string inputPath = "";
    std::string outputName = "";
    nylang::Target target = nylang::Target::Linux;
    bool dumpIR = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--target") {
            if (i + 1 < argc) {
                std::string t = argv[++i];
                if (t == "windows") target = nylang::Target::Windows;
                else if (t == "linux") target = nylang::Target::Linux;
                else {
                    std::cerr << "Unknown target: " << t << std::endl;
                    return 1;
                }
            }
        } else if (arg == "--dump-ir") {
            dumpIR = true;
        } else if (inputPath.empty()) {
            inputPath = arg;
        } else if (outputName.empty()) {
            outputName = arg;
        }
    }

    if (inputPath.empty()) {
        printUsage(argv[0]);
        return 1;
    }

    fs::path inputFs(inputPath);
    if (outputName.empty()) {
        outputName = inputFs.stem().string();
    }

    std::string objPath;
    if (target == nylang::Target::Windows) {
        if (outputName.find(".exe") == std::string::npos) {
            outputName += ".exe";
        }
        objPath = outputName; // we write the final exe directly
    } else {
        objPath = outputName + ".o";
    }

    try {
        // ── Step 1: Read source ──────────────────────────────────────────
        std::cout << "[NyLang] Reading " << inputPath << std::endl;
        std::string source = readFile(inputPath);

        // ── Step 2: Lex ──────────────────────────────────────────────────
        std::cout << "[NyLang] Lexing..." << std::endl;
        nylang::Lexer lexer(source);
        auto tokens = lexer.tokenize();

        // ── Step 3: Parse ────────────────────────────────────────────────
        std::cout << "[NyLang] Parsing..." << std::endl;
        nylang::Parser parser(tokens);
        auto program = parser.parseProgram();

        std::cout << "[NyLang] Found " << program->functions.size()
                  << " function(s)" << std::endl;

        // ── Step 4: Code generation ──────────────────────────────────────
        nylang::CodeGen codegen(target);

        if (dumpIR) {
            std::cout << "[NyLang] Dumping IR..." << std::endl;
            nylang::IRProgram ir = codegen.generateIR(*program);
            nylang::IRPrinter printer(std::cout);
            printer.print(ir);
            return 0;
        }

        std::cout << "[NyLang] Generating machine code for " 
                  << (target == nylang::Target::Windows ? "Windows" : "Linux") << "..." << std::endl;
        std::vector<uint8_t> objData = codegen.generate(*program);

        writeBinaryFile(objPath, objData);
        std::cout << "[NyLang] Wrote " << objPath
                  << " (" << objData.size() << " bytes)" << std::endl;

        if (target == nylang::Target::Linux) {
            // ── Step 5: Link with GCC (for libc) ────────────────────────────
            std::cout << "[NyLang] Linking with GCC..." << std::endl;
            std::string gccCmd = "gcc " + objPath + " -o " + outputName + " -no-pie";
            int gccRet = runCommand(gccCmd);
            if (gccRet != 0) {
                std::cerr << "[NyLang] ERROR: GCC linking failed with exit code "
                          << gccRet << std::endl;
                return 1;
            }

            // ── Step 6: Clean up intermediate files ─────────────────────────
            fs::remove(objPath);
        }

        std::cout << "[NyLang] ✓ Build successful: " << (target == nylang::Target::Windows ? "" : "./") << outputName << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "[NyLang] ERROR: " << e.what() << std::endl;
        return 1;
    }
}
