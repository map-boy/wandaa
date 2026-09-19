#pragma once
#include <string>
enum class Tok {
  NUM, STR, IDENT,
  PLUS, MINUS, STAR, SLASH,
  EQ, EQEQ, NEQ, LT, GT, LE, GE,
  LPAREN, RPAREN, LBRACE, RBRACE, LBRACKET, RBRACKET, COMMA, SEMI, DOT, COLON,
  QUESTION,
  REKA, NIBA, UBUNDI, MUGIHE, UMURIMO, TANGA, ANDIKA, NIBYO, OYA,
  HANZE, INJIZA, NA, CYANGWA, SI, HAGARIKA, KOMEZA, UBWOKO,
  ANDAND, OROR, BANG, AMP,
  END
};
// NOTE: isFloat is LAST on purpose. Every other token in the lexer is built
// with a four-element brace initializer {type, text, num, line}; inserting a
// field before `line` silently shifts the line number into the new field and
// defaults every line to 1.
struct Token { Tok type; std::string text; double num=0; int line=1; bool isFloat=false; };

// The name of a token kind, for `wandaac --tokens`. The Wandaa lexer written
// for the bootstrap prints these same names, so the two streams can be diffed
// line for line.
inline const char* tokenName(Tok t){
  switch(t){
    case Tok::NUM: return "NUM";        case Tok::STR: return "STR";
    case Tok::IDENT: return "IDENT";    case Tok::PLUS: return "PLUS";
    case Tok::MINUS: return "MINUS";    case Tok::STAR: return "STAR";
    case Tok::SLASH: return "SLASH";    case Tok::EQ: return "EQ";
    case Tok::EQEQ: return "EQEQ";      case Tok::NEQ: return "NEQ";
    case Tok::LT: return "LT";          case Tok::GT: return "GT";
    case Tok::LE: return "LE";          case Tok::GE: return "GE";
    case Tok::LPAREN: return "LPAREN";  case Tok::RPAREN: return "RPAREN";
    case Tok::LBRACE: return "LBRACE";  case Tok::RBRACE: return "RBRACE";
    case Tok::LBRACKET: return "LBRACKET"; case Tok::RBRACKET: return "RBRACKET";
    case Tok::COMMA: return "COMMA";    case Tok::SEMI: return "SEMI";
    case Tok::DOT: return "DOT";        case Tok::COLON: return "COLON";
    case Tok::QUESTION: return "QUESTION";
    case Tok::REKA: return "REKA";      case Tok::NIBA: return "NIBA";
    case Tok::UBUNDI: return "UBUNDI";  case Tok::MUGIHE: return "MUGIHE";
    case Tok::UMURIMO: return "UMURIMO"; case Tok::TANGA: return "TANGA";
    case Tok::ANDIKA: return "ANDIKA";  case Tok::NIBYO: return "NIBYO";
    case Tok::OYA: return "OYA";        case Tok::HANZE: return "HANZE";
    case Tok::INJIZA: return "INJIZA";  case Tok::NA: return "NA";
    case Tok::CYANGWA: return "CYANGWA"; case Tok::SI: return "SI";
    case Tok::HAGARIKA: return "HAGARIKA"; case Tok::KOMEZA: return "KOMEZA";
    case Tok::UBWOKO: return "UBWOKO";  case Tok::ANDAND: return "ANDAND";
    case Tok::OROR: return "OROR";      case Tok::BANG: return "BANG";
    case Tok::AMP: return "AMP";        case Tok::END: return "END";
  }
  return "?";
}