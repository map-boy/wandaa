#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>
#include <fstream>

class PEWriter {
public:
    int addImport(const std::string& dll, const std::string& func) {
        int id = (int)imports_.size();
        imports_.push_back({dll, func});
        return id;
    }

    // Call once, after ALL addImport() calls. Lays out the import block
    // at the start of the section. Returns the offset where code begins.
    size_t beginCode() {
        std::vector<std::string> dllOrder;
        std::unordered_map<std::string, std::vector<int>> byDll;
        for (int i = 0; i < (int)imports_.size(); ++i) {
            auto& im = imports_[i];
            if (!byDll.count(im.dll)) dllOrder.push_back(im.dll);
            byDll[im.dll].push_back(i);
        }

        size_t numDlls = dllOrder.size();
        size_t descTableSize = (numDlls + 1) * 20;
        buf_.assign(descTableSize, 0);

        std::unordered_map<std::string, size_t> iatOffsetForDll;
        std::unordered_map<int, size_t> slotOffset;
        size_t cursor = buf_.size();
        for (auto& dll : dllOrder) {
            iatOffsetForDll[dll] = cursor;
            for (int idx : byDll[dll]) { slotOffset[idx] = cursor; cursor += 8; }
            cursor += 8; // null terminator thunk
        }
        buf_.resize(cursor, 0);

        std::unordered_map<int, size_t> nameOffset;
        for (auto& dll : dllOrder)
            for (int idx : byDll[dll]) {
                nameOffset[idx] = buf_.size();
                put16(0);
                putStr(imports_[idx].func);
                if (buf_.size() % 2) buf_.push_back(0);
            }

        std::unordered_map<std::string, size_t> dllNameOffset;
        for (auto& dll : dllOrder) { dllNameOffset[dll] = buf_.size(); putStr(dll); }

        for (int i = 0; i < (int)imports_.size(); ++i)
            put64At(slotOffset[i], (uint64_t)(SECTION_RVA + nameOffset[i]));

        for (size_t d = 0; d < numDlls; ++d) {
            size_t base = d * 20;
            put32At(base + 0, 0);
            put32At(base + 4, 0);
            put32At(base + 8, 0);
            put32At(base + 12, (uint32_t)(SECTION_RVA + dllNameOffset[dllOrder[d]]));
            put32At(base + 16, (uint32_t)(SECTION_RVA + iatOffsetForDll[dllOrder[d]]));
        }

        importDirRVA_ = SECTION_RVA;
        importDirSize_ = (uint32_t)descTableSize;
        for (auto& kv : slotOffset) iatSlot_[kv.first] = kv.second;

        codeBase_ = buf_.size();
        return codeBase_;
    }

    size_t emit(std::initializer_list<uint8_t> bytes) {
        size_t off = buf_.size();
        buf_.insert(buf_.end(), bytes.begin(), bytes.end());
        return off;
    }
    size_t emit(const std::vector<uint8_t>& bytes) {
        size_t off = buf_.size();
        buf_.insert(buf_.end(), bytes.begin(), bytes.end());
        return off;
    }
    size_t currentOffset() const { return buf_.size(); }

    // FF 15 rel32  ->  call qword ptr [rip+rel32]  (calls an imported function)
    void callImport(int id) {
        buf_.push_back(0xFF);
        buf_.push_back(0x15);
        size_t patchAt = buf_.size();
        put32(0);
        int64_t disp = (int64_t)iatSlot_.at(id) - (int64_t)(patchAt + 4);
        put32At(patchAt, (uint32_t)(int32_t)disp);
    }

    size_t iatSlotOffset(int id) const { return iatSlot_.at(id); }
    void setEntryOffset(size_t off) { entryOffset_ = off; }

    bool writeExe(const std::string& path) {
        std::vector<uint8_t> file;
        auto put16f = [&](uint16_t v){ file.push_back(v&0xFF); file.push_back((v>>8)&0xFF); };
        auto put32f = [&](uint32_t v){ for(int i=0;i<4;i++) file.push_back((v>>(8*i))&0xFF); };
        auto put64f = [&](uint64_t v){ for(int i=0;i<8;i++) file.push_back((uint8_t)((v>>(8*i))&0xFF)); };
        auto padTo  = [&](size_t n){ while(file.size()<n) file.push_back(0); };

        put16f(0x5A4D);
        for (int i=0;i<29;i++) put16f(0);
        put32f(0x40);
        padTo(0x40);

        put32f(0x00004550);
        put16f(0x8664);
        put16f(1);
        put32f(0); put32f(0); put32f(0);
        put16f(240);
        put16f(0x0022);

        size_t sectionVSize = buf_.size();
        uint32_t sectionRawSize = (uint32_t)alignUp(sectionVSize, FILE_ALIGN);

        put16f(0x020B);
        file.push_back(0); file.push_back(0);
        put32f((uint32_t)sectionVSize);
        put32f(0); put32f(0);
        put32f((uint32_t)(SECTION_RVA + entryOffset_));
        put32f(SECTION_RVA);
        put64f(IMAGE_BASE);
        put32f(SECT_ALIGN);
        put32f(FILE_ALIGN);
        put16f(6); put16f(0);
        put16f(0); put16f(0);
        put16f(6); put16f(0);
        put32f(0);
        uint32_t sizeOfImage = (uint32_t)alignUp(SECTION_RVA + sectionVSize, SECT_ALIGN);
        put32f(sizeOfImage);
        uint32_t sizeOfHeaders = (uint32_t)alignUp(0x40 + 4+20+240 + 40, FILE_ALIGN);
        put32f(sizeOfHeaders);
        put32f(0);
        put16f(3);
        put16f(0);
        put64f(0x100000); put64f(0x1000);
        put64f(0x100000); put64f(0x1000);
        put32f(0);
        put32f(16);
        for (int i = 0; i < 16; i++) {
            if (i == 1) { put32f(importDirRVA_); put32f(importDirSize_); }
            else { put32f(0); put32f(0); }
        }

        const char name[8] = {'.','t','e','x','t',0,0,0};
        file.insert(file.end(), name, name+8);
        put32f((uint32_t)sectionVSize);
        put32f(SECTION_RVA);
        put32f(sectionRawSize);
        put32f(sizeOfHeaders);
        put32f(0); put32f(0);
        put16f(0); put16f(0);
        put32f(0xE0000020);

        padTo(sizeOfHeaders);
        file.insert(file.end(), buf_.begin(), buf_.end());
        padTo(sizeOfHeaders + sectionRawSize);

        std::ofstream out(path, std::ios::binary);
        if (!out) return false;
        out.write((const char*)file.data(), (std::streamsize)file.size());
        return true;
    }

private:
    static constexpr uint64_t IMAGE_BASE  = 0x140000000ULL;
    static constexpr uint32_t SECTION_RVA = 0x1000;
    static constexpr uint32_t FILE_ALIGN  = 0x200;
    static constexpr uint32_t SECT_ALIGN  = 0x1000;

    std::vector<uint8_t> buf_;
    struct ImportFn { std::string dll, func; };
    std::vector<ImportFn> imports_;
    std::unordered_map<int, size_t> iatSlot_;
    uint32_t importDirRVA_ = 0, importDirSize_ = 0;
    size_t entryOffset_ = 0, codeBase_ = 0;

    void put16(uint16_t v) { buf_.push_back(v&0xFF); buf_.push_back((v>>8)&0xFF); }
    void put32(uint32_t v) { for (int i=0;i<4;i++) buf_.push_back((v>>(8*i))&0xFF); }
    void putStr(const std::string& s) { buf_.insert(buf_.end(), s.begin(), s.end()); buf_.push_back(0); }
    void put32At(size_t off, uint32_t v) { for (int i=0;i<4;i++) buf_[off+i] = (uint8_t)((v>>(8*i))&0xFF); }
    void put64At(size_t off, uint64_t v) { for (int i=0;i<8;i++) buf_[off+i] = (uint8_t)((v>>(8*i))&0xFF); }
    static size_t alignUp(size_t v, size_t a) { return (v + a - 1) / a * a; }
};