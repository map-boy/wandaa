#pragma once
#include <string>
enum class Tok {
  NUM, STR, IDENT,
  PLUS, MINUS, STAR, SLASH,
  EQ, EQEQ, NEQ, LT, GT, LE, GE,
  LPAREN, RPAREN, LBRACE, RBRACE, LBRACKET, RBRACKET, COMMA, SEMI,
  REKA, NIBA, UBUNDI, MUGIHE, UMURIMO, TANGA, ANDIKA, NIBYO, OYA,
  HANZE, INJIZA, NA, CYANGWA, SI, HAGARIKA, KOMEZA,
  ANDAND, OROR, BANG,
  END
};
struct Token { Tok type; std::string text; double num=0; int line=1; };
