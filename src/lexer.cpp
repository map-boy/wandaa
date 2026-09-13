#include "../include/lexer.hpp"
#include <unordered_map>
#include <cctype>
#include <stdexcept>

static const std::unordered_map<std::string, Tok> KW = {
  {"reka",Tok::REKA},{"niba",Tok::NIBA},{"ubundi",Tok::UBUNDI},
  {"mugihe",Tok::MUGIHE},{"umurimo",Tok::UMURIMO},{"tanga",Tok::TANGA},
  {"andika",Tok::ANDIKA},{"nibyo",Tok::NIBYO},{"oya",Tok::OYA},
  {"hanze",Tok::HANZE},{"injiza",Tok::INJIZA},
  {"na",Tok::NA},{"cyangwa",Tok::CYANGWA},{"si",Tok::SI},
  {"hagarika",Tok::HAGARIKA},{"komeza",Tok::KOMEZA}
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
      int dots=0;
      while(i<n && (isdigit((unsigned char)s[i]) || s[i]=='.')){
        if(s[i]=='.') dots++;
        i++;
      }
      const std::string lit = s.substr(start, i-start);
      if(dots > 1)
        throw std::runtime_error("umubare utaremewe '"+lit+"' ku murongo "+std::to_string(line));
      if(dots == 1 && lit.back() == '.')
        throw std::runtime_error("umubare ukeneye ibice nyuma ya '.' : '"+lit+"' ku murongo "+std::to_string(line));
      out.push_back({Tok::NUM, lit, std::stod(lit), line, dots == 1});
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
      // String literal with C-style escapes. Without these a program cannot
      // contain a quote, a backslash, a tab or a newline.
      i++;
      std::string val;
      while(i<n && s[i]!='"'){
        if(s[i]=='\\' && i+1<n){
          const char e = s[i+1];
          switch(e){
            case 'n':  val += '\n';  i+=2; break;
            case 't':  val += '\t';  i+=2; break;
            case 'r':  val += '\r';  i+=2; break;
            case '0':  val += '\0';  i+=2; break;
            case '\\': val += '\\'; i+=2; break;
            case '"':  val += '"';   i+=2; break;
            default:
              throw std::runtime_error("ikimenyetso cyo guhunga kitazwi '\\" +
                                       std::string(1,e) + "' ku murongo " + std::to_string(line));
          }
          continue;
        }
        if(s[i]=='\n') line++;
        val += s[i++];
      }
      if(i>=n) throw std::runtime_error("ijambo ritarangiye ku murongo "+std::to_string(line));
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
        else out.push_back({Tok::BANG,"!",0,line});
        break;
      case '&':
        i++; if(peek()=='&'){ out.push_back({Tok::ANDAND,"&&",0,line}); i++; }
        else throw std::runtime_error("ikimenyetso kitazwi '&' ku murongo "+std::to_string(line));
        break;
      case '|':
        i++; if(peek()=='|'){ out.push_back({Tok::OROR,"||",0,line}); i++; }
        else throw std::runtime_error("ikimenyetso kitazwi '|' ku murongo "+std::to_string(line));
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
