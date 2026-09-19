// ============================================================================
//  tools/extract_blob.cpp  --  Wandaa runtime blob extractor (BUILD-TIME ONLY)
// ============================================================================
//
//  Runs ONCE, during the Wandaa compiler's own build. Never at end-user
//  compile time. Usage:
//
//      x86_64-w64-mingw32-as -o runtime.o tools/runtime/runtime.s
//      g++ -std=c++17 -O2 -o extract_blob tools/extract_blob.cpp
//      ./extract_blob runtime.o include/runtime_blob.hpp
//
//  It parses the COFF/AMD64 object produced by GNU as and emits a generated
//  header containing:
//
//    * RUNTIME_BLOB[]   -- the raw .text bytes, code and data together
//    * IMPORT_FIXUPS[]  -- byte offsets of every `call [rip+__imp_X]` site,
//                          paired with the DLL + function it must be
//                          repointed at (PEWriter's own IAT slot)
//    * CODE_FIXUPS[]    -- byte offsets of blob -> generated-code references
//                          (just `wandaa_main`, from _start)
//    * LABELS[]         -- symbol name -> byte offset within the blob, so the
//                          dynamic codegen can emit `call rel32` into it
//
//  Correctness rests on one invariant, which this tool ENFORCES rather than
//  assumes: the blob contains no internal relocations. Code and data both
//  live in .text and every reference between them is RIP-relative, so `as`
//  resolves them at assembly time. That is what makes the blob relocatable by
//  plain memcpy. If that ever stops being true, extraction fails loudly here
//  instead of producing a subtly broken executable.
//
//  To check the result mechanically:
//      x86_64-w64-mingw32-objdump -d -M intel runtime.o
//  and compare against the disassembly of the emitted .exe (the blob bytes
//  are copied verbatim; only the 18 recorded fixup sites differ).
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Which DLL each imported symbol comes from. Deliberately an explicit table:
// an unrecognised import is a hard error, so a new API call in runtime.s can
// never silently end up bound to the wrong library.
// ---------------------------------------------------------------------------
const std::unordered_map<std::string, std::string> kImportDll = {
    {"GetStdHandle",                 "kernel32.dll"},
    {"WriteFile",                    "kernel32.dll"},
    {"ReadFile",                     "kernel32.dll"},
    {"ExitProcess",                  "kernel32.dll"},
    {"GetProcessHeap",               "kernel32.dll"},
    {"HeapAlloc",                    "kernel32.dll"},
    {"HeapFree",                     "kernel32.dll"},
    {"CreateFileA",                  "kernel32.dll"},
    {"GetFileSize",                  "kernel32.dll"},
    {"CloseHandle",                  "kernel32.dll"},
    {"GetLastError",                 "kernel32.dll"},
    {"AddVectoredExceptionHandler",  "kernel32.dll"},
};

// Symbols the blob may legitimately reference that are NOT Windows imports:
// the single edge from _start into the code the compiler generates.
const std::vector<std::string> kExternalCodeSymbols = { "wandaa_main" };

// COFF relocation types we understand (AMD64).
constexpr uint16_t IMAGE_REL_AMD64_REL32 = 0x0004;

[[noreturn]] void fail(const std::string& msg) {
    std::cerr << "extract_blob: " << msg << "\n";
    std::exit(1);
}

// Explicit little-endian readers. No packed structs, no reinterpret_cast onto
// the buffer: COFF relocation records are 10 bytes and symbol records 18, and
// neither is a size any compiler would lay out naturally.
uint16_t rd16(const std::vector<uint8_t>& b, size_t o) {
    if (o + 2 > b.size()) fail("read past end of file at " + std::to_string(o));
    return (uint16_t)(b[o] | (b[o + 1] << 8));
}
uint32_t rd32(const std::vector<uint8_t>& b, size_t o) {
    if (o + 4 > b.size()) fail("read past end of file at " + std::to_string(o));
    return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8) |
           ((uint32_t)b[o + 2] << 16) | ((uint32_t)b[o + 3] << 24);
}

struct Symbol {
    std::string name;
    uint32_t    value = 0;
    int16_t     section = 0;   // 1-based; 0 = undefined/external, -1 = absolute
    uint8_t     storageClass = 0;
};

struct Reloc {
    uint32_t site = 0;         // byte offset within .text
    uint32_t symIndex = 0;
    uint16_t type = 0;
};

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: extract_blob <runtime.o> <out runtime_blob.hpp>\n";
        return 1;
    }
    const std::string inPath = argv[1], outPath = argv[2];

    std::ifstream in(inPath, std::ios::binary);
    if (!in) fail("cannot open " + inPath);
    std::vector<uint8_t> f((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
    if (f.size() < 20) fail("file too small to be a COFF object");

    // ---------------------- COFF file header (20 bytes) ---------------------
    const uint16_t machine       = rd16(f, 0);
    const uint16_t numSections   = rd16(f, 2);
    const uint32_t symTabPtr     = rd32(f, 8);
    const uint32_t numSymbols    = rd32(f, 12);
    const uint16_t optHeaderSize = rd16(f, 16);

    if (machine != 0x8664)
        fail("not an x86-64 COFF object (machine=0x" + std::to_string(machine) + ")");
    if (symTabPtr == 0 || numSymbols == 0)
        fail("object has no symbol table; assemble without -s");

    // --------------------- string table (follows symbols) -------------------
    const size_t strTabOff = symTabPtr + (size_t)numSymbols * 18;
    auto symName = [&](size_t recOff) -> std::string {
        // If the first four bytes are zero, the next four are an offset into
        // the string table; otherwise the name is inline, NUL-padded to 8.
        if (rd32(f, recOff) == 0) {
            uint32_t off = rd32(f, recOff + 4);
            size_t p = strTabOff + off;
            if (p >= f.size()) fail("symbol name offset out of range");
            std::string s;
            while (p < f.size() && f[p]) s += (char)f[p++];
            return s;
        }
        std::string s;
        for (size_t i = 0; i < 8 && f[recOff + i]; ++i) s += (char)f[recOff + i];
        return s;
    };

    // ----------------------------- symbol table -----------------------------
    // Aux records must be skipped when reading, but they still consume symbol
    // indices, so relocations can index straight into this vector.
    std::vector<Symbol> syms(numSymbols);
    for (uint32_t i = 0; i < numSymbols; ) {
        const size_t rec = symTabPtr + (size_t)i * 18;
        Symbol s;
        s.name         = symName(rec);
        s.value        = rd32(f, rec + 8);
        s.section      = (int16_t)rd16(f, rec + 12);
        s.storageClass = f[rec + 16];
        const uint8_t numAux = f[rec + 17];
        syms[i] = s;
        i += 1 + numAux;
    }

    // ------------------------------- sections -------------------------------
    const size_t secTabOff = 20 + optHeaderSize;
    int textIndex = -1;                 // 1-based COFF section number
    std::vector<uint8_t> text;
    std::vector<Reloc> relocs;

    for (uint16_t s = 0; s < numSections; ++s) {
        const size_t h = secTabOff + (size_t)s * 40;
        std::string name;
        for (size_t i = 0; i < 8 && f[h + i]; ++i) name += (char)f[h + i];

        const uint32_t sizeOfRaw   = rd32(f, h + 16);
        const uint32_t ptrToRaw    = rd32(f, h + 20);
        const uint32_t ptrToReloc  = rd32(f, h + 24);
        uint32_t       numReloc    = rd16(f, h + 32);
        const uint32_t chars       = rd32(f, h + 36);

        // IMAGE_SCN_LNK_NRELOC_OVFL: >65535 relocations, real count lives in
        // the VirtualAddress field of the first (dummy) relocation record.
        if (numReloc == 0xFFFF && (chars & 0x01000000u))
            numReloc = rd32(f, ptrToReloc);

        if (name == ".text") {
            textIndex = (int)s + 1;
            if (ptrToRaw == 0 || sizeOfRaw == 0) fail(".text has no raw data");
            if (ptrToRaw + sizeOfRaw > f.size()) fail(".text raw data out of range");
            text.assign(f.begin() + ptrToRaw, f.begin() + ptrToRaw + sizeOfRaw);
            for (uint32_t r = 0; r < numReloc; ++r) {
                const size_t p = ptrToReloc + (size_t)r * 10;  // 10, not 12
                relocs.push_back({rd32(f, p), rd32(f, p + 4), rd16(f, p + 8)});
            }
        } else if ((name == ".data" || name == ".bss") && sizeOfRaw != 0) {
            // runtime.s deliberately keeps everything in .text so the blob is
            // memcpy-relocatable. Anything landing here would be silently
            // dropped, so refuse instead.
            fail("section " + name + " is non-empty (" + std::to_string(sizeOfRaw) +
                 " bytes). runtime.s must keep all code and data in .text.");
        }
    }
    if (textIndex < 0) fail("no .text section found");

    // ------------------------- classify relocations -------------------------
    struct ImpFix { uint32_t site; std::string dll, func; int32_t addend; };
    struct CodeFix { uint32_t site; std::string sym; int32_t addend; };
    std::vector<ImpFix>  impFixups;
    std::vector<CodeFix> codeFixups;

    auto isExternalCodeSym = [&](const std::string& n) {
        for (const auto& e : kExternalCodeSymbols) if (e == n) return true;
        return false;
    };

    for (const auto& r : relocs) {
        if (r.symIndex >= syms.size()) fail("relocation symbol index out of range");
        const Symbol& sym = syms[r.symIndex];

        if (r.type != IMAGE_REL_AMD64_REL32)
            fail("unsupported relocation type 0x" + std::to_string(r.type) +
                 " against '" + sym.name + "' at .text+0x" + std::to_string(r.site) +
                 ". Only IMAGE_REL_AMD64_REL32 (RIP-relative) is handled; the "
                 "runtime must stay position-independent.");

        // THE invariant: a relocation against a symbol defined inside .text
        // means `as` could not resolve it internally, and the blob would not
        // survive being memcpy'd to a different address.
        if (sym.section == textIndex)
            fail("internal relocation against '" + sym.name + "' at .text+0x" +
                 std::to_string(r.site) + ". The blob must be position-independent; "
                 "this reference would break when it is relocated.");
        if (sym.section > 0)
            fail("relocation against '" + sym.name + "' in a section other than .text");

        if (r.site + 4 > text.size()) fail("relocation site past end of .text");
        const int32_t addend = (int32_t)rd32(text, r.site);   // COFF stores addend in-place

        if (sym.name.rfind("__imp_", 0) == 0) {
            const std::string fn = sym.name.substr(6);
            auto it = kImportDll.find(fn);
            if (it == kImportDll.end())
                fail("unknown Windows import '" + fn + "'. Add it to kImportDll "
                     "in tools/extract_blob.cpp with the DLL it lives in.");
            impFixups.push_back({r.site, it->second, fn, addend});
        } else if (isExternalCodeSym(sym.name)) {
            codeFixups.push_back({r.site, sym.name, addend});
        } else {
            fail("unexpected undefined symbol '" + sym.name + "' at .text+0x" +
                 std::to_string(r.site) + ". Every external reference must be "
                 "either __imp_<WinAPI> or a declared external code symbol.");
        }
    }

    // ------------------------------- labels ---------------------------------
    // Every externally-visible symbol defined in .text becomes a call target
    // or data slot the dynamic codegen can address. Sorted for stable output
    // so the generated header is reproducible byte-for-byte.
    std::map<std::string, uint32_t> labels;
    for (const auto& s : syms) {
        if (s.section != textIndex) continue;
        if (s.storageClass != 2 /*IMAGE_SYM_CLASS_EXTERNAL*/) continue;
        if (s.name.empty() || s.name[0] == '.') continue;
        labels[s.name] = s.value;
    }
    for (const char* required : {"_start", "wandaa_print_int", "wandaa_print_strval",
                                 "wandaa_print_str", "wandaa_str_len", "wandaa_str_concat",
                                 "wandaa_str_eq", "wandaa_read_file", "wandaa_write_file",
                                 "wandaa_str_from_c", "wandaa_str_at", "wandaa_substr",
                                 "wandaa_int_to_str", "wandaa_str_to_int", "wandaa_array_new",
                                 "wandaa_print_float", "wandaa_bounds_trap",
                                 "wandaa_result_trap", "wandaa_misuse_trap", "wandaa_free",
                                 "wandaa_array_push", "wandaa_map_new", "wandaa_map_put",
                                 "wandaa_map_get", "wandaa_map_has",
                                 "wandaa_print_result",
                                 "wandaa_current_line", "wandaa_empty_str"}) {
        if (!labels.count(required))
            fail(std::string("required label '") + required +
                 "' not exported from runtime.s (needs a .globl)");
    }

    // ------------------------------- emit -----------------------------------
    std::ofstream out(outPath);
    if (!out) fail("cannot write " + outPath);

    out << "// ===========================================================================\n"
           "//  include/runtime_blob.hpp  --  GENERATED FILE, DO NOT EDIT BY HAND\n"
           "// ===========================================================================\n"
           "//  Produced by tools/extract_blob.cpp from tools/runtime/runtime.s.\n"
           "//  Regenerate with:  tools/build_runtime.sh   (or build_runtime.ps1)\n"
           "//\n"
           "//  CI regenerates this file and fails if it differs from the checked-in\n"
           "//  copy, so the bytes below always correspond to the .s in the tree.\n"
           "// ===========================================================================\n\n"
           "#pragma once\n#include <cstdint>\n#include <cstddef>\n#include <string>\n"
           "#include <stdexcept>\n\nnamespace wandaa_rt {\n\n";

    out << "// Raw .text of the assembled runtime: code and data together, with every\n"
           "// internal reference RIP-relative, so this is relocatable by memcpy.\n";
    out << "inline constexpr uint8_t RUNTIME_BLOB[] = {\n";
    for (size_t i = 0; i < text.size(); ++i) {
        if (i % 12 == 0) out << "    ";
        char buf[8];
        std::snprintf(buf, sizeof buf, "0x%02X", text[i]);
        out << buf << (i + 1 == text.size() ? "" : ",");
        out << ((i % 12 == 11 || i + 1 == text.size()) ? "\n" : " ");
    }
    out << "};\n";
    out << "inline constexpr size_t RUNTIME_BLOB_SIZE = sizeof(RUNTIME_BLOB);\n\n";

    out << "// `call [rip+disp32]` sites that must be repointed at PEWriter's IAT slot\n"
           "// for the named function. `site` is the byte offset of the disp32 field\n"
           "// within the blob; the patched value is:\n"
           "//     iatSlotOffset - (blobBase + site + 4) + addend\n";
    out << "struct ImportFixup { uint32_t site; const char* dll; const char* func; int32_t addend; };\n";
    out << "inline constexpr ImportFixup IMPORT_FIXUPS[] = {\n";
    for (const auto& x : impFixups)
        out << "    { 0x" << std::hex << x.site << std::dec << ", \"" << x.dll
            << "\", \"" << x.func << "\", " << x.addend << " },\n";
    out << "};\n";
    out << "inline constexpr size_t IMPORT_FIXUP_COUNT = sizeof(IMPORT_FIXUPS)/sizeof(IMPORT_FIXUPS[0]);\n\n";

    out << "// References from the blob into the code the compiler generates.\n";
    out << "struct CodeFixup { uint32_t site; const char* symbol; int32_t addend; };\n";
    out << "inline constexpr CodeFixup CODE_FIXUPS[] = {\n";
    for (const auto& x : codeFixups)
        out << "    { 0x" << std::hex << x.site << std::dec << ", \"" << x.sym
            << "\", " << x.addend << " },\n";
    out << "};\n";
    out << "inline constexpr size_t CODE_FIXUP_COUNT = sizeof(CODE_FIXUPS)/sizeof(CODE_FIXUPS[0]);\n\n";

    out << "// Byte offset of each runtime entry point / data slot within the blob.\n";
    out << "struct Label { const char* name; uint32_t off; };\n";
    out << "inline constexpr Label LABELS[] = {\n";
    for (const auto& kv : labels)
        out << "    { \"" << kv.first << "\", 0x" << std::hex << kv.second << std::dec << " },\n";
    out << "};\n";
    out << "inline constexpr size_t LABEL_COUNT = sizeof(LABELS)/sizeof(LABELS[0]);\n\n";

    out << "inline uint32_t labelOffset(const std::string& name) {\n"
           "    for (size_t i = 0; i < LABEL_COUNT; ++i)\n"
           "        if (name == LABELS[i].name) return LABELS[i].off;\n"
           "    throw std::runtime_error(\"runtime blob has no label: \" + name);\n"
           "}\n"
           "inline bool hasLabel(const std::string& name) {\n"
           "    for (size_t i = 0; i < LABEL_COUNT; ++i)\n"
           "        if (name == LABELS[i].name) return true;\n"
           "    return false;\n"
           "}\n\n"
           "} // namespace wandaa_rt\n";
    out.close();

    std::cout << "extract_blob: " << text.size() << " bytes, "
              << impFixups.size() << " import fixups, "
              << codeFixups.size() << " code fixups, "
              << labels.size() << " labels -> " << outPath << "\n";
    return 0;
}
