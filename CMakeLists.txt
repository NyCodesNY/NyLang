cmake_minimum_required(VERSION 3.16)
project(NyLang VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(nylang
    src/main.cpp
    src/Lexer.cpp
    src/Parser.cpp
    src/CodeGen.cpp
    src/IRGen.cpp
    src/IRPrinter.cpp
    src/IRCodeGen.cpp
    src/Assembler.cpp
    src/ElfWriter.cpp
    src/PeWriter.cpp
)

target_include_directories(nylang PRIVATE src)
