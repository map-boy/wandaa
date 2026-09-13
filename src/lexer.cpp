#include "../include/lexer.hpp"
#include <unordered_map>
#include <cctype>
#include <stdexcept>

static const std::unordered_map<std::string, Tok> KW = {
  {"reka",Tok::REKA},{"niba",Tok::NIBA},{"ubundi",Tok::UBUNDI},
  {"mugihe",Tok::MUGIHE},{"umurimo",Tok::UMURIMO},{"tanga",Tok::TANGA},
  {"andika",Tok::ANDIKA},{"nibyo",Tok::NIBYO},{"oya",Tok::OYA}
};

std::vector<Token> tokenize(const std::string& s){
  size_t bomSkip = 0;
  if(s.size()>=3 && (unsigned char)s[0]==0xEF && (unsigned char)s[1]==0xBB && (unsigned char)s[2]==0xBF) bomSkip = 3;
  std::vector<Token> out;
  size_t i=bomSkip, n=s.size(); int line=1;
  auto peek=[&](int o=0)->char{ return i+o<n ? s[i+o] : '\0'; };
  while(i<n){
    char c=s[i];
    if(c=='\n'){ line++; i++; continue; }
    if(isspace((unsigned char)c)){ i++; continue; }
    if(c=='#'){ while(i<n && s[i]!='\n') i++; continue; }
    if(isdigit((unsigned char)c)){
      size_t start=i;
      while(i<n && (isdigit((unsigned char)s[i])||s[i]=='.')) i++;
      out.push_back({Tok::NUM, s.substr(start,i-start), std::stod(s.substr(start,i-start)), line});
      continue;
    }
    if(isalpha((unsigned char)c)||c=='_'){
      size_t start=i;
      while(i<n && (isalnum((unsigned char)s[i])||s[i]=='_')) i++;
      std::string word=s.substr(start,i-start);
      auto it=KW.find(word);
      Tok tt = it!=KW.end()? it->second : Tok::IDENT;
      out.push_back({tt,word,0,line}); continue;
    }
    if(c=='"'){
      i++; size_t start=i;
      while(i<n && s[i]!='"') i++;
      std::string val=s.substr(start,i-start);
      i++; out.push_back({Tok::STR,val,0,line}); continue;
    }
    switch(c){
      case '+': out.push_back({Tok::PLUS,"+",0,line}); i++; break;
      case '-': out.push_back({Tok::MINUS,"-",0,line}); i++; break;
      case '*': out.push_back({Tok::STAR,"*",0,line}); i++; break;
      case '/': out.push_back({Tok::SLASH,"/",0,line}); i++; break;
      case '(': out.push_back({Tok::LPAREN,"(",0,line}); i++; break;
      case ')': out.push_back({Tok::RPAREN,")",0,line}); i++; break;
      case '{': out.push_back({Tok::LBRACE,"{",0,line}); i++; break;
      case '}': out.push_back({Tok::RBRACE,"}",0,line}); i++; break;
      case '[': out.push_back({Tok::LBRACKET,"[",0,line}); i++; break;
      case ']': out.push_back({Tok::RBRACKET,"]",0,line}); i++; break;
      case ',': out.push_back({Tok::COMMA,",",0,line}); i++; break;
      case ';': out.push_back({Tok::SEMI,";",0,line}); i++; break;
      case '=':
        i++; if(peek()=='='){ out.push_back({Tok::EQEQ,"==",0,line}); i++; }
        else out.push_back({Tok::EQ,"=",0,line});
        break;
      case '!':
        i++; if(peek()=='='){ out.push_back({Tok::NEQ,"!=",0,line}); i++; }
        else throw std::runtime_error("ikimenyetso kitazwi: !");
        break;
      case '<':
        i++; if(peek()=='='){ out.push_back({Tok::LE,"<=",0,line}); i++; }
        else out.push_back({Tok::LT,"<",0,line});
        break;
      case '>':
        i++; if(peek()=='='){ out.push_back({Tok::GE,">=",0,line}); i++; }
        else out.push_back({Tok::GT,">",0,line});
        break;
      default:
        throw std::runtime_error("ikimenyetso kitazwi kuri umurongo "+std::to_string(line));
    }
  }
  out.push_back({Tok::END,"",0,line});
  return out;
}
