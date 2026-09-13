// ============================================================================
//  tools/enc/verify_enc.cpp -- byte-for-byte ground-truth test for X64Asm
// ============================================================================
//  Emits the SAME instruction sequence twice: once as GNU-as Intel-syntax
//  text, once through X64Asm. tools/enc/run_verify.sh assembles the text,
//  extracts .text, and diffs it against the encoder's bytes. Any single wrong
//  byte fails the build and the harness reports which instruction it fell in.
//
//    ./verify_enc --asm out.s     write the GAS source
//    ./verify_enc --bin out.bin   write the encoder's bytes
//    ./verify_enc --map out.map   per-instruction offsets, for pinpointing
//
//  Coverage is deliberately exhaustive over the things that actually go wrong
//  in x86-64 encoding: every REX.W/R/X/B combination (all 16 registers in
//  every operand position), the RSP/R12 mandatory-SIB case, the RBP/R13
//  "mod=00 means RIP-relative" case, every disp0/disp8/disp32 boundary, the
//  imm8/imm32 boundary for group-1 opcodes, and RIP-relative displacement
//  with a trailing immediate (where the disp is measured past the immediate).
// ============================================================================

#include "../../include/x64asm.hpp"
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

static const char* RN[16] = { "rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi",
                              "r8","r9","r10","r11","r12","r13","r14","r15" };

struct Harness {
    X64Asm a;
    std::string src = ".intel_syntax noprefix\n.text\n";
    std::vector<std::pair<size_t, std::string>> map;   // encoder offset -> text

    void note(const std::string& text) { map.push_back({a.code.size(), text}); src += "  " + text + "\n"; }
};

static std::string disp(int32_t d) {
    if (d == 0) return "";
    return (d < 0) ? ("-" + std::to_string(-(int64_t)d)) : ("+" + std::to_string(d));
}

int main(int argc, char** argv) {
    if (argc < 3) { std::cerr << "usage: verify_enc --asm|--bin|--map <out>\n"; return 1; }
    const std::string mode = argv[1], out = argv[2];
    Harness h;
    auto R = [](int i){ return (X64Asm::Reg)i; };

    // ---- the disp values that straddle every encoding boundary ----
    const int32_t DISPS[] = { 0, 1, 8, 127, 128, 255, 4096, -1, -8, -128, -129, -4096 };
    const int32_t IMMS[]  = { 0, 1, 127, 128, -1, -128, -129, 32767, -32768, 1000000, -1000000 };

    // ---- mov reg, reg : all 256 REX.R/REX.B combinations ----
    for (int d = 0; d < 16; ++d)
        for (int s = 0; s < 16; ++s) {
            h.note(std::string("mov ") + RN[d] + ", " + RN[s]);
            h.a.mov_reg(R(d), R(s));
        }

    // ---- group-1 immediate forms : imm8 vs imm32 boundary, all registers ----
    struct { const char* m; void (X64Asm::*fn)(X64Asm::Reg,int32_t); } G1[] = {
        {"add", &X64Asm::add_imm}, {"sub", &X64Asm::sub_imm},
        {"xor", &X64Asm::xor_imm}, {"cmp", &X64Asm::cmp_imm},
    };
    for (auto& g : G1)
        for (int d = 0; d < 16; ++d)
            for (int32_t imm : IMMS) {
                h.note(std::string(g.m) + " " + RN[d] + ", " + std::to_string(imm));
                (h.a.*g.fn)(R(d), imm);
            }

    // ---- mov/lea with [base + disp] : RSP/R12 SIB + RBP/R13 mod=00 quirks ----
    for (int b = 0; b < 16; ++b)
        for (int32_t dd : DISPS) {
            const std::string mem = std::string("[") + RN[b] + disp(dd) + "]";
            for (int r : {0, 3, 7, 8, 12, 15}) {
                h.note(std::string("mov ") + RN[r] + ", qword ptr " + mem);
                h.a.mov_load_base(R(r), R(b), dd);

                h.note(std::string("mov qword ptr ") + mem + ", " + RN[r]);
                h.a.mov_store_base(R(b), dd, R(r));

                h.note(std::string("lea ") + RN[r] + ", " + mem);
                h.a.lea_base(R(r), R(b), dd);
            }
            for (int32_t imm : {0, 1, 127, 128, -1, -129, 1000000}) {
                h.note(std::string("mov qword ptr ") + mem + ", " + std::to_string(imm));
                h.a.mov_store_imm_base(R(b), dd, imm);
            }
        }

    // ---- lea dst, [base + index*8] : REX.X, plus RBP/R13 base in SIB ----
    for (int b = 0; b < 16; ++b)
        for (int i = 0; i < 16; ++i) {
            if ((i & 7) == 4) continue;               // RSP cannot be an index
            for (int d : {0, 9, 15}) {
                h.note(std::string("lea ") + RN[d] + ", [" + RN[b] + "+" + RN[i] + "*8]");
                h.a.lea_sib(R(d), R(b), R(i));
            }
        }

    // ---- pre-existing forms, re-verified so the extension cannot regress them --
    for (int d = 0; d < 16; ++d) {
        h.note(std::string("push ") + RN[d]);      h.a.push(R(d));
        h.note(std::string("pop ")  + RN[d]);      h.a.pop(R(d));
        h.note(std::string("neg ")  + RN[d]);      h.a.neg(R(d));
        h.note(std::string("idiv ") + RN[d]);      h.a.idiv(R(d));
        for (int s : {0, 3, 8, 15}) {
            h.note(std::string("add ")  + RN[d] + ", " + RN[s]); h.a.add(R(d), R(s));
            h.note(std::string("sub ")  + RN[d] + ", " + RN[s]); h.a.sub(R(d), R(s));
            h.note(std::string("cmp ")  + RN[d] + ", " + RN[s]); h.a.cmp(R(d), R(s));
            h.note(std::string("xor ")  + RN[d] + ", " + RN[s]); h.a.xorr(R(d), R(s));
            h.note(std::string("test ") + RN[d] + ", " + RN[s]); h.a.test(R(d), R(s));
            h.note(std::string("imul ") + RN[d] + ", " + RN[s]); h.a.imul(R(d), R(s));
        }
        for (int32_t dd : DISPS) {
            h.note(std::string("mov ") + RN[d] + ", qword ptr [rbp" + disp(dd) + "]");
            h.a.mov_load_rbp(R(d), dd);
            h.note(std::string("mov qword ptr [rbp") + disp(dd) + "], " + RN[d]);
            h.a.mov_store_rbp(dd, R(d));
            h.note(std::string("lea ") + RN[d] + ", [rbp" + disp(dd) + "]");
            h.a.lea_rbp(R(d), dd);
        }
        h.note(std::string("mov ") + RN[d] + ", 5");            h.a.mov_imm(R(d), 5);
        h.note(std::string("mov ") + RN[d] + ", -5");           h.a.mov_imm(R(d), -5);
        h.note(std::string("movabs ") + RN[d] + ", 0x123456789abcdef0");
        h.a.mov_imm(R(d), (int64_t)0x123456789abcdef0LL);
    }
    for (const char* cc : {"l","g","le","ge","e","ne"}) {
        h.note(std::string("set") + cc + " al"); h.a.setcc(cc);
    }
    h.note("movzx rax, al"); h.a.movzx_rax_al();
    h.note("cqo");           h.a.cqo();
    h.note("xor eax, eax");  h.a.xor_eax_eax();
    h.note("ret");           h.a.ret();

    // ---- RIP-relative, including the trailing-immediate (tail=4) case ----
    //
    // This is the one that silently produces an off-by-four if the
    // displacement is measured from the disp field instead of the end of the
    // instruction. A real label makes GNU as compute the truth for us.
    h.src += "Lrip_target:\n  .quad 0\n";
    h.a.defineAbsLabel("Lrip_target", h.a.code.size());
    h.a.code.insert(h.a.code.end(), 8, 0);

    for (int d = 0; d < 16; ++d) {
        h.note(std::string("lea ") + RN[d] + ", [rip+Lrip_target]");
        h.a.lea_rip(R(d), "Lrip_target");
        h.note(std::string("mov ") + RN[d] + ", qword ptr [rip+Lrip_target]");
        h.a.mov_load_rip(R(d), "Lrip_target");
    }
    for (int32_t imm : {0, 1, -1, 127, 128, 1000000}) {
        h.note("mov qword ptr [rip+Lrip_target], " + std::to_string(imm));
        h.a.mov_store_imm_rip("Lrip_target", imm);
    }
    h.note("call qword ptr [rip+Lrip_target]");
    h.a.call_mem_rip("Lrip_target");

    // ---- rel32 control flow, backward and forward ----
    //
    // X64Asm always emits rel32: a single-pass encoder cannot know a forward
    // jump's distance when it emits the opcode. GNU as runs multiple passes
    // and shortens anything within rel8 range, so to compare like with like
    // the targets are pushed beyond +/-128 bytes with padding, which forces
    // as to emit rel32 too. That makes this a real check of the displacement
    // arithmetic, not just the opcode byte.
    const int PAD = 200;
    auto pad = [&]{
        h.src += "  .space " + std::to_string(PAD) + ", 0x90\n";
        h.a.code.insert(h.a.code.end(), (size_t)PAD, 0x90);
    };

    h.src += "Lback:\n";
    h.a.defineLabel("Lback");
    pad();
    h.note("jmp Lback");       h.a.jmp("Lback");
    h.note("jz Lback");        h.a.jz("Lback");
    h.note("jnz Lback");       h.a.jnz("Lback");
    h.note("call Lback");      h.a.call_label("Lback");
    h.note("jmp Lfwd");        h.a.jmp("Lfwd");
    h.note("jz Lfwd");         h.a.jz("Lfwd");
    h.note("jnz Lfwd");        h.a.jnz("Lfwd");
    h.note("call Lfwd");       h.a.call_label("Lfwd");
    pad();
    h.src += "Lfwd:\n";
    h.a.defineLabel("Lfwd");
    h.note("ret");             h.a.ret();

    h.a.resolveFixups();

    if (mode == "--asm") {
        std::ofstream f(out); f << h.src;
    } else if (mode == "--bin") {
        std::ofstream f(out, std::ios::binary);
        f.write((const char*)h.a.code.data(), (std::streamsize)h.a.code.size());
    } else if (mode == "--map") {
        std::ofstream f(out);
        for (auto& m : h.map) f << m.first << "\t" << m.second << "\n";
        f << h.a.code.size() << "\t<end>\n";
    } else { std::cerr << "bad mode\n"; return 1; }

    std::cerr << "verify_enc: " << h.map.size() << " instructions, "
              << h.a.code.size() << " bytes (" << mode << ")\n";
    return 0;
}
