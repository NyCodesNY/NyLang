cmake_minimum_required(VERSION 3.16)
project(NyLang VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(nylang
    src/main.cpp
    src/frontend/Lexer.cpp
    src/frontend/Parser.cpp
    src/backend/CodeGen.cpp
    src/ir/IRGen.cpp
    src/ir/IRPrinter.cpp
    src/backend/IRCodeGen.cpp
    src/backend/Assembler.cpp
    src/linker/ElfWriter.cpp
    src/linker/PeWriter.cpp
)

target_include_directories(nylang PRIVATE 
    src
    src/frontend
    src/ir
    src/backend
    src/linker
)
