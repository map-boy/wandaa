#include "../include/modules.hpp"
#include "../include/lexer.hpp"
#include "../include/parser.hpp"

#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {

std::string dirOf(const std::string& path){
  const size_t sep = path.find_last_of("/\\");
  return sep == std::string::npos ? std::string(".") : path.substr(0, sep);
}

std::string join(const std::string& dir, const std::string& name){
  if(dir.empty() || dir == ".") return name;
  return dir + "/" + name;
}

bool readFile(const std::string& path, std::string& out){
  std::ifstream f(path, std::ios::binary);
  if(!f) return false;
  std::stringstream ss; ss << f.rdbuf();
  out = ss.str();
  return true;
}

// Normalise enough to recognise the same file reached by different spellings.
// Not a full realpath -- it collapses separators and "./" segments, which is
// what matters for include-once and cycle detection.
//
// The result is also used to OPEN the file, so an absolute path has to survive
// intact: a leading separator, and a Windows drive prefix, are both preserved.
std::string canonical(const std::string& path){
  std::string prefix;
  size_t start = 0;
  if(path.size() >= 2 && path[1] == ':' &&
     ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'))){
    prefix = path.substr(0, 2);                       // "C:"
    start = 2;
    if(start < path.size() && (path[start]=='/' || path[start]=='\\')){ prefix += "/"; ++start; }
  } else if(!path.empty() && (path[0]=='/' || path[0]=='\\')){
    prefix = "/";
    start = 1;
  }

  std::vector<std::string> parts;
  std::string cur;
  for(size_t i = start; i <= path.size(); ++i){
    const bool atEnd = (i == path.size());
    const char c = atEnd ? '/' : path[i];
    if(c == '/' || c == '\\'){
      if(cur.empty() || cur == "."){ cur.clear(); continue; }
      if(cur == ".." && !parts.empty() && parts.back() != ".."){ parts.pop_back(); cur.clear(); continue; }
      parts.push_back(cur); cur.clear();
    } else cur += c;
  }

  std::string out = prefix;
  for(size_t i=0;i<parts.size();++i){ if(i) out += "/"; out += parts[i]; }
  return out.empty() ? std::string(".") : out;
}

struct Resolver {
  const std::vector<std::string>& searchPaths;
  std::set<std::string> included;     // already spliced in -- include once
  std::vector<std::string> stack;     // in-progress, for cycle detection

  explicit Resolver(const std::vector<std::string>& sp) : searchPaths(sp) {}

  std::string locate(const std::string& request, const std::string& fromDir){
    std::vector<std::string> candidates;
    candidates.push_back(join(fromDir, request));
    for(const auto& sp : searchPaths) candidates.push_back(join(sp, request));
    candidates.push_back(request);

    for(const auto& c : candidates){
      std::ifstream f(c, std::ios::binary);
      if(f) return canonical(c);
    }
    std::string tried;
    for(const auto& c : candidates) tried += "\n  - " + c;
    throw std::runtime_error("dosiye yo kwinjiza ntiboneka: '" + request + "'. Twashatse muri:" + tried);
  }

  // Walk a parsed program and replace every import marker with the resolved
  // module's statements.
  void resolveInto(const NodePtr& node, const std::string& fromDir, std::vector<NodePtr>& out){
    for(const auto& kid : node->kids){
      if(kid && kid->type == NT::Block && kid->bval){
        const std::string path = locate(kid->sval, fromDir);

        for(const auto& s : stack)
          if(s == path){
            std::string chain;
            for(const auto& e : stack) chain += e + " -> ";
            throw std::runtime_error("uruziga rwo kwinjiza: " + chain + path);
          }
        if(included.count(path)) continue;      // already in the program
        included.insert(path);

        std::string src;
        if(!readFile(path, src))
          throw std::runtime_error("ntidushoboye gufungura: " + path);

        const NodePtr mod = parse(tokenize(src));
        stack.push_back(path);
        resolveInto(mod, dirOf(path), out);
        stack.pop_back();
        continue;
      }
      out.push_back(kid);
    }
  }
};

} // namespace

NodePtr parseProgramWithImports(const std::string& mainPath,
                                const std::vector<std::string>& searchPaths){
  std::string src;
  {
    std::ifstream f(mainPath, std::ios::binary);
    if(!f) throw std::runtime_error("ntidushoboye gufungura: " + mainPath);
    std::stringstream ss; ss << f.rdbuf();
    src = ss.str();
  }

  const NodePtr root = parse(tokenize(src));

  Resolver r(searchPaths);
  r.included.insert(canonical(mainPath));
  r.stack.push_back(canonical(mainPath));

  auto program = mk(NT::Program);
  r.resolveInto(root, dirOf(mainPath), program->kids);
  return program;
}
