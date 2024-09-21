
#include "RTG.hpp"

#include "RTGRenderer.hpp"

#include "Scene.hpp"

#include <iostream>

int main(int argc, char **argv) {
	//main wrapped in a try-catch so we can print some debug info about uncaught exceptions:
	try {

		//configure application:
		RTG::Configuration configuration;

		configuration.application_info = VkApplicationInfo{
			.pApplicationName = "RTG A1",
			.applicationVersion = VK_MAKE_VERSION(0,0,0),
			.pEngineName = "Unknown",
			.engineVersion = VK_MAKE_VERSION(0,0,0),
			.apiVersion = VK_API_VERSION_1_3
		};

		bool print_usage = false;

		try {
			configuration.parse(argc, argv);
		} catch (std::runtime_error &e) {
			std::cerr << "Failed to parse arguments:\n" << e.what() << std::endl;
			print_usage = true;
		}

		if (print_usage) {
			std::cerr << "Usage:" << std::endl;
			RTG::Configuration::usage( [](const char *arg, const char *desc){ 
				std::cerr << "    " << arg << "\n        " << desc << std::endl;
			});
			return 1;
		}
		Scene scene(configuration.scene_path);

		//loads vulkan library, creates surface, initializes helpers:
		RTG rtg(configuration);

		//initializes global (whole-life-of-application) resources:
		RTGRenderer application(rtg);

		//main loop -- handles events, renders frames, etc:
		rtg.run(application);

	} catch (std::exception &e) {
		std::cerr << "Exception: " << e.what() << std::endl;
		return 1;
	}
}
