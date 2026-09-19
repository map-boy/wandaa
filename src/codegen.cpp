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
#include <set>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using R = X64Asm::Reg;

enum class VType { Int, Str, Arr, Float, Record, Result };

// An f64 value travels in a general-purpose register as its raw IEEE-754 bit
// pattern, and only moves into an XMM register for the arithmetic itself. That
// keeps every existing mechanism -- 8-byte frame slots, push/pop temporaries,
// the [rsp+8*i] outgoing-argument area -- working unchanged, at the cost of a
// movq pair around each operation.
inline bool isNum(VType t){ return t == VType::Int || t == VType::Float; }

std::unordered_map<std::string,VType> g_fnReturnTypes;
std::unordered_map<std::string,VType> g_arrElemTypes;
// The element type a FUNCTION returns, carrying it across the return boundary.
std::unordered_map<std::string,VType> g_fnArrElemTypes;

// What a SUCCESSFUL result carries. There are no generics yet, so a result's
// payload has no type of its own and would read back as plain Int -- exactly
// the problem g_arrElemTypes solves for arrays. `reka r = byakunze("ni byiza");`
// records Str against r, so `agaciro(r)` and `r?` come back as a string rather
// than as the pointer's numeric value. Keyed by variable and by function,
// mirroring g_arrElemTypes / g_fnArrElemTypes.
// Closures. The lifting pass gives every anonymous `umurimo` a top-level name
// and works out which enclosing variables it uses; both maps are keyed by that
// generated name. g_declaredFns is every NAMED function in the program, which
// free-variable analysis needs in order to tell `f(1)` calling a declared
// function from `f(1)` calling a closure held in a variable.
std::unordered_map<std::string,std::vector<std::string>> g_lambdaCaptures;
// What TYPE each captured variable had in the enclosing scope. Without this a
// captured string is read back inside the lambda as a plain Int and prints as
// a pointer: the lifted body is typed on its own, and nothing in it says where
// those values came from. Recorded by inferPass at the site of the lambda,
// where the enclosing scope's types are in hand.
std::unordered_map<std::string,std::unordered_map<std::string,VType>> g_lambdaCaptureTypes;
std::set<std::string> g_declaredFns;
// Which lifted function a variable holds, for `reka f = umurimo(...) { ... };`.
// Without it a call through f has no return type and andika(f()) prints a
// string payload as a pointer.
std::unordered_map<std::string,std::string> g_varLambda;
// Which lifted function a NAMED function hands back, for
// `umurimo mk(p){ tanga umurimo(s){ ... }; }`. It carries the closure's
// identity across the return boundary, the same job g_fnArrElemTypes does for
// an array's element type.
std::unordered_map<std::string,std::string> g_fnLambda;
// Parameter types that were WRITTEN DOWN, by position. Separate from
// fnParamTypes, which mixes declarations with what inference guessed: only a
// declared type is certain enough to reject a call over.
std::unordered_map<std::string,std::map<size_t,VType>> g_declaredParamTypes;

// Generic functions. `umurimo mbere<T>(a: urutonde<T>): T` needs no
// monomorphisation: every Wandaa value is 8 bytes in a register, so ONE body
// serves every T and generics are purely a compile-time device for working out
// what a call gives back. Inside the body T is opaque -- correctly so, since
// the code there only moves 8 bytes around.
std::unordered_map<std::string,std::vector<std::string>> g_fnTypeParams;
// The annotation text of each declared parameter, kept verbatim so a call site
// can match `urutonde<T>` against the argument it was actually given.
std::unordered_map<std::string,std::map<size_t,std::string>> g_fnParamAnnot;
std::unordered_map<std::string,std::string> g_fnRetAnnot;
// Which parameter positions are generic. Those must travel RAW: the same 8
// bytes the caller had, in a general-purpose register, with no conversion.
// Everywhere else a parameter has one settled type and emitCall coerces to it,
// which for a T used at two different types would destroy the value.
std::unordered_map<std::string,std::set<size_t>> g_fnGenericPos;

std::unordered_map<std::string,VType> g_resultPayload;     // variable -> payload
std::unordered_map<std::string,VType> g_fnResultPayload;   // function -> payload

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

// Defined below, with the rest of the annotation handling; used by both
// inference walks, which appear before it.
void applyVarDecl(const NodePtr& n, std::unordered_map<std::string,VType>& types);
void noteGenericVarDecl(const NodePtr& n, const std::unordered_map<std::string,VType>& types);

void collectNames(const NodePtr& n, std::vector<std::string>& out){
  if(!n) return;
  if(n->type==NT::VarDecl) out.push_back(n->sval);
  if(n->type==NT::FuncDecl) return;
  // A lambda body's locals live in ITS frame, not this one. Without this the
  // enclosing function reserves slots for names it never uses, and worse, a
  // name declared in both frames would collide.
  if(n->type==NT::Lambda) return;
  for(auto& k : n->kids) collectNames(k, out);
}

FnInfo buildFnInfo(const std::vector<std::string>& params, const NodePtr& body,
                   const std::vector<std::string>* extraLocals = nullptr){
  FnInfo fi;
  int off = 8;
  for(auto& p : params){ fi.offset[p] = off; off += 8; }
  // Captured variables are copied out of the closure block into ordinary frame
  // slots in the prologue, so from there on the rest of codegen treats them as
  // plain locals and needs to know nothing about closures.
  if(extraLocals)
    for(auto& e : *extraLocals) if(!fi.offset.count(e)){ fi.offset[e] = off; off += 8; }
  std::vector<std::string> locals;
  collectNames(body, locals);
  for(auto& l : locals) if(!fi.offset.count(l)){ fi.offset[l] = off; off += 8; }
  int size = off - 8;
  size = (size + 15) & ~15;
  fi.frameSize = size;
  return fi;
}

VType evalType(const NodePtr& n, const std::unordered_map<std::string,VType>& types);

// What an array-valued expression holds. The mirror of payloadTypeOf, and the
// same kind of inference: certain in three cases, Int otherwise.
VType elemTypeOf(const NodePtr& n, const std::unordered_map<std::string,VType>& types);

// The type of the value inside a successful result.
//
// This is inference, not knowledge: nothing at runtime records what those 8
// bytes are. Three cases are certain enough to use -- a byakunze() written out
// directly, a variable a result was assigned to, and a call to a function whose
// `tanga byakunze(...)` was already seen. Everything else falls back to Int,
// the same fallback the rest of this file uses, which means the payload prints
// as a number. That is the cost of not having generics yet.
VType payloadTypeOf(const NodePtr& n, const std::unordered_map<std::string,VType>& types){
  if(!n) return VType::Int;
  if(n->type==NT::Call){
    if(n->sval=="byakunze" && n->kids.size()==1) return evalType(n->kids[0], types);
    auto it = g_fnResultPayload.find(n->sval);
    if(it != g_fnResultPayload.end()) return it->second;
  } else if(n->type==NT::Var){
    auto it = g_resultPayload.find(n->sval);
    if(it != g_resultPayload.end()) return it->second;
  }
  return VType::Int;
}

VType elemTypeOf(const NodePtr& n, const std::unordered_map<std::string,VType>& types){
  if(!n) return VType::Int;
  if(n->type==NT::Var){
    auto it = g_arrElemTypes.find(n->sval);
    if(it != g_arrElemTypes.end()) return it->second;
  } else if(n->type==NT::ArrayLit){
    if(!n->kids.empty()) return evalType(n->kids[0], types);
  } else if(n->type==NT::Call){
    auto it = g_fnArrElemTypes.find(n->sval);
    if(it != g_fnArrElemTypes.end()) return it->second;
  }
  return VType::Int;
}

// Defined with the rest of the annotation handling, below.
std::string typeBase(const std::string& t);
std::string typeArg(const std::string& t);
bool typeFromName(const std::string& t, VType& out);

// Bind a generic function's type parameters from the arguments at a call site.
// `mbere<T>(a: urutonde<T>)` given an array of strings binds T = ijambo.
//
// Only the three shapes that can actually be matched are handled: a parameter
// that IS the type parameter, `urutonde<T>` and `igisubizo<T>`. The first
// binding wins, so a call whose arguments disagree takes the earliest rather
// than guessing between them.
std::map<std::string,VType> bindTypeParams(const std::string& fn, const NodePtr& call,
                                           const std::unordered_map<std::string,VType>& types){
  std::map<std::string,VType> bound;
  auto tp = g_fnTypeParams.find(fn);
  auto an = g_fnParamAnnot.find(fn);
  if(tp == g_fnTypeParams.end() || an == g_fnParamAnnot.end()) return bound;
  const std::set<std::string> tparams(tp->second.begin(), tp->second.end());

  for(const auto& kv : an->second){
    const size_t i = kv.first;
    if(i >= call->kids.size()) continue;
    const std::string base = typeBase(kv.second), arg = typeArg(kv.second);
    if(arg.empty()){
      if(tparams.count(base) && !bound.count(base))
        bound[base] = evalType(call->kids[i], types);
    } else if(tparams.count(arg) && !bound.count(arg)){
      if(base=="urutonde")  bound[arg] = elemTypeOf(call->kids[i], types);
      if(base=="igisubizo") bound[arg] = payloadTypeOf(call->kids[i], types);
    }
  }
  return bound;
}

// The type a call to a generic function yields and -- when that is an array or
// a result -- what it holds. False when the callee is not generic.
bool genericReturn(const NodePtr& call, const std::unordered_map<std::string,VType>& types,
                   VType& out, VType* inner = nullptr){
  auto ra = g_fnRetAnnot.find(call->sval);
  auto tp = g_fnTypeParams.find(call->sval);
  if(ra == g_fnRetAnnot.end() || tp == g_fnTypeParams.end()) return false;
  const std::set<std::string> tparams(tp->second.begin(), tp->second.end());
  const std::string base = typeBase(ra->second), arg = typeArg(ra->second);

  if(arg.empty()){
    if(!tparams.count(base)) return false;            // a concrete return type
    auto bound = bindTypeParams(call->sval, call, types);
    auto it = bound.find(base);
    out = (it != bound.end()) ? it->second : VType::Int;
    return true;
  }
  if(!tparams.count(arg)) return false;               // e.g. urutonde<ijambo>
  VType b;
  if(!typeFromName(base, b)) return false;
  out = b;
  if(inner){
    auto bound = bindTypeParams(call->sval, call, types);
    auto it = bound.find(arg);
    *inner = (it != bound.end()) ? it->second : VType::Int;
  }
  return true;
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
    // `e?` is the value inside a success -- the failure path never reaches here.
    case NT::Try: return payloadTypeOf(n->kids[0], types);
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
    // A pointer is just an address. It is an integer as far as the language is
    // concerned -- there is no pointer arithmetic and nothing dereferences it
    // except the foreign function it is handed to.
    case NT::AddrOf: return VType::Int;
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
      // A generic call is resolved from its arguments, and this comes first:
      // the body of `mbere<T>` returns a[0], which infers as Int, and that
      // must not win over what the call site can actually work out.
      { VType g; if(genericReturn(n, types, g)) return g; }
      // A call through a variable holding a closure returns whatever the
      // lifted function returns.
      auto lv = g_varLambda.find(n->sval);
      if(lv != g_varLambda.end()){
        auto rt = g_fnReturnTypes.find(lv->second);
        if(rt != g_fnReturnTypes.end()) return rt->second;
      }
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
        {"byakunze",  VType::Result},
        {"byanze",    VType::Result},
        {"byarakunze",VType::Int},     // 1 or 0
        {"ikosa",     VType::Str},     // the failure message
      };
      // agaciro() hands back whatever was put in, so it is not a fixed type.
      if(n->sval=="agaciro" && n->kids.size()==1)
        return payloadTypeOf(n->kids[0], types);
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

// The same idea for results: `tanga byakunze("mwiriwe");` inside umurimo F
// records Str against F, so `F()?` at the call site is a string. Also follows
// `tanga r;` where r is a variable already known to hold a result.
void findResultReturnType(const NodePtr& n, const std::string& fnName,
                          const std::unordered_map<std::string,VType>& types){
  if(!n) return;
  if(n->type==NT::Return && !n->kids.empty()){
    const VType pt = payloadTypeOf(n->kids[0], types);
    if(pt != VType::Int) g_fnResultPayload[fnName] = pt;
  }
  for(auto& k : n->kids) findResultReturnType(k, fnName, types);
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
  if(n->type==NT::Lambda){
    // Same reasoning as FuncDecl above: separate scope, but still walked so
    // the global side maps (array element types, record types) get filled.
    std::unordered_map<std::string,VType> localTypes;
    for(auto& k : n->kids) collectTypesRec(k, localTypes);
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
    noteGenericVarDecl(n, types);
    applyVarDecl(n, types);        // a written type wins over all of the above
    // `reka r = byakunze(x);` or `reka r = gusoma(...);` -- remember what a
    // success would carry, so agaciro(r) and r? read back as the right type.
    if(evalType(n->kids[0], types) == VType::Result){
      const VType pt = payloadTypeOf(n->kids[0], types);
      if(pt != VType::Int) g_resultPayload[n->sval] = pt;
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

// ===========================================================================
//  Type annotations
//
//  `izina: ijambo` on a parameter, a field, a `reka` or a return type. Every
//  annotation is OPTIONAL: where one is absent, inference does exactly what it
//  did before, so no existing .waa file changes meaning (GOVERNANCE principle
//  5). Where one is present it WINS over inference, which is the point --
//  inference falls back to Int for anything it cannot work out, and a written
//  type is how the programmer says the fallback is wrong.
// ===========================================================================

std::string typeBase(const std::string& t){
  const auto p = t.find('<');
  return p == std::string::npos ? t : t.substr(0, p);
}
// The argument of `urutonde<ijambo>`, or "" when the type has none.
std::string typeArg(const std::string& t){
  const auto p = t.find('<');
  if(p == std::string::npos || t.size() < p + 2) return "";
  return t.substr(p + 1, t.size() - p - 2);
}

bool typeFromName(const std::string& t, VType& out){
  const std::string b = typeBase(t);
  if(b=="umubare")   { out = VType::Int;    return true; }
  if(b=="ibice")     { out = VType::Float;  return true; }
  if(b=="ijambo")    { out = VType::Str;    return true; }
  if(b=="urutonde")  { out = VType::Arr;    return true; }
  if(b=="igisubizo") { out = VType::Result; return true; }
  if(g_recordTypes.count(b)) { out = VType::Record; return true; }
  return false;
}

// Resolve or refuse. A misspelled type has to be an error rather than a silent
// fallback to Int, or the annotation would be worse than useless.
VType requireType(const std::string& t, const std::string& where, int line){
  VType v;
  if(typeFromName(t, v)) return v;
  throw std::runtime_error("ubwoko butazwi '" + typeBase(t) + "' " + where +
                           " ku murongo " + std::to_string(line) +
                           ". Ubwoko buzwi: umubare, ibice, ijambo, urutonde, igisubizo, "
                           "cyangwa izina ry'ubwoko bwatangajwe na 'ubwoko'");
}

// A type argument on a NAMED thing -- a parameter or a variable -- says what
// its elements or its payload are. This is what replaces the guesswork:
// `reka a: urutonde<ibice> = urutonde(3);` states outright what noteIndexedWrite
// could previously only infer from a later assignment.
void recordTypeArg(const std::string& decl, VType base, const std::string& name, int line){
  const std::string arg = typeArg(decl);
  if(arg.empty()) return;
  const VType a = requireType(arg, "ku '" + name + "'", line);
  if(base == VType::Arr)    g_arrElemTypes[name] = a;
  if(base == VType::Result) g_resultPayload[name] = a;
}

// `reka a = shungura(...);` where shungura is generic and returns
// `urutonde<T>` -- the binding says what the elements are, which is the whole
// point of writing the signature down.
void noteGenericVarDecl(const NodePtr& n, const std::unordered_map<std::string,VType>& types){
  if(n->kids.empty() || n->kids[0]->type != NT::Call) return;
  VType outT, innerT = VType::Int;
  if(!genericReturn(n->kids[0], types, outT, &innerT)) return;
  if(outT == VType::Arr)    g_arrElemTypes[n->sval] = innerT;
  if(outT == VType::Result) g_resultPayload[n->sval] = innerT;
}

// A `reka` with a written type. Shared by both inference walks.
void applyVarDecl(const NodePtr& n, std::unordered_map<std::string,VType>& types){
  if(n->retType.empty()) return;
  const VType t = requireType(n->retType, "ku kigereranyo '" + n->sval + "'", n->line);
  types[n->sval] = t;
  recordTypeArg(n->retType, t, n->sval, n->line);
}

void registerRecordTypes(const NodePtr& program){
  // Every record NAME first, so a field may be annotated with a record type
  // declared further down the file. The layouts are filled in below.
  for(auto& k : program->kids)
    if(k->type == NT::RecordDecl && !g_recordTypes.count(k->sval))
      g_recordTypes[k->sval] = RecordTypeInfo{};

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
        // A declared field type wins; without one scanRecordLits infers it
        // from the values a constructor is called with.
        info.fieldType[f]   = VType::Int;
        if(i < k->paramTypes.size() && !k->paramTypes[i].empty())
          info.fieldType[f] = requireType(k->paramTypes[i],
                                          "ku mwanya '" + f + "' muri '" + k->sval + "'",
                                          k->line);
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
    noteGenericVarDecl(n, types);
    applyVarDecl(n, types);
    return;
  }
  if(n->type==NT::Call){
    // A call through a variable holding a closure teaches the LIFTED function
    // about its parameters, not the variable. The lifted function takes the
    // closure block as a hidden first argument, so user parameter i is its
    // parameter i+1.
    std::string key = n->sval;
    size_t shift = 0;
    auto lv = g_varLambda.find(n->sval);
    if(lv != g_varLambda.end()){ key = lv->second; shift = 1; }

    auto& vec = fnParamTypes[key];
    if(vec.size() < n->kids.size() + shift) vec.resize(n->kids.size() + shift, VType::Int);
    for(size_t i=0;i<n->kids.size();++i){
      inferPass(n->kids[i], types, fnParamTypes);
      const VType at = evalType(n->kids[i], types);
      // A parameter is Str or Float if ANY call site passes one. Both are
      // sticky: the callee reads that parameter out of one register file, and
      // every caller has to agree, so the widest observed type wins.
      if(at == VType::Str)        vec[i+shift] = VType::Str;
      else if(at == VType::Float && vec[i+shift] != VType::Str) vec[i+shift] = VType::Float;
    }
    return;
  }
  if(n->type==NT::Lambda){
    // The body is inferred separately, as the lifted function. What only THIS
    // point knows is what the captured names meant out here.
    auto cit = g_lambdaCaptures.find(n->sval);
    if(cit != g_lambdaCaptures.end()){
      auto& rec = g_lambdaCaptureTypes[n->sval];
      for(const auto& cap : cit->second){
        auto t = types.find(cap);
        if(t != types.end() && t->second != VType::Int) rec[cap] = t->second;
      }
    }
    return;
  }
  if(n->type==NT::Assign){ inferPass(n->kids[0], types, fnParamTypes); return; }
  noteIndexedWrite(n, types);
  for(auto& k : n->kids) inferPass(k, types, fnParamTypes);
}

// ===========================================================================
//  Closure lifting
//
//  An anonymous `umurimo` becomes an ordinary top-level function plus a heap
//  block holding its code pointer and a copy of every enclosing variable it
//  uses. The block is laid out like an array, the same shape everything else
//  in this language uses:
//
//      [P-8] = 1 + ncaptures     the element count
//      [P+0] = code pointer
//      [P+8 + 8*i] = capture i
//
//  The lifted function takes the block as a hidden FIRST argument and copies
//  the captures into ordinary frame slots in its prologue. After that the rest
//  of codegen treats them as plain locals and knows nothing about closures.
//
//  Capture is BY VALUE, at the moment the closure is created. That is a real
//  language decision, not a shortcut: without it a closure outliving its
//  enclosing call would read a dead frame, and there is no reference counting
//  yet to keep a shared cell alive. It is also the rule that is easiest to
//  explain, which for this language counts.
// ===========================================================================

struct Lifted {
  std::string name;
  std::vector<std::string> params;
  std::vector<std::string> paramTypes;   // parallel to params, "" where unannotated
  std::string retType;
  NodePtr body;
};
std::vector<Lifted> g_lifted;
int g_lambdaCounter = 0;

// Every name a Call node can refer to that is NOT a variable holding a
// closure: declared functions, foreign declarations, record constructors and
// builtins. Whatever is left over must be a closure call.
void collectCallableNames(const NodePtr& n, std::set<std::string>& out){
  if(!n) return;
  if(n->type==NT::FuncDecl || n->type==NT::ExternDecl || n->type==NT::RecordDecl)
    out.insert(n->sval);
  for(auto& k : n->kids) collectCallableNames(k, out);
}

// Names used but not bound here. Descends into a nested lambda, adding ITS
// parameters and locals to the bound set for that subtree -- so a variable the
// inner lambda needs is captured by the outer one too, and reaches the inner
// through the outer's frame.
void usesRec(const NodePtr& n, std::set<std::string> bound,
             const std::set<std::string>& callables,
             std::vector<std::string>& out, std::set<std::string>& seen){
  if(!n) return;
  if(n->type==NT::FuncDecl) return;              // a named function has its own scope
  if(n->type==NT::Lambda){
    bound.insert(n->params.begin(), n->params.end());
    std::vector<std::string> inner;
    collectNames(n->kids[0], inner);
    bound.insert(inner.begin(), inner.end());
    usesRec(n->kids[0], bound, callables, out, seen);
    return;
  }
  auto use = [&](const std::string& nm){
    if(bound.count(nm) || callables.count(nm) || seen.count(nm)) return;
    seen.insert(nm);
    out.push_back(nm);
  };
  if(n->type==NT::Var)    use(n->sval);
  if(n->type==NT::Assign) use(n->sval);          // the target name lives in sval
  if(n->type==NT::Call)   use(n->sval);          // a callable is filtered out above
  for(auto& k : n->kids) usesRec(k, bound, callables, out, seen);
}

// Which variables hold which lifted function. Run after liftLambdas, once the
// generated names exist.
// `tanga umurimo(...) { ... };` inside fnName -- that function yields a closure.
void noteFnLambda(const NodePtr& n, const std::string& fnName){
  if(!n) return;
  if(n->type==NT::Return && !n->kids.empty() && n->kids[0]->type==NT::Lambda)
    g_fnLambda[fnName] = n->kids[0]->sval;
  for(auto& k : n->kids) noteFnLambda(k, fnName);
}

void noteLambdaVars(const NodePtr& n){
  if(!n) return;
  if((n->type==NT::VarDecl || n->type==NT::Assign) && !n->kids.empty()){
    if(n->kids[0]->type==NT::Lambda)
      g_varLambda[n->sval] = n->kids[0]->sval;
    else if(n->kids[0]->type==NT::Call){
      // `reka f = mk("mwiriwe ");` where mk returns a closure.
      auto it = g_fnLambda.find(n->kids[0]->sval);
      if(it != g_fnLambda.end()) g_varLambda[n->sval] = it->second;
    }
  }
  for(auto& k : n->kids) noteLambdaVars(k);
}

// `reka f = umurimo(n){ ... f(n-1) ... };` captures f BEFORE the declaration
// binds it, so the closure holds an unset slot and calling it jumps through
// garbage. That crashed with only a line number, which is no help at all, so
// it is rejected here with the fix in the message.
void checkSelfCapture(const NodePtr& n){
  if(!n) return;
  if(n->type==NT::VarDecl && !n->kids.empty() && n->kids[0]->type==NT::Lambda){
    auto it = g_lambdaCaptures.find(n->kids[0]->sval);
    if(it != g_lambdaCaptures.end())
      for(const auto& c : it->second)
        if(c == n->sval)
          throw std::runtime_error(
            "umurimo utagira izina ntushobora kwihamagara ('" + n->sval +
            "') ku murongo " + std::to_string(n->line) +
            ". Koresha umurimo ufite izina: 'umurimo " + n->sval + "(...) { ... }'");
  }
  for(auto& k : n->kids) checkSelfCapture(k);
}

void liftLambdas(const NodePtr& n, const std::set<std::string>& callables){
  if(!n) return;
  if(n->type==NT::Lambda){
    const std::string nm = "__umurimo_" + std::to_string(g_lambdaCounter++);
    n->sval = nm;                                // genLambda reads the label back

    // Locals are function-scoped here -- buildFnInfo puts every VarDecl in the
    // body into one frame -- so the bound set is the parameters plus all of
    // them, and anything else the body mentions is captured.
    std::set<std::string> bound(n->params.begin(), n->params.end());
    std::vector<std::string> locals;
    collectNames(n->kids[0], locals);
    bound.insert(locals.begin(), locals.end());

    std::vector<std::string> caps;
    std::set<std::string> seen;
    usesRec(n->kids[0], bound, callables, caps, seen);
    g_lambdaCaptures[nm] = caps;

    std::vector<std::string> ps, pts;
    ps.push_back("__ctx");                       // the hidden first argument
    pts.push_back("");                           // which is never annotated
    for(size_t i=0;i<n->params.size();++i){
      ps.push_back(n->params[i]);
      pts.push_back(i < n->paramTypes.size() ? n->paramTypes[i] : std::string());
    }
    g_lifted.push_back({nm, ps, pts, n->retType, n->kids[0]});

    liftLambdas(n->kids[0], callables);          // nested lambdas lift too
    return;
  }
  for(auto& k : n->kids) liftLambdas(k, callables);
}

// ===========================================================================
//  Semantic checks
//
//  A separate pass that runs before any code is emitted, so a bad program is
//  rejected with a source line instead of producing an executable that prints
//  a pointer. Unknown names were already caught (curOffset and the label
//  resolver throw); what was missing is everything about SHAPE: calling a
//  function with the wrong number of arguments, naming a field that does not
//  exist, or doing arithmetic on a string.
//
//  The rule for what to reject: only what is DEFINITELY wrong. Type inference
//  falls back to Int for anything it cannot work out, so rejecting on "Int
//  where Str expected" would reject correct programs. Rejecting a string in a
//  multiplication is safe, because Str is only ever inferred when it is known.
// ===========================================================================

struct Checker {
  std::unordered_map<std::string,size_t> fnArity;      // user functions
  std::unordered_map<std::string,size_t> externArity;  // hanze declarations
  std::vector<std::string> errors;

  // Is an argument acceptable for a declared parameter type?
  //
  // Int is what inference falls back to when it cannot work a type out, so an
  // Int argument is treated as "unknown, allow it" UNLESS the expression is
  // plainly a number -- a literal, or arithmetic on literals. Otherwise every
  // unannotated helper passing a value through would be rejected.
  static bool argFits(const NodePtr& a, VType have, VType want){
    if(have == want) return true;
    if(want == VType::Float && have == VType::Int) return true;   // promotion
    if(have == VType::Int){
      const bool certainlyNumber = (a->type == NT::Num && !a->isFloat);
      if(!certainlyNumber) return true;                            // unknown, allow
      return want == VType::Int || want == VType::Float;
    }
    return false;
  }

  // Builtin name -> exact argument count.
  static const std::unordered_map<std::string,size_t>& builtinArity(){
    static const std::unordered_map<std::string,size_t> m = {
      {"uburebure",1},{"ubunini",1},{"soma",1},{"andikamo",2},{"ongeramo",2},
      {"ijambo",1},{"inyuguti",2},{"igice",3},{"mu_ijambo",1},{"mu_mubare",1},
      {"urutonde",1},{"mu_bice",1},{"mu_mubare_wuzuye",1},
      {"byakunze",1},{"byanze",1},{"byarakunze",1},{"agaciro",1},{"ikosa",1}
    };
    return m;
  }

  void err(const NodePtr& n, const std::string& msg){
    const std::string where = n && n->line > 0
      ? " ku murongo " + std::to_string(n->line) : "";
    errors.push_back(msg + where);
  }

  static std::string typeName(VType t){
    switch(t){
      case VType::Str:    return "ijambo";
      case VType::Arr:    return "urutonde";
      case VType::Float:  return "umubare w'ibice";
      case VType::Record: return "ubwoko";
      case VType::Result: return "igisubizo";
      default:            return "umubare";
    }
  }

  void collect(const NodePtr& n){
    if(!n) return;
    if(n->type == NT::FuncDecl)   fnArity[n->sval]     = n->params.size();
    if(n->type == NT::ExternDecl) externArity[n->sval] = n->params.size();
    for(auto& k : n->kids) collect(k);
  }

  void walk(const NodePtr& n, const std::unordered_map<std::string,VType>& types){
    if(!n) return;

    switch(n->type){
      case NT::RecordLit: {
        // A record literal is its own node type, so it needs the same arity
        // check as a call -- `S(1)` on a two-field record left the second
        // field reading whatever HeapAlloc handed back.
        auto rit = g_recordTypes.find(n->sval);
        if(rit != g_recordTypes.end()){
          const size_t want = rit->second.fieldNames.size();
          if(n->kids.size() != want)
            err(n, "ubwoko '" + n->sval + "' busaba imyanya " + std::to_string(want) +
                   ", bwahawe " + std::to_string(n->kids.size()));
        }
        break;
      }

      case NT::Call: {
        const std::string& name = n->sval;
        const size_t given = n->kids.size();
        size_t want = given;
        bool known = false;

        auto bi = builtinArity().find(name);
        auto fi = fnArity.find(name);
        auto ei = externArity.find(name);
        auto ri = g_recordTypes.find(name);
        if(bi != builtinArity().end()){ want = bi->second; known = true; }
        else if(fi != fnArity.end())  { want = fi->second; known = true; }
        else if(ei != externArity.end()){ want = ei->second; known = true; }
        else if(ri != g_recordTypes.end()){ want = ri->second.fieldNames.size(); known = true; }

        if(known && given != want)
          err(n, "umurimo '" + name + "' usaba ibipimo " + std::to_string(want) +
                 ", wahawe " + std::to_string(given));

        // Arguments against DECLARED parameter types. Only declarations are
        // checked: fnParamTypes also holds what inference guessed, and
        // rejecting a call over a guess would reject working programs.
        auto dp = g_declaredParamTypes.find(name);
        if(dp != g_declaredParamTypes.end()){
          for(size_t i=0;i<n->kids.size();++i){
            auto want_it = dp->second.find(i);
            if(want_it == dp->second.end()) continue;
            const VType w = want_it->second;
            const VType h = evalType(n->kids[i], types);
            if(!argFits(n->kids[i], h, w))
              err(n, "igipimo cya " + std::to_string(i+1) + " cya '" + name +
                     "' gisaba " + typeName(w) + ", cyahawe " + typeName(h));
          }
        }
        break;
      }

      case NT::FieldAccess: case NT::FieldAssign: {
        const std::string tname = recordTypeNameOf(n->kids[0]);
        if(!tname.empty()){
          auto rit = g_recordTypes.find(tname);
          if(rit != g_recordTypes.end() && !rit->second.fieldOffset.count(n->sval))
            err(n, "ubwoko '" + tname + "' nta mwanya ufite witwa '" + n->sval + "'");
        }
        break;
      }

      case NT::Bin: {
        const std::string& op = n->sval;
        const VType lt = evalType(n->kids[0], types);
        const VType rt = evalType(n->kids[1], types);

        // Strings support + (concatenation) and == / != only.
        const bool arith = (op=="-" || op=="*" || op=="/");
        if(arith && (lt==VType::Str || rt==VType::Str))
          err(n, "ntibishoboka gukoresha '" + op + "' ku ijambo");

        // `+` joins two strings or adds two numbers. One of each adds the
        // pointer to the number and prints nonsense.
        //
        // Only flagged when the non-string side is CERTAIN: a numeric literal,
        // or a float/array/record. Int is also the fallback for anything
        // inference could not resolve, so treating every Int as definitely-not
        // a string would reject correct programs.
        if(op=="+" && ((lt==VType::Str) != (rt==VType::Str))){
          const NodePtr& other = (lt==VType::Str) ? n->kids[1] : n->kids[0];
          const VType ot = (lt==VType::Str) ? rt : lt;
          const bool certain = (other->type == NT::Num)
                            || ot == VType::Float || ot == VType::Arr || ot == VType::Record;
          if(certain)
            err(n, "ntibishoboka guteranya " + typeName(lt) + " na " + typeName(rt) +
                   " -- koresha mu_ijambo() kugira ngo uhindure umubare mu ijambo");
        }
        break;
      }

      default: break;
    }

    for(auto& k : n->kids) walk(k, types);
  }
};

void checkProgram(const NodePtr& program,
                  const std::unordered_map<std::string,std::vector<VType>>& fnParamTypes){
  Checker c;
  c.collect(program);

  std::unordered_map<std::string,VType> topTypes;
  collectTypesRec(program, topTypes);
  c.walk(program, topTypes);

  // Each function body is checked with its parameters bound to their INFERRED
  // types, mirroring genFunction exactly. Defaulting them to Int instead would
  // make `tanga "mwiriwe " + izina` look like string-plus-number for every
  // function that takes a string.
  for(auto& k : program->kids){
    if(k->type != NT::FuncDecl) continue;
    std::unordered_map<std::string,VType> fnTypes;
    auto pit = fnParamTypes.find(k->sval);
    for(size_t i=0;i<k->params.size();++i){
      const VType pt = (pit != fnParamTypes.end() && i < pit->second.size())
                       ? pit->second[i] : VType::Int;
      fnTypes[k->params[i]] = pt;
    }
    collectTypesRec(k->kids[0], fnTypes);
    c.walk(k->kids[0], fnTypes);
  }

  if(!c.errors.empty()){
    std::string all;
    for(size_t i=0;i<c.errors.size();++i){
      if(i) all += "\n";
      all += c.errors[i];
    }
    throw std::runtime_error(all);
  }
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
  std::string curFnName;
  // Every name a Call can refer to that is NOT a closure held in a variable.
  std::set<std::string> callables;
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
  // `indirect` means argument 0 is a closure block and the call goes through
  // the code pointer in its first slot, rather than to a known label.
  void emitCall(const std::string& target, const std::vector<NodePtr>& args, bool isImport,
                const std::vector<VType>* paramTypes = nullptr, bool indirect = false,
                const std::set<size_t>* rawArgs = nullptr){
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
      // A generic position takes the caller's 8 bytes exactly as they are.
      const bool raw = rawArgs && rawArgs->count((size_t)i);
      const VType want = raw ? have
                       : (paramTypes && (size_t)i < paramTypes->size())
                         ? (*paramTypes)[i] : have;
      genExpr(args[i]);
      if(!raw) coerceRax(have, want);
      a.mov_store_base(X64Asm::RSP, i * 8, X64Asm::RAX);
      useXmm[(size_t)i] = !raw && (want == VType::Float);
    }

    // Win64 homes argument i at [rsp + 8*i] for BOTH register files, so the
    // stack layout above is already correct and only the load differs.
    for(int i = 0; i < n && i < 4; ++i){
      if(useXmm[(size_t)i]) a.movsd_load(XMM_ARGS[i], X64Asm::RSP, i * 8);
      else                  a.mov_load_base(ARG_REGS[i], X64Asm::RSP, i * 8);
    }

    if(indirect){
      // RAX is volatile and never an argument register, so it is free here --
      // after the four argument registers have already been loaded.
      a.mov_load_base(X64Asm::RAX, X64Asm::RSP, 0);   // argument 0: the closure
      a.mov_load_base(X64Asm::RAX, X64Asm::RAX, 0);   // its code pointer
      a.call_reg(X64Asm::RAX);
    }
    else if(isImport) a.call_mem_rip("__imp_" + target);
    else              a.call_label(target);

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
      // R12 has to be saved across the element's own code, not just across the
      // calls in it. A CALL preserves R12 -- it is callee-saved -- but a
      // NESTED array literal or result, which is codegen rather than a call,
      // overwrites it with its own block. Without this `[[1,2],[3,4]]` built
      // the inner arrays and then stored them through a clobbered pointer.
      pushTmp(X64Asm::R12);
      genExpr(n->kids[i]);
      popTmp(X64Asm::R12);
      // The old backend always emitted a disp8 here, which silently truncated
      // past 15 elements. mov_store_base picks disp8/disp32 correctly.
      a.mov_store_base(X64Asm::R12, (int32_t)(8 + i*8), X64Asm::RAX);
    }
    a.lea_base(X64Asm::RAX, X64Asm::R12, 8);
  }

  // Build the closure block: the code pointer, then a copy of each captured
  // variable read out of the CURRENT frame. Nothing here runs nested codegen
  // between the allocation and the stores -- captures are plain frame loads --
  // so R12 needs no saving, unlike genArrayLit.
  void genLambda(const NodePtr& n){
    const auto& caps = g_lambdaCaptures.at(n->sval);
    const size_t count = 1 + caps.size();
    callImport("GetProcessHeap");
    a.mov_reg(X64Asm::RCX, X64Asm::RAX);
    a.xorr(X64Asm::RDX, X64Asm::RDX);
    a.mov_imm(X64Asm::R8, (int64_t)(8 + count*8));
    callImport("HeapAlloc");
    a.mov_reg(X64Asm::R12, X64Asm::RAX);
    a.mov_store_imm_base(X64Asm::R12, 0, (int32_t)count);
    a.lea_rip(X64Asm::RAX, n->sval);                    // the lifted function
    a.mov_store_base(X64Asm::R12, 8, X64Asm::RAX);
    for(size_t i=0;i<caps.size();++i){
      a.mov_load_rbp(X64Asm::RAX, -curOffset(caps[i]));
      a.mov_store_base(X64Asm::R12, (int32_t)(16 + 8*i), X64Asm::RAX);
    }
    a.lea_base(X64Asm::RAX, X64Asm::R12, 8);
  }

  // ---- results -----------------------------------------------------------
  //
  //  A result is an ordinary heap block in the array shape: an 8-byte count
  //  header, then two slots, with the value pointing past the header.
  //
  //      [ptr-8] = 2          the element count, so ubunini() and the bounds
  //                           check see a two-element array
  //      [ptr+0] = tag        1 success, 0 failure
  //      [ptr+8] = payload    the value, or the failure's message
  //
  //  Reusing the array shape is deliberate: no new heap layout, no new runtime
  //  allocator, and a result can be printed and indexed by code that predates
  //  it. The tag being 1/0 is what lets byarakunze() be a single load.
  void genResultNew(const NodePtr& n, int32_t tag){
    // Allocate BEFORE evaluating the payload, exactly as genArrayLit does: the
    // block pointer then lives in R12 across the payload's own code, and
    // nothing has to survive the two import calls.
    callImport("GetProcessHeap");
    a.mov_reg(X64Asm::RCX, X64Asm::RAX);
    a.xorr(X64Asm::RDX, X64Asm::RDX);
    a.mov_imm(X64Asm::R8, (int64_t)(8 + 2*8));
    callImport("HeapAlloc");
    a.mov_reg(X64Asm::R12, X64Asm::RAX);
    a.mov_store_imm_base(X64Asm::R12, 0, 2);            // count header
    a.mov_store_imm_base(X64Asm::R12, 8, tag);          // slot 0: the tag
    pushTmp(X64Asm::R12);                               // see genArrayLit
    genExpr(n->kids[0]);
    popTmp(X64Asm::R12);
    a.mov_store_base(X64Asm::R12, 16, X64Asm::RAX);     // slot 1: the payload
    a.lea_base(X64Asm::RAX, X64Asm::R12, 8);
  }

  // agaciro(r) -- the value inside. On a failure there is nothing to hand
  // back, so this stops the program with the stored message, the way Rust's
  // unwrap panics. Use byarakunze() first, or `?`, to avoid it.
  void genResultUnwrap(const NodePtr& n){
    const std::string Lok = newLabel("Lunwrap");
    genExpr(n->kids[0]);
    a.mov_reg(X64Asm::R12, X64Asm::RAX);
    a.mov_load_base(X64Asm::RAX, X64Asm::R12, 0);       // the tag
    a.test(X64Asm::RAX, X64Asm::RAX);
    a.jnz(Lok);
    a.mov_load_base(X64Asm::RCX, X64Asm::R12, 8);       // the failure message
    callRuntime("wandaa_result_trap");                  // does not return
    a.defineLabel(Lok);
    a.mov_load_base(X64Asm::RAX, X64Asm::R12, 8);
  }

  // ikosa(r) -- the failure message. Asking a success for its error is a bug
  // in the program rather than something to handle, so it stops too.
  void genResultErr(const NodePtr& n){
    const std::string Lerr = newLabel("Lerr");
    genExpr(n->kids[0]);
    a.mov_reg(X64Asm::R12, X64Asm::RAX);
    a.mov_load_base(X64Asm::RAX, X64Asm::R12, 0);
    a.test(X64Asm::RAX, X64Asm::RAX);
    a.jz(Lerr);
    callRuntime("wandaa_misuse_trap");                  // does not return
    a.defineLabel(Lerr);
    a.mov_load_base(X64Asm::RAX, X64Asm::R12, 8);
  }

  // Postfix `?` -- the whole point of the type. On a success it is the value
  // inside. On a failure the enclosing function returns that same failure
  // immediately, so a chain of fallible calls reads like a chain of ordinary
  // ones and every error still has to go somewhere.
  //
  // At top level there is no caller to return to: wandaa_main returning a
  // failure would exit 0 and print nothing, silently losing it. So top level
  // reports the message and exits 1, which is what Rust does for a main that
  // returns an error.
  void genTry(const NodePtr& n){
    const std::string Lok = newLabel("Ltry");
    genExpr(n->kids[0]);
    a.mov_reg(X64Asm::R12, X64Asm::RAX);
    a.mov_load_base(X64Asm::RAX, X64Asm::R12, 0);       // the tag
    a.test(X64Asm::RAX, X64Asm::RAX);
    a.jnz(Lok);
    if(curFnName == ENTRY_LABEL){
      a.mov_load_base(X64Asm::RCX, X64Asm::R12, 8);
      callRuntime("wandaa_result_trap");                // does not return
    } else {
      a.mov_reg(X64Asm::RAX, X64Asm::R12);              // the failure, unchanged
      a.jmp(epilogue);
    }
    a.defineLabel(Lok);
    a.mov_load_base(X64Asm::RAX, X64Asm::R12, 8);
  }

  // ---- bounds checking ---------------------------------------------------
  //
  // Emitted before every indexed read and write. RAX holds the data pointer,
  // RBX the index; the element count sits in the 8-byte header at [ptr-8],
  // the same header ubunini() reads.
  //
  // The comparison is UNSIGNED, so a negative index wraps to a huge value and
  // fails the same test -- one branch covers both ends. On failure control
  // goes to wandaa_bounds_trap, which reports the source line and does not
  // return, so nothing needs saving across it.
  void emitBoundsCheck(){
    const std::string Lok = newLabel("Lbound");
    a.mov_load_base(X64Asm::RCX, X64Asm::RAX, -8);   // RCX = element count
    a.cmp(X64Asm::RBX, X64Asm::RCX);
    a.jb(Lok);                                        // index < count -> fine
    a.mov_reg(X64Asm::RCX, X64Asm::RBX);              // arg 1: the index
    a.mov_load_base(X64Asm::RDX, X64Asm::RAX, -8);    // arg 2: the count
    callRuntime("wandaa_bounds_trap");
    a.defineLabel(Lok);
  }

  void genIndex(const NodePtr& n){
    genExpr(n->kids[0]);
    pushTmp(X64Asm::RAX);
    genExpr(n->kids[1]);
    a.mov_reg(X64Asm::RBX, X64Asm::RAX);
    popTmp(X64Asm::RAX);
    emitBoundsCheck();
    a.mov_load_sib(X64Asm::RAX, X64Asm::RAX, X64Asm::RBX);
  }

  void genIndexAssign(const NodePtr& n){
    genExpr(n->kids[0]);
    pushTmp(X64Asm::RAX);
    genExpr(n->kids[1]);
    a.mov_reg(X64Asm::RBX, X64Asm::RAX);
    popTmp(X64Asm::RAX);
    emitBoundsCheck();
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
      case NT::AddrOf: {
        // Records, arrays and strings are ALREADY heap pointers: the frame
        // slot holds the address, so &r is just r. Taking the address of the
        // slot instead would hand a foreign function a pointer-to-pointer,
        // which is the kind of bug that corrupts memory rather than failing.
        //
        // An integer or float local is stored by value, so its address is the
        // frame slot itself -- that is what an out-parameter like accept()'s
        // addrlen needs.
        const int slot = -curOffset(n->sval);
        VType vt = VType::Int;
        if(cur){
          auto it = cur->types.find(n->sval);
          if(it != cur->types.end()) vt = it->second;
        }
        if(vt == VType::Record || vt == VType::Arr || vt == VType::Str)
          a.mov_load_rbp(X64Asm::RAX, slot);
        else
          a.lea_rbp(X64Asm::RAX, slot);
        break;
      }
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
      case NT::Try:    genTry(n); break;
      case NT::Lambda: genLambda(n); break;
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
    // A name that is a variable in this frame rather than a declared function
    // is a closure. The closure block travels as a hidden first argument, so
    // the lifted body can read its captures out of it.
    if(!callables.count(n->sval) && cur && cur->offset.count(n->sval)){
      std::vector<NodePtr> args;
      auto ctx = std::make_shared<Node>();
      ctx->type = NT::Var; ctx->sval = n->sval; ctx->line = n->line;
      args.push_back(ctx);
      for(auto& k : n->kids) args.push_back(k);
      // Pass the lifted function's parameter types when they are known, so a
      // float argument is coerced and travels in XMM as the callee expects.
      // Indices line up: args[0] is the context, and so is parameter 0.
      const std::vector<VType>* pt = nullptr;
      auto lv = g_varLambda.find(n->sval);
      if(lv != g_varLambda.end()){
        auto f = fnParamTypes.find(lv->second);
        if(f != fnParamTypes.end()) pt = &f->second;
      }
      emitCall(n->sval, args, false, pt, true);
      return;
    }

    // The result builtins are emitted inline: each is a handful of
    // instructions over the two-slot block, with no runtime routine to call
    // except the two traps.
    if(n->sval=="byakunze" || n->sval=="byanze" || n->sval=="byarakunze" ||
       n->sval=="agaciro"  || n->sval=="ikosa"){
      if(n->kids.size() != 1)
        throw std::runtime_error("'" + n->sval + "' isaba igipimo kimwe gusa");
      if(n->sval=="byakunze")        { genResultNew(n, 1); return; }
      if(n->sval=="byanze")          { genResultNew(n, 0); return; }
      if(n->sval=="agaciro")         { genResultUnwrap(n); return; }
      if(n->sval=="ikosa")           { genResultErr(n);    return; }
      genExpr(n->kids[0]);                                 // byarakunze
      a.mov_load_base(X64Asm::RAX, X64Asm::RAX, 0);        // the tag is already 1/0
      return;
    }

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
    const auto git = g_fnGenericPos.find(target);
    emitCall(target, n->kids, /*isImport=*/false,
             pit != fnParamTypes.end() ? &pit->second : nullptr,
             /*indirect=*/false,
             git != g_fnGenericPos.end() ? &git->second : nullptr);
  }

  void genPrint(const NodePtr& n){
    for(auto& arg : n->kids){
      const VType t = inferType(arg);
      genExpr(arg);
      a.mov_reg(X64Asm::RCX, X64Asm::RAX);
      // wandaa_print_float takes the f64 BIT PATTERN in RCX, not in XMM0. It
      // is an internal helper, and passing bits keeps every runtime call site
      // in this file identical.
      callRuntime(t == VType::Str    ? "wandaa_print_strval" :
                  t == VType::Float  ? "wandaa_print_float"  :
                  t == VType::Result ? "wandaa_print_result" :
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

  // Parameter, return, element and payload types written down in the source.
  void applyDeclaredTypes(const std::vector<NodePtr>& funcs,
                          std::unordered_map<std::string,std::vector<VType>>& fnParamTypes){
    for(auto& f : funcs){
      // `T` in `umurimo mbere<T>(a: urutonde<T>): T` names no concrete type --
      // it is bound per call site -- so resolving it here would be an error
      // about a type the programmer never claimed existed.
      auto mentionsTypeParam = [&](const std::string& a){
        if(f->typeParams.empty()) return false;
        const std::string b = typeBase(a), g = typeArg(a);
        for(const auto& tp : f->typeParams) if(tp == b || tp == g) return true;
        return false;
      };

      auto& vec = fnParamTypes[f->sval];
      if(vec.size() < f->params.size()) vec.resize(f->params.size(), VType::Int);
      // Pin every generic position to the integer file. A float already
      // travels in a general-purpose register as its raw bits everywhere else
      // in this language, so nothing is lost, and caller and callee agree
      // without either of them knowing what T is.
      {
        auto gp = g_fnGenericPos.find(f->sval);
        if(gp != g_fnGenericPos.end())
          for(size_t i : gp->second) if(i < vec.size()) vec[i] = VType::Int;
      }
      for(size_t i=0;i<f->paramTypes.size() && i<f->params.size();++i){
        if(f->paramTypes[i].empty()) continue;
        if(mentionsTypeParam(f->paramTypes[i])) continue;
        vec[i] = requireType(f->paramTypes[i],
                             "ku gipimo '" + f->params[i] + "' cya '" + f->sval + "'", f->line);
        g_declaredParamTypes[f->sval][i] = vec[i];
        recordTypeArg(f->paramTypes[i], vec[i], f->params[i], f->line);
      }
      if(f->retType.empty() || mentionsTypeParam(f->retType)) continue;
      const VType rt = requireType(f->retType, "ku bisubizwa na '" + f->sval + "'", f->line);
      g_fnReturnTypes[f->sval] = rt;
      // `urutonde<ijambo>` / `igisubizo<umubare>` also say what the elements or
      // the payload are, which is exactly what inference had to guess before.
      const std::string arg = typeArg(f->retType);
      if(!arg.empty()){
        const VType a = requireType(arg, "ku bisubizwa na '" + f->sval + "'", f->line);
        if(rt == VType::Arr)    g_fnArrElemTypes[f->sval] = a;
        if(rt == VType::Result) g_fnResultPayload[f->sval] = a;
      }
    }
  }

  // A type argument on a NAMED thing (a parameter or a variable) records what
  // its elements or payload are, under that name.
  void genFunction(const std::string& name, const std::vector<std::string>& params, const NodePtr& body){
    // A lifted lambda body: its captures need frame slots of their own, filled
    // from the closure block in the prologue.
    auto capIt = g_lambdaCaptures.find(name);
    const std::vector<std::string>* caps =
        (capIt != g_lambdaCaptures.end()) ? &capIt->second : nullptr;

    FnInfo fi = buildFnInfo(params, body, caps);
    if(caps){
      auto tit = g_lambdaCaptureTypes.find(name);
      if(tit != g_lambdaCaptureTypes.end())
        for(const auto& kv : tit->second) fi.types[kv.first] = kv.second;
    }
    auto pit = fnParamTypes.find(name);
    for(size_t i=0;i<params.size();++i){
      VType pt = (pit!=fnParamTypes.end() && i<pit->second.size()) ? pit->second[i] : VType::Int;
      fi.types[params[i]] = pt;
    }
    collectTypesRec(body, fi.types);
    // Record what a successful result from this function carries, now that
    // fi.types holds the PARAMETER types too. Doing this in collectTypesRec
    // instead was wrong: there a body like `tanga byakunze("mwiriwe " + izina);`
    // could not see that izina was a string, so the payload came out as Int and
    // the caller printed a pointer.
    //
    // User functions are emitted before wandaa_main, so by the time a top-level
    // call site is typed this is populated. One function calling another that is
    // declared LATER in the file does not see it -- the same ordering limit
    // g_fnArrElemTypes has.
    findResultReturnType(body, name, fi.types);
    cur = &fi;
    epilogue = name + "_epilogue";
    curFnName = name;

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

    // Copy the captures out of the closure block into their frame slots. From
    // here on they are indistinguishable from locals.
    if(caps && !caps->empty()){
      const int ctxSlot = -fi.offset.at("__ctx");
      for(size_t i=0;i<caps->size();++i){
        a.mov_load_rbp(X64Asm::RAX, ctxSlot);
        a.mov_load_base(X64Asm::RAX, X64Asm::RAX, (int32_t)(8 + 8*i));
        a.mov_store_rbp(-fi.offset.at((*caps)[i]), X64Asm::RAX);
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
    g_resultPayload.clear();
    g_fnResultPayload.clear();
    g_lambdaCaptureTypes.clear();
    g_declaredParamTypes.clear();
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
    // --- closure lifting --------------------------------------------------
    // Before anything else looks at the tree: every anonymous umurimo becomes
    // a named top-level function, so inference, checking and emission all see
    // ordinary functions and need no special case.
    g_lambdaCaptures.clear();
    g_lifted.clear();
    g_lambdaCounter = 0;
    g_declaredFns.clear();
    callables.clear();
    collectCallableNames(program, callables);
    for(auto& kv : externs) callables.insert(kv.first);
    for(const auto& b : Checker::builtinArity()) callables.insert(b.first);
    callables.insert("andika");
    for(auto& f : funcs) g_declaredFns.insert(f->sval);
    g_varLambda.clear();
    g_fnLambda.clear();
    liftLambdas(program, callables);
    for(auto& L : g_lifted){
      auto fd = mk(NT::FuncDecl);
      fd->sval = L.name;
      fd->params = L.params;
      fd->paramTypes = L.paramTypes;
      fd->retType = L.retType;
      fd->kids.push_back(L.body);
      funcs.push_back(fd);
      callables.insert(L.name);
      g_declaredFns.insert(L.name);
    }
    // Which functions yield closures has to be known before the variables that
    // receive them, so these are two separate walks over the whole program.
    // Generic declarations, recorded before inference so that a call site can
    // resolve one. The annotation text is kept verbatim because matching
    // `urutonde<T>` against an argument needs the shape, not just a VType.
    g_fnTypeParams.clear();
    g_fnParamAnnot.clear();
    g_fnRetAnnot.clear();
    g_fnGenericPos.clear();
    for(auto& f : funcs){
      if(!f->typeParams.empty()) g_fnTypeParams[f->sval] = f->typeParams;
      for(size_t i=0;i<f->paramTypes.size();++i){
        if(f->paramTypes[i].empty()) continue;
        g_fnParamAnnot[f->sval][i] = f->paramTypes[i];
        // A parameter whose type IS the type parameter carries a value of
        // whatever type the caller had. `urutonde<T>` is not one of these: it
        // is an array pointer whatever T turns out to be.
        for(const auto& tp : f->typeParams)
          if(tp == typeBase(f->paramTypes[i]) && typeArg(f->paramTypes[i]).empty())
            g_fnGenericPos[f->sval].insert(i);
      }
      if(!f->retType.empty()) g_fnRetAnnot[f->sval] = f->retType;
    }

    for(auto& f : funcs) noteFnLambda(f->kids[0], f->sval);
    noteLambdaVars(program);
    checkSelfCapture(program);

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
        // A lifted lambda's captures are not parameters, so nothing above puts
        // them in `types`. inferPass over restBlock ran first this iteration
        // and recorded what they meant in the enclosing scope; without this the
        // body of `umurimo() { tanga "mwiriwe " + izina; }` types as Int and
        // the call site prints the concatenated string as a pointer.
        {
          auto tit = g_lambdaCaptureTypes.find(f->sval);
          if(tit != g_lambdaCaptureTypes.end())
            for(const auto& kv : tit->second) types[kv.first] = kv.second;
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

    // Declared types win. Applied after the inference loop so nothing it
    // worked out can overwrite what the programmer actually wrote.
    applyDeclaredTypes(funcs, fnParamTypes);

    // --- 0. semantic checks -----------------------------------------------
    // Runs after inference (so types are known) but before a single byte is
    // emitted, so a bad program fails with a source line instead of building
    // an executable that prints a pointer.
    checkProgram(program, fnParamTypes);

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
