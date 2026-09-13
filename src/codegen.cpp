// ============================================================================
//  src/codegen.cpp -- Wandaa code generation, straight to a Windows PE64 image
// ============================================================================
//
//  Replaces the old "emit GNU-assembler text, shell out to gcc" backend. The
//  compiler now writes the .exe itself, so end users need nothing but
//  wandaac.exe -- no MinGW, no assembler, no linker.
//
//  Layout of the single RWX PE section (offsets are section-relative, which is
//  the coordinate system PEWriter, X64Asm and the blob fixups all share):
//
//      +------------------+ 0
//      | import block     |   descriptors, IAT slots, name tables  (PEWriter)
//      +------------------+ blobBase  = pe.beginCode()
//      | runtime blob     |   fixed runtime: print/string/file/crash/_start
//      +------------------+ codeBase  = blobBase + RUNTIME_BLOB_SIZE
//      | generated code   |   user functions, then wandaa_main
//      +------------------+ dataBase  (8-byte aligned)
//      | string literals  |   8-byte length header, bytes, NUL -- same layout
//      +------------------+   as the old str_N_hdr / str_N pair
//
//  Three kinds of cross-region reference, all resolved before anything is
//  handed to PEWriter:
//
//    generated code -> runtime     defineAbsLabel(blobBase + label offset),
//                                  then an ordinary call rel32
//    generated code -> IAT         defineAbsLabel(pe.iatSlotOffset(id)),
//                                  then call qword ptr [rip+slot]
//    runtime -> generated code     the one `call wandaa_main` in _start,
//                                  patched from runtime_blob.hpp's CODE_FIXUPS
//
//  Everything above the emission layer is unchanged from the assembler-text
//  backend: the same FnInfo frame layout, the same evalType/inferPass type
//  inference, the same string and array representations, the same Win64
//  calling convention, the same builtin name mapping.
// ============================================================================

#include "../include/codegen.hpp"
#include "../include/pe_writer.hpp"
#include "../include/x64asm.hpp"
#include "../include/runtime_blob.hpp"

#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using R = X64Asm::Reg;

enum class VType { Int, Str, Arr };

std::unordered_map<std::string,VType> g_fnReturnTypes;
std::unordered_map<std::string,VType> g_arrElemTypes;

struct FnInfo {
  std::unordered_map<std::string,int> offset;
  std::unordered_map<std::string,VType> types;
  int frameSize = 0;
};

// ===========================================================================
//  Frame layout and type inference -- carried over verbatim from the previous
//  backend. This logic never touched code emission and is unchanged, so the
//  two backends agree on every variable slot and every inferred type.
// ===========================================================================

void collectNames(const NodePtr& n, std::vector<std::string>& out){
  if(!n) return;
  if(n->type==NT::VarDecl) out.push_back(n->sval);
  if(n->type==NT::FuncDecl) return;
  for(auto& k : n->kids) collectNames(k, out);
}

FnInfo buildFnInfo(const std::vector<std::string>& params, const NodePtr& body){
  FnInfo fi;
  int off = 8;
  for(auto& p : params){ fi.offset[p] = off; off += 8; }
  std::vector<std::string> locals;
  collectNames(body, locals);
  for(auto& l : locals) if(!fi.offset.count(l)){ fi.offset[l] = off; off += 8; }
  int size = off - 8;
  size = (size + 15) & ~15;
  fi.frameSize = size;
  return fi;
}

VType evalType(const NodePtr& n, const std::unordered_map<std::string,VType>& types){
  switch(n->type){
    case NT::Str: return VType::Str;
    case NT::Num: case NT::Bool: return VType::Int;
    case NT::ArrayLit: return VType::Arr;
    case NT::Index: {
      if(n->kids[0]->type==NT::Var){
        auto it = g_arrElemTypes.find(n->kids[0]->sval);
        if(it != g_arrElemTypes.end()) return it->second;
      }
      return VType::Int;
    }
    case NT::IndexAssign: return evalType(n->kids[2], types);
    case NT::Var: {
      auto it = types.find(n->sval);
      return it != types.end() ? it->second : VType::Int;
    }
    case NT::Un: return VType::Int;
    case NT::Bin:
      if(n->sval=="+" && evalType(n->kids[0],types)==VType::Str && evalType(n->kids[1],types)==VType::Str)
        return VType::Str;
      return VType::Int;
    case NT::Assign: return evalType(n->kids[0], types);
    case NT::Call: {
      auto it = g_fnReturnTypes.find(n->sval);
      return it != g_fnReturnTypes.end() ? it->second : VType::Int;
    }
    default: return VType::Int;
  }
}

void collectTypesRec(const NodePtr& n, std::unordered_map<std::string,VType>& types){
  if(!n) return;
  if(n->type==NT::FuncDecl) return;
  if(n->type==NT::VarDecl){
    types[n->sval] = evalType(n->kids[0], types);
    if(n->kids[0]->type==NT::ArrayLit){
      VType elemT = n->kids[0]->kids.empty() ? VType::Int : evalType(n->kids[0]->kids[0], types);
      g_arrElemTypes[n->sval] = elemT;
    }
  }
  for(auto& k : n->kids) collectTypesRec(k, types);
}

void findReturnType(const NodePtr& n, const std::unordered_map<std::string,VType>& types, bool& found, VType& result){
  if(!n || found) return;
  if(n->type==NT::Return){
    if(!n->kids.empty()){ result = evalType(n->kids[0], types); found = true; }
    return;
  }
  if(n->type==NT::FuncDecl) return;
  for(auto& k : n->kids) findReturnType(k, types, found, result);
}

void inferPass(const NodePtr& n, std::unordered_map<std::string,VType>& types,
               std::unordered_map<std::string,std::vector<VType>>& fnParamTypes){
  if(!n) return;
  if(n->type==NT::FuncDecl) return;
  if(n->type==NT::VarDecl){
    inferPass(n->kids[0], types, fnParamTypes);
    types[n->sval] = evalType(n->kids[0], types);
    return;
  }
  if(n->type==NT::Call){
    auto& vec = fnParamTypes[n->sval];
    if(vec.size() < n->kids.size()) vec.resize(n->kids.size(), VType::Int);
    for(size_t i=0;i<n->kids.size();++i){
      inferPass(n->kids[i], types, fnParamTypes);
      if(evalType(n->kids[i], types) == VType::Str) vec[i] = VType::Str;
    }
    return;
  }
  if(n->type==NT::Assign){ inferPass(n->kids[0], types, fnParamTypes); return; }
  for(auto& k : n->kids) inferPass(k, types, fnParamTypes);
}

// ===========================================================================
//  Emission
// ===========================================================================

// Win64: first four integer arguments in RCX, RDX, R8, R9.
const R ARG_REGS[4] = { X64Asm::RCX, X64Asm::RDX, X64Asm::R8, X64Asm::R9 };

// The label the runtime blob's _start calls into. Distinct from "main" so a
// Wandaa program may still define `umurimo main()` without colliding.
const char* ENTRY_LABEL = "wandaa_main";

struct Codegen {
  PEWriter pe;
  X64Asm   a;

  std::vector<std::string> strings;
  FnInfo* cur = nullptr;
  int labelCounter = 0;
  std::string epilogue;
  std::unordered_map<std::string,std::vector<VType>> fnParamTypes;

  size_t blobBase = 0, codeBase = 0;
  std::map<std::string,int> importId;     // "dll!func" -> PEWriter import id
  std::vector<std::string> definedFns;    // for a better error than "undefined label"

  std::string newLabel(const std::string& p){ return p + "_" + std::to_string(labelCounter++); }

  int curOffset(const std::string& name){
    if(!cur || !cur->offset.count(name)) throw std::runtime_error("ikigereranyo kitazwi: " + name);
    return cur->offset.at(name);
  }

  VType inferType(const NodePtr& n){ return evalType(n, cur->types); }

  std::string addStringLiteral(const std::string& v){
    std::string lbl = "str_" + std::to_string(strings.size());
    strings.push_back(v);
    return lbl;
  }

  // ---- stack discipline --------------------------------------------------
  //
  // Win64 requires RSP to be 16-byte aligned at every CALL. After the prologue
  // RSP is aligned (push rbp makes it so, and frameSize is rounded to 16), but
  // expression evaluation pushes temporaries, so mid-expression it can sit at
  // an odd multiple of 8. The old backend ignored this: `f(a, g(b))` called g
  // with RSP % 16 == 8. Our own runtime never noticed -- it uses no SSE -- but
  // any DLL that does (and FFI makes those reachable) would fault on a movaps.
  //
  // stackSlots counts the 8-byte temporaries currently below the frame, so
  // every call site can insert 8 bytes of padding when the parity is wrong.
  int stackSlots = 0;

  void pushTmp(R r){ a.push(r); ++stackSlots; }
  void popTmp(R r) { a.pop(r);  --stackSlots; }

  int callPad() const { return (stackSlots % 2) ? 8 : 0; }

  // Call with arguments already in registers. Reserves the 32-byte shadow
  // space Win64 requires (caller-cleaned), plus alignment padding.
  void callRuntime(const std::string& rtLabel){
    const int pad = callPad();
    a.sub_imm(X64Asm::RSP, 32 + pad);
    a.call_label(rtLabel);
    a.add_imm(X64Asm::RSP, 32 + pad);
  }
  void callImport(const std::string& func){
    const int pad = callPad();
    a.sub_imm(X64Asm::RSP, 32 + pad);
    a.call_mem_rip("__imp_" + func);
    a.add_imm(X64Asm::RSP, 32 + pad);
  }

  // Evaluate `args` and call `target`, supporting any number of arguments.
  //
  // Win64 gives argument i its home at [rsp + 8*i] in the outgoing area -- the
  // 32-byte shadow space IS the home for the first four. So the whole thing is
  // one reservation: store every argument at [rsp + 8*i] as it is evaluated,
  // then load the first four into RCX/RDX/R8/R9. That preserves left-to-right
  // evaluation order (which matters when arguments have side effects), needs no
  // shuffling for arguments five and up, and keeps RSP aligned throughout
  // because a single aligned amount is reserved before any of it runs.
  void emitCall(const std::string& target, const std::vector<NodePtr>& args, bool isImport){
    const int n = (int)args.size();
    const int homeSlots = (n > 4 ? n : 4);            // shadow space is always 4
    const int pad = ((homeSlots + stackSlots) % 2) ? 8 : 0;
    const int reserve = homeSlots * 8 + pad;

    a.sub_imm(X64Asm::RSP, reserve);

    // RSP is 16-byte aligned again inside this region, so nested calls in the
    // argument expressions start from a clean parity.
    const int savedSlots = stackSlots;
    stackSlots = 0;

    for(int i = 0; i < n; ++i){
      genExpr(args[i]);
      a.mov_store_base(X64Asm::RSP, i * 8, X64Asm::RAX);
    }
    for(int i = 0; i < n && i < 4; ++i)
      a.mov_load_base(ARG_REGS[i], X64Asm::RSP, i * 8);

    if(isImport) a.call_mem_rip("__imp_" + target);
    else         a.call_label(target);

    stackSlots = savedSlots;
    a.add_imm(X64Asm::RSP, reserve);
  }

  // ---- expressions -------------------------------------------------------

  void genArrayLit(const NodePtr& n){
    const size_t count = n->kids.size();
    // Heap-allocated, with an 8-byte element count ahead of the data, and the
    // value of the expression pointing just past that header.
    callImport("GetProcessHeap");
    a.mov_reg(X64Asm::RCX, X64Asm::RAX);
    a.xorr(X64Asm::RDX, X64Asm::RDX);
    a.mov_imm(X64Asm::R8, (int64_t)(8 + count*8));
    callImport("HeapAlloc");
    a.mov_reg(X64Asm::R12, X64Asm::RAX);
    a.mov_store_imm_base(X64Asm::R12, 0, (int32_t)count);
    for(size_t i=0;i<count;++i){
      genExpr(n->kids[i]);
      // The old backend always emitted a disp8 here, which silently truncated
      // past 15 elements. mov_store_base picks disp8/disp32 correctly.
      a.mov_store_base(X64Asm::R12, (int32_t)(8 + i*8), X64Asm::RAX);
    }
    a.lea_base(X64Asm::RAX, X64Asm::R12, 8);
  }

  void genIndex(const NodePtr& n){
    genExpr(n->kids[0]);
    pushTmp(X64Asm::RAX);
    genExpr(n->kids[1]);
    a.mov_reg(X64Asm::RBX, X64Asm::RAX);
    popTmp(X64Asm::RAX);
    a.mov_load_sib(X64Asm::RAX, X64Asm::RAX, X64Asm::RBX);
  }

  void genIndexAssign(const NodePtr& n){
    genExpr(n->kids[0]);
    pushTmp(X64Asm::RAX);
    genExpr(n->kids[1]);
    a.mov_reg(X64Asm::RBX, X64Asm::RAX);
    popTmp(X64Asm::RAX);
    a.lea_sib(X64Asm::RAX, X64Asm::RAX, X64Asm::RBX);
    pushTmp(X64Asm::RAX);
    genExpr(n->kids[2]);
    popTmp(X64Asm::RCX);
    a.mov_store_base(X64Asm::RCX, 0, X64Asm::RAX);
  }

  void genExpr(const NodePtr& n){
    switch(n->type){
      case NT::Num:  a.mov_imm(X64Asm::RAX, (int64_t)n->nval); break;
      case NT::Bool: a.mov_imm(X64Asm::RAX, n->bval ? 1 : 0);  break;
      case NT::Str:  a.lea_rip(X64Asm::RAX, addStringLiteral(n->sval)); break;
      case NT::ArrayLit:    genArrayLit(n); break;
      case NT::Index:       genIndex(n); break;
      case NT::IndexAssign: genIndexAssign(n); break;
      case NT::Var:  a.mov_load_rbp(X64Asm::RAX, -curOffset(n->sval)); break;
      case NT::Un:   genExpr(n->kids[0]); a.neg(X64Asm::RAX); break;
      case NT::Assign:
        genExpr(n->kids[0]);
        a.mov_store_rbp(-curOffset(n->sval), X64Asm::RAX);
        break;
      case NT::Bin: {
        const bool bothStr = inferType(n->kids[0])==VType::Str && inferType(n->kids[1])==VType::Str;

        if(n->sval=="+" && bothStr){
          genExpr(n->kids[0]);
          pushTmp(X64Asm::RAX);
          genExpr(n->kids[1]);
          a.mov_reg(X64Asm::RDX, X64Asm::RAX);
          popTmp(X64Asm::RCX);
          callRuntime("wandaa_str_concat");
          break;
        }
        if((n->sval=="==" || n->sval=="!=") && bothStr){
          genExpr(n->kids[0]);
          pushTmp(X64Asm::RAX);
          genExpr(n->kids[1]);
          a.mov_reg(X64Asm::RDX, X64Asm::RAX);
          popTmp(X64Asm::RCX);
          callRuntime("wandaa_str_eq");
          if(n->sval=="!=") a.xor_imm(X64Asm::RAX, 1);
          break;
        }

        genExpr(n->kids[0]);
        pushTmp(X64Asm::RAX);
        genExpr(n->kids[1]);
        a.mov_reg(X64Asm::RBX, X64Asm::RAX);
        popTmp(X64Asm::RAX);

        const std::string& op = n->sval;
        if      (op=="+") a.add(X64Asm::RAX, X64Asm::RBX);
        else if (op=="-") a.sub(X64Asm::RAX, X64Asm::RBX);
        else if (op=="*") a.imul(X64Asm::RAX, X64Asm::RBX);
        else if (op=="/") { a.cqo(); a.idiv(X64Asm::RBX); }
        else {
          a.cmp(X64Asm::RAX, X64Asm::RBX);
          const std::string cc = op=="<"  ? "l"  : op==">"  ? "g"  :
                                 op=="<=" ? "le" : op==">=" ? "ge" :
                                 op=="==" ? "e"  : "ne";
          a.setcc(cc);
          a.movzx_rax_al();
        }
        break;
      }
      case NT::Call: genCall(n); break;
      default: throw std::runtime_error("iyi expression ntiyemewe muri codegen");
    }
  }

  void genCall(const NodePtr& n){
    static const std::unordered_map<std::string,std::string> builtins = {
      {"uburebure", "wandaa_str_len"},
      {"ubunini",   "wandaa_str_len"},
      {"soma",      "wandaa_read_file"},
      {"andikamo",  "wandaa_write_file"}
    };
    std::string target = n->sval;
    auto bit = builtins.find(target);
    if(bit != builtins.end()) target = bit->second;

    emitCall(target, n->kids, /*isImport=*/false);
  }

  void genPrint(const NodePtr& n){
    for(auto& arg : n->kids){
      const bool isStr = inferType(arg) == VType::Str;
      genExpr(arg);
      a.mov_reg(X64Asm::RCX, X64Asm::RAX);
      callRuntime(isStr ? "wandaa_print_strval" : "wandaa_print_int");
    }
  }

  void genStmt(const NodePtr& n){
    // Line tracking for the crash handler. wandaa_current_line is an 8-byte
    // slot inside the runtime blob's data region, reached RIP-relatively --
    // this is where the old .bss .lcomm lived.
    if(n->line > 0) a.mov_store_imm_rip("wandaa_current_line", n->line);

    switch(n->type){
      case NT::VarDecl:
        genExpr(n->kids[0]);
        a.mov_store_rbp(-curOffset(n->sval), X64Asm::RAX);
        break;
      case NT::ExprStmt: genExpr(n->kids[0]); break;
      case NT::Print:    genPrint(n); break;
      case NT::If: {
        const std::string Lelse = newLabel("Lelse"), Lend = newLabel("Lend");
        genExpr(n->kids[0]);
        a.test(X64Asm::RAX, X64Asm::RAX);
        a.jz(Lelse);
        genStmt(n->kids[1]);
        a.jmp(Lend);
        a.defineLabel(Lelse);
        if(n->kids.size() > 2) genStmt(n->kids[2]);
        a.defineLabel(Lend);
        break;
      }
      case NT::While: {
        const std::string Lstart = newLabel("Lstart"), Lend = newLabel("Lend");
        a.defineLabel(Lstart);
        genExpr(n->kids[0]);
        a.test(X64Asm::RAX, X64Asm::RAX);
        a.jz(Lend);
        genStmt(n->kids[1]);
        a.jmp(Lstart);
        a.defineLabel(Lend);
        break;
      }
      case NT::Block: for(auto& k : n->kids) genStmt(k); break;
      case NT::Return:
        if(!n->kids.empty()) genExpr(n->kids[0]);
        else a.xor_eax_eax();
        a.jmp(epilogue);
        break;
      case NT::FuncDecl: break;
      default: genExpr(n);
    }
  }

  void genFunction(const std::string& name, const std::vector<std::string>& params, const NodePtr& body){
    FnInfo fi = buildFnInfo(params, body);
    auto pit = fnParamTypes.find(name);
    for(size_t i=0;i<params.size();++i){
      VType pt = (pit!=fnParamTypes.end() && i<pit->second.size()) ? pit->second[i] : VType::Int;
      fi.types[params[i]] = pt;
    }
    collectTypesRec(body, fi.types);
    cur = &fi;
    epilogue = name + "_epilogue";

    a.defineLabel(name);
    definedFns.push_back(name);

    // push rbp is part of the frame, not a temporary: RSP is 16-byte aligned
    // again once the (16-rounded) frame is subtracted, so reset the counter.
    a.push(X64Asm::RBP);
    a.mov_reg(X64Asm::RBP, X64Asm::RSP);
    stackSlots = 0;
    if(fi.frameSize > 0) a.sub_imm(X64Asm::RSP, fi.frameSize);

    // Spill incoming arguments into their frame slots.
    //
    // Arguments 0-3 arrive in registers. Arguments 5 and up were written by the
    // caller into the outgoing area; after `push rbp; mov rbp, rsp` argument i's
    // home sits at [rbp + 16 + 8*i] (8 for the return address, 8 for the saved
    // rbp), which is where emitCall placed it as [rsp + 8*i].
    for(size_t i=0;i<params.size();++i){
      const int slot = -fi.offset.at(params[i]);
      if(i < 4){
        a.mov_store_rbp(slot, ARG_REGS[i]);
      } else {
        a.mov_load_rbp(X64Asm::RAX, (int32_t)(16 + 8*i));
        a.mov_store_rbp(slot, X64Asm::RAX);
      }
    }

    genStmt(body);

    a.xor_eax_eax();
    a.defineLabel(epilogue);
    if(fi.frameSize > 0) a.add_imm(X64Asm::RSP, fi.frameSize);
    a.pop(X64Asm::RBP);
    a.ret();
  }

  // ---- data area ---------------------------------------------------------
  // One entry per string literal, laid out exactly as the old .data block:
  //
  //     .align 8
  //     str_N_hdr: .quad <length>
  //     str_N:     .ascii "..."
  //                .byte 0
  //
  // so wandaa_str_len's `mov rax, [rcx-8]` finds the length where it expects.
  std::vector<uint8_t> buildDataArea(size_t dataBase){
    std::vector<uint8_t> data;
    for(size_t i=0;i<strings.size();++i){
      while((dataBase + data.size()) % 8 != 0) data.push_back(0);
      const std::string& s = strings[i];
      const uint64_t len = s.size();
      for(int b=0;b<8;++b) data.push_back((uint8_t)((len >> (8*b)) & 0xFF));
      a.defineAbsLabel("str_" + std::to_string(i), dataBase + data.size());
      data.insert(data.end(), s.begin(), s.end());
      data.push_back(0);
    }
    return data;
  }

  // ---- driver ------------------------------------------------------------

  std::vector<uint8_t> generate(const NodePtr& program){
    g_fnReturnTypes.clear();
    g_arrElemTypes.clear();
    g_fnReturnTypes["soma"] = VType::Str;

    std::vector<NodePtr> funcs, rest;
    for(auto& k : program->kids){
      if(k->type==NT::FuncDecl) funcs.push_back(k);
      else rest.push_back(k);
    }
    auto restBlock = mk(NT::Block);
    restBlock->kids = rest;

    // Type inference to a fixed point, unchanged from the old backend.
    for(int iter=0; iter<4; ++iter){
      {
        std::unordered_map<std::string,VType> types;
        inferPass(restBlock, types, fnParamTypes);
      }
      for(auto& f : funcs){
        std::unordered_map<std::string,VType> types;
        auto pit = fnParamTypes.find(f->sval);
        for(size_t i=0;i<f->params.size();++i){
          VType pt = (pit!=fnParamTypes.end() && i<pit->second.size()) ? pit->second[i] : VType::Int;
          types[f->params[i]] = pt;
        }
        inferPass(f->kids[0], types, fnParamTypes);
        bool found=false; VType rt=VType::Int;
        findReturnType(f->kids[0], types, found, rt);
        if(rt==VType::Str) g_fnReturnTypes[f->sval] = VType::Str;
        else if(!g_fnReturnTypes.count(f->sval)) g_fnReturnTypes[f->sval] = rt;
      }
    }

    // --- 1. imports -------------------------------------------------------
    // Every function the runtime blob calls, plus the two the generated code
    // calls directly for array literals. Registering from the blob's own fixup
    // table means a new API call in runtime.s needs no change here.
    for(size_t i=0;i<wandaa_rt::IMPORT_FIXUP_COUNT;++i){
      const auto& fx = wandaa_rt::IMPORT_FIXUPS[i];
      const std::string key = std::string(fx.dll) + "!" + fx.func;
      if(!importId.count(key)) importId[key] = pe.addImport(fx.dll, fx.func);
    }
    for(const char* fn : {"GetProcessHeap", "HeapAlloc"}){
      const std::string key = std::string("kernel32.dll!") + fn;
      if(!importId.count(key)) importId[key] = pe.addImport("kernel32.dll", fn);
    }

    // --- 2. section layout ------------------------------------------------
    // The import block leaves the cursor at an arbitrary offset. Pad the blob
    // up to 16 bytes so the .p2align directives inside runtime.s actually mean
    // something once the blob is relocated -- otherwise wandaa_hStdOut and
    // friends land on whatever alignment the import name table happened to end
    // on. Nothing here faults when misaligned, but honouring the alignment the
    // source asked for keeps the blob's layout exactly as assembled.
    const size_t rawBase = pe.beginCode();
    const size_t blobPad = (16 - (rawBase % 16)) % 16;
    blobBase = rawBase + blobPad;
    codeBase = blobBase + wandaa_rt::RUNTIME_BLOB_SIZE;
    a.setBaseOffset(codeBase);

    // Runtime entry points and data slots become ordinary labels.
    for(size_t i=0;i<wandaa_rt::LABEL_COUNT;++i)
      a.defineAbsLabel(wandaa_rt::LABELS[i].name, blobBase + wandaa_rt::LABELS[i].off);

    // IAT slots likewise, so generated code can `call [rip+__imp_X]`.
    for(const auto& kv : importId){
      const std::string func = kv.first.substr(kv.first.find('!') + 1);
      a.defineAbsLabel("__imp_" + func, pe.iatSlotOffset(kv.second));
    }

    // --- 3. generated code ------------------------------------------------
    for(auto& f : funcs) genFunction(f->sval, f->params, f->kids[0]);
    const size_t mainOffset = codeBase + a.code.size();
    genFunction(ENTRY_LABEL, {}, restBlock);

    // --- 4. string data, then resolve every fixup -------------------------
    size_t dataBase = codeBase + a.code.size();
    dataBase = (dataBase + 7) & ~(size_t)7;
    const size_t codePad = dataBase - (codeBase + a.code.size());
    std::vector<uint8_t> data = buildDataArea(dataBase);

    try {
      a.resolveFixups();
    } catch(const std::exception& e){
      // Turn "undefined label: foo" into something a Wandaa programmer can act on.
      const std::string what = e.what();
      const std::string pfx = "undefined label: ";
      if(what.rfind(pfx, 0) == 0)
        throw std::runtime_error("umurimo utazwi: " + what.substr(pfx.size()));
      throw;
    }

    // --- 5. patch the runtime blob ---------------------------------------
    std::vector<uint8_t> blob(wandaa_rt::RUNTIME_BLOB,
                              wandaa_rt::RUNTIME_BLOB + wandaa_rt::RUNTIME_BLOB_SIZE);

    auto patch32 = [&](std::vector<uint8_t>& buf, size_t off, int32_t v){
      for(int b=0;b<4;++b) buf[off+b] = (uint8_t)(((uint32_t)v >> (8*b)) & 0xFF);
    };

    // Each `call [rip+__imp_X]` in the blob was assembled against a placeholder.
    // Repoint it at PEWriter's IAT slot. The displacement is measured from the
    // end of the instruction, which is the 4-byte field itself.
    for(size_t i=0;i<wandaa_rt::IMPORT_FIXUP_COUNT;++i){
      const auto& fx = wandaa_rt::IMPORT_FIXUPS[i];
      const std::string key = std::string(fx.dll) + "!" + fx.func;
      const size_t slot = pe.iatSlotOffset(importId.at(key));
      const int64_t site = (int64_t)(blobBase + fx.site);
      patch32(blob, fx.site, (int32_t)((int64_t)slot + fx.addend - (site + 4)));
    }

    // The single blob -> generated-code edge: _start's `call wandaa_main`.
    for(size_t i=0;i<wandaa_rt::CODE_FIXUP_COUNT;++i){
      const auto& fx = wandaa_rt::CODE_FIXUPS[i];
      if(std::string(fx.symbol) != ENTRY_LABEL)
        throw std::runtime_error("runtime blob references an unknown symbol: " +
                                 std::string(fx.symbol));
      const int64_t site = (int64_t)(blobBase + fx.site);
      patch32(blob, fx.site, (int32_t)((int64_t)mainOffset + fx.addend - (site + 4)));
    }

    // --- 6. assemble the image -------------------------------------------
    if(blobPad) pe.emit(std::vector<uint8_t>(blobPad, 0));
    pe.emit(blob);
    pe.emit(a.code);
    if(codePad) pe.emit(std::vector<uint8_t>(codePad, 0));
    pe.emit(data);
    pe.setEntryOffset(blobBase + wandaa_rt::labelOffset("_start"));

    return pe.buildImage();
  }
};

} // namespace

std::vector<uint8_t> generateExeBytes(const NodePtr& program){
  Codegen cg;
  return cg.generate(program);
}

void generateExe(const NodePtr& program, const std::string& outPath){
  Codegen cg;
  const std::vector<uint8_t> image = cg.generate(program);
  std::ofstream out(outPath, std::ios::binary);
  if(!out) throw std::runtime_error("ntidushoboye kwandika: " + outPath);
  out.write((const char*)image.data(), (std::streamsize)image.size());
  if(!out.good()) throw std::runtime_error("ikosa mu kwandika: " + outPath);
}
