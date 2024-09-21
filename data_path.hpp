#pragma once

#include <string>

//construct a path based on the location of the currently-running executable:
//from https://github.com/15-466/15-466-f24-base1/blob/main/data_path.hpp
std::string data_path(std::string const &suffix);