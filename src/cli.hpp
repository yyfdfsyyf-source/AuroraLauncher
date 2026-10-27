#pragma once

#include <string>
#include <vector>

namespace aurora {

int run_cli(const std::vector<std::string>& arguments);
int run_cli(int argc, char** argv);

} // namespace aurora
