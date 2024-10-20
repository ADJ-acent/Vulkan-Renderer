#pragma once
#include "../VK.hpp"
#include "../Helpers.hpp"

#include <functional>
#include <optional>

struct RTGCube {
    struct Configuration;

	RTGCube(Configuration const &); //creates/acquires resources
	~RTGCube(); //destroys/deallocates resources
	RTGCube(RTGCube const &) = delete; //don't copy this structure!

    struct Configuration {
		//application info passed to Vulkan:
		VkApplicationInfo application_info{
			.pApplicationName = "Unknown",
			.applicationVersion = VK_MAKE_VERSION(0,0,0),
			.pEngineName = "Unknown",
			.engineVersion = VK_MAKE_VERSION(0,0,0),
			.apiVersion = VK_API_VERSION_1_3
		};

		//if true, add debug and validation layers and print more debug output:
		//  `--debug` and `--no-debug` command-line flags
		bool debug = true;

		//if set, use a specific device for rendering:
		// `--physical-device <name>` command-line flag
		std::string physical_device_name = "";

		//path to the input image
		std::string in_image = "";

        std::string ggx_out_image = "";

        std::string lambert_out_image = "";

        uint8_t ggx_levels;

		//for configuration construction + management:
		Configuration() = default;
		void parse(int argc, char **argv); //parse command-line options; throws on error
		static void usage(std::function< void(const char *, const char *) > const &callback); //reports command line usage by passing flag and description to callback.
	};

    Configuration configuration;

	//------------------------------------------------
	//Helper functions, split off into their own little package:
	// see Helpers.hpp
	Helpers helpers;

	//------------------------------------------------
	//Basic vulkan handles:

	VkInstance instance = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
	VkPhysicalDevice physical_device = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;

	//queue for graphics and transfer operations:
	std::optional< uint32_t > compute_queue_family;
	VkQueue compute_queue = VK_NULL_HANDLE;

	//queue for present operations:
	std::optional< uint32_t > present_queue_family;
	VkQueue present_queue = VK_NULL_HANDLE;

	
};