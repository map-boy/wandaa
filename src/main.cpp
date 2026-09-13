#include "../include/lexer.hpp"
#include "../include/parser.hpp"
#include "../include/codegen.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <string>

// wandaac -- the Wandaa compiler.
//
// Reads a .waa source file and writes a native Windows x86-64 .exe. Nothing
// else is required on the machine: no assembler, no linker, no MinGW.
int main(int argc, char** argv){
  if(argc < 2){
    std::cerr << "gukoresha: wandaac <dosiye.waa> [output.exe]\n";
    return 1;
  }
  const std::string srcPath = argv[1];

  std::ifstream f(srcPath, std::ios::binary);
  if(!f){ std::cerr << "ntidushoboye gufungura: " << srcPath << "\n"; return 1; }
  std::stringstream ss; ss << f.rdbuf();

  std::string outPath;
  if(argc > 2) outPath = argv[2];
  else {
    // foo.waa -> foo.exe  (replace a trailing extension, else append)
    const size_t dot = srcPath.find_last_of('.');
    const size_t sep = srcPath.find_last_of("/\\");
    const bool hasExt = dot != std::string::npos && (sep == std::string::npos || dot > sep);
    outPath = (hasExt ? srcPath.substr(0, dot) : srcPath) + ".exe";
  }

  try {
    const auto toks = tokenize(ss.str());
    const auto ast  = parse(toks);
    generateExe(ast, outPath);
    std::cout << "Byubatswe: " << outPath << "\n";
  } catch(const std::exception& e){
    std::cerr << "Ikosa ryo gukusanya: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
