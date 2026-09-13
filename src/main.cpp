#include "../include/lexer.hpp"
#include "../include/parser.hpp"
#include "../include/codegen.hpp"
#include <fstream>
#include <sstream>
#include <iostream>

int main(int argc, char** argv){
  if(argc<2){ std::cerr << "gukoresha: wandaac <dosiye.waa> [output.s]\n"; return 1; }
  std::ifstream f(argv[1]);
  if(!f){ std::cerr << "ntidushoboye gufungura: " << argv[1] << "\n"; return 1; }
  std::stringstream ss; ss << f.rdbuf();
  std::string outPath = argc>2 ? argv[2] : std::string(argv[1]) + ".s";
  try {
    auto toks = tokenize(ss.str());
    auto ast = parse(toks);
    std::string code = generateAsm(ast);
    std::ofstream out(outPath);
    out << code;
    std::cout << "Byanditswe: " << outPath << "\n";
  } catch(std::exception& e){
    std::cerr << "Ikosa ryo gukusanya: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
