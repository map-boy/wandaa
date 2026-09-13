#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <stdexcept>

// Minimal x86-64 encoder covering only the instruction shapes the Wandaa
// codegen needs. Every encoding here was checked byte-for-byte against
// GNU `as` output before being written (see enc/test3.s + ground truth).
class X64Asm {
public:
    enum Reg { RAX=0, RCX=1, RDX=2, RBX=3, RSP=4, RBP=5, RSI=6, RDI=7,
               R8=8, R9=9, R10=10, R11=11, R12=12, R13=13, R14=14, R15=15 };

    std::vector<uint8_t> code;

    // ---- labels & fixups (all resolved once at the very end) ----
    void defineLabel(const std::string& name) { labels_[name] = code.size(); }

    void resolveFixups() {
        for (auto& f : fixups_) {
            auto it = labels_.find(f.label);
            if (it == labels_.end())
                throw std::runtime_error("undefined label: " + f.label);
            int32_t rel = (int32_t)((int64_t)it->second - (int64_t)(f.patchOffset + 4));
            code[f.patchOffset+0] = (uint8_t)(rel & 0xFF);
            code[f.patchOffset+1] = (uint8_t)((rel>>8) & 0xFF);
            code[f.patchOffset+2] = (uint8_t)((rel>>16) & 0xFF);
            code[f.patchOffset+3] = (uint8_t)((rel>>24) & 0xFF);
        }
    }

    // ---- data movement ----
    void mov_imm(Reg dst, int64_t imm) {
        if (imm >= INT32_MIN && imm <= INT32_MAX) {
            emitRex(true, false, false, dst>=8);
            emit(0xC7); emit(modrm(3, 0, dst)); emit32((uint32_t)(int32_t)imm);
        } else {
            emitRex(true, false, false, dst>=8);
            emit(0xB8 + (dst & 7));
            for (int i=0;i<8;i++) emit((uint8_t)((uint64_t)imm >> (8*i)));
        }
    }

    void push(Reg r) { if (r>=8) emit(0x41); emit(0x50 + (r&7)); }
    void pop(Reg r)  { if (r>=8) emit(0x41); emit(0x58 + (r&7)); }

    // "op dst, src" for ADD/SUB/CMP/XOR/TEST: rm=dst, reg=src
    void add(Reg dst, Reg src) { rmReg(0x01, dst, src); }
    void sub(Reg dst, Reg src) { rmReg(0x29, dst, src); }
    void cmp(Reg dst, Reg src) { rmReg(0x39, dst, src); }
    void xorr(Reg dst, Reg src){ rmReg(0x31, dst, src); }
    void test(Reg dst, Reg src){ rmReg(0x85, dst, src); }

    void xor_eax_eax() { emit(0x31); emit(0xC0); } // 32-bit zero idiom, no REX

    // IMUL is reversed: reg=dst, rm=src
    void imul(Reg dst, Reg src) {
        emitRex(true, dst>=8, false, src>=8);
        emit(0x0F); emit(0xAF); emit(modrm(3, dst, src));
    }

    void cqo() { emit(0x48); emit(0x99); }
    void idiv(Reg divisor) {
        emitRex(true, false, false, divisor>=8);
        emit(0xF7); emit(modrm(3, 7, divisor));
    }
    void neg(Reg r) {
        emitRex(true, false, false, r>=8);
        emit(0xF7); emit(modrm(3, 3, r));
    }

    // setcc al ; cc = "l","g","le","ge","e","ne"
    void setcc(const std::string& cc) {
        uint8_t op;
        if (cc=="l") op=0x9C; else if (cc=="g") op=0x9F;
        else if (cc=="le") op=0x9E; else if (cc=="ge") op=0x9D;
        else if (cc=="e") op=0x94; else if (cc=="ne") op=0x95;
        else throw std::runtime_error("bad setcc");
        emit(0x0F); emit(op); emit(0xC0);
    }
    void movzx_rax_al() { emit(0x48); emit(0x0F); emit(0xB6); emit(0xC0); }

    // lea dst, [rbp + disp]  (disp typically negative -> local var slot)
    void lea_rbp(Reg dst, int32_t disp) { rbpMem(0x8D, dst, disp); }
    void mov_store_rbp(int32_t disp, Reg src) { rbpMem(0x89, src, disp); }
    void mov_load_rbp(Reg dst, int32_t disp) { rbpMem(0x8B, dst, disp); }

    // mov dst, [base + index*8]   (array read; index is always RBX in codegen)
    void mov_load_sib(Reg dst, Reg base, Reg index) {
        emitRex(true, dst>=8, index>=8, base>=8);
        emit(0x8B); emit(modrm(0, dst, 4));
        emit(sib(3, index, base)); // scale=8
    }

    // mov [base + disp8], src   (array write; handles RSP/R12 SIB quirk)
    void mov_store_base_disp(Reg base, int32_t disp, Reg src) {
        emitRex(true, src>=8, false, base>=8);
        emit(0x89);
        if ((base & 7) == 4) { // RSP or R12: SIB byte mandatory
            emit(modrm(1, src, 4));
            emit(sib(0, /*no index*/4, base));
            emit((uint8_t)(int8_t)disp);
        } else {
            emit(modrm(1, src, base));
            emit((uint8_t)(int8_t)disp);
        }
    }

    // lea dst, [rip + label]
    void lea_rip(Reg dst, const std::string& label) {
        emitRex(true, dst>=8, false, false);
        emit(0x8D); emit(modrm(0, dst, 5));
        fixups_.push_back({code.size(), label});
        emit32(0);
    }

    void jmp(const std::string& label)  { emit(0xE9); fixups_.push_back({code.size(), label}); emit32(0); }
    void jz(const std::string& label)   { emit(0x0F); emit(0x84); fixups_.push_back({code.size(), label}); emit32(0); }
    void jnz(const std::string& label)  { emit(0x0F); emit(0x85); fixups_.push_back({code.size(), label}); emit32(0); }
    void call_label(const std::string& label) { emit(0xE8); fixups_.push_back({code.size(), label}); emit32(0); }

    void ret() { emit(0xC3); }

private:
    struct Fixup { size_t patchOffset; std::string label; };
    std::unordered_map<std::string, size_t> labels_;
    std::vector<Fixup> fixups_;

    void emit(uint8_t b) { code.push_back(b); }
    void emit32(uint32_t v) { for (int i=0;i<4;i++) emit((uint8_t)(v >> (8*i))); }

    static uint8_t modrm(int mod, int reg, int rm) {
        return (uint8_t)(((mod&3)<<6) | ((reg&7)<<3) | (rm&7));
    }
    static uint8_t sib(int scale, int index, int base) {
        return (uint8_t)(((scale&3)<<6) | ((index&7)<<3) | (base&7));
    }
    void emitRex(bool W, bool R, bool X, bool B) {
        if (!W && !R && !X && !B) return;
        emit((uint8_t)(0x40 | (W<<3) | (R<<2) | (X<<1) | B));
    }

    void rmReg(uint8_t opcode, Reg rm, Reg reg) {
        emitRex(true, reg>=8, false, rm>=8);
        emit(opcode); emit(modrm(3, reg, rm));
    }

    // shared by lea_rbp / mov_store_rbp / mov_load_rbp
    void rbpMem(uint8_t opcode, Reg reg, int32_t disp) {
        emitRex(true, reg>=8, false, false);
        emit(opcode);
        if (disp >= -128 && disp <= 127) {
            emit(modrm(1, reg, RBP)); emit((uint8_t)(int8_t)disp);
        } else {
            emit(modrm(2, reg, RBP)); emit32((uint32_t)disp);
        }
    }
};
