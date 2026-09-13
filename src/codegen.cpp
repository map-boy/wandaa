#include "../include/codegen.hpp"
#include <unordered_map>
#include <sstream>
#include <stdexcept>

namespace {

enum class VType { Int, Str, Arr };

std::unordered_map<std::string,VType> g_fnReturnTypes;
std::unordered_map<std::string,VType> g_arrElemTypes;

struct FnInfo {
  std::unordered_map<std::string,int> offset;
  std::unordered_map<std::string,VType> types;
  int frameSize = 0;
};

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

std::string escape(const std::string& s){
  std::string o;
  for(char c : s){
    if(c=='"' || c=='\\') o += '\\';
    o += c;
  }
  return o;
}

struct Codegen {
  std::ostringstream text;
  std::vector<std::string> strings;
  FnInfo* cur = nullptr;
  int labelCounter = 0;
  std::string epilogue;
  std::unordered_map<std::string,std::vector<VType>> fnParamTypes;

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

  void genArrayLit(const NodePtr& n){
    size_t count = n->kids.size();
    text << "  sub rsp, 32\n  call GetProcessHeap\n  add rsp, 32\n";
    text << "  mov rcx, rax\n  xor rdx, rdx\n  mov r8, " << (8 + count*8) << "\n";
    text << "  sub rsp, 32\n  call HeapAlloc\n  add rsp, 32\n";
    text << "  mov r12, rax\n";
    text << "  mov qword ptr [r12], " << count << "\n";
    for(size_t i=0;i<count;++i){
      genExpr(n->kids[i]);
      text << "  mov [r12+8+" << (i*8) << "], rax\n";
    }
    text << "  lea rax, [r12+8]\n";
  }

  void genIndex(const NodePtr& n){
    genExpr(n->kids[0]);
    text << "  push rax\n";
    genExpr(n->kids[1]);
    text << "  mov rbx, rax\n  pop rax\n";
    text << "  mov rax, [rax+rbx*8]\n";
  }

  void genIndexAssign(const NodePtr& n){
    genExpr(n->kids[0]);
    text << "  push rax\n";
    genExpr(n->kids[1]);
    text << "  mov rbx, rax\n  pop rax\n";
    text << "  lea rax, [rax+rbx*8]\n  push rax\n";
    genExpr(n->kids[2]);
    text << "  pop rcx\n  mov [rcx], rax\n";
  }

  void genExpr(const NodePtr& n){
    switch(n->type){
      case NT::Num: text << "  mov rax, " << (long long)n->nval << "\n"; break;
      case NT::Bool: text << "  mov rax, " << (n->bval?1:0) << "\n"; break;
      case NT::Str: {
        std::string lbl = addStringLiteral(n->sval);
        text << "  lea rax, [rip+" << lbl << "]\n";
        break;
      }
      case NT::ArrayLit: genArrayLit(n); break;
      case NT::Index: genIndex(n); break;
      case NT::IndexAssign: genIndexAssign(n); break;
      case NT::Var: text << "  mov rax, [rbp-" << curOffset(n->sval) << "]\n"; break;
      case NT::Un: genExpr(n->kids[0]); text << "  neg rax\n"; break;
      case NT::Assign: {
        genExpr(n->kids[0]);
        text << "  mov [rbp-" << curOffset(n->sval) << "], rax\n";
        break;
      }
      case NT::Bin: {
        bool bothStr = inferType(n->kids[0])==VType::Str && inferType(n->kids[1])==VType::Str;
        if(n->sval=="+" && bothStr){
          genExpr(n->kids[0]);
          text << "  push rax\n";
          genExpr(n->kids[1]);
          text << "  mov rdx, rax\n  pop rcx\n";
          text << "  sub rsp, 32\n  call wandaa_str_concat\n  add rsp, 32\n";
          break;
        }
        if((n->sval=="==" || n->sval=="!=") && bothStr){
          genExpr(n->kids[0]);
          text << "  push rax\n";
          genExpr(n->kids[1]);
          text << "  mov rdx, rax\n  pop rcx\n";
          text << "  sub rsp, 32\n  call wandaa_str_eq\n  add rsp, 32\n";
          if(n->sval=="!=") text << "  xor rax, 1\n";
          break;
        }
        genExpr(n->kids[0]);
        text << "  push rax\n";
        genExpr(n->kids[1]);
        text << "  mov rbx, rax\n  pop rax\n";
        const std::string& op = n->sval;
        if(op=="+") text << "  add rax, rbx\n";
        else if(op=="-") text << "  sub rax, rbx\n";
        else if(op=="*") text << "  imul rax, rbx\n";
        else if(op=="/") text << "  cqo\n  idiv rbx\n";
        else {
          text << "  cmp rax, rbx\n";
          std::string setcc = op=="<" ? "setl" : op==">" ? "setg" :
                               op=="<=" ? "setle" : op==">=" ? "setge" :
                               op=="==" ? "sete" : "setne";
          text << "  " << setcc << " al\n  movzx rax, al\n";
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
      {"ubunini", "wandaa_str_len"},
      {"soma", "wandaa_read_file"},
      {"andikamo", "wandaa_write_file"}
    };
    std::string target = n->sval;
    auto bit = builtins.find(target);
    if(bit != builtins.end()) target = bit->second;
    if(n->kids.size() > 4) throw std::runtime_error("umurimo '" + n->sval + "' ufite parametero nyinshi (max 4)");
    static const char* regs[4] = {"rcx","rdx","r8","r9"};
    for(auto& a : n->kids){ genExpr(a); text << "  push rax\n"; }
    for(int i=(int)n->kids.size()-1; i>=0; --i) text << "  pop " << regs[i] << "\n";
    text << "  sub rsp, 32\n  call " << target << "\n  add rsp, 32\n";
  }

  void genPrint(const NodePtr& n){
    for(auto& a : n->kids){
      if(inferType(a) == VType::Str){
        genExpr(a);
        text << "  mov rcx, rax\n";
        text << "  sub rsp, 32\n  call wandaa_print_strval\n  add rsp, 32\n";
      } else {
        genExpr(a);
        text << "  mov rcx, rax\n";
        text << "  sub rsp, 32\n  call wandaa_print_int\n  add rsp, 32\n";
      }
    }
  }

  void genStmt(const NodePtr& n){
    if(n->line > 0) text << "  mov qword ptr [rip+wandaa_current_line], " << n->line << "\n";
    switch(n->type){
      case NT::VarDecl: genExpr(n->kids[0]); text << "  mov [rbp-" << curOffset(n->sval) << "], rax\n"; break;
      case NT::ExprStmt: genExpr(n->kids[0]); break;
      case NT::Print: genPrint(n); break;
      case NT::If: {
        std::string Lelse = newLabel("Lelse"), Lend = newLabel("Lend");
        genExpr(n->kids[0]);
        text << "  test rax, rax\n  jz " << Lelse << "\n";
        genStmt(n->kids[1]);
        text << "  jmp " << Lend << "\n" << Lelse << ":\n";
        if(n->kids.size() > 2) genStmt(n->kids[2]);
        text << Lend << ":\n";
        break;
      }
      case NT::While: {
        std::string Lstart = newLabel("Lstart"), Lend = newLabel("Lend");
        text << Lstart << ":\n";
        genExpr(n->kids[0]);
        text << "  test rax, rax\n  jz " << Lend << "\n";
        genStmt(n->kids[1]);
        text << "  jmp " << Lstart << "\n" << Lend << ":\n";
        break;
      }
      case NT::Block: for(auto& k : n->kids) genStmt(k); break;
      case NT::Return: {
        if(!n->kids.empty()) genExpr(n->kids[0]);
        else text << "  xor eax, eax\n";
        text << "  jmp " << epilogue << "\n";
        break;
      }
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
    text << "\n" << name << ":\n  push rbp\n  mov rbp, rsp\n";
    if(fi.frameSize > 0) text << "  sub rsp, " << fi.frameSize << "\n";
    static const char* regs[4] = {"rcx","rdx","r8","r9"};
    for(size_t i=0;i<params.size();++i)
      text << "  mov [rbp-" << fi.offset.at(params[i]) << "], " << regs[i] << "\n";
    genStmt(body);
    text << "  xor eax, eax\n" << epilogue << ":\n";
    if(fi.frameSize > 0) text << "  add rsp, " << fi.frameSize << "\n";
    text << "  pop rbp\n  ret\n";
  }

  std::string generate(const NodePtr& program){
    g_fnReturnTypes["soma"] = VType::Str;

    std::vector<NodePtr> funcs, rest;
    for(auto& k : program->kids){
      if(k->type==NT::FuncDecl) funcs.push_back(k);
      else rest.push_back(k);
    }

    auto restBlock = mk(NT::Block);
    restBlock->kids = rest;

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

    for(auto& f : funcs) genFunction(f->sval, f->params, f->kids[0]);
    genFunction("main", {}, restBlock);

    std::ostringstream data;
    data << ".data\n";
    for(size_t i=0;i<strings.size();++i){
      data << ".align 8\n";
      data << "str_" << i << "_hdr: .quad " << strings[i].size() << "\n";
      data << "str_" << i << ": .ascii \"" << escape(strings[i]) << "\"\n";
      data << "  .byte 0\n";
    }
    data << ".align 8\nempty_str_hdr: .quad 0\nempty_str:\nnl_char: .byte 10\n";
    data << "crash_msg: .ascii \"Ikosa ku murongo: \"\ncrash_msg_len = . - crash_msg\n";

    std::ostringstream bss;
    bss << ".bss\n.lcomm hStdOut, 8\n.lcomm bytesWritten, 8\n.lcomm intbuf, 32\n.lcomm wandaa_current_line, 8\n";

    std::ostringstream rt2;
    rt2 << "\n"
       << "init_stdout:\n"
       << "  push rbp\n  mov rbp, rsp\n  sub rsp, 32\n"
       << "  mov ecx, -11\n  call GetStdHandle\n"
       << "  mov [rip+hStdOut], rax\n"
       << "  add rsp, 32\n  pop rbp\n  ret\n\n"
       << "wandaa_print_str:\n"
       << "  push rbp\n  mov rbp, rsp\n  sub rsp, 48\n"
       << "  mov r8d, edx\n  mov rdx, rcx\n  mov rcx, [rip+hStdOut]\n"
       << "  lea r9, [rip+bytesWritten]\n"
       << "  mov qword ptr [rsp+32], 0\n"
       << "  call WriteFile\n"
       << "  add rsp, 48\n  pop rbp\n  ret\n\n"
       << "wandaa_crash_handler:\n"
       << "  push rbp\n  mov rbp, rsp\n"
       << "  lea rcx, [rip+crash_msg]\n  mov edx, crash_msg_len\n"
       << "  sub rsp, 32\n  call wandaa_print_str\n  add rsp, 32\n"
       << "  mov rcx, [rip+wandaa_current_line]\n"
       << "  sub rsp, 32\n  call wandaa_print_int\n  add rsp, 32\n"
       << "  mov ecx, 1\n"
       << "  sub rsp, 32\n  call ExitProcess\n"
       << "  pop rbp\n  ret\n\n"
       << "wandaa_print_int:\n"
       << "  push rbp\n  mov rbp, rsp\n  sub rsp, 48\n"
       << "  push rbx\n"
       << "  sub rsp, 8\n"
       << "  mov rax, rcx\n"
       << "  xor r10, r10\n  xor r11, r11\n"
       << "  cmp rax, 0\n  jge pi_pos\n"
       << "  mov r11, 1\n  neg rax\n"
       << "pi_pos:\n"
       << "  lea rbx, [rip+intbuf]\n  add rbx, 30\n"
       << "  mov byte ptr [rbx], 10\n"
       << "pi_loop:\n"
       << "  xor rdx, rdx\n  mov rcx, 10\n  div rcx\n"
       << "  add dl, 48\n"
       << "  dec rbx\n  mov [rbx], dl\n"
       << "  inc r10\n"
       << "  cmp rax, 0\n  jne pi_loop\n"
       << "  cmp r11, 0\n  je pi_nosign\n"
       << "  dec rbx\n  mov byte ptr [rbx], 45\n  inc r10\n"
       << "pi_nosign:\n"
       << "  mov rcx, rbx\n  mov rdx, r10\n  inc rdx\n"
       << "  call wandaa_print_str\n"
       << "  add rsp, 8\n"
       << "  pop rbx\n"
       << "  add rsp, 48\n  pop rbp\n  ret\n\n"
       << "wandaa_str_len:\n"
       << "  mov rax, [rcx-8]\n"
       << "  ret\n\n"
       << "wandaa_print_strval:\n"
       << "  push rbp\n  mov rbp, rsp\n  sub rsp, 48\n"
       << "  push rbx\n"
       << "  sub rsp, 8\n"
       << "  mov rbx, rcx\n"
       << "  mov rdx, [rbx-8]\n"
       << "  mov rcx, rbx\n"
       << "  call wandaa_print_str\n"
       << "  lea rcx, [rip+nl_char]\n"
       << "  mov rdx, 1\n"
       << "  call wandaa_print_str\n"
       << "  add rsp, 8\n"
       << "  pop rbx\n"
       << "  add rsp, 48\n  pop rbp\n  ret\n\n"
       << "wandaa_str_concat:\n"
       << "  push rbp\n  mov rbp, rsp\n  sub rsp, 64\n"
       << "  push rbx\n  push rsi\n  push rdi\n  push r12\n  push r13\n  push r14\n  push r15\n"
       << "  sub rsp, 8\n"
       << "  mov r12, rcx\n"
       << "  mov r13, rdx\n"
       << "  mov rax, [r12-8]\n"
       << "  mov rbx, rax\n"
       << "  add rbx, [r13-8]\n"
       << "  sub rsp, 32\n  call GetProcessHeap\n  add rsp, 32\n"
       << "  mov r14, rax\n"
       << "  mov rcx, r14\n"
       << "  xor rdx, rdx\n"
       << "  lea r8, [rbx+9]\n"
       << "  sub rsp, 32\n  call HeapAlloc\n  add rsp, 32\n"
       << "  mov r15, rax\n"
       << "  mov [r15], rbx\n"
       << "  mov rsi, r12\n"
       << "  lea rdi, [r15+8]\n"
       << "  mov rcx, [r12-8]\n"
       << "  rep movsb\n"
       << "  mov rsi, r13\n"
       << "  mov rcx, [r13-8]\n"
       << "  rep movsb\n"
       << "  mov byte ptr [rdi], 0\n"
       << "  lea rax, [r15+8]\n"
       << "  add rsp, 8\n"
       << "  pop r15\n  pop r14\n  pop r13\n  pop r12\n  pop rdi\n  pop rsi\n  pop rbx\n"
       << "  add rsp, 64\n  pop rbp\n  ret\n\n"
       << "wandaa_str_eq:\n"
       << "  push rbx\n  push rsi\n  push rdi\n"
       << "  mov rax, [rcx-8]\n"
       << "  cmp rax, [rdx-8]\n"
       << "  jne streq_false\n"
       << "  mov rsi, rcx\n  mov rdi, rdx\n  mov rcx, rax\n"
       << "  repe cmpsb\n"
       << "  jne streq_false\n"
       << "  mov rax, 1\n  jmp streq_done\n"
       << "streq_false:\n"
       << "  xor rax, rax\n"
       << "streq_done:\n"
       << "  pop rdi\n  pop rsi\n  pop rbx\n"
       << "  ret\n\n"
       << "wandaa_read_file:\n"
       << "  push rbp\n  mov rbp, rsp\n  sub rsp, 64\n"
       << "  push rbx\n  push r12\n  push r13\n\n"
       << "  sub rsp, 8\n"
       << "  mov r12, rcx\n"
       << "  mov rcx, r12\n"
       << "  mov edx, 0x80000000\n"
       << "  mov r8, 1\n"
       << "  xor r9, r9\n"
       << "  mov qword ptr [rsp+32], 3\n"
       << "  mov qword ptr [rsp+40], 0x80\n"
       << "  mov qword ptr [rsp+48], 0\n"
       << "  call CreateFileA\n"
       << "  cmp rax, -1\n"
       << "  je rf_fail\n"
       << "  mov rbx, rax\n"
       << "  mov rcx, rbx\n"
       << "  xor rdx, rdx\n"
       << "  call GetFileSize\n"
       << "  mov r13, rax\n"
       << "  call GetProcessHeap\n"
       << "  mov rcx, rax\n"
       << "  xor rdx, rdx\n"
       << "  lea r8, [r13+9]\n"
       << "  call HeapAlloc\n"
       << "  mov [rax], r13\n"
       << "  mov r12, rax\n"
       << "  mov rcx, rbx\n"
       << "  lea rdx, [r12+8]\n"
       << "  mov r8, r13\n"
       << "  lea r9, [rip+bytesWritten]\n"
       << "  mov qword ptr [rsp+32], 0\n"
       << "  call ReadFile\n"
       << "  test eax, eax\n"
       << "  jnz rf_readok\n"
       << "  call GetLastError\n"
       << "  mov rcx, rax\n"
       << "  sub rsp, 32\n  call wandaa_print_int\n  add rsp, 32\n"
       << "rf_readok:\n"
       << "  mov rcx, rbx\n"
       << "  call CloseHandle\n"
       << "  lea rbx, [r12+8]\n"
       << "  add rbx, r13\n"
       << "  mov byte ptr [rbx], 0\n"
       << "  lea rax, [r12+8]\n"
       << "  jmp rf_done\n"
       << "rf_fail:\n"
       << "  lea rax, [rip+empty_str]\n"
       << "rf_done:\n"
       << "  add rsp, 8\n"
       << "  pop r13\n  pop r12\n  pop rbx\n"
       << "  add rsp, 64\n  pop rbp\n  ret\n\n"
       << "wandaa_write_file:\n"
       << "  push rbp\n  mov rbp, rsp\n  sub rsp, 64\n"
       << "  push rbx\n  push r12\n  push r13\n"
       << "  sub rsp, 8\n"
       << "  mov r12, rcx\n"
       << "  mov r13, rdx\n"
       << "  mov rcx, r12\n"
       << "  mov edx, 0x40000000\n"
       << "  xor r8, r8\n"
       << "  xor r9, r9\n"
       << "  mov qword ptr [rsp+32], 2\n"
       << "  mov qword ptr [rsp+40], 0x80\n"
       << "  mov qword ptr [rsp+48], 0\n"
       << "  call CreateFileA\n"
       << "  cmp rax, -1\n"
       << "  je wf_fail\n"
       << "  mov rbx, rax\n"
       << "  mov rcx, rbx\n"
       << "  mov rdx, r13\n"
       << "  mov r8, [r13-8]\n"
       << "  lea r9, [rip+bytesWritten]\n"
       << "  mov qword ptr [rsp+32], 0\n"
       << "  call WriteFile\n"
       << "  mov rcx, rbx\n"
       << "  call CloseHandle\n"
       << "  mov rax, 1\n"
       << "  jmp wf_done\n"
       << "wf_fail:\n"
       << "  xor rax, rax\n"
       << "wf_done:\n"
       << "  add rsp, 8\n"
       << "  pop r13\n  pop r12\n  pop rbx\n"
       << "  add rsp, 64\n  pop rbp\n  ret\n\n"
       << "_start:\n"
       << "  sub rsp, 40\n  call init_stdout\n  add rsp, 40\n"
       << "  sub rsp, 40\n  mov rcx, 1\n  lea rdx, [rip+wandaa_crash_handler]\n  call AddVectoredExceptionHandler\n  add rsp, 40\n"
       << "  sub rsp, 40\n  call main\n  add rsp, 40\n"
       << "  mov ecx, eax\n"
       << "  sub rsp, 40\n  call ExitProcess\n"
       << "  hlt\n";

    std::ostringstream full;
    full << ".intel_syntax noprefix\n"
         << data.str() << bss.str()
         << ".text\n.globl _start\n"
         << ".extern WriteFile\n.extern GetStdHandle\n.extern ExitProcess\n.extern GetProcessHeap\n.extern HeapAlloc\n"
         << ".extern CreateFileA\n.extern GetFileSize\n.extern ReadFile\n.extern CloseHandle\n.extern GetLastError\n.extern AddVectoredExceptionHandler\n"
         << text.str() << rt2.str();
    return full.str();
  }
};

}

std::string generateAsm(const NodePtr& program){
  Codegen cg;
  return cg.generate(program);
}
