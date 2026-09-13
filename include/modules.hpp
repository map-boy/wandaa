#pragma once
#include "node.hpp"
#include <string>
#include <vector>

// Parse a Wandaa source file and resolve its `injiza "..."` imports, splicing
// each imported file's top-level statements into the program.
//
// searchPaths are consulted, in order, for imports that are not found relative
// to the importing file -- this is how the standard library under lib/ is
// found. Each module is included at most once, and an import cycle is an error
// rather than an infinite loop.
NodePtr parseProgramWithImports(const std::string& mainPath,
                                const std::vector<std::string>& searchPaths);
