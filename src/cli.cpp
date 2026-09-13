// ============================================================================
//  src/cli.cpp -- `wandaa`, the project and package tool
// ============================================================================
//
//  wandaac compiles one file. This drives projects: scaffolding, building,
//  running tests, and resolving dependencies.
//
//      wandaa tangira <izina>    kora umushinga mushya      new project
//      wandaa shakisha           kuzana ibisabwa            fetch dependencies
//      wandaa ongeraho <izina> <aho>   ongeraho igisabwa    add a dependency
//      wandaa yubaka             yubaka umushinga           build
//      wandaa koresha [--] ...   yubaka hanyuma ukoreshe    build and run
//      wandaa gerageza           koresha ibigeragezo        run tests
//      wandaa verisiyo                                      version
//
//  Dependencies are vendored into ibipapuro/ and handed to wandaac as -I
//  paths, which the existing module resolver already understands. Nothing
//  about this requires network access at build time: once ibipapuro/ is
//  populated (or committed), `wandaa yubaka` is fully offline. That is a
//  deliberate requirement, not a side effect -- a package manager that assumes
//  reliable broadband is not usable everywhere this language needs to work.
// ============================================================================

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------------------
//  SHA-256, so the lockfile can record what was actually vendored. Verified
//  against sha256sum in tests/test_cli.sh -- a hash nobody checks is theatre.
// ---------------------------------------------------------------------------
struct Sha256 {
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                     0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    uint64_t len = 0;
    uint8_t  buf[64]{};
    size_t   have = 0;

    static uint32_t ror(uint32_t x, int n){ return (x >> n) | (x << (32 - n)); }

    void block(const uint8_t* p){
        static const uint32_t K[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        uint32_t w[64];
        for(int i=0;i<16;i++)
            w[i] = ((uint32_t)p[i*4]<<24)|((uint32_t)p[i*4+1]<<16)|((uint32_t)p[i*4+2]<<8)|p[i*4+3];
        for(int i=16;i<64;i++){
            uint32_t s0 = ror(w[i-15],7) ^ ror(w[i-15],18) ^ (w[i-15] >> 3);
            uint32_t s1 = ror(w[i-2],17) ^ ror(w[i-2],19)  ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for(int i=0;i<64;i++){
            uint32_t S1 = ror(e,6) ^ ror(e,11) ^ ror(e,25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = ror(a,2) ^ ror(a,13) ^ ror(a,22);
            uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + mj;
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }

    void update(const uint8_t* p, size_t n){
        len += n;
        while(n){
            const size_t take = std::min(n, 64 - have);
            std::copy(p, p + take, buf + have);
            have += take; p += take; n -= take;
            if(have == 64){ block(buf); have = 0; }
        }
    }
    void update(const std::string& s){ update((const uint8_t*)s.data(), s.size()); }

    std::string hex(){
        const uint64_t bits = len * 8;
        uint8_t pad = 0x80;
        update(&pad, 1);
        uint8_t zero = 0;
        while(have != 56) update(&zero, 1);
        uint8_t tail[8];
        for(int i=0;i<8;i++) tail[i] = (uint8_t)(bits >> (56 - 8*i));
        update(tail, 8);
        std::ostringstream o;
        for(int i=0;i<8;i++)
            for(int b=3;b>=0;b--)
                o << "0123456789abcdef"[(h[i] >> (8*b+4)) & 0xF]
                  << "0123456789abcdef"[(h[i] >> (8*b))   & 0xF];
        return o.str();
    }
};

// Hash a directory's Wandaa sources: every .waa path and its bytes, in sorted
// order, so the digest is stable regardless of filesystem enumeration order.
std::string hashTree(const fs::path& dir){
    std::vector<fs::path> files;
    std::error_code ec;
    for(auto it = fs::recursive_directory_iterator(dir, ec);
        it != fs::recursive_directory_iterator(); ++it){
        if(ec) break;
        if(it->is_regular_file() && it->path().extension() == ".waa")
            files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());

    Sha256 sha;
    for(const auto& f : files){
        sha.update(fs::relative(f, dir).generic_string());
        std::ifstream in(f, std::ios::binary);
        std::stringstream ss; ss << in.rdbuf();
        sha.update(ss.str());
    }
    return sha.hex();
}

// ---------------------------------------------------------------------------
//  Manifest. A deliberately small, strict TOML subset: [sections], key = value
//  with quoted strings, and inline tables of quoted strings. Anything else is
//  an error naming the line, rather than being silently ignored.
// ---------------------------------------------------------------------------
struct Dependency {
    std::string name;
    std::string inzira;     // local path
    std::string git;        // git URL
    std::string tag;        // git tag or branch
};

struct Manifest {
    std::string izina = "umushinga";
    std::string verisiyo = "0.1.0";
    std::string intangiriro = "src/mbere.waa";
    std::vector<Dependency> ibisabwa;
};

std::string trim(const std::string& s){
    size_t a = s.find_first_not_of(" \t\r\n");
    if(a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string unquote(const std::string& s, int line){
    const std::string t = trim(s);
    if(t.size() < 2 || t.front() != '"' || t.back() != '"')
        throw std::runtime_error("agaciro kagomba kuba mu byuma ku murongo " + std::to_string(line) + ": " + t);
    return t.substr(1, t.size() - 2);
}

Manifest readManifest(const fs::path& path){
    std::ifstream in(path);
    if(!in) throw std::runtime_error("wandaa.toml ntiboneka. Koresha: wandaa tangira <izina>");

    Manifest m;
    std::string section, raw;
    int lineNo = 0;
    while(std::getline(in, raw)){
        ++lineNo;
        std::string line = trim(raw);
        const size_t hash = line.find('#');
        if(hash != std::string::npos) line = trim(line.substr(0, hash));
        if(line.empty()) continue;

        if(line.front() == '['){
            if(line.back() != ']')
                throw std::runtime_error("igice kitarangiye ku murongo " + std::to_string(lineNo));
            section = line.substr(1, line.size() - 2);
            continue;
        }

        const size_t eq = line.find('=');
        if(eq == std::string::npos)
            throw std::runtime_error("umurongo " + std::to_string(lineNo) + " ntabwo ari 'urufunguzo = agaciro'");
        const std::string key = trim(line.substr(0, eq));
        const std::string val = trim(line.substr(eq + 1));

        if(section == "umushinga"){
            if(key == "izina")            m.izina = unquote(val, lineNo);
            else if(key == "verisiyo")    m.verisiyo = unquote(val, lineNo);
            else if(key == "intangiriro") m.intangiriro = unquote(val, lineNo);
            else throw std::runtime_error("urufunguzo rutazwi muri [umushinga]: " + key);
        } else if(section == "ibisabwa"){
            Dependency d;
            d.name = key;
            if(!val.empty() && val.front() == '{'){
                if(val.back() != '}')
                    throw std::runtime_error("imbonerahamwe itarangiye ku murongo " + std::to_string(lineNo));
                std::string inner = val.substr(1, val.size() - 2);
                std::stringstream ss(inner);
                std::string field;
                while(std::getline(ss, field, ',')){
                    field = trim(field);
                    if(field.empty()) continue;
                    const size_t fe = field.find('=');
                    if(fe == std::string::npos)
                        throw std::runtime_error("umwanya mubi ku murongo " + std::to_string(lineNo));
                    const std::string fk = trim(field.substr(0, fe));
                    const std::string fv = unquote(field.substr(fe + 1), lineNo);
                    if(fk == "inzira")   d.inzira = fv;
                    else if(fk == "git") d.git = fv;
                    else if(fk == "tag") d.tag = fv;
                    else throw std::runtime_error("urufunguzo rutazwi mu gisabwa: " + fk);
                }
            } else {
                // Bare string: treat as a local path, the offline-friendly default.
                d.inzira = unquote(val, lineNo);
            }
            if(d.inzira.empty() && d.git.empty())
                throw std::runtime_error("igisabwa '" + key + "' gikeneye 'inzira' cyangwa 'git'");
            m.ibisabwa.push_back(d);
        } else if(!section.empty()){
            throw std::runtime_error("igice kitazwi: [" + section + "]");
        }
    }
    return m;
}

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------
const char* PKG_DIR = "ibipapuro";

fs::path compilerPath(){
    // Look for wandaac beside this binary first, then on PATH.
    const char* env = std::getenv("WANDAAC");
    if(env) return env;
#ifdef _WIN32
    return "wandaac.exe";
#else
    return "./wandaac";
#endif
}

std::string quoteArg(const std::string& s){
    if(s.find_first_of(" \t\"") == std::string::npos) return s;
    return "\"" + s + "\"";
}

// How to launch a produced .exe. On Windows that is the file itself; when
// cross-developing on Linux, set WANDAA_RUNNER=wine so `wandaa koresha` and
// `wandaa gerageza` still work. CI uses this for the Linux job.
std::string runnerPrefix(){
    const char* r = std::getenv("WANDAA_RUNNER");
    return (r && *r) ? (std::string(r) + " ") : std::string();
}

int runCommand(const std::string& cmd){
    const int rc = std::system(cmd.c_str());
#ifdef _WIN32
    return rc;
#else
    if(rc == -1) return -1;
    return (rc & 0x7F) ? 128 + (rc & 0x7F) : ((rc >> 8) & 0xFF);
#endif
}

void copyTree(const fs::path& from, const fs::path& to){
    std::error_code ec;
    fs::remove_all(to, ec);
    fs::create_directories(to.parent_path(), ec);
    fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
}

// ---------------------------------------------------------------------------
//  Commands
// ---------------------------------------------------------------------------
int cmdTangira(const std::string& izina){
    const fs::path root = izina;
    if(fs::exists(root)){ std::cerr << "ububiko '" << izina << "' busanzwe buhari\n"; return 1; }
    fs::create_directories(root / "src");
    fs::create_directories(root / "tests");

    {
        std::ofstream m(root / "wandaa.toml");
        m << "[umushinga]\n"
          << "izina = \"" << izina << "\"\n"
          << "verisiyo = \"0.1.0\"\n"
          << "intangiriro = \"src/mbere.waa\"\n"
          << "\n"
          << "# Ibisabwa -- dependencies. Vendored into " << PKG_DIR << "/ by `wandaa shakisha`.\n"
          << "#   amagambo = { inzira = \"../amagambo\" }\n"
          << "#   json     = { git = \"https://github.com/...\", tag = \"v1.0\" }\n"
          << "[ibisabwa]\n";
    }
    {
        std::ofstream s(root / "src" / "mbere.waa");
        s << "andika(\"Mwiriwe " << izina << "\");\n";
    }
    {
        std::ofstream t(root / "tests" / "mbere_test.waa");
        t << "# Ibigeragezo bisohoka na 0 iyo byatsinze.\n"
          << "# A test passes when the program exits 0.\n"
          << "reka igisubizo = 2 + 2;\n"
          << "niba (igisubizo != 4) {\n"
          << "  andika(\"ikosa: 2 + 2 ntabwo ari 4\");\n"
          << "  tanga 1;\n"
          << "}\n"
          << "andika(\"byatsinze\");\n";
    }
    {
        std::ofstream g(root / ".gitignore");
        g << "*.exe\n" << PKG_DIR << "/\n";
    }
    std::cout << "Umushinga '" << izina << "' wakozwe.\n"
              << "  cd " << izina << "\n"
              << "  wandaa koresha\n";
    return 0;
}

int cmdShakisha(){
    const Manifest m = readManifest("wandaa.toml");
    std::error_code ec;
    fs::create_directories(PKG_DIR, ec);

    std::ofstream lock("wandaa.lock");
    lock << "# Iyi dosiye ikorwa na `wandaa shakisha`. Ntuyihindure intoki.\n"
         << "# Generated by `wandaa shakisha`. Do not edit by hand.\n"
         << "# izina\tubwoko\tinkomoko\tsha256\n";

    for(const auto& d : m.ibisabwa){
        const fs::path dest = fs::path(PKG_DIR) / d.name;
        std::string kind, source;

        if(!d.inzira.empty()){
            if(!fs::exists(d.inzira)){
                std::cerr << "igisabwa '" << d.name << "': inzira ntiboneka: " << d.inzira << "\n";
                return 1;
            }
            copyTree(d.inzira, dest);
            kind = "inzira"; source = d.inzira;
        } else {
            // Git is needed only here, to fetch a dependency. Building a
            // project from an already-populated ibipapuro/ needs nothing.
            std::string cmd = "git clone --quiet --depth 1";
            if(!d.tag.empty()) cmd += " --branch " + quoteArg(d.tag);
            cmd += " " + quoteArg(d.git) + " " + quoteArg(dest.string());
            fs::remove_all(dest, ec);
            if(runCommand(cmd) != 0){
                std::cerr << "igisabwa '" << d.name << "': git clone yanze\n";
                return 1;
            }
            fs::remove_all(dest / ".git", ec);
            kind = "git"; source = d.git + (d.tag.empty() ? "" : "#" + d.tag);
        }

        const std::string digest = hashTree(dest);
        lock << d.name << "\t" << kind << "\t" << source << "\t" << digest << "\n";
        std::cout << "  " << d.name << "  " << digest.substr(0, 12) << "  <- " << source << "\n";
    }
    std::cout << "Ibisabwa " << m.ibisabwa.size() << " byazanywe muri " << PKG_DIR << "/\n";
    return 0;
}

// Confirm what is on disk still matches wandaa.lock. A lockfile whose hashes
// are never checked records nothing useful.
int cmdGenzura(){
    std::ifstream lock("wandaa.lock");
    if(!lock){ std::cerr << "wandaa.lock ntiboneka. Koresha: wandaa shakisha\n"; return 1; }
    std::string line;
    int bad = 0, checked = 0;
    while(std::getline(lock, line)){
        if(line.empty() || line[0] == '#') continue;
        std::stringstream ss(line);
        std::string name, kind, source, digest;
        std::getline(ss, name, '\t');
        std::getline(ss, kind, '\t');
        std::getline(ss, source, '\t');
        std::getline(ss, digest, '\t');
        const fs::path dir = fs::path(PKG_DIR) / name;
        if(!fs::exists(dir)){
            std::cerr << "  " << name << ": ntiboneka muri " << PKG_DIR << "/\n"; ++bad; continue;
        }
        const std::string actual = hashTree(dir);
        ++checked;
        if(actual != digest){
            std::cerr << "  " << name << ": sha256 ntihuye\n"
                      << "    wandaa.lock: " << digest << "\n"
                      << "    ku disiki:   " << actual << "\n";
            ++bad;
        }
    }
    if(bad){ std::cerr << bad << " bitahuye.\n"; return 1; }
    std::cout << "Byose " << checked << " bihuye na wandaa.lock.\n";
    return 0;
}

std::string includeFlags(){
    std::string flags;
    std::error_code ec;
    if(fs::exists(PKG_DIR)){
        flags += " -I " + quoteArg(PKG_DIR);
        for(auto& e : fs::directory_iterator(PKG_DIR, ec))
            if(e.is_directory()) flags += " -I " + quoteArg(e.path().string());
    }
    return flags;
}

int compileOne(const fs::path& src, const fs::path& exe){
    const std::string cmd = quoteArg(compilerPath().string()) + includeFlags() +
                            " " + quoteArg(src.string()) + " " + quoteArg(exe.string());
    return runCommand(cmd);
}

int cmdYubaka(fs::path* builtExe = nullptr){
    const Manifest m = readManifest("wandaa.toml");
    const fs::path src = m.intangiriro;
    if(!fs::exists(src)){ std::cerr << "intangiriro ntiboneka: " << src << "\n"; return 1; }
    const fs::path exe = fs::path(m.izina + ".exe");
    if(compileOne(src, exe) != 0) return 1;
    if(builtExe) *builtExe = exe;
    return 0;
}

int cmdKoresha(const std::vector<std::string>& args){
    fs::path exe;
    if(cmdYubaka(&exe) != 0) return 1;
    std::string cmd = runnerPrefix() + quoteArg(fs::absolute(exe).string());
    for(const auto& a : args) cmd += " " + quoteArg(a);
    return runCommand(cmd);
}

int cmdGerageza(){
    if(!fs::exists("tests")){ std::cout << "nta bigeragezo bihari (tests/)\n"; return 0; }
    std::vector<fs::path> cases;
    std::error_code ec;
    for(auto& e : fs::directory_iterator("tests", ec))
        if(e.is_regular_file() && e.path().extension() == ".waa") cases.push_back(e.path());
    std::sort(cases.begin(), cases.end());

    const fs::path work = fs::temp_directory_path() / "wandaa_gerageza";
    fs::remove_all(work, ec);
    fs::create_directories(work, ec);

    int pass = 0, fail = 0;
    for(const auto& c : cases){
        const std::string name = c.stem().string();
        const fs::path exe = work / (name + ".exe");
        if(compileOne(c, exe) != 0){
            std::cout << "YANZE  " << name << " (gukusanya)\n"; ++fail; continue;
        }
        if(runCommand(runnerPrefix() + quoteArg(fs::absolute(exe).string()) + " > " +
                      quoteArg((work / (name + ".out")).string()) + " 2>&1") != 0){
            std::cout << "YANZE  " << name << "\n";
            std::ifstream out(work / (name + ".out"));
            std::string l;
            while(std::getline(out, l)) std::cout << "    " << l << "\n";
            ++fail;
        } else {
            std::cout << "YATSINZE  " << name << "\n"; ++pass;
        }
    }
    std::cout << "\n" << pass << " byatsinze, " << fail << " byanze\n";
    return fail ? 1 : 0;
}

int cmdOngeraho(const std::string& name, const std::string& source){
    // Append to [ibisabwa], creating the section if the manifest lacks it.
    std::ifstream in("wandaa.toml");
    if(!in){ std::cerr << "wandaa.toml ntiboneka\n"; return 1; }
    std::vector<std::string> lines;
    std::string l;
    bool hasSection = false;
    while(std::getline(in, l)){
        lines.push_back(l);
        if(trim(l) == "[ibisabwa]") hasSection = true;
    }
    in.close();

    const bool isGit = source.rfind("http", 0) == 0 || source.rfind("git@", 0) == 0;
    const std::string entry = name + " = { " +
        (isGit ? "git = \"" : "inzira = \"") + source + "\" }";

    std::ofstream out("wandaa.toml");
    bool written = false;
    for(const auto& line : lines){
        out << line << "\n";
        if(!written && trim(line) == "[ibisabwa]"){ out << entry << "\n"; written = true; }
    }
    if(!hasSection){ out << "\n[ibisabwa]\n" << entry << "\n"; written = true; }
    out.close();

    std::cout << "Byongeweho: " << entry << "\n"
              << "Koresha `wandaa shakisha` kugira ngo bizanwe.\n";
    return 0;
}

void usage(){
    std::cout <<
      "wandaa -- igikoresho cy'imishinga ya Wandaa\n"
      "\n"
      "  wandaa tangira <izina>          kora umushinga mushya (new project)\n"
      "  wandaa ongeraho <izina> <aho>   ongeraho igisabwa (add a dependency)\n"
      "  wandaa shakisha                 kuzana ibisabwa (fetch dependencies)\n"
      "  wandaa genzura                  genzura ibisabwa na wandaa.lock (verify)\n"
      "  wandaa yubaka                   yubaka umushinga (build)\n"
      "  wandaa koresha [args...]        yubaka hanyuma ukoreshe (build and run)\n"
      "  wandaa gerageza                 koresha ibigeragezo (run tests)\n"
      "  wandaa verisiyo                 verisiyo (version)\n"
      "\n"
      "WANDAAC       inzira ya wandaac (override the compiler path)\n"
      "WANDAA_RUNNER icyo gukoresha mu gukoresha .exe, urugero `wine`\n"
      "              (how to launch a produced .exe when cross-developing)\n";
}

} // namespace

int main(int argc, char** argv){
    if(argc < 2){ usage(); return 1; }
    const std::string cmd = argv[1];
    std::vector<std::string> rest(argv + 2, argv + argc);

    try {
        if(cmd == "tangira"){
            if(rest.empty()){ std::cerr << "gukoresha: wandaa tangira <izina>\n"; return 1; }
            return cmdTangira(rest[0]);
        }
        if(cmd == "ongeraho"){
            if(rest.size() < 2){ std::cerr << "gukoresha: wandaa ongeraho <izina> <inzira|git-url>\n"; return 1; }
            return cmdOngeraho(rest[0], rest[1]);
        }
        if(cmd == "shakisha") return cmdShakisha();
        if(cmd == "genzura")  return cmdGenzura();
        if(cmd == "yubaka")   return cmdYubaka();
        if(cmd == "koresha")  return cmdKoresha(rest);
        if(cmd == "gerageza") return cmdGerageza();
        if(cmd == "verisiyo"){ std::cout << "wandaa 0.2.0\n"; return 0; }
        if(cmd == "-h" || cmd == "--help" || cmd == "ubufasha"){ usage(); return 0; }

        std::cerr << "itegeko ritazwi: " << cmd << "\n\n";
        usage();
        return 1;
    } catch(const std::exception& e){
        std::cerr << "Ikosa: " << e.what() << "\n";
        return 1;
    }
}
