#include "../include/lexer.hpp"
#include "../include/parser.hpp"
#include "../include/codegen.hpp"
#include "../include/modules.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>

namespace {

std::string escText(const std::string& in){
  std::string o;
  for(char ch : in){
    if(ch == '\n')      o += "\\n";
    else if(ch == '\t') o += "\\t";
    else if(ch == '\r') o += "\\r";
    else if(ch == '\\') o += "\\\\";
    else                o += ch;
  }
  return o;
}

std::string joinList(const std::vector<std::string>& v){
  std::string o;
  for(size_t i=0;i<v.size();++i){ if(i) o += ","; o += v[i]; }
  return o;
}

// One node per line, two spaces of indent per level. Every field the parser
// can set is printed, and only when it is set, so the format stays readable
// and the Wandaa parser has an exact target to match.
void dumpNode(const NodePtr& n, int depth){
  if(!n) return;
  std::string line(depth * 2, ' ');
  line += nodeName(n->type);
  if(!n->sval.empty())  line += " s=" + escText(n->sval);
  if(!n->sval2.empty()) line += " s2=" + escText(n->sval2);
  // bval is also how importDecl marks an unresolved import Block, so it is
  // printed whenever it is set, not only for Bool.
  if(n->bval)                  line += " b=1";
  else if(n->type == NT::Bool) line += " b=0";
  if(n->isFloat)        line += " f=1";
  if(!n->params.empty()) line += " p=" + joinList(n->params);
  {
    bool any = false;
    for(const auto& t : n->paramTypes) if(!t.empty()) any = true;
    if(any) line += " pt=" + joinList(n->paramTypes);
  }
  if(!n->retType.empty())    line += " r=" + n->retType;
  if(!n->typeParams.empty()) line += " tp=" + joinList(n->typeParams);
  if(!n->widths.empty()){
    std::vector<std::string> w;
    for(int x : n->widths) w.push_back(std::to_string(x));
    line += " w=" + joinList(w);
  }
  if(n->line > 0) line += " l=" + std::to_string(n->line);
  std::cout << line << "\n";
  for(const auto& k : n->kids) dumpNode(k, depth + 1);
}

} // namespace

// wandaac -- the Wandaa compiler.
//
// Reads a .waa source file and writes a native Windows x86-64 .exe. Nothing
// else is required on the machine: no assembler, no linker, no MinGW.
int main(int argc, char** argv){
  // Parse flags. -I <dir> adds a module search path; the package manager passes
  // one per vendored dependency directory.
  std::string srcPath, outPath;
  std::vector<std::string> includeDirs;
  bool quiet = false;                       // -q: say nothing on success
  // --tokens dumps the token stream and stops. It exists so the Wandaa lexer
  // being written for the bootstrap has something to be diffed against: the
  // same ground-truth-by-comparison approach the instruction encoder uses.
  bool dumpTokens = false;
  // --ast does the same job for the parser: a dump the Wandaa parser can be
  // compared against, one node per line.
  bool dumpAst = false;
  for(int i = 1; i < argc; ++i){
    const std::string arg = argv[i];
    if(arg == "-q" || arg == "--quiet"){
      quiet = true;
    } else if(arg == "--tokens"){
      dumpTokens = true;
    } else if(arg == "--ast"){
      dumpAst = true;
    } else if(arg == "-I"){
      if(i + 1 >= argc){ std::cerr << "-I isaba ububiko\n"; return 1; }
      includeDirs.push_back(argv[++i]);
    } else if(arg.rfind("-I", 0) == 0 && arg.size() > 2){
      includeDirs.push_back(arg.substr(2));
    } else if(srcPath.empty()){
      srcPath = arg;
    } else if(outPath.empty()){
      outPath = arg;
    } else {
      std::cerr << "ikimenyetso kitazwi: " << arg << "\n";
      return 1;
    }
  }
  if(srcPath.empty()){
    std::cerr << "gukoresha: wandaac [-q] [-I ububiko] <dosiye.waa> [output.exe]\n";
    return 1;
  }

  if(outPath.empty()){
    // foo.waa -> foo.exe  (replace a trailing extension, else append)
    const size_t dot = srcPath.find_last_of('.');
    const size_t sep = srcPath.find_last_of("/\\");
    const bool hasExt = dot != std::string::npos && (sep == std::string::npos || dot > sep);
    outPath = (hasExt ? srcPath.substr(0, dot) : srcPath) + ".exe";
  }

  // Where `injiza` looks for modules that are not beside the importing file:
  // -I paths, then WANDAA_PATH, then the standard library.
  //
  // The standard library is located relative to the COMPILER, not the current
  // directory, so a project anywhere on disk can `injiza "imibare.waa"`. The
  // ./lib entry keeps working when running from a source checkout.
  std::vector<std::string> searchPaths = includeDirs;
  if(const char* env = std::getenv("WANDAA_PATH")) searchPaths.push_back(env);

  {
    std::string exeDir = ".";
    const std::string self = argv[0];
    const size_t sep = self.find_last_of("/\\");
    if(sep != std::string::npos) exeDir = self.substr(0, sep);
    searchPaths.push_back(exeDir + "/lib");        // installed beside wandaac
    searchPaths.push_back(exeDir + "/../lib");     // wandaac in a bin/ subdir
  }
  searchPaths.push_back("lib");                    // source checkout, cwd-relative

  if(dumpTokens){
    std::ifstream in(srcPath, std::ios::binary);
    if(!in){ std::cerr << "ntibashoboye gusoma: " << srcPath << "\n"; return 1; }
    std::stringstream ss; ss << in.rdbuf();
    try {
      // A string token's text is the DECODED value, which can contain a
      // newline or a tab. Re-escape so every token stays on one line and the
      // two streams can be compared line for line.
      auto esc = [](const std::string& in){
        std::string o;
        for(char ch : in){
          if(ch == '\n')      o += "\\n";
          else if(ch == '\t') o += "\\t";
          else if(ch == '\r') o += "\\r";
          else if(ch == '\\') o += "\\\\";
          else                o += ch;
        }
        return o;
      };
      for(const auto& t : tokenize(ss.str())){
        if(t.type == Tok::END) break;
        std::cout << tokenName(t.type) << " " << t.line << " " << esc(t.text) << "\n";
      }
    } catch(const std::exception& e){
      std::cerr << "Ikosa ryo gukusanya: " << e.what() << "\n";
      return 1;
    }
    return 0;
  }

  if(dumpAst){
    try {
      std::ifstream in(srcPath, std::ios::binary);
      if(!in){ std::cerr << "ntibashoboye gusoma: " << srcPath << "\n"; return 1; }
      std::stringstream ss; ss << in.rdbuf();
      // Deliberately NOT parseProgramWithImports: a dump is of THIS file, so
      // that it can be compared against a parser that does not follow imports.
      dumpNode(parse(tokenize(ss.str())), 0);
    } catch(const std::exception& e){
      std::cerr << "Ikosa ryo gukusanya: " << e.what() << "\n";
      return 1;
    }
    return 0;
  }

  try {
    const auto ast = parseProgramWithImports(srcPath, searchPaths);
    generateExe(ast, outPath);
    if(!quiet) std::cout << "Byubatswe: " << outPath << "\n";
  } catch(const std::exception& e){
    std::cerr << "Ikosa ryo gukusanya: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
