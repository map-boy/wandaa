// ===========================================================================
//  tools/enc/enc_script.cpp -- ground truth for the WANDAA encoder.
//
//  Writes two files that line up one-for-one:
//
//      script.txt   one case per line: "NAME arg arg arg"
//      expect.hex   the bytes X64Asm emits for that case, as hex
//
//  compiler/enc_check.waa reads script.txt, encodes each case with
//  compiler/x64.waa, and prints hex. Diffing that against expect.hex checks
//  the Wandaa encoder against the C++ one, which tools/enc/run_verify.sh has
//  already checked against GNU as. Neither is hand-derived from the manual.
// ===========================================================================
#include "../../include/x64asm.hpp"
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {
X64Asm a;
std::ofstream script, hex;
size_t mark = 0;

void begin() { mark = a.code.size(); }
void end(const std::string& line) {
    script << line << "\n";
    std::string h;
    char buf[4];
    for (size_t i = mark; i < a.code.size(); ++i) {
        std::snprintf(buf, sizeof buf, "%02x", a.code[i]);
        h += buf;
    }
    hex << h << "\n";
}
X64Asm::Reg R(int i) { return (X64Asm::Reg)i; }
std::string S(long v) { return std::to_string(v); }
} // namespace

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: enc_script <script.txt> <expect.hex>\n"); return 1; }
    script.open(argv[1]);
    hex.open(argv[2]);

    const long DISPS[] = { 0, 1, 8, 127, 128, 255, 4096, -1, -8, -128, -129, -4096 };
    const long IMMS[]  = { 0, 1, 127, 128, -1, -128, -129, 32767, -32768, 1000000, -1000000 };

    struct { const char* name; void (X64Asm::*fn)(X64Asm::Reg, X64Asm::Reg); } RR[] = {
        {"mov_reg", &X64Asm::mov_reg}, {"add", &X64Asm::add}, {"sub", &X64Asm::sub},
        {"cmp", &X64Asm::cmp}, {"xorr", &X64Asm::xorr}, {"andr", &X64Asm::andr},
        {"orr", &X64Asm::orr}, {"test", &X64Asm::test}, {"imul", &X64Asm::imul},
    };
    for (auto& g : RR)
        for (int d = 0; d < 16; ++d)
            for (int s = 0; s < 16; ++s) {
                begin(); (a.*g.fn)(R(d), R(s));
                end(std::string(g.name) + " " + S(d) + " " + S(s));
            }

    struct { const char* name; void (X64Asm::*fn)(X64Asm::Reg); } RSINGLE[] = {
        {"push", &X64Asm::push}, {"pop", &X64Asm::pop}, {"shl_cl", &X64Asm::shl_cl},
        {"shr_cl", &X64Asm::shr_cl}, {"sar_cl", &X64Asm::sar_cl},
        {"idiv", &X64Asm::idiv}, {"neg", &X64Asm::neg}, {"call_reg", &X64Asm::call_reg},
    };
    for (auto& g : RSINGLE)
        for (int d = 0; d < 16; ++d) {
            begin(); (a.*g.fn)(R(d));
            end(std::string(g.name) + " " + S(d));
        }

    begin(); a.cqo();          end("cqo");
    begin(); a.ret();          end("ret");
    begin(); a.xor_eax_eax();  end("xor_eax_eax");
    begin(); a.movzx_rax_al(); end("movzx_rax_al");

    // mov_imm: both the imm32 form and the 64-bit movabs form.
    const long long BIG[] = { 0, 1, -1, 2147483647LL, -2147483648LL,
                              2147483648LL, -2147483649LL, 1234605616436508552LL };
    for (int d = 0; d < 16; ++d)
        for (long long v : BIG) {
            begin(); a.mov_imm(R(d), v);
            end("mov_imm " + S(d) + " " + std::to_string(v));
        }

    struct { const char* name; void (X64Asm::*fn)(X64Asm::Reg, int32_t); } G1[] = {
        {"add_imm", &X64Asm::add_imm}, {"sub_imm", &X64Asm::sub_imm},
        {"xor_imm", &X64Asm::xor_imm}, {"cmp_imm", &X64Asm::cmp_imm},
    };
    for (auto& g : G1)
        for (int d = 0; d < 16; ++d)
            for (long imm : IMMS) {
                begin(); (a.*g.fn)(R(d), (int32_t)imm);
                end(std::string(g.name) + " " + S(d) + " " + S(imm));
            }

    for (int b = 0; b < 16; ++b)
        for (long d : DISPS) {
            for (int r : {0, 3, 4, 5, 7, 8, 12, 13, 15}) {
                begin(); a.mov_load_base(R(r), R(b), (int32_t)d);
                end("mov_load_base " + S(r) + " " + S(b) + " " + S(d));
                begin(); a.mov_store_base(R(b), (int32_t)d, R(r));
                end("mov_store_base " + S(b) + " " + S(d) + " " + S(r));
                begin(); a.lea_base(R(r), R(b), (int32_t)d);
                end("lea_base " + S(r) + " " + S(b) + " " + S(d));
            }
            for (long imm : {0L, 1L, -1L, 1000000L}) {
                begin(); a.mov_store_imm_base(R(b), (int32_t)d, (int32_t)imm);
                end("mov_store_imm_base " + S(b) + " " + S(d) + " " + S(imm));
            }
        }

    for (int b = 0; b < 16; ++b)
        for (int i = 0; i < 16; ++i) {
            if ((i & 7) == 4) continue;                 // RSP cannot be an index
            for (int d : {0, 9, 15}) {
                begin(); a.lea_sib(R(d), R(b), R(i));
                end("lea_sib " + S(d) + " " + S(b) + " " + S(i));
                begin(); a.mov_load_sib(R(d), R(b), R(i));
                end("mov_load_sib " + S(d) + " " + S(b) + " " + S(i));
            }
        }

    for (int r = 0; r < 16; ++r)
        for (long d : DISPS) {
            begin(); a.lea_rbp(R(r), (int32_t)d);
            end("lea_rbp " + S(r) + " " + S(d));
            begin(); a.mov_store_rbp((int32_t)d, R(r));
            end("mov_store_rbp " + S(d) + " " + S(r));
            begin(); a.mov_load_rbp(R(r), (int32_t)d);
            end("mov_load_rbp " + S(r) + " " + S(d));
        }

    const char* CCS[] = {"l","g","le","ge","e","ne","b","a","be","ae","p","np"};
    for (const char* cc : CCS) {
        begin(); a.setcc(cc);
        end(std::string("setcc ") + cc);
    }

    std::fprintf(stderr, "enc_script: %zu bytes of cases\n", a.code.size());
    return 0;
}
