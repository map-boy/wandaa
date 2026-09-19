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

    // SSE registers, kept a distinct type so an XMM index can never be passed
    // where a general-purpose register is expected -- they share the same 0-15
    // numbering and would otherwise encode silently as the wrong operand.
    enum Xmm { XMM0=0, XMM1=1, XMM2=2,  XMM3=3,  XMM4=4,  XMM5=5,  XMM6=6,  XMM7=7,
               XMM8=8, XMM9=9, XMM10=10, XMM11=11, XMM12=12, XMM13=13, XMM14=14, XMM15=15 };

    std::vector<uint8_t> code;

    // ---- labels & fixups (all resolved once at the very end) ----
    //
    // Everything is tracked in SECTION coordinates, not buffer coordinates.
    // `code[0]` sits at sectionBase() within the PE section, so a label defined
    // here resolves to baseOffset_ + code.size(). That lets pre-existing code
    // (baseOffset_ == 0) behave exactly as before, while the PE backend can
    // place generated code after the fixed runtime blob and still emit
    // `call rel32` straight into it via defineAbsLabel().
    void setBaseOffset(size_t base) { baseOffset_ = base; }
    size_t baseOffset() const { return baseOffset_; }

    void defineLabel(const std::string& name) { labels_[name] = baseOffset_ + code.size(); }

    // A label whose section offset is already known and lies OUTSIDE this
    // buffer -- runtime blob entry points, string literals in the data area.
    void defineAbsLabel(const std::string& name, size_t sectionOffset) {
        labels_[name] = sectionOffset;
    }

    void resolveFixups() {
        for (auto& f : fixups_) {
            auto it = labels_.find(f.label);
            if (it == labels_.end())
                throw std::runtime_error("undefined label: " + f.label);
            // A RIP-relative displacement is measured from the END of the
            // instruction. For most forms the disp32 field IS the last thing,
            // so tail == 0; for `mov qword ptr [rip+X], imm32` four immediate
            // bytes follow the field and tail == 4. Getting this wrong is the
            // classic RIP-relative-with-immediate bug, so it is explicit.
            int64_t site = (int64_t)(baseOffset_ + f.patchOffset);
            int32_t rel = (int32_t)((int64_t)it->second - (site + 4 + f.tail));
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
    void andr(Reg dst, Reg src){ rmReg(0x21, dst, src); }
    void orr (Reg dst, Reg src){ rmReg(0x09, dst, src); }
    // Shift by CL. The count register is fixed by the encoding -- there is no
    // shift-by-any-register form -- so the caller puts it in RCX first.
    void shl_cl(Reg dst){ emitRex(true,false,false,dst>=8); emit(0xD3); emit(modrm(3,4,dst)); }
    void shr_cl(Reg dst){ emitRex(true,false,false,dst>=8); emit(0xD3); emit(modrm(3,5,dst)); }
    void sar_cl(Reg dst){ emitRex(true,false,false,dst>=8); emit(0xD3); emit(modrm(3,7,dst)); }
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

    // setcc al
    //
    // Signed conditions ("l","g","le","ge") are for integer cmp. UCOMISD sets
    // CF/ZF rather than SF/OF, so float comparisons need the UNSIGNED forms
    // ("b","a","be","ae") -- using "l" after a ucomisd silently compares the
    // wrong flags.
    void setcc(const std::string& cc) {
        uint8_t op;
        if (cc=="l") op=0x9C; else if (cc=="g") op=0x9F;
        else if (cc=="le") op=0x9E; else if (cc=="ge") op=0x9D;
        else if (cc=="e") op=0x94; else if (cc=="ne") op=0x95;
        else if (cc=="b") op=0x92; else if (cc=="a") op=0x97;
        else if (cc=="be") op=0x96; else if (cc=="ae") op=0x93;
        else if (cc=="p") op=0x9A; else if (cc=="np") op=0x9B;
        else throw std::runtime_error("bad setcc: " + cc);
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
        fixups_.push_back({code.size(), label, 0});
        emit32(0);
    }

    void jmp(const std::string& label)  { emit(0xE9); fixups_.push_back({code.size(), label, 0}); emit32(0); }
    void jz(const std::string& label)   { emit(0x0F); emit(0x84); fixups_.push_back({code.size(), label, 0}); emit32(0); }
    void jnz(const std::string& label)  { emit(0x0F); emit(0x85); fixups_.push_back({code.size(), label, 0}); emit32(0); }
    // jp -- "parity", which after UCOMISD means UNORDERED, i.e. an operand was
    // NaN. Every float comparison has to branch this out first, because NaN
    // compares false against everything including itself.
    void jp(const std::string& label)   { emit(0x0F); emit(0x8A); fixups_.push_back({code.size(), label, 0}); emit32(0); }
    // jb -- UNSIGNED below. Bounds checks rely on this: comparing a signed
    // index against a length unsigned means a negative index wraps to a huge
    // value and fails the same test, so one branch catches both ends.
    void jb(const std::string& label)   { emit(0x0F); emit(0x82); fixups_.push_back({code.size(), label, 0}); emit32(0); }
    void call_label(const std::string& label) { emit(0xE8); fixups_.push_back({code.size(), label, 0}); emit32(0); }

    // call r64 -- an INDIRECT call, needed to call a closure whose code
    // pointer is only known at runtime. No REX.W: a near call is already
    // 64-bit in long mode, so the only REX bit that can appear is B, for
    // r8-r15. `call rax` is FF D0; `call r15` is 41 FF D7.
    void call_reg(Reg target) {
        emitRex(false, false, false, target>=8);
        emit(0xFF); emit(modrm(3, 2, target));
    }

    // =======================================================================
    //  Additional forms required by the direct-to-PE backend.
    //
    //  Every encoding below was checked byte-for-byte against GNU `as`
    //  output across all 16 general-purpose registers and every
    //  disp0/disp8/disp32 boundary -- see tools/enc/verify_enc.cpp, which is
    //  run as a CI gate. They are NOT hand-derived from the manual.
    // =======================================================================

    // mov dst, src   (both 64-bit registers).  REX.W 89 /r, rm=dst reg=src
    void mov_reg(Reg dst, Reg src) { rmReg(0x89, dst, src); }

    // Group-1 immediate forms: REX.W 83 /n ib when the immediate fits in a
    // signed byte, otherwise REX.W 81 /n id.
    void add_imm(Reg dst, int32_t imm) { grp1Imm(0, dst, imm); }
    void sub_imm(Reg dst, int32_t imm) { grp1Imm(5, dst, imm); }
    void xor_imm(Reg dst, int32_t imm) { grp1Imm(6, dst, imm); }
    void cmp_imm(Reg dst, int32_t imm) { grp1Imm(7, dst, imm); }

    // mov dst, [base + disp]      REX.W 8B /r
    void mov_load_base(Reg dst, Reg base, int32_t disp) {
        emitRex(true, dst>=8, false, base>=8);
        emit(0x8B); memBaseDisp(dst, base, disp);
    }
    // mov [base + disp], src      REX.W 89 /r
    void mov_store_base(Reg base, int32_t disp, Reg src) {
        emitRex(true, src>=8, false, base>=8);
        emit(0x89); memBaseDisp(src, base, disp);
    }
    // lea dst, [base + disp]      REX.W 8D /r
    void lea_base(Reg dst, Reg base, int32_t disp) {
        emitRex(true, dst>=8, false, base>=8);
        emit(0x8D); memBaseDisp(dst, base, disp);
    }
    // mov qword ptr [base + disp], imm32   REX.W C7 /0 id  (sign-extended)
    void mov_store_imm_base(Reg base, int32_t disp, int32_t imm) {
        emitRex(true, false, false, base>=8);
        emit(0xC7); memBaseDisp(0 /* /0 */, base, disp); emit32((uint32_t)imm);
    }

    // lea dst, [base + index*8]   REX.W 8D /r + SIB
    void lea_sib(Reg dst, Reg base, Reg index) {
        emitRex(true, dst>=8, index>=8, base>=8);
        emit(0x8D); memSib(dst, base, index);
    }

    // mov qword ptr [rip + label], imm32
    //
    // The disp32 is relative to the end of the instruction, which here is
    // FOUR BYTES PAST the displacement field because the immediate follows it.
    // Hence tail = 4 on the fixup.
    void mov_store_imm_rip(const std::string& label, int32_t imm) {
        emit(0x48); emit(0xC7); emit(modrm(0, 0, 5));
        fixups_.push_back({code.size(), label, 4});
        emit32(0);
        emit32((uint32_t)imm);
    }

    // call qword ptr [rip + label]   FF /2, mod=00 rm=101
    //
    // How generated code reaches a Windows import: `label` names an IAT slot
    // (registered with defineAbsLabel from PEWriter::iatSlotOffset). No REX --
    // FF /2 defaults to 64-bit operand size in long mode.
    void call_mem_rip(const std::string& label) {
        emit(0xFF); emit(modrm(0, 2, 5));
        fixups_.push_back({code.size(), label, 0});
        emit32(0);
    }

    // mov dst, [rip + label]      REX.W 8B /r, mod=00 rm=101
    void mov_load_rip(Reg dst, const std::string& label) {
        emitRex(true, dst>=8, false, false);
        emit(0x8B); emit(modrm(0, dst, 5));
        fixups_.push_back({code.size(), label, 0});
        emit32(0);
    }

    // =======================================================================
    //  SSE2 scalar double (f64).
    //
    //  Encoding shape for all of these:
    //
    //      [mandatory prefix] [REX] 0F <opcode> <modrm> ...
    //
    //  The prefix comes BEFORE the REX byte. Emitting REX first assembles to a
    //  different instruction, and it is the single easiest mistake to make
    //  here -- which is why every form below is covered in
    //  tools/enc/verify_enc.cpp across XMM0-15 and diffed against GNU `as`.
    // =======================================================================

    // movsd xmm, xmm            F2 0F 10 /r
    void movsd_rr(Xmm dst, Xmm src)            { sseRR(0xF2, false, 0x10, dst, src); }
    // movsd xmm, qword ptr [base+disp]   F2 0F 10 /r
    void movsd_load(Xmm dst, Reg base, int32_t disp) { sseRM(0xF2, false, 0x10, dst, base, disp); }
    // movsd qword ptr [base+disp], xmm   F2 0F 11 /r
    void movsd_store(Reg base, int32_t disp, Xmm src) { sseRM(0xF2, false, 0x11, src, base, disp); }

    // Arithmetic: dst = dst op src
    void addsd(Xmm dst, Xmm src) { sseRR(0xF2, false, 0x58, dst, src); }
    void mulsd(Xmm dst, Xmm src) { sseRR(0xF2, false, 0x59, dst, src); }
    void subsd(Xmm dst, Xmm src) { sseRR(0xF2, false, 0x5C, dst, src); }
    void divsd(Xmm dst, Xmm src) { sseRR(0xF2, false, 0x5E, dst, src); }
    void sqrtsd(Xmm dst, Xmm src){ sseRR(0xF2, false, 0x51, dst, src); }

    // ucomisd xmm, xmm          66 0F 2E /r   (sets ZF/PF/CF, not SF/OF)
    void ucomisd(Xmm a, Xmm b) { sseRR(0x66, false, 0x2E, a, b); }

    // xorpd xmm, xmm            66 0F 57 /r   (zeroing, and sign flips)
    void xorpd(Xmm dst, Xmm src) { sseRR(0x66, false, 0x57, dst, src); }

    // cvtsi2sd xmm, r64         F2 REX.W 0F 2A /r   -- reg=xmm, rm=gpr
    void cvtsi2sd(Xmm dst, Reg src) { sseRRmixed(0xF2, true, 0x2A, dst, src); }
    // cvttsd2si r64, xmm        F2 REX.W 0F 2C /r   -- reg=gpr, rm=xmm
    //                                                  (truncates toward zero)
    void cvttsd2si(Reg dst, Xmm src) { sseRRmixed(0xF2, true, 0x2C, dst, src); }

    // movq xmm, r64             66 REX.W 0F 6E /r   -- reg=xmm, rm=gpr
    void movq_xmm_r64(Xmm dst, Reg src) { sseRRmixed(0x66, true, 0x6E, dst, src); }
    // movq r64, xmm             66 REX.W 0F 7E /r   -- reg=xmm, rm=gpr
    //
    // Note the operand roles: even though the destination is the GPR, the xmm
    // is still the ModRM.reg field. The direction lives in the opcode.
    void movq_r64_xmm(Reg dst, Xmm src) { sseRRmixed(0x66, true, 0x7E, src, dst); }

    // ---- sub-64-bit field access, for C-ABI struct layouts (hanze FFI) ----
    // Stores truncate naturally from the low bits of src. Loads zero-extend
    // via movzx so the result is a clean 64-bit Wandaa integer.

    // mov byte ptr [base+disp], r8    88 /r
    void mov_store_base_byte(Reg base, int32_t disp, Reg src) {
        bool needForce = (src>=4 && src<8); // avoid AH/CH/DH/BH aliasing
        emitRex(false, src>=8, false, base>=8, needForce);
        emit(0x88); memBaseDisp(src, base, disp);
    }
    // movzx r64, byte ptr [base+disp]   0F B6 /r
    void mov_load_base_byte_zx(Reg dst, Reg base, int32_t disp) {
        emitRex(true, dst>=8, false, base>=8);
        emit(0x0F); emit(0xB6); memBaseDisp(dst, base, disp);
    }
    // mov word ptr [base+disp], r16   66 89 /r
    void mov_store_base_word(Reg base, int32_t disp, Reg src) {
        emit(0x66);
        emitRex(false, src>=8, false, base>=8);
        emit(0x89); memBaseDisp(src, base, disp);
    }
    // movzx r64, word ptr [base+disp]   0F B7 /r
    void mov_load_base_word_zx(Reg dst, Reg base, int32_t disp) {
        emitRex(true, dst>=8, false, base>=8);
        emit(0x0F); emit(0xB7); memBaseDisp(dst, base, disp);
    }
    // mov dword ptr [base+disp], r32   89 /r
    void mov_store_base_dword(Reg base, int32_t disp, Reg src) {
        emitRex(false, src>=8, false, base>=8);
        emit(0x89); memBaseDisp(src, base, disp);
    }
    // mov r32, dword ptr [base+disp]   8B /r   (writing r32 auto-zero-extends r64)
    void mov_load_base_dword_zx(Reg dst, Reg base, int32_t disp) {
        emitRex(false, dst>=8, false, base>=8);
        emit(0x8B); memBaseDisp(dst, base, disp);
    }

    void ret() { emit(0xC3); }

private:
    struct Fixup { size_t patchOffset; std::string label; int32_t tail = 0; };
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
    void emitRex(bool W, bool R, bool X, bool B, bool force=false) {
        if (!W && !R && !X && !B && !force) return;
        emit((uint8_t)(0x40 | (W<<3) | (R<<2) | (X<<1) | B));
    }

    // ---- SSE2 encoding helpers -------------------------------------------
    //
    // Order is mandatory-prefix, then REX, then 0F, then opcode. REX is only
    // emitted when it carries a bit, exactly as for the integer forms.

    // Both operands in the same register file (xmm/xmm).
    void sseRR(uint8_t prefix, bool W, uint8_t op, int reg, int rm) {
        emit(prefix);
        emitRex(W, reg >= 8, false, rm >= 8);
        emit(0x0F); emit(op); emit(modrm(3, reg, rm));
    }

    // Operands in different register files (xmm <-> gpr). Identical encoding;
    // named separately so the call sites document which operand is which.
    void sseRRmixed(uint8_t prefix, bool W, uint8_t op, int reg, int rm) {
        sseRR(prefix, W, op, reg, rm);
    }

    // reg is an xmm index; the memory operand uses the ordinary ModRM path,
    // so the RSP/R12 SIB and RBP/R13 mod=00 quirks are handled there.
    void sseRM(uint8_t prefix, bool W, uint8_t op, int reg, Reg base, int32_t disp) {
        emit(prefix);
        emitRex(W, reg >= 8, false, base >= 8);
        emit(0x0F); emit(op); memBaseDisp(reg, base, disp);
    }

    // Encode ModRM (+SIB, +disp) for [base + disp], covering the two quirks
    // that make x86-64 addressing unforgiving:
    //   * RSP/R12 (rm & 7 == 4) cannot be a bare base -- a SIB byte with
    //     index=100b (none) is mandatory.
    //   * RBP/R13 (rm & 7 == 5) cannot use mod=00 -- that encoding means
    //     RIP-relative -- so a zero disp8 must be emitted instead.
    void memBaseDisp(int reg, Reg base, int32_t disp) {
        const bool needSib  = ((base & 7) == 4);
        const bool baseIsBp = ((base & 7) == 5);
        int mod;
        if (disp == 0 && !baseIsBp)          mod = 0;
        else if (disp >= -128 && disp <= 127) mod = 1;
        else                                  mod = 2;

        emit(modrm(mod, reg, needSib ? 4 : (base & 7)));
        if (needSib) emit(sib(0, 4 /* no index */, base));
        if (mod == 1)      emit((uint8_t)(int8_t)disp);
        else if (mod == 2) emit32((uint32_t)disp);
    }

    // Encode ModRM+SIB for [base + index*8]. Same RBP/R13 mod=00 restriction.
    void memSib(int reg, Reg base, Reg index) {
        const bool baseIsBp = ((base & 7) == 5);
        const int mod = baseIsBp ? 1 : 0;
        emit(modrm(mod, reg, 4));
        emit(sib(3 /* scale = 8 */, index, base));
        if (mod == 1) emit(0);
    }

    // Group-1 (ADD/SUB/XOR/CMP) with an immediate. Three encodings, picked
    // shortest-first, matching what GNU as emits:
    //
    //   imm8 fits          -> REX.W 83 /n ib          (4 bytes)
    //   dst is RAX         -> REX.W <05+n*8> id       (6 bytes, no ModRM)
    //   otherwise          -> REX.W 81 /n id          (7 bytes)
    //
    // The accumulator form has no ModRM byte and therefore no REX.B, so it is
    // only valid for RAX -- never for R8, which would otherwise look identical.
    void grp1Imm(int digit, Reg dst, int32_t imm) {
        emitRex(true, false, false, dst>=8);
        if (imm >= -128 && imm <= 127) {
            emit(0x83); emit(modrm(3, digit, dst)); emit((uint8_t)(int8_t)imm);
        } else if (dst == RAX) {
            emit((uint8_t)(0x05 + digit * 8)); emit32((uint32_t)imm);
        } else {
            emit(0x81); emit(modrm(3, digit, dst)); emit32((uint32_t)imm);
        }
    }

    size_t baseOffset_ = 0;

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
