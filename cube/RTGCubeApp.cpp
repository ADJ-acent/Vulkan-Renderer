#include "RTGCubeApp.hpp"
#include <stdexcept>

RTGCubeApp::RTGCubeApp(RTG & rtg_) : rtg(rtg_)
{
}

RTGCubeApp::~RTGCubeApp()
{
}


void RTGCubeApp::Configuration::parse(int argc, char **argv) {
    if (argc < 2) throw std::runtime_error("insufficient number of parameters, input image required.");
    in_image = argv[1];
	for (int argi = 2; argi < argc; ++argi) {
		std::string arg = argv[argi];
		if (arg == "--debug") {
			debug = true;
		} else if (arg == "--no-debug") {
			debug = false;
		} else if (arg == "--ggx") {
			if (argi + 1 >= argc) throw std::runtime_error("--ggx requires a parameter (a file path for output image).");
			argi += 1;
			ggx_out_image = argv[argi];
		} else if (arg == "--lambertian ") {
			if (argi + 1 >= argc) throw std::runtime_error("--lambertian requires a parameter (a file path for output image).");
			argi += 1;
			lambert_out_image = argv[argi];
		} else if (arg == "--ggx-levels ") {
			if (argi + 1 >= argc) throw std::runtime_error("--lambertian requires a parameter (a file path for output image).");
			argi += 1;
			ggx_levels = atoi(argv[argi]);
		} else {
			throw std::runtime_error("Unrecognized argument '" + arg + "'.");
		}
	}

}

void RTG::Configuration::usage(std::function< void(const char *, const char *) > const &callback) {
	callback("--debug, --no-debug", "Turn on/off debug and validation layers.");
	callback("--lambertian <name>", "Save the output lambertian image to <name>.");
	callback("--ggx <name.png>", "Save the output ggx images to <name.1.png> to <name.N.png>.");
	callback("--ggx-levels <N>", "Set the number of levels wanted for ggx, default min(5, log2(input size))");
}