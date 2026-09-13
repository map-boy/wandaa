#pragma once
#include "token.hpp"
#include "node.hpp"
#include <vector>
NodePtr parse(const std::vector<Token>& toks);
