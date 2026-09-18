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
  int line = 0;
};

inline NodePtr mk(NT t){ auto n = std::make_shared<Node>(); n->type = t; return n; }
