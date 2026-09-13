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

#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using R = X64Asm::Reg;

enum class VType { Int, Str, Arr, Float, Record };

// An f64 value travels in a general-purpose register as its raw IEEE-754 bit
// pattern, and only moves into an XMM register for the arithmetic itself. That
// keeps every existing mechanism -- 8-byte frame slots, push/pop temporaries,
// the [rsp+8*i] outgoing-argument area -- working unchanged, at the cost of a
// movq pair around each operation.
inline bool isNum(VType t){ return t == VType::Int || t == VType::Float; }

std::unordered_map<std::string,VType> g_fnReturnTypes;
std::unordered_map<std::string,VType> g_arrElemTypes;

struct RecordTypeInfo {
  std::vector<std::string> fieldNames;
  std::unordered_map<std::string,int> fieldOffset;
  // Declared byte width per field: 1, 2, 4 or 8. Defaults to 8 when the
  // declaration gives no `:N` annotation, so records written before widths
  // existed keep their old layout exactly.
  std::unordered_map<std::string,int> fieldWidth;
  std::unordered_map<std::string,VType> fieldType;
  int totalSize = 0;
};
std::unordered_map<std::string, RecordTypeInfo> g_recordTypes;
std::unordered_map<std::string, std::string> g_varRecordType;

std::string recordTypeNameOf(const NodePtr& n){
  if(!n) return "";
  if(n->type == NT::RecordLit) return n->sval;
  if(n->type == NT::Var){
    auto it = g_varRecordType.find(n->sval);
    return it != g_varRecordType.end() ? it->second : "";
  }
  return "";
}

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
    case NT::Num:  return n->isFloat ? VType::Float : VType::Int;
    case NT::Bool: return VType::Int;
    case NT::ArrayLit: return VType::Arr;
    case NT::Index: {
      if(n->kids[0]->type==NT::Var){
        auto it = g_arrElemTypes.find(n->kids[0]->sval);
        if(it != g_arrElemTypes.end()) return it->second;
      }
      return VType::Int;
    }
    case NT::IndexAssign: return evalType(n->kids[2], types);
    case NT::RecordLit: return VType::Record;
    case NT::FieldAccess: {
      const std::string tname = recordTypeNameOf(n->kids[0]);
      auto rit = g_recordTypes.find(tname);
      if(rit != g_recordTypes.end()){
        auto fit = rit->second.fieldType.find(n->sval);
        if(fit != rit->second.fieldType.end()) return fit->second;
      }
      return VType::Int;
    }
    case NT::FieldAssign: return evalType(n->kids[1], types);
    case NT::Var: {
      auto it = types.find(n->sval);
      return it != types.end() ? it->second : VType::Int;
    }
    case NT::Un:
      // `si`/`!` yields 0 or 1; unary minus keeps its operand's type.
      if(n->sval=="!") return VType::Int;
      return evalType(n->kids[0], types);
    case NT::Bin: {
      if(n->sval=="&&" || n->sval=="||") return VType::Int;
      // Comparisons always yield 0 or 1, whatever they compared.
      if(n->sval=="<" || n->sval==">" || n->sval=="<=" || n->sval==">=" ||
         n->sval=="==" || n->sval=="!=") return VType::Int;
      const VType lt = evalType(n->kids[0], types);
      const VType rt = evalType(n->kids[1], types);
      if(n->sval=="+" && lt==VType::Str && rt==VType::Str) return VType::Str;
      // Arithmetic promotes to f64 when either operand already is one. An
      // all-integer expression stays integer, so `7 / 2` is still 3 and every
      // existing program keeps its results (GOVERNANCE principle 5).
      if(lt==VType::Float || rt==VType::Float) return VType::Float;
      return VType::Int;
    }
    case NT::Assign: return evalType(n->kids[0], types);
    case NT::Call: {
      auto it = g_fnReturnTypes.find(n->sval);
      if(it != g_fnReturnTypes.end()) return it->second;
      static const std::unordered_map<std::string,VType> builtinReturnTypes = {
        {"ijambo",    VType::Str},
        {"igice",     VType::Str},
        {"mu_ijambo", VType::Str},
        {"soma",      VType::Str},
        {"uburebure", VType::Int},
        {"ubunini",   VType::Int},
        {"inyuguti",  VType::Int},
        {"mu_mubare", VType::Int},
        {"urutonde",  VType::Arr},
      };
      auto bit = builtinReturnTypes.find(n->sval);
      return bit != builtinReturnTypes.end() ? bit->second : VType::Int;
    }
    default: return VType::Int;
  }
}

// Record the element type of an array that is written to by index, e.g.
// `a[0] = 0.5`. Without this only array LITERALS carry an element type, so an
// array from `urutonde(n)` reads back as Int and a float in it prints as its
// raw bit pattern.
//
// Non-Int types are sticky, matching how parameter types are inferred: reading
// an element out of the wrong register file or printing it with the wrong
// routine is worse than widening a mixed array to its non-Int element type.
void noteIndexedWrite(const NodePtr& n, const std::unordered_map<std::string,VType>& types){
  if(n->type != NT::IndexAssign || n->kids[0]->type != NT::Var) return;
  const VType et = evalType(n->kids[2], types);
  if(et != VType::Int) g_arrElemTypes[n->kids[0]->sval] = et;
}

std::unordered_map<std::string,VType> g_fnArrElemTypes;

// Look for `tanga <var>;` inside a function body, where <var> is an array
// whose element type g_arrElemTypes now knows (from the walk just done).
// Without this, a function that returns an array -- e.g. `tanga ibisubizo;`
// -- loses that array's element type the moment it crosses the return
// boundary, so `reka imirongo = gabanya(...);` at the call site reads
// back as plain Int no matter how correct gabanya's own body is.
void findArrayReturnType(const NodePtr& n, const std::string& fnName){
  if(!n) return;
  if(n->type==NT::Return && !n->kids.empty()){
    if(n->kids[0]->type==NT::Var){
      auto it = g_arrElemTypes.find(n->kids[0]->sval);
      if(it != g_arrElemTypes.end()) g_fnArrElemTypes[fnName] = it->second;
    } else if(n->kids[0]->type==NT::Call){
      // `tanga andereBaTypeOf(...);` -- returning another function's call
      // directly, with no intermediate variable. That function's own array
      // element type (if known already -- which requires it to appear
      // earlier in the file, since collectTypesRec walks top-level
      // declarations in order) propagates straight through.
      auto it = g_fnArrElemTypes.find(n->kids[0]->sval);
      if(it != g_fnArrElemTypes.end()) g_fnArrElemTypes[fnName] = it->second;
    }
  }
  for(auto& k : n->kids) findArrayReturnType(k, fnName);
}

void collectTypesRec(const NodePtr& n, std::unordered_map<std::string,VType>& types){
  if(!n) return;
  if(n->type==NT::FuncDecl){
    // Function bodies get their own scope for local variable types, so a
    // fresh map keeps them from leaking into (or overwriting) the outer
    // scope. But array element types (g_arrElemTypes) are a *global* map,
    // so array writes inside a function body still have to be walked --
    // otherwise an urutonde() array assigned to by index inside a umurimo
    // never gets an element type and reads back as raw Int/pointer bits.
    std::unordered_map<std::string,VType> localTypes;
    for(auto& k : n->kids) collectTypesRec(k, localTypes);
    findArrayReturnType(n, n->sval);
    return;
  }
  if(n->type==NT::VarDecl){
    types[n->sval] = evalType(n->kids[0], types);
    if(n->kids[0]->type==NT::ArrayLit){
      VType elemT = n->kids[0]->kids.empty() ? VType::Int : evalType(n->kids[0]->kids[0], types);
      g_arrElemTypes[n->sval] = elemT;
    } else if(n->kids[0]->type==NT::Call){
      // Propagate a function's known array-element type onto the variable
      // receiving its return value, e.g. `reka imirongo = gabanya(...);`
      auto fit = g_fnArrElemTypes.find(n->kids[0]->sval);
      if(fit != g_fnArrElemTypes.end()) g_arrElemTypes[n->sval] = fit->second;
    }
  }
  noteIndexedWrite(n, types);
  for(auto& k : n->kids) collectTypesRec(k, types);
}

void scanRecordLits(const NodePtr& n, std::unordered_map<std::string,VType>& types){
  if(!n) return;
  if(n->type == NT::VarDecl && !n->kids.empty() && n->kids[0]->type==NT::RecordLit){
    g_varRecordType[n->sval] = n->kids[0]->sval;
  }
  if(n->type == NT::RecordLit){
    auto rit = g_recordTypes.find(n->sval);
    if(rit != g_recordTypes.end()){
      for(size_t i=0;i<n->kids.size() && i<rit->second.fieldNames.size();++i){
        VType ft = evalType(n->kids[i], types);
        const std::string& f = rit->second.fieldNames[i];
        VType& cur = rit->second.fieldType[f];
        if(ft==VType::Str) cur = VType::Str;
        else if(ft==VType::Float && cur!=VType::Str) cur = VType::Float;
      }
    }
  }
  for(auto& c : n->kids) scanRecordLits(c, types);
}

void registerRecordTypes(const NodePtr& program){
  for(auto& k : program->kids){
    if(k->type == NT::RecordDecl){
      // Fields are packed in declaration order with NO automatic alignment
      // padding: the declared widths are the layout, byte for byte. That is
      // the whole point -- `ubwoko SockAddrIn { family:2, port:2, addr:4,
      // zero:8 }` has to match Win32's sockaddr_in exactly, and silently
      // inserting padding to "helpfully" align a field would corrupt it.
      RecordTypeInfo info;
      int off = 0;
      for(size_t i=0;i<k->params.size();++i){
        const std::string& f = k->params[i];
        int w = (i < k->widths.size() && k->widths[i] > 0) ? k->widths[i] : 8;
        if(w != 1 && w != 2 && w != 4 && w != 8)
          throw std::runtime_error("ubugari bw'umwanya '" + f + "' muri '" + k->sval +
                                   "' bugomba kuba 1, 2, 4 cyangwa 8 (byabonetse " +
                                   std::to_string(w) + ")");
        info.fieldNames.push_back(f);
        info.fieldOffset[f] = off;
        info.fieldWidth[f]  = w;
        info.fieldType[f]   = VType::Int;
        off += w;
      }
      info.totalSize = off;
      g_recordTypes[k->sval] = info;
    }
  }
  std::unordered_map<std::string,VType> types;
  collectTypesRec(program, types);
  scanRecordLits(program, types);
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
      const VType at = evalType(n->kids[i], types);
      // A parameter is Str or Float if ANY call site passes one. Both are
      // sticky: the callee reads that parameter out of one register file, and
      // every caller has to agree, so the widest observed type wins.
      if(at == VType::Str)        vec[i] = VType::Str;
      else if(at == VType::Float && vec[i] != VType::Str) vec[i] = VType::Float;
    }
    return;
  }
  if(n->type==NT::Assign){ inferPass(n->kids[0], types, fnParamTypes); return; }
  noteIndexedWrite(n, types);
  for(auto& k : n->kids) inferPass(k, types, fnParamTypes);
}

// ===========================================================================
//  Emission
// ===========================================================================

// Win64: first four integer arguments in RCX, RDX, R8, R9.
const R ARG_REGS[4] = { X64Asm::RCX, X64Asm::RDX, X64Asm::R8, X64Asm::R9 };

// Win64 pairs the integer and SSE argument registers BY POSITION: a float in
// argument slot 2 goes in XMM2, not in "the next free XMM". The two files are
// never both used for the same slot.
const X64Asm::Xmm XMM_ARGS[4] = { X64Asm::XMM0, X64Asm::XMM1, X64Asm::XMM2, X64Asm::XMM3 };

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

  // Functions declared with `hanze "some.dll" Name(...)`. These are called
  // through the import address table rather than by rel32, which is what lets a
  // Wandaa program reach any C ABI entry point the system exposes.
  std::map<std::string,std::string> externs;   // function name -> DLL
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

  // Innermost enclosing loop, for `hagarika` (break) and `komeza` (continue).
  struct LoopLabels { std::string brk, cont; };
  std::vector<LoopLabels> loops;

  void pushTmp(R r){ a.push(r); ++stackSlots; }
  void popTmp(R r) { a.pop(r);  --stackSlots; }

  int callPad() const { return (stackSlots % 2) ? 8 : 0; }

  // ---- f64 helpers -------------------------------------------------------
  //
  // A float lives in RAX as its bit pattern between operations. These two move
  // between that representation and an actual integer value.

  // RAX holds a signed integer -> replace it with the same value as f64 bits.
  void raxIntToFloat(){
    a.cvtsi2sd(X64Asm::XMM0, X64Asm::RAX);
    a.movq_r64_xmm(X64Asm::RAX, X64Asm::XMM0);
  }
  // RAX holds f64 bits -> replace it with the integer value, truncated toward
  // zero (cvttsd2si, not cvtsd2si, so 2.9 becomes 2 rather than rounding).
  void raxFloatToInt(){
    a.movq_xmm_r64(X64Asm::XMM0, X64Asm::RAX);
    a.cvttsd2si(X64Asm::RAX, X64Asm::XMM0);
  }
  // Coerce whatever genExpr just left in RAX from `have` to `want`.
  void coerceRax(VType have, VType want){
    if(want == VType::Float && have != VType::Float) raxIntToFloat();
    else if(want != VType::Float && have == VType::Float) raxFloatToInt();
  }

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
  void emitCall(const std::string& target, const std::vector<NodePtr>& args, bool isImport,
                const std::vector<VType>* paramTypes = nullptr){
    const int n = (int)args.size();
    const int homeSlots = (n > 4 ? n : 4);            // shadow space is always 4
    const int pad = ((homeSlots + stackSlots) % 2) ? 8 : 0;
    const int reserve = homeSlots * 8 + pad;

    a.sub_imm(X64Asm::RSP, reserve);

    // RSP is 16-byte aligned again inside this region, so nested calls in the
    // argument expressions start from a clean parity.
    const int savedSlots = stackSlots;
    stackSlots = 0;

    // Which register file each argument travels in.
    //
    // The decision belongs to the PARAMETER's type, not the argument's: the
    // callee reads parameter i out of one specific file, and inferPass makes a
    // parameter Float if any call site passes a float. A second call site
    // passing an integer must therefore promote it and use XMM too, or the
    // callee would read a register the caller never wrote.
    std::vector<bool> useXmm((size_t)n, false);

    for(int i = 0; i < n; ++i){
      const VType have = inferType(args[i]);
      const VType want = (paramTypes && (size_t)i < paramTypes->size())
                         ? (*paramTypes)[i] : have;
      genExpr(args[i]);
      coerceRax(have, want);
      a.mov_store_base(X64Asm::RSP, i * 8, X64Asm::RAX);
      useXmm[(size_t)i] = (want == VType::Float);
    }

    // Win64 homes argument i at [rsp + 8*i] for BOTH register files, so the
    // stack layout above is already correct and only the load differs.
    for(int i = 0; i < n && i < 4; ++i){
      if(useXmm[(size_t)i]) a.movsd_load(XMM_ARGS[i], X64Asm::RSP, i * 8);
      else                  a.mov_load_base(ARG_REGS[i], X64Asm::RSP, i * 8);
    }

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

  // ---- width-aware field access ------------------------------------------
  //
  // Stores truncate from the low bits of RAX; loads zero-extend, so a narrow
  // field still reads back as a clean 64-bit Wandaa integer.
  void storeFieldRax(R base, int32_t disp, int width){
    switch(width){
      case 1: a.mov_store_base_byte (base, disp, X64Asm::RAX); break;
      case 2: a.mov_store_base_word (base, disp, X64Asm::RAX); break;
      case 4: a.mov_store_base_dword(base, disp, X64Asm::RAX); break;
      default: a.mov_store_base     (base, disp, X64Asm::RAX); break;
    }
  }
  void loadFieldRax(R base, int32_t disp, int width){
    switch(width){
      case 1: a.mov_load_base_byte_zx (X64Asm::RAX, base, disp); break;
      case 2: a.mov_load_base_word_zx (X64Asm::RAX, base, disp); break;
      case 4: a.mov_load_base_dword_zx(X64Asm::RAX, base, disp); break;
      default: a.mov_load_base        (X64Asm::RAX, base, disp); break;
    }
  }

  void genRecordLit(const NodePtr& n){
    auto& info = g_recordTypes[n->sval];
    const size_t count = info.fieldNames.size();
    callImport("GetProcessHeap");
    a.mov_reg(X64Asm::RCX, X64Asm::RAX);
    // HEAP_ZERO_MEMORY: a partially initialised record must not expose heap
    // garbage to a Win32 call that reads the whole struct (sockaddr_in's
    // sin_zero is the obvious case).
    a.mov_imm(X64Asm::RDX, 8);
    a.mov_imm(X64Asm::R8, (int64_t)(8 + info.totalSize));
    callImport("HeapAlloc");
    a.mov_reg(X64Asm::R12, X64Asm::RAX);
    a.mov_store_imm_base(X64Asm::R12, 0, 0);
    for(size_t i=0;i<n->kids.size() && i<count;++i){
      const std::string& f = info.fieldNames[i];
      const VType want = info.fieldType[f];
      const VType have = inferType(n->kids[i]);
      genExpr(n->kids[i]);
      coerceRax(have, want);
      storeFieldRax(X64Asm::R12, (int32_t)(8 + info.fieldOffset[f]), info.fieldWidth[f]);
    }
    a.lea_base(X64Asm::RAX, X64Asm::R12, 8);
  }

  void genFieldAccess(const NodePtr& n){
    genExpr(n->kids[0]);
    const std::string tname = recordTypeNameOf(n->kids[0]);
    int off = 0, width = 8;
    auto rit = g_recordTypes.find(tname);
    if(rit != g_recordTypes.end()){
      auto fit = rit->second.fieldOffset.find(n->sval);
      if(fit != rit->second.fieldOffset.end()) off = fit->second;
      auto wit = rit->second.fieldWidth.find(n->sval);
      if(wit != rit->second.fieldWidth.end()) width = wit->second;
    }
    loadFieldRax(X64Asm::RAX, off, width);
  }

  void genFieldAssign(const NodePtr& n){
    genExpr(n->kids[0]);
    pushTmp(X64Asm::RAX);
    const std::string tname = recordTypeNameOf(n->kids[0]);
    int off = 0, width = 8;
    VType want = VType::Int;
    auto rit = g_recordTypes.find(tname);
    if(rit != g_recordTypes.end()){
      auto fit = rit->second.fieldOffset.find(n->sval);
      if(fit != rit->second.fieldOffset.end()) off = fit->second;
      auto wit = rit->second.fieldWidth.find(n->sval);
      if(wit != rit->second.fieldWidth.end()) width = wit->second;
      auto tit = rit->second.fieldType.find(n->sval);
      if(tit != rit->second.fieldType.end()) want = tit->second;
    }
    const VType have = inferType(n->kids[1]);
    genExpr(n->kids[1]);
    coerceRax(have, want);
    popTmp(X64Asm::RCX);
    storeFieldRax(X64Asm::RCX, off, width);
  }

  void genExpr(const NodePtr& n){
    switch(n->type){
      case NT::Num:
        if(n->isFloat){
          // The literal's IEEE-754 bit pattern, loaded as an ordinary 64-bit
          // immediate. No constant pool is needed because mov_imm already
          // emits movabs for anything outside int32 range.
          double d = n->nval;
          int64_t bits;
          std::memcpy(&bits, &d, sizeof bits);
          a.mov_imm(X64Asm::RAX, bits);
        } else {
          a.mov_imm(X64Asm::RAX, (int64_t)n->nval);
        }
        break;
      case NT::Bool: a.mov_imm(X64Asm::RAX, n->bval ? 1 : 0);  break;
      case NT::Str:  a.lea_rip(X64Asm::RAX, addStringLiteral(n->sval)); break;
      case NT::ArrayLit:    genArrayLit(n); break;
      case NT::Index:       genIndex(n); break;
      case NT::IndexAssign: genIndexAssign(n); break;
      case NT::RecordLit:    genRecordLit(n); break;
      case NT::FieldAccess:  genFieldAccess(n); break;
      case NT::FieldAssign:  genFieldAssign(n); break;
      case NT::Var:  a.mov_load_rbp(X64Asm::RAX, -curOffset(n->sval)); break;
      case NT::Un:
        genExpr(n->kids[0]);
        if(n->sval=="!"){                  // si / ! -- logical negation to 0/1
          a.test(X64Asm::RAX, X64Asm::RAX);
          a.setcc("e");
          a.movzx_rax_al();
        } else if(inferType(n->kids[0]) == VType::Float) {
          // Flip the sign bit rather than computing 0.0 - x, which would turn
          // -0.0 into +0.0.
          a.movq_xmm_r64(X64Asm::XMM0, X64Asm::RAX);
          a.mov_imm(X64Asm::RBX, INT64_MIN);        // 0x8000000000000000
          a.movq_xmm_r64(X64Asm::XMM1, X64Asm::RBX);
          a.xorpd(X64Asm::XMM0, X64Asm::XMM1);
          a.movq_r64_xmm(X64Asm::RAX, X64Asm::XMM0);
        } else {
          a.neg(X64Asm::RAX);
        }
        break;
      case NT::Assign:
        genExpr(n->kids[0]);
        a.mov_store_rbp(-curOffset(n->sval), X64Asm::RAX);
        break;
      case NT::Bin: {
        // Short-circuit operators: the right side must not be evaluated when
        // the left already decides the result.
        if(n->sval=="&&" || n->sval=="||"){
          const bool isAnd = n->sval=="&&";
          const std::string Lshort = newLabel(isAnd ? "Land_false" : "Lor_true");
          const std::string Lend   = newLabel("Lbool_end");

          genExpr(n->kids[0]);
          a.test(X64Asm::RAX, X64Asm::RAX);
          if(isAnd) a.jz(Lshort); else a.jnz(Lshort);

          genExpr(n->kids[1]);
          a.test(X64Asm::RAX, X64Asm::RAX);
          if(isAnd) a.jz(Lshort); else a.jnz(Lshort);

          // Normalise to 0/1 rather than passing the operand value through.
          a.mov_imm(X64Asm::RAX, isAnd ? 1 : 0);
          a.jmp(Lend);
          a.defineLabel(Lshort);
          a.mov_imm(X64Asm::RAX, isAnd ? 0 : 1);
          a.defineLabel(Lend);
          break;
        }

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

        // ---- f64 path ----------------------------------------------------
        const VType lt = inferType(n->kids[0]), rt = inferType(n->kids[1]);
        if(lt == VType::Float || rt == VType::Float){
          genExpr(n->kids[0]);
          if(lt != VType::Float) raxIntToFloat();     // promote the int side
          pushTmp(X64Asm::RAX);
          genExpr(n->kids[1]);
          if(rt != VType::Float) raxIntToFloat();
          a.mov_reg(X64Asm::RBX, X64Asm::RAX);
          popTmp(X64Asm::RAX);

          a.movq_xmm_r64(X64Asm::XMM0, X64Asm::RAX);   // XMM0 = left
          a.movq_xmm_r64(X64Asm::XMM1, X64Asm::RBX);   // XMM1 = right

          const std::string& fop = n->sval;
          if(fop=="+" || fop=="-" || fop=="*" || fop=="/"){
            if      (fop=="+") a.addsd(X64Asm::XMM0, X64Asm::XMM1);
            else if (fop=="-") a.subsd(X64Asm::XMM0, X64Asm::XMM1);
            else if (fop=="*") a.mulsd(X64Asm::XMM0, X64Asm::XMM1);
            else               a.divsd(X64Asm::XMM0, X64Asm::XMM1);
            a.movq_r64_xmm(X64Asm::RAX, X64Asm::XMM0);
            break;
          }

          // Comparisons. UCOMISD reports "unordered" (PF=1, and CF=ZF=1) when
          // either operand is NaN, and NaN must compare false against
          // everything -- including itself.
          //
          // The ordering tests are written with `a`/`ae`, which both require
          // CF=0, so an unordered result yields 0 without any extra branch.
          // `<` and `<=` get there by swapping the operands rather than using
          // `b`/`be`, which would wrongly return 1 for NaN.
          if(fop=="<" || fop==">" || fop=="<=" || fop==">="){
            const bool swap  = (fop=="<" || fop=="<=");
            const bool orEq  = (fop=="<=" || fop==">=");
            if(swap) a.ucomisd(X64Asm::XMM1, X64Asm::XMM0);
            else     a.ucomisd(X64Asm::XMM0, X64Asm::XMM1);
            a.setcc(orEq ? "ae" : "a");
            a.movzx_rax_al();
            break;
          }

          // Equality needs the parity flag explicitly: for NaN, ZF is set, so
          // a bare sete would report NaN == NaN as true.
          const std::string Lend = newLabel("Lfcmp");
          a.ucomisd(X64Asm::XMM0, X64Asm::XMM1);
          a.mov_imm(X64Asm::RAX, fop=="==" ? 0 : 1);   // MOV does not touch flags
          a.jp(Lend);                                   // unordered -> keep it
          a.jnz(Lend);                                  // differ     -> keep it
          a.mov_imm(X64Asm::RAX, fop=="==" ? 1 : 0);    // ordered and equal
          a.defineLabel(Lend);
          break;
        }

        // ---- integer path ------------------------------------------------
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
      {"andikamo",  "wandaa_write_file"},
      {"ongeramo",  "wandaa_append_file"},
      // Convert a raw NUL-terminated pointer returned by a `hanze` function
      // into a Wandaa string. Needed because foreign strings carry no length
      // header of their own.
      {"ijambo",    "wandaa_str_from_c"},
      {"inyuguti",  "wandaa_str_at"},      // byte at index, -1 if out of range
      {"igice",     "wandaa_substr"},      // substring(start, len), clamped
      {"mu_ijambo", "wandaa_int_to_str"},  // number -> string
      {"mu_mubare", "wandaa_str_to_int"},  // string -> number
      {"urutonde",  "wandaa_array_new"}    // zero-filled array of n elements
    };
    std::string target = n->sval;
    auto bit = builtins.find(target);
    if(bit != builtins.end()) target = bit->second;

    // The two f64 conversions are single instructions, so they are emitted
    // inline rather than becoming runtime calls.
    if(n->sval == "mu_bice" || n->sval == "mu_mubare_wuzuye"){
      if(n->kids.size() != 1)
        throw std::runtime_error("'" + n->sval + "' isaba igipimo kimwe gusa");
      const VType have = inferType(n->kids[0]);
      genExpr(n->kids[0]);
      coerceRax(have, n->sval == "mu_bice" ? VType::Float : VType::Int);
      return;
    }

    const auto ext = externs.find(target);
    if(ext != externs.end()){
      if(bit != builtins.end())
        throw std::runtime_error("izina '" + n->sval + "' ryamaze gufatwa na Wandaa");
      // A `hanze` function has no declared signature, so each argument travels
      // in the file its own type implies.
      emitCall(target, n->kids, /*isImport=*/true);
      return;
    }

    // For a Wandaa function, pass the inferred parameter types so the caller
    // and the callee agree on which register file each argument uses.
    const auto pit = fnParamTypes.find(target);
    emitCall(target, n->kids, /*isImport=*/false,
             pit != fnParamTypes.end() ? &pit->second : nullptr);
  }

  void genPrint(const NodePtr& n){
    for(auto& arg : n->kids){
      const VType t = inferType(arg);
      genExpr(arg);
      a.mov_reg(X64Asm::RCX, X64Asm::RAX);
      // wandaa_print_float takes the f64 BIT PATTERN in RCX, not in XMM0. It
      // is an internal helper, and passing bits keeps every runtime call site
      // in this file identical.
      callRuntime(t == VType::Str   ? "wandaa_print_strval" :
                  t == VType::Float ? "wandaa_print_float"  :
                                      "wandaa_print_int");
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
        loops.push_back({Lend, Lstart});
        genStmt(n->kids[1]);
        loops.pop_back();
        a.jmp(Lstart);
        a.defineLabel(Lend);
        break;
      }
      case NT::Break:
        if(loops.empty()) throw std::runtime_error("'hagarika' iri hanze ya 'mugihe'");
        a.jmp(loops.back().brk);
        break;
      case NT::Continue:
        if(loops.empty()) throw std::runtime_error("'komeza' iri hanze ya 'mugihe'");
        a.jmp(loops.back().cont);
        break;
      case NT::Block: for(auto& k : n->kids) genStmt(k); break;
      case NT::Return:
        if(!n->kids.empty()) genExpr(n->kids[0]);
        else a.xor_eax_eax();
        a.jmp(epilogue);
        break;
      case NT::FuncDecl: break;
      case NT::ExternDecl: break;
      case NT::RecordDecl: break;   // declaration only, emits nothing   // declaration only, emits nothing
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
      const bool isFloatParam = (fi.types.count(params[i]) &&
                                 fi.types.at(params[i]) == VType::Float);
      if(i < 4){
        // Arguments 0-3 arrive in a register, which file depending on type.
        if(isFloatParam) a.movsd_store(X64Asm::RBP, slot, XMM_ARGS[i]);
        else             a.mov_store_rbp(slot, ARG_REGS[i]);
      } else {
        // Arguments 5 and up were written by the caller into the outgoing
        // area; after `push rbp; mov rbp, rsp` argument i's home is at
        // [rbp + 16 + 8*i] regardless of which file it would have used.
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
    g_recordTypes.clear();
    g_varRecordType.clear();
    registerRecordTypes(program);
    g_fnReturnTypes["soma"] = VType::Str;
    g_fnReturnTypes["ijambo"] = VType::Str;
    g_fnReturnTypes["igice"] = VType::Str;
    g_fnReturnTypes["mu_ijambo"] = VType::Str;
    g_fnReturnTypes["mu_bice"] = VType::Float;
    g_fnReturnTypes["mu_mubare_wuzuye"] = VType::Int;

    std::vector<NodePtr> funcs, rest;
    for(auto& k : program->kids){
      if(k->type==NT::FuncDecl) funcs.push_back(k);
      else if(k->type==NT::ExternDecl){
        // Declaration only -- it emits no code, it just tells the linker-less
        // backend to put this function in the import table.
        auto prev = externs.find(k->sval);
        if(prev != externs.end() && prev->second != k->sval2)
          throw std::runtime_error("umurimo wo hanze '" + k->sval +
                                   "' watangajwe kabiri muri DLL zitandukanye: " +
                                   prev->second + " na " + k->sval2);
        externs[k->sval] = k->sval2;
      }
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
        auto priority = [](VType t){
          switch(t){ case VType::Str: return 2; case VType::Float: return 1; default: return 0; }
        };
        auto exist = g_fnReturnTypes.find(f->sval);
        if(exist == g_fnReturnTypes.end() || priority(rt) > priority(exist->second))
          g_fnReturnTypes[f->sval] = rt;
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
    // Anything the program declared with `hanze`.
    for(const auto& ex : externs){
      const std::string key = ex.second + "!" + ex.first;
      if(!importId.count(key)) importId[key] = pe.addImport(ex.second, ex.first);
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
