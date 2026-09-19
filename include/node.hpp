#pragma once
#include <string>
#include <vector>
#include <memory>

enum class NT {
  Num, Str, Bool, Var, Bin, Un, Assign, VarDecl, Call,
  Block, If, While, FuncDecl, Return, Print, ExprStmt, Program,
  ArrayLit, Index, IndexAssign, ExternDecl, Break, Continue,
  RecordDecl, RecordLit, FieldAccess, FieldAssign, AddrOf,
  Try,                      // postfix `?` -- unwrap a result or propagate its error
  Lambda                    // anonymous `umurimo (...) { ... }`, capturing by value
};

struct Node;
using NodePtr = std::shared_ptr<Node>;

struct Node {
  NT type;
  std::string sval;
  // Secondary string payload. For ExternDecl this is the DLL name.
  std::string sval2;
  double nval = 0;
  // A NUM literal written with a decimal point. The lexer already parses every
  // number as a double, so without this flag `3.14` and `3` are
  // indistinguishable in the AST and both silently become integers.
  bool isFloat = false;
  bool bval = false;
  std::vector<NodePtr> kids;
  std::vector<std::string> params;
  // Per-field byte width for RecordDecl, parallel to params. 0 means not
  // specified (defaults to 8 bytes), so old-style records still work.
  std::vector<int> widths;
  // Declared types, parallel to params: parameters of a FuncDecl or Lambda,
  // fields of a RecordDecl. An empty string means the type was not written
  // down, so inference works it out exactly as it did before -- annotations
  // are optional everywhere and every existing .waa file keeps compiling.
  std::vector<std::string> paramTypes;
  // Declared return type of a FuncDecl or Lambda, or the declared type of a
  // VarDecl. Empty when not annotated. Held as text (e.g. "urutonde<ijambo>")
  // because VType lives in codegen, not in the AST.
  std::string retType;
  // Type parameters of a generic FuncDecl: `umurimo mbere<T>(...)`. Empty for
  // an ordinary function. A name listed here is NOT a concrete type -- it is
  // bound to one at each call site.
  std::vector<std::string> typeParams;
  int line = 0;
};

inline NodePtr mk(NT t){ auto n = std::make_shared<Node>(); n->type = t; return n; }
