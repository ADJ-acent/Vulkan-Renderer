#include "data_path.hpp"

#include <iostream>
#include <vector>
#include <sstream>

#if defined(_WIN32)
#include <windows.h>
#include <Knownfolders.h>
#include <Shlobj.h>
#include <direct.h>
#include <io.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#include <sys/stat.h>
#endif //WINDOWS


//This function gets the path to the current executable in various os-specific ways:
//from https://github.com/15-466/15-466-f24-base1/blob/main/data_path.cpp
static std::string get_exe_path() {
	#if defined(_WIN32)
	TCHAR buffer[MAX_PATH];
	GetModuleFileName(NULL, buffer, MAX_PATH);
	std::string ret = buffer;
	ret = ret.substr(0, ret.rfind('\\'));
	return ret;
	#elif defined(__linux__)
	std::vector< char > buffer(1000);
	while (1) {
		ssize_t got = readlink("/proc/self/exe", &buffer[0], buffer.size());
		if (got <= 0) {
			return "";
		} else if (got < (ssize_t)buffer.size()) {
			std::string ret = std::string(buffer.begin(), buffer.begin() + got);
			return ret.substr(0, ret.rfind('/'));
		}
		buffer.resize(buffer.size() + 4000);
	}
	#elif defined(__APPLE__)
	uint32_t bufsize = 0;
	std::vector< char > buffer;
	_NSGetExecutablePath(&buffer[0], &bufsize);
	buffer.resize(bufsize, '\0');
	bufsize = buffer.size();
	if (_NSGetExecutablePath(&buffer[0], &bufsize) != 0) {
		throw std::runtime_error("Call to _NSGetExecutablePath failed for mysterious reasons.");
	}
	std::string ret = std::string(&buffer[0]);
	return ret.substr(0, ret.rfind('/'));
	#else
	#error "No idea what the OS is."
	#endif
}

std::string data_path(std::string const &suffix) {
	static std::string path = get_exe_path(); //cache result of get_exe_path()
	return path + "/" + suffix;
}