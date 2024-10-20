#pragma once
#include "../RTG.hpp"
#include <vector>

struct RTGCubeApp : RTG::Application {
    RTGCubeApp(RTG &);
	RTGCubeApp(RTGCubeApp const &) = delete; //you shouldn't be copying this object
	~RTGCubeApp();
    RTG &rtg;

	//vulkan resources
	VkCommandPool command_pool;
	VkCommandBuffer command_buffer;
	VkDescriptorPool descriptor_pool;
	VkDescriptorSet STORAGE_IMAGES;

    struct CubeComputePipeline {
		//descriptor set layouts:
		VkDescriptorSetLayout set0_TEXTURE = VK_NULL_HANDLE;

		VkPipelineLayout layout = VK_NULL_HANDLE;

		VkPipeline handle = VK_NULL_HANDLE;

		void create(RTG &);
		void destroy(RTG &);
	} compute_pipeline;

	// image resources
	int input_width, input_height;
	Helpers::AllocatedImage source_image;
	std::vector<Helpers::AllocatedImage> dst_images;
	bool lambertian_only = false;

	void compute_cubemap(uint8_t mip_level);

	//empty functions to use RTG...
    void on_input(InputEvent const &) override;
	void on_swapchain(RTG &, RTG::SwapchainEvent const &) override;
	void update(float dt) override;
	void render(RTG &, RTG::RenderParams const &) override;
	void set_animation_time(float t) override;
};