// ===========================================================================
//  tools/pe/pe_run.cpp -- build the shared PE cases with the C++ PEWriter.
//
//  Reads tools/pe/pe_cases.txt and prints one line of hex per image, which
//  compiler/pe_check.waa must reproduce byte-for-byte using compiler/pe.waa.
// ===========================================================================
#include "../../include/pe_writer.hpp"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
void dump(const std::vector<uint8_t>& img) {
    std::string h;
    char b[4];
    for (uint8_t x : img) { std::snprintf(b, sizeof b, "%02x", x); h += b; }
    std::cout << h << "\n";
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: pe_run <cases.txt>\n"); return 1; }
    std::ifstream in(argv[1]);
    if (!in) { std::fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }

    PEWriter* pe = new PEWriter();
    size_t codeBase = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        if (line == "---") { dump(pe->buildImage()); delete pe; pe = new PEWriter(); codeBase = 0; continue; }

        std::istringstream ss(line);
        std::string op; ss >> op;
        if (op == "import") {
            std::string dll, func; ss >> dll >> func;
            pe->addImport(dll, func);
        } else if (op == "begincode") {
            codeBase = pe->beginCode();
        } else if (op == "emit") {
            std::string hx; ss >> hx;
            std::vector<uint8_t> bytes;
            for (size_t i = 0; i + 1 < hx.size(); i += 2)
                bytes.push_back((uint8_t)(hexDigit(hx[i]) * 16 + hexDigit(hx[i+1])));
            pe->emit(bytes);
        } else if (op == "callimport") {
            int id; ss >> id;
            pe->callImport(id);
        } else if (op == "entry") {
            long n; ss >> n;
            pe->setEntryOffset(codeBase + (size_t)n);
        } else {
            std::fprintf(stderr, "unknown op: %s\n", op.c_str());
            return 1;
        }
    }
    delete pe;
    return 0;
}
