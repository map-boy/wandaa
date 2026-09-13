#include "../include/parser.hpp"
#include <stdexcept>

namespace {
struct Parser {
  const std::vector<Token>& t; size_t pos=0;
  Parser(const std::vector<Token>& toks):t(toks){}
  Token& cur(){ return const_cast<Token&>(t[pos]); }
  bool check(Tok tt){ return cur().type==tt; }
  Token advance(){ return t[pos++]; }
  Token expect(Tok tt, const std::string& msg){
    if(!check(tt)) throw std::runtime_error("Ikosa: "+msg+" ku murongo "+std::to_string(cur().line));
    return advance();
  }

  NodePtr program(){
    auto n=mk(NT::Program);
    while(!check(Tok::END)) n->kids.push_back(statement());
    return n;
  }

  NodePtr block(){
    expect(Tok::LBRACE,"twari dutegereje '{'");
    auto n=mk(NT::Block);
    while(!check(Tok::RBRACE)) n->kids.push_back(statement());
    expect(Tok::RBRACE,"twari dutegereje '}'");
    return n;
  }

  NodePtr statement(){
    int __ln = cur().line;
    NodePtr __n;
    if(check(Tok::REKA)) __n = varDecl();
    else if(check(Tok::NIBA)) __n = ifStmt();
    else if(check(Tok::MUGIHE)) __n = whileStmt();
    else if(check(Tok::UMURIMO)) __n = funcDecl();
    else if(check(Tok::TANGA)) __n = returnStmt();
    else if(check(Tok::ANDIKA)) __n = printStmt();
    else if(check(Tok::LBRACE)) __n = block();
    else {
      auto e = expression();
      expect(Tok::SEMI,"twari dutegereje ';'");
      __n = mk(NT::ExprStmt); __n->kids.push_back(e);
    }
    if(__n) __n->line = __ln;
    return __n;
  }

  NodePtr varDecl(){
    advance();
    std::string name = expect(Tok::IDENT,"izina ry'ikigereranyo").text;
    expect(Tok::EQ,"'='");
    auto val = expression();
    expect(Tok::SEMI,"';'");
    auto n=mk(NT::VarDecl); n->sval=name; n->kids.push_back(val); return n;
  }

  NodePtr ifStmt(){
    advance();
    expect(Tok::LPAREN,"'('");
    auto cond=expression();
    expect(Tok::RPAREN,"')'");
    auto thenB = block();
    NodePtr elseB=nullptr;
    if(check(Tok::UBUNDI)){ advance(); elseB = check(Tok::NIBA)? statement() : block(); }
    auto n=mk(NT::If); n->kids.push_back(cond); n->kids.push_back(thenB);
    if(elseB) n->kids.push_back(elseB);
    return n;
  }

  NodePtr whileStmt(){
    advance();
    expect(Tok::LPAREN,"'('");
    auto cond=expression();
    expect(Tok::RPAREN,"')'");
    auto body=block();
    auto n=mk(NT::While); n->kids.push_back(cond); n->kids.push_back(body);
    return n;
  }

  NodePtr funcDecl(){
    advance();
    std::string name = expect(Tok::IDENT,"izina ry'umurimo").text;
    expect(Tok::LPAREN,"'('");
    auto n=mk(NT::FuncDecl); n->sval=name;
    if(!check(Tok::RPAREN)){
      n->params.push_back(expect(Tok::IDENT,"parameter").text);
      while(check(Tok::COMMA)){ advance(); n->params.push_back(expect(Tok::IDENT,"parameter").text); }
    }
    expect(Tok::RPAREN,"')'");
    n->kids.push_back(block());
    return n;
  }

  NodePtr returnStmt(){
    advance();
    auto n=mk(NT::Return);
    if(!check(Tok::SEMI)) n->kids.push_back(expression());
    expect(Tok::SEMI,"';'");
    return n;
  }

  NodePtr printStmt(){
    advance();
    expect(Tok::LPAREN,"'('");
    auto n=mk(NT::Print);
    if(!check(Tok::RPAREN)){
      n->kids.push_back(expression());
      while(check(Tok::COMMA)){ advance(); n->kids.push_back(expression()); }
    }
    expect(Tok::RPAREN,"')'");
    expect(Tok::SEMI,"';'");
    return n;
  }

  NodePtr expression(){ return assignment(); }

  NodePtr assignment(){
    auto left = equality();
    if(check(Tok::EQ)){
      advance();
      auto val = assignment();
      if(left->type==NT::Var){
        auto n=mk(NT::Assign); n->sval=left->sval; n->kids.push_back(val); return n;
      }
      if(left->type==NT::Index){
        auto n=mk(NT::IndexAssign);
        n->kids.push_back(left->kids[0]);
        n->kids.push_back(left->kids[1]);
        n->kids.push_back(val);
        return n;
      }
      throw std::runtime_error("aho ushyira agaciro ntibyemewe");
    }
    return left;
  }

  NodePtr equality(){
    auto n = comparison();
    while(check(Tok::EQEQ)||check(Tok::NEQ)){
      std::string op=advance().text;
      auto r=comparison();
      auto b=mk(NT::Bin); b->sval=op; b->kids={n,r}; n=b;
    }
    return n;
  }

  NodePtr comparison(){
    auto n = term();
    while(check(Tok::LT)||check(Tok::GT)||check(Tok::LE)||check(Tok::GE)){
      std::string op=advance().text;
      auto r=term();
      auto b=mk(NT::Bin); b->sval=op; b->kids={n,r}; n=b;
    }
    return n;
  }

  NodePtr term(){
    auto n = factor();
    while(check(Tok::PLUS)||check(Tok::MINUS)){
      std::string op=advance().text;
      auto r=factor();
      auto b=mk(NT::Bin); b->sval=op; b->kids={n,r}; n=b;
    }
    return n;
  }

  NodePtr factor(){
    auto n = unary();
    while(check(Tok::STAR)||check(Tok::SLASH)){
      std::string op=advance().text;
      auto r=unary();
      auto b=mk(NT::Bin); b->sval=op; b->kids={n,r}; n=b;
    }
    return n;
  }

  NodePtr unary(){
    if(check(Tok::MINUS)){
      advance(); auto r=unary();
      auto n=mk(NT::Un); n->sval="-"; n->kids.push_back(r); return n;
    }
    return call();
  }

  NodePtr call(){
    auto n = primary();
    if(n->type==NT::Var && check(Tok::LPAREN)){
      advance();
      auto c=mk(NT::Call); c->sval=n->sval;
      if(!check(Tok::RPAREN)){
        c->kids.push_back(expression());
        while(check(Tok::COMMA)){ advance(); c->kids.push_back(expression()); }
      }
      expect(Tok::RPAREN,"')'");
      n = c;
    }
    while(check(Tok::LBRACKET)){
      advance();
      auto idx = expression();
      expect(Tok::RBRACKET,"']'");
      auto ix = mk(NT::Index);
      ix->kids.push_back(n);
      ix->kids.push_back(idx);
      n = ix;
    }
    return n;
  }

  NodePtr primary(){
    if(check(Tok::NUM)){ auto tk=advance(); auto n=mk(NT::Num); n->nval=tk.num; return n; }
    if(check(Tok::STR)){ auto tk=advance(); auto n=mk(NT::Str); n->sval=tk.text; return n; }
    if(check(Tok::NIBYO)){ advance(); auto n=mk(NT::Bool); n->bval=true; return n; }
    if(check(Tok::OYA)){ advance(); auto n=mk(NT::Bool); n->bval=false; return n; }
    if(check(Tok::IDENT)){ auto tk=advance(); auto n=mk(NT::Var); n->sval=tk.text; return n; }
    if(check(Tok::LPAREN)){ advance(); auto e=expression(); expect(Tok::RPAREN,"')'"); return e; }
    if(check(Tok::LBRACKET)){
      advance();
      auto n=mk(NT::ArrayLit);
      if(!check(Tok::RBRACKET)){
        n->kids.push_back(expression());
        while(check(Tok::COMMA)){ advance(); n->kids.push_back(expression()); }
      }
      expect(Tok::RBRACKET,"']'");
      return n;
    }
    throw std::runtime_error("ikintu kitazwi ku murongo "+std::to_string(cur().line));
  }
};
}

NodePtr parse(const std::vector<Token>& toks){
  Parser p(toks);
  return p.program();
}
