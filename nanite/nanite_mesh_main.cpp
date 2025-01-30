#include "NaniteMeshApp.hpp"
#include <iostream>

int main(int argc, char **argv) {

    try {
		//configure application:
		NaniteMeshApp::Configuration configuration;

		bool print_usage = false;

		try {
			configuration.parse(argc, argv);
		} catch (std::runtime_error &e) {
			std::cerr << "Failed to parse arguments:\n" << e.what() << std::endl;
			print_usage = true;
		}

		if (print_usage) {
			std::cerr << "Usage:" << std::endl;
			NaniteMeshApp::Configuration::usage( [](const char *arg, const char *desc){ 
				std::cerr << "    " << arg << "\n        " << desc << std::endl;
			});
			return 1;
		}

		NaniteMeshApp app(configuration);


	} catch (std::exception &e) {
		std::cerr << "Exception: " << e.what() << std::endl;
		return 1;
	}
}