#pragma once
#include <string>
enum class Tok {
  NUM, STR, IDENT,
  PLUS, MINUS, STAR, SLASH,
  EQ, EQEQ, NEQ, LT, GT, LE, GE,
  LPAREN, RPAREN, LBRACE, RBRACE, LBRACKET, RBRACKET, COMMA, SEMI, DOT, COLON,
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