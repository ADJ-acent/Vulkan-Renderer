#include "RTGCubeApp.hpp"
#include "../data_path.hpp"
#include "../stb_image.h"
#include "../rgbe.hpp"
#include "../VK.hpp"
#include <stdexcept>
#include <cassert>

RTGCubeApp::RTGCubeApp(RTG & rtg_) : rtg(rtg_)
{
	{ //create command pool
		VkCommandPoolCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = rtg.compute_queue_family.value(),
		};
		VK(vkCreateCommandPool(rtg.device, &create_info, nullptr, &command_pool));
	}
	{ // command buffer
		VkCommandBufferAllocateInfo alloc_info{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = command_pool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1,
		};
		VK(vkAllocateCommandBuffers(rtg.device, &alloc_info, &command_buffer));
	}

	//load input image
	int n;
	std::string input_path = data_path(rtg.configuration.in_image);
	unsigned char* input_image = stbi_load(input_path.c_str(), &input_width, &input_height, &n, 0);
	if (input_image == NULL) {
		throw std::runtime_error("Could not load input image "+ input_path);
	}
	if (n != 4 || input_width * 6 != input_height) {
		throw std::runtime_error("Input image has an incorrect layout (required to have 4 channels (rgbe) and width * 6 = height)");
	}

	std::vector<uint32_t> converted_image(input_width * input_height);
	for (uint32_t pixel_i = 0; pixel_i < uint32_t(input_width * input_height); ++pixel_i) {
		glm::u8vec4 rgbe_pixel = glm::u8vec4(input_image[4*pixel_i], input_image[4*pixel_i + 1], input_image[4*pixel_i + 2], input_image[4*pixel_i + 3]);
		converted_image[pixel_i] = rgbe_to_E5B9G9R9(rgbe_pixel);
	}
	source_image = rtg.helpers.create_image(
		VkExtent2D{ .width = uint32_t(input_width) , .height = uint32_t(input_width) }, //size of image
		VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_STORAGE_BIT, //will sample and used as storage image
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, //should be device-local
		Helpers::Unmapped, 6, 6
	);
	
	rtg.helpers.transfer_to_image_cube(converted_image.data(), sizeof(converted_image[0])*input_width*input_height, source_image);

	dst_images.clear();
	if (rtg.configuration.ggx_out_image == "") {
		assert(rtg.configuration.lambert_out_image != "");
		lambertian_only = true;
		dst_images.resize(1);
		dst_images[0] = rtg.helpers.create_image(
			VkExtent2D{ .width = uint32_t(32) , .height = uint32_t(32) }, //size of image
			VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_STORAGE_BIT, //will sample and used as storage image
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, //should be device-local
			Helpers::Unmapped, 6, 6
		);

		VkClearColorValue clear_color = {{0.0f, 0.0f, 0.0f, 0.0f}};

		VkImageSubresourceRange range = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 6,
			.baseArrayLayer = 0,
			.layerCount = 6,
		};
		vkCmdClearColorImage(command_buffer, dst_images[0].handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_color, 1, &range);
	}
	else {
		dst_images.resize(rtg.configuration.ggx_levels);
		for (uint8_t i = 0; i < rtg.configuration.ggx_levels; ++i) {
			Helpers::AllocatedImage& image = dst_images[i];
			int cur_width = input_width >> (i+1);
			int cur_height = input_height >> (i+1);
			assert(cur_width != 0 && cur_height != 0 && "too high of a mip level, current level has zero size");
			assert(cur_width * 6 == cur_height);
			image = rtg.helpers.create_image(
				VkExtent2D{ .width = uint32_t(cur_width) , .height = uint32_t(cur_width) }, //size of image
				VK_FORMAT_R8G8B8A8_UNORM,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, //used as storage image and will use as transfer
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, //should be device-local
				Helpers::Unmapped, 6, 6
			);

			VkClearColorValue clear_color = {{0.0f, 0.0f, 0.0f, 0.0f}};

				VkImageSubresourceRange range = {
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 6,
					.baseArrayLayer = 0,
					.layerCount = 6,
				};
				vkCmdClearColorImage(command_buffer, image.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_color, 1, &range);
		}
	}

	{//descriptor pool and descriptors allocation
		std::array< VkDescriptorPoolSize, 1> pool_sizes{
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.descriptorCount = 1 + uint32_t(dst_images.size()), //one descriptor per set, two sets per workspace
			},
			
		};
		
		VkDescriptorPoolCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.flags = 0, //because CREATE_FREE_DESCRIPTOR_SET_BIT isn't included, *can't* free individual descriptors allocated from this pool
			.maxSets = 1, //1 set
			.poolSizeCount = uint32_t(pool_sizes.size()),
			.pPoolSizes = pool_sizes.data(),
		};

		VK(vkCreateDescriptorPool(rtg.device, &create_info, nullptr, &descriptor_pool));


		VkDescriptorSetAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &compute_pipeline.set0_TEXTURE,
			};

		VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &STORAGE_IMAGES));
	}

	{//point descriptor to Camera buffer:

		VkDescriptorImageInfo Source_image{
			.sampler = World_environment_sampler,
			.imageView = World_environment_view,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};

		VkDescriptorImageInfo Dst_Image{
			.sampler = texture_sampler,
			.imageView = World_environment_brdf_lut_view,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};

		std::array< VkWriteDescriptorSet, 4 > writes{
			VkWriteDescriptorSet{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = workspace.Camera_descriptors,
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.pBufferInfo = &Camera_info,
			},
			
			VkWriteDescriptorSet{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = workspace.World_descriptors,
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.pBufferInfo = &World_info,
			},

			VkWriteDescriptorSet{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = workspace.World_descriptors,
				.dstBinding = 1,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &World_environment_info,
			},

			VkWriteDescriptorSet{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = workspace.World_descriptors,
				.dstBinding = 2,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &World_environment_brdf_lut_info,
			},
		};

		vkUpdateDescriptorSets(
			rtg.device, //device
			uint32_t(writes.size()), //descriptorWriteCount
			writes.data(), //pDescriptorWrites
			0, //descriptorCopyCount
			nullptr //pDescriptorCopies
		);
	}

	//create pipeline
	compute_pipeline.create(rtg);

	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline.handle);

}

RTGCubeApp::~RTGCubeApp()
{
}

void RTGCubeApp::compute_cubemap(uint8_t mip_level)
{
}

void RTGCubeApp::on_input(InputEvent const &)
{
	assert(false && "This function should not be called on cube app");
}

void RTGCubeApp::on_swapchain(RTG &, RTG::SwapchainEvent const &)
{
	assert(false && "This function should not be called on cube app");
}

void RTGCubeApp::update(float dt)
{
	assert(false && "This function should not be called on cube app");
}

void RTGCubeApp::render(RTG &, RTG::RenderParams const &)
{
	assert(false && "This function should not be called on cube app");
}

void RTGCubeApp::set_animation_time(float t)
{
	assert(false && "This function should not be called on cube app");
}
