#pragma once
#include <string>
#include <vector>
#include <memory>

enum class NT {
  Num, Str, Bool, Var, Bin, Un, Assign, VarDecl, Call,
  Block, If, While, FuncDecl, Return, Print, ExprStmt, Program,
  ArrayLit, Index, IndexAssign
};

struct Node;
using NodePtr = std::shared_ptr<Node>;

struct Node {
  NT type;
  std::string sval;
  double nval = 0;
  bool bval = false;
  std::vector<NodePtr> kids;
  std::vector<std::string> params;
  int line = 0;
};

inline NodePtr mk(NT t){ auto n = std::make_shared<Node>(); n->type = t; return n; }
