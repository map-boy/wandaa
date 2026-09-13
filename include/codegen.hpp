#pragma once
#include "node.hpp"
#include <string>
#include <vector>
#include <cstdint>

// Compile a parsed Wandaa program straight to a native x86-64 Windows PE
// executable and write it to outPath. No assembler, no linker, no MinGW --
// wandaac.exe is the only tool an end user runs.
//
// Throws std::runtime_error (with a Kinyarwanda message) on any compile error.
void generateExe(const NodePtr& program, const std::string& outPath);

// Same compilation, but returns the finished PE image in memory instead of
// writing it. Used by the test harness to diff two builds byte-for-byte.
std::vector<uint8_t> generateExeBytes(const NodePtr& program);
