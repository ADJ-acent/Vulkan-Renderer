#include "RTGCube.hpp"
#include <iostream>
#include <vector>

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
	VkDebugUtilsMessageSeverityFlagBitsEXT severity,
	VkDebugUtilsMessageTypeFlagsEXT type,
	const VkDebugUtilsMessengerCallbackDataEXT *data,
	void *user_data
) {
	if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
		std::cerr << "\x1b[91m" << "E: ";
	} else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
		std::cerr << "\x1b[33m" << "w: ";
	} else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
		std::cerr << "\x1b[90m" << "i: ";
	} else { //VERBOSE
		std::cerr << "\x1b[90m" << "v: ";
	}
	std::cerr << data->pMessage << "\x1b[0m" << std::endl;

	return VK_FALSE;
}

RTGCube::RTGCube(Configuration const &configuration_)
{
    //copy input configuration:
	configuration = configuration_;

	{ //create the `instance` (main handle to Vulkan library):
		VkInstanceCreateFlags instance_flags = 0;
		std::vector<const char *> instance_extensions;
		std::vector<const char *> instance_layers;


		//add extensions and layers for debugging:
		if (configuration.debug) {
			instance_extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
			instance_layers.emplace_back("VK_LAYER_KHRONOS_validation");
		}

		//write debug messenger structure
		VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info{
			.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
			.messageSeverity =
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT
				| VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT
				| VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
				| VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
			.messageType =
				VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
				| VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
				| VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
			.pfnUserCallback = debug_callback,
			.pUserData = nullptr
		};

		VkInstanceCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
			.pNext = (configuration.debug ? &debug_messenger_create_info : nullptr),
			.flags = instance_flags,
			.pApplicationInfo = &configuration.application_info,
			.enabledLayerCount = uint32_t(instance_layers.size()),
			.ppEnabledLayerNames = instance_layers.data(),
			.enabledExtensionCount = uint32_t(instance_extensions.size()),
			.ppEnabledExtensionNames = instance_extensions.data()
		};
		VK(vkCreateInstance(&create_info, nullptr, &instance));

		//create debug messenger
		if (configuration.debug) {
			PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
			if (!vkCreateDebugUtilsMessengerEXT) {
				throw std::runtime_error("Failed to lookup debug utils create fn.");
			}
			VK( vkCreateDebugUtilsMessengerEXT(instance, &debug_messenger_create_info, nullptr, &debug_messenger) );
		}
	}


	{ //select the `physical_device` -- the gpu that will be used to draw:
		std::vector< std::string > physical_device_names; //for later error message
		{ //pick a physical device
			uint32_t count = 0;
			VK( vkEnumeratePhysicalDevices(instance, &count, nullptr) );
			std::vector< VkPhysicalDevice > physical_devices(count);
			VK( vkEnumeratePhysicalDevices(instance, &count, physical_devices.data()) );

			uint32_t best_score = 0;

			for (auto const &pd : physical_devices) {
				VkPhysicalDeviceProperties properties;
				vkGetPhysicalDeviceProperties(pd, &properties);

				VkPhysicalDeviceFeatures features;
				vkGetPhysicalDeviceFeatures(pd, &features);

				physical_device_names.emplace_back(properties.deviceName);

				if (!configuration.physical_device_name.empty()) {
					if (configuration.physical_device_name == properties.deviceName) {
						if (physical_device) {
							std::cerr << "WARNING: have two physical devices with the name '" << properties.deviceName << "'; using the first to be enumerated." << std::endl;
						} else {
							physical_device = pd;
						}
					}
				} else {
					uint32_t score = 1;
					if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
						score += 0x8000;
					}

					if (score > best_score) {
						best_score = score;
						physical_device = pd;
					}
				}
			}
		}

		if (physical_device == VK_NULL_HANDLE) {
			std::cerr << "Physical devices:\n";
			for (std::string const &name : physical_device_names) {
				std::cerr << "    " << name << "\n";
			}
			std::cerr.flush();

			if (!configuration.physical_device_name.empty()) {
				throw std::runtime_error("No physical device with name '" + configuration.physical_device_name + "'.");
			} else {
				throw std::runtime_error("No suitable GPU found.");
			}
		}

		{ //report device name:
			VkPhysicalDeviceProperties properties;
			vkGetPhysicalDeviceProperties(physical_device, &properties);
			std::cout << "Selected physical device '" << properties.deviceName << "'." << std::endl;
		}
	}
	

	{ //create the `device` (logical interface to the GPU) and the `queue`s to which we can submit commands:
		{ //look up queue indices:
			uint32_t count = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
			std::vector< VkQueueFamilyProperties > queue_families(count);
			vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, queue_families.data());

			for (auto const &queue_family : queue_families) {
				uint32_t i = uint32_t(&queue_family - &queue_families[0]);

				//if it does graphics, set the graphics queue family:
				if (queue_family.queueFlags & VK_QUEUE_COMPUTE_BIT) {
					if (!compute_queue_family) {
						compute_queue_family = i;
						break;
					}
				}
			}

			if (!compute_queue_family) {
				throw std::runtime_error("No queue with compute support.");
			}
		}

		//select device extensions:
		std::vector< const char * > device_extensions;

		{ //create the logical device:
			std::vector< VkDeviceQueueCreateInfo > queue_create_infos;
			std::vector< uint32_t > unique_queue_families = {
					compute_queue_family.value(),
			};
		
			float queue_priorities[1] = { 1.0f };
			for (uint32_t queue_family : unique_queue_families) {
				queue_create_infos.emplace_back(VkDeviceQueueCreateInfo{
					.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
					.queueFamilyIndex = queue_family,
					.queueCount = 1,
					.pQueuePriorities = queue_priorities,
				});
			}

			VkDeviceCreateInfo create_info{
				.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
				.queueCreateInfoCount = uint32_t(queue_create_infos.size()),
				.pQueueCreateInfos = queue_create_infos.data(),

				//device layers are depreciated; spec suggests passing instance_layers or nullptr:
				.enabledLayerCount = 0,
				.ppEnabledLayerNames = nullptr,

				.enabledExtensionCount = static_cast< uint32_t>(device_extensions.size()),
				.ppEnabledExtensionNames = device_extensions.data(),

				//pass a pointer to a VkPhysicalDeviceFeatures to request specific features: (e.g., thick lines)
				.pEnabledFeatures = nullptr,
			};

			VK( vkCreateDevice(physical_device, &create_info, nullptr, &device) );

			vkGetDeviceQueue(device, graphics_queue_family.value(), 0, &graphics_queue);
			if (!configuration.headless_mode) {
				vkGetDeviceQueue(device, present_queue_family.value(), 0, &present_queue);
			}
		}
	}

	//run any resource creation required by Helpers structure:
	helpers.create();

	//create initial swapchain:
	recreate_swapchain();

	//create workspace resources:
	workspaces.resize(configuration.workspaces);
	VkFenceCreateInfo fence_create_info{
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT, //start signaled, because all workspaces are available to start
	};
	VkSemaphoreCreateInfo semaphore_create_info{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	};
	for (auto &workspace : workspaces) {
		{ //create workspace fences:

			VK(vkCreateFence(device, &fence_create_info, nullptr, &workspace.workspace_available));
		}

		{ //create workspace semaphores:

			VK(vkCreateSemaphore(device, &semaphore_create_info, nullptr, &workspace.image_available));
			VK(vkCreateSemaphore(device, &semaphore_create_info, nullptr, &workspace.image_done));
		}
	}
}