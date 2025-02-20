#ifdef _WIN32
//ensure we have M_PI
#define _USE_MATH_DEFINES
#endif
#include <GLFW/glfw3.h>
#include "RTGRenderer.hpp"

// Nanite include
#include "nanite/read_cluster.hpp"

#include "VK.hpp"
#include "rgbe.hpp"
#include "data_path.hpp"

#include "stb_image.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <deque>
#include <iostream>
#include <fstream>

static constexpr unsigned int WORKGROUP_SIZE = 32;

RTGRenderer::RTGRenderer(RTG &rtg_, Scene &scene_) : rtg(rtg_), scene(scene_), shadow_atlas(ShadowAtlas(shadow_atlas_length)) {

	// read cluster info
	RuntimeDAG dag;
	read_clsr("output", &dag);

	{ //create command pool
		VkCommandPoolCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			.queueFamilyIndex = rtg.graphics_queue_family.value(),
		};
		VK(vkCreateCommandPool(rtg.device, &create_info, nullptr, &command_pool));
	}

	//select a depth format:
	//	at least one of these two must be supported, according to the spec; but neither are required
	depth_format = rtg.helpers.find_image_format(
		{VK_FORMAT_D32_SFLOAT, VK_FORMAT_X8_D24_UNORM_PACK32},
		VK_IMAGE_TILING_OPTIMAL,
		VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
	);

	VkImageLayout color_final_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	if (scene.has_cloud) {
		color_final_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	{ //create render pass
		std::array<VkAttachmentDescription, 2> attachments{
			VkAttachmentDescription{//0 - color attachment:
				.format = rtg.surface_format.format,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout = color_final_layout, // rtg.configuration.headless_mode ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			},
			VkAttachmentDescription{//1 - depth attachment:
				.format = depth_format,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			},
		};

		VkAttachmentReference color_attachment_ref{
			.attachment = 0,
			.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		};

		VkAttachmentReference depth_attachment_ref{
			.attachment = 1,
			.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		};

		VkSubpassDescription subpass{
			.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
			.inputAttachmentCount = 0,
			.pInputAttachments = nullptr,
			.colorAttachmentCount = 1,
			.pColorAttachments = &color_attachment_ref,
			.pDepthStencilAttachment = &depth_attachment_ref,
		};

		//this defers the image load actions for the attachments:
		std::array<VkSubpassDependency, 2> dependencies{
			VkSubpassDependency{
				.srcSubpass = VK_SUBPASS_EXTERNAL,
				.dstSubpass = 0,
				.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				.srcAccessMask = 0,
				.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			},
			VkSubpassDependency{
				.srcSubpass = VK_SUBPASS_EXTERNAL,
				.dstSubpass = 0,
				.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
				.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			}
		};

		VkRenderPassCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
			.attachmentCount = uint32_t(attachments.size()),
			.pAttachments = attachments.data(),
			.subpassCount = 1,
			.pSubpasses = &subpass,
			.dependencyCount = uint32_t(dependencies.size()),
			.pDependencies = dependencies.data(),
		};

		VK( vkCreateRenderPass(rtg.device, &create_info, nullptr, &render_pass) );
	}

	{// create shadow atlas render pass, referenced https://github.com/SaschaWillems/Vulkan/blob/master/examples/shadowmapping/shadowmapping.cpp

		VkAttachmentDescription attachment_description{
			.format = depth_format,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
		};

		VkAttachmentReference depth_reference = {
			.attachment = 0,
			.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		};

		VkSubpassDescription subpass = {
			.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
			.colorAttachmentCount = 0,
			.pDepthStencilAttachment = &depth_reference,
		};

		// Use subpass dependencies for layout transitions
		std::array<VkSubpassDependency, 2> dependencies {
			VkSubpassDependency {
				.srcSubpass = VK_SUBPASS_EXTERNAL,
				.dstSubpass = 0,
				.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
				.srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
				.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
			},
			VkSubpassDependency {
				.srcSubpass = 0,
				.dstSubpass = VK_SUBPASS_EXTERNAL,
				.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
				.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
			}

		};


		VkRenderPassCreateInfo render_pass_create_info {
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
			.attachmentCount = 1,
			.pAttachments = &attachment_description,
			.subpassCount = 1,
			.pSubpasses = &subpass,
			.dependencyCount = static_cast<uint32_t>(dependencies.size()),
			.pDependencies = dependencies.data(),
		};

		VK(vkCreateRenderPass(rtg.device, &render_pass_create_info, nullptr, &shadow_atlas_pass));

	}
	{ //shadow atlas depth framebuffer

		// For shadow mapping we only need a depth attachment
		shadow_atlas_image = rtg.helpers.create_image(
			VkExtent2D{ .width = shadow_atlas.size, .height = shadow_atlas.size }, // size of each face
			depth_format,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped
		);

		VkImageViewCreateInfo depth_stencil_view{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = shadow_atlas_image.handle,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = depth_format,
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
		};
		VK(vkCreateImageView(rtg.device, &depth_stencil_view, nullptr, &Shadow_atlas_view));

		// Create sampler to sample from to depth attachment
		// Used to sample in the fragment shader for shadowed rendering

		VkSamplerCreateInfo sampler {
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.magFilter = VK_FILTER_LINEAR,
			.minFilter = VK_FILTER_LINEAR,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			.addressModeV = sampler.addressModeU,
			.addressModeW = sampler.addressModeU,
			.mipLodBias = 0.0f,
			.maxAnisotropy = 1.0f,
			.compareEnable = VK_TRUE,
			.compareOp = VK_COMPARE_OP_LESS,
			.minLod = 0.0f,
			.maxLod = 1.0f,
			.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
		};
		VK(vkCreateSampler(rtg.device, &sampler, nullptr, &shadow_sampler));


		// Create frame buffer
		VkFramebufferCreateInfo framebuffer_create_info {
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = shadow_atlas_pass,
			.attachmentCount = 1,
			.pAttachments = &Shadow_atlas_view,
			.width = shadow_atlas_length,
			.height = shadow_atlas_length,
			.layers = 1,
		};

		VK (vkCreateFramebuffer(rtg.device, &framebuffer_create_info, nullptr, &shadow_framebuffer));

	}

	background_pipeline.create(rtg, render_pass, 0);
	lines_pipeline.create(rtg, render_pass, 0);
	lambertian_pipeline.create(rtg, render_pass, 0);
	environment_pipeline.create(rtg, render_pass, 0);
	mirror_pipeline.create(rtg, render_pass, 0);
	pbr_pipeline.create(rtg, render_pass, 0);
	shadow_pipeline.create(rtg, shadow_atlas_pass, 0);
	cloud_pipeline.create(rtg);
	cloud_lightgrid_pipeline.create(rtg);

	if (scene.has_cloud) {//cloud resources
		{// lodad cloud voxel data as 3D images
		
			Cloud_noise = Cloud::load_noise(rtg);
			if (scene.cloud->cloud_type == Scene::Cloud::CloudType::PARKOUR) {

				Clouds_NVDF = Cloud::load_cloud(rtg, std::string("../resource/NubisVoxelCloudsPack/NVDFs/Examples/ParkouringCloud/TGA/"));
			}
			else if (scene.cloud->cloud_type == Scene::Cloud::CloudType::STORMBIRD) {
				Clouds_NVDF = Cloud::load_cloud(rtg, std::string("../resource/NubisVoxelCloudsPack/NVDFs/Examples/StormbirdCloud/TGA/"));
			}
			else {
				Clouds_NVDF = Cloud::load_cloud(rtg, scene.cloud->folder_path);
			}
		}

		{//make image view for voxel datas

			VkImageViewCreateInfo create_info{
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.flags = 0,
				.image = Cloud_noise.handle,
				.viewType = VK_IMAGE_VIEW_TYPE_3D,
				.format = Cloud_noise.format,
				// .components sets swizzling and is fine when zero-initialized
				.subresourceRange{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
			};

			VK(vkCreateImageView(rtg.device, &create_info, nullptr, &Cloud_noise_view));

			VkImageViewCreateInfo field_view_create_info{
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.flags = 0,
				.image = Clouds_NVDF.field_data.handle,
				.viewType = VK_IMAGE_VIEW_TYPE_3D,
				.format = Clouds_NVDF.field_data.format,
				// .components sets swizzling and is fine when zero-initialized
				.subresourceRange{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
			};

			VK(vkCreateImageView(rtg.device, &field_view_create_info, nullptr, &Clouds_NVDF.field_data_view));

			VkImageViewCreateInfo model_view_create_info{
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.flags = 0,
				.image = Clouds_NVDF.modeling_data.handle,
				.viewType = VK_IMAGE_VIEW_TYPE_3D,
				.format = Clouds_NVDF.modeling_data.format,
				// .components sets swizzling and is fine when zero-initialized
				.subresourceRange{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
			};

			VK(vkCreateImageView(rtg.device, &model_view_create_info, nullptr, &Clouds_NVDF.modeling_data_view));
			
		}

		{//make a sampler for clouds
			VkSamplerCreateInfo create_info{
				.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
				.flags = 0,
				.magFilter = VK_FILTER_LINEAR,
				.minFilter = VK_FILTER_LINEAR,
				.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
				.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
				.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
				.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
				.mipLodBias = 0.0f,
				.anisotropyEnable = VK_TRUE,
				.maxAnisotropy = 16.0f, 
				.compareEnable = VK_FALSE,
				.compareOp = VK_COMPARE_OP_ALWAYS, //doesn't matter if compare isn't enabled
				.minLod = 0.0f,
				.maxLod = 0.0f,
				.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
				.unnormalizedCoordinates = VK_FALSE,
			};
			VK(vkCreateSampler(rtg.device, &create_info, nullptr, &noise_3D_sampler));
		}

		{//make a sampler for clouds
			VkSamplerCreateInfo create_info{
				.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
				.flags = 0,
				.magFilter = VK_FILTER_LINEAR,
				.minFilter = VK_FILTER_LINEAR,
				.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
				.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
				.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
				.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
				.mipLodBias = 0.0f,
				.anisotropyEnable = VK_TRUE,
				.maxAnisotropy = 16.0f, 
				.compareEnable = VK_FALSE,
				.compareOp = VK_COMPARE_OP_ALWAYS, //doesn't matter if compare isn't enabled
				.minLod = 0.0f,
				.maxLod = 0.0f,
				.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
				.unnormalizedCoordinates = VK_FALSE,
			};
			VK(vkCreateSampler(rtg.device, &create_info, nullptr, &cloud_sampler));
		}
	}

	
	{//create environment texture
		std::string environment_source;
		if (scene.environment.source == "") {
			environment_source  = data_path("../resource/default_environment.png");
		}
		else {
			environment_source = scene.scene_path +"/"+ scene.environment.source;
		}
		int width,height,n;
		std::vector<unsigned char*> images;
		images.push_back(stbi_load(environment_source.c_str(), &width, &height, &n, 4));
		if (images[0] == NULL) throw std::runtime_error("Error loading texture " + environment_source);
		 // cube map must have 6 sides and stacked vertically
		if (height % 6 != 0 || width != height / 6) {
			throw std::runtime_error("Invalid image dimensions for a cubemap");
		}

		uint8_t mip_levels = 1;
		size_t period_index = environment_source.find_last_of(".");
  		std::string base_source = environment_source.substr(0, period_index);
		std::string file_type = environment_source.substr(period_index+1, environment_source.size()-period_index);
		int last_width = width;
		int last_height = height;
		int total_size = width*height;
		// load all ggx mip levels of the environment map
		while (true) {
			// attempt to load the next mip level, if failed, exit
			int cur_width, cur_height, cur_n;
			std::string cur_source = base_source + "." + std::to_string(mip_levels) + "." + file_type;
			images.push_back(stbi_load(cur_source.c_str(), &cur_width, &cur_height, &cur_n, 4));
			if (images[mip_levels] == NULL) {
				if (rtg.configuration.debug) {
					std::cout<<"Environment Loading Completed, " << int(mip_levels) << " mip levels\n";
				}
				break;
			}
			if (cur_width == last_width >> 2 && cur_height == last_height >> 2) {
				throw std::runtime_error("Mip not properly resized");
			}
			if (cur_height % 6 != 0 || cur_width != cur_height / 6) {
				throw std::runtime_error("Invalid image dimensions for a cubemap");
			}
			total_size += cur_width * cur_height;
			last_width = cur_width;
			last_height = cur_height;
			mip_levels++;
		}

		int face_length = width;

		// convert rgbe to rgb values
		std::vector<uint32_t> rgb_image(total_size); // Store the converted RGB data
		int temp_width = width;
		int temp_height = height;
		uint64_t pixel_index = 0;
		for (uint8_t level = 0; level < mip_levels; ++level) {
			for (int i = 0; i < temp_width*temp_height; ++i) {
				glm::u8vec4 rgbe_pixel = glm::u8vec4(images[level][4*i], images[level][4*i + 1], images[level][4*i + 2], images[level][4*i + 3]);
				rgb_image[pixel_index] = rgbe_to_E5B9G9R9(rgbe_pixel);
				++pixel_index;
			}
			temp_width = temp_width >> 1;
			temp_height = temp_height >> 1;
		}

		World_environment = rtg.helpers.create_image(
			VkExtent2D{ .width = uint32_t(face_length), .height = uint32_t(face_length) }, // size of each face
			VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped, 6, mip_levels
		);
		rtg.helpers.transfer_to_image_cube(rgb_image.data(), sizeof(rgb_image[0]) * rgb_image.size(), World_environment, mip_levels);
	
		//free images:
		for (unsigned char* image : images){
			stbi_image_free(image);
		}

		// set world mip level
		world.ENVIRONMENT_MIPS = mip_levels-1;

		{//make image view for environment

			VkImageViewCreateInfo create_info{
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.flags = 0,
				.image = World_environment.handle,
				.viewType = VK_IMAGE_VIEW_TYPE_CUBE,
				.format = World_environment.format,
				// .components sets swizzling and is fine when zero-initialized
				.subresourceRange{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = mip_levels,
					.baseArrayLayer = 0,
					.layerCount = 6,
				},
			};

			VK(vkCreateImageView(rtg.device, &create_info, nullptr, &World_environment_view));
		}

		{//make a sampler for the environment
			VkSamplerCreateInfo create_info{
				.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
				.flags = 0,
				.magFilter = VK_FILTER_LINEAR,
				.minFilter = VK_FILTER_LINEAR,
				.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
				.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
				.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
				.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
				.mipLodBias = 0.0f,
				.anisotropyEnable = VK_FALSE,
				.maxAnisotropy = 0.0f, //doesn't matter if anisotropy isn't enabled
				.compareEnable = VK_FALSE,
				.compareOp = VK_COMPARE_OP_ALWAYS, //doesn't matter if compare isn't enabled
				.minLod = 0.0f,
				.maxLod = float(mip_levels - 1),
				.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
				.unnormalizedCoordinates = VK_FALSE,
			};
			VK(vkCreateSampler(rtg.device, &create_info, nullptr, &World_environment_sampler));
		}
	}

	{//make a sampler for the normal textures and BRDF LUT
		VkSamplerCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.flags = 0,
			.magFilter = VK_FILTER_NEAREST,
			.minFilter = VK_FILTER_NEAREST,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.mipLodBias = 0.0f,
			.anisotropyEnable = VK_FALSE,
			.maxAnisotropy = 0.0f, //doesn't matter if anisotropy isn't enabled
			.compareEnable = VK_FALSE,
			.compareOp = VK_COMPARE_OP_ALWAYS, //doesn't matter if compare isn't enabled
			.minLod = 0.0f,
			.maxLod = 0.0f,
			.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
			.unnormalizedCoordinates = VK_FALSE,
		};
		VK(vkCreateSampler(rtg.device, &create_info, nullptr, &texture_sampler));
	}

	{ // environment BRDF LUT
		{ // create the BRDF LUT
			int width,height,n;
			float* image = stbi_loadf(data_path("../resource/ibl_brdf_lut.png").c_str(), &width, &height, &n, 0);
			if (image == NULL) throw std::runtime_error("Error loading Environment BRDF LUT texture: ../resource/ibl_brdf_lut.png");
			std::vector<float> converted_image(width*height*2);
			for (uint32_t i = 0; i < uint32_t(width * height); ++i) {
				converted_image[i*2] = image[i*3];
				converted_image[i*2+1] = image[i*3+1];
			}
			World_environment_brdf_lut = rtg.helpers.create_image(
				VkExtent2D{ .width = uint32_t(width) , .height = uint32_t(height) }, //size of image
				VK_FORMAT_R32G32_SFLOAT,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, //will sample and upload
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, //should be device-local
				Helpers::Unmapped
			);
			
			rtg.helpers.transfer_to_image(converted_image.data(), sizeof(converted_image[0]) * width * height * 2, World_environment_brdf_lut);
			
			//free image:
			stbi_image_free(image);
		}
		{//make image view for lut

			VkImageViewCreateInfo create_info{
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.flags = 0,
				.image = World_environment_brdf_lut.handle,
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.format = World_environment_brdf_lut.format,
				// .components sets swizzling and is fine when zero-initialized
				.subresourceRange{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
			};

			VK(vkCreateImageView(rtg.device, &create_info, nullptr, &World_environment_brdf_lut_view));
		}
	}

	{//create descriptor pool:
		uint32_t per_workspace = uint32_t(rtg.workspaces.size()); //for easier-to-read counting

		std::array< VkDescriptorPoolSize, 3> pool_sizes{
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.descriptorCount = 2 * per_workspace, //one descriptor per set, two sets per workspace
			},
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 5 * per_workspace, //one descriptor per set, one set per workspace
			},
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 4 * per_workspace, //three descriptor for set 0, one for set 1, one set per workspace
			},
		};
		
		VkDescriptorPoolCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.flags = 0, //because CREATE_FREE_DESCRIPTOR_SET_BIT isn't included, *can't* free individual descriptors allocated from this pool
			.maxSets = 6 * per_workspace, //three sets per workspace
			.poolSizeCount = uint32_t(pool_sizes.size()),
			.pPoolSizes = pool_sizes.data(),
		};

		VK(vkCreateDescriptorPool(rtg.device, &create_info, nullptr, &descriptor_pool));
	}

	if (scene.has_cloud) { //allocate descriptor sets for Cloud descriptor
		VkDescriptorSetAllocateInfo alloc_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = descriptor_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &cloud_pipeline.set1_Cloud,
		};

		VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &Cloud_descriptors));
		VkDescriptorImageInfo Cloud_Model_info{
			.sampler = cloud_sampler,
			.imageView = Clouds_NVDF.modeling_data_view,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};

		VkDescriptorImageInfo Cloud_Field_info{
			.sampler = cloud_sampler,
			.imageView = Clouds_NVDF.field_data_view,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};

		VkDescriptorImageInfo Cloud_Noise_info{
			.sampler = noise_3D_sampler,
			.imageView = Cloud_noise_view,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};

		std::array< VkWriteDescriptorSet, 3 > writes{
			VkWriteDescriptorSet{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = Cloud_descriptors,
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &Cloud_Model_info,
			},

			VkWriteDescriptorSet{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = Cloud_descriptors,
				.dstBinding = 1,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &Cloud_Field_info,
			},

			VkWriteDescriptorSet{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = Cloud_descriptors,
				.dstBinding = 2,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &Cloud_Noise_info,
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

	workspaces.resize(rtg.workspaces.size());
	for (Workspace &workspace : workspaces) {
		{//allocate command buffer
			VkCommandBufferAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
				.commandPool = command_pool,
				.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
				.commandBufferCount = 1,
			};
			VK(vkAllocateCommandBuffers(rtg.device, &alloc_info, &workspace.command_buffer));
		}
	
		workspace.Camera_src = rtg.helpers.create_buffer(
			sizeof(LinesPipeline::Camera),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT, //going to have GPU copy from this memory
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, //host-visible memory, coherent (no special sync needed)
			Helpers::Mapped //get a pointer to the memory
		);
		workspace.Camera = rtg.helpers.create_buffer(
			sizeof(LinesPipeline::Camera),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, //going to use as a uniform buffer, also going to have GPU copy into this memory
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, //GPU-local memory
			Helpers::Unmapped //don't get a pointer to the memory
		);

		{//allocate descriptor set for Camera descriptor
			VkDescriptorSetAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &lines_pipeline.set0_Camera,
			};

			VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &workspace.Camera_descriptors));
		}

		workspace.World_src = rtg.helpers.create_buffer(
			sizeof(LambertianPipeline::World),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			Helpers::Mapped
		);
		workspace.World = rtg.helpers.create_buffer(
			sizeof(LambertianPipeline::World),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped
		);

		workspace.Cloud_World_src = rtg.helpers.create_buffer(
			sizeof(CloudPipeline::CloudWorld),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			Helpers::Mapped
		);

		workspace.Cloud_World = rtg.helpers.create_buffer(
			sizeof(CloudPipeline::CloudWorld),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped
		);
		{// create 3D image and image view for the light grid
			constexpr VkExtent3D lightgrid_extent = {256, 256, 32};
			workspace.Cloud_lightgrid = rtg.helpers.create_image_3D(
				lightgrid_extent, 
				VK_FORMAT_R32G32B32A32_SFLOAT,
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				Helpers::Unmapped
			);

			VkImageViewCreateInfo lightgrid_view_create_info{
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.flags = 0,
				.image = workspace.Cloud_lightgrid.handle,
				.viewType = VK_IMAGE_VIEW_TYPE_3D,
				.format = workspace.Cloud_lightgrid.format,
				// .components sets swizzling and is fine when zero-initialized
				.subresourceRange{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
			};

			VK(vkCreateImageView(rtg.device, &lightgrid_view_create_info, nullptr, &workspace.Cloud_lightgrid_view));
		}

		{ //allocate descriptor set for World descriptor
			VkDescriptorSetAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &lambertian_pipeline.set0_World,
			};

			VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &workspace.World_descriptors));
			//NOTE: will actually fill in this descriptor set just a bit lower
		}

		{//allocate descriptor set for tagert image in cloud compute shader
			VkDescriptorSetAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &cloud_pipeline.set0_World,
			};

			VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &workspace.Cloud_World_descriptors));
		}

		{//allocate descriptor set for tagert image in cloud compute shader
			VkDescriptorSetAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &cloud_pipeline.set0_World,
			};

			VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &workspace.Cloud_LightGrid_World_descriptors));
		}

		{// set light infos
			light_info.sun_light_size = std::max(scene.light_instance_count.sun_light * sizeof(LambertianPipeline::SunLight),
				sizeof(LambertianPipeline::SunLight));
			light_info.sun_light_alignment = rtg.helpers.align_buffer_size(light_info.sun_light_size, rtg.device_properties.limits.minStorageBufferOffsetAlignment);
			light_info.sphere_light_size = std::max(scene.light_instance_count.sphere_light * sizeof(LambertianPipeline::SphereLight), 
				sizeof(LambertianPipeline::SphereLight));
			light_info.sphere_light_alignment = rtg.helpers.align_buffer_size(light_info.sun_light_alignment + light_info.sphere_light_size, rtg.device_properties.limits.minStorageBufferOffsetAlignment);
			light_info.spot_light_size = std::max(scene.light_instance_count.spot_light * sizeof(LambertianPipeline::SpotLight),
				sizeof(LambertianPipeline::SpotLight));
			
			world.SUN_LIGHT_COUNT = scene.light_instance_count.sun_light;
			world.SPHERE_LIGHT_COUNT = scene.light_instance_count.sphere_light;
			world.SPOT_LIGHT_COUNT = scene.light_instance_count.spot_light;
		}

		{// create Light buffers
			size_t needed_bytes = light_info.sphere_light_alignment + light_info.spot_light_size;
			workspace.Light_src = rtg.helpers.create_buffer(
				needed_bytes,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT, //going to have GPU copy from this memory
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, //host-visible memory, coherent (no special sync needed)
				Helpers::Mapped //get a pointer to the memory
			);
			workspace.Light = rtg.helpers.create_buffer(
				needed_bytes,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, //going to use as storage buffer, also going to have GPU into this memory
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, //GPU-local memory
				Helpers::Unmapped //don't get a pointer to the memory
			);
		}
		

		{//allocate descriptor set for Transforms descriptor
			VkDescriptorSetAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &lambertian_pipeline.set1_Transforms,
			};

			VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &workspace.Transforms_descriptors));
			//NOTE: will fill in this descriptor set in render when buffers are [re-]allocated
		}

		{//point descriptors to buffers:
			VkDescriptorBufferInfo Camera_info{
				.buffer = workspace.Camera.handle,
				.offset = 0,
				.range = workspace.Camera.size,
			};

			VkDescriptorBufferInfo World_info{
				.buffer = workspace.World.handle,
				.offset = 0,
				.range = workspace.World.size,
			};

			VkDescriptorBufferInfo SunLight_info{
				.buffer = workspace.Light.handle,
				.offset = 0,
				.range = light_info.sun_light_size,
			};
			VkDescriptorBufferInfo SphereLight_info{
				.buffer = workspace.Light.handle,
				.offset = light_info.sun_light_alignment,
				.range = light_info.sphere_light_size,
			};
			VkDescriptorBufferInfo SpotLight_info{
				.buffer = workspace.Light.handle,
				.offset = light_info.sphere_light_alignment,
				.range = light_info.spot_light_size,
			};

			VkDescriptorImageInfo World_environment_info{
				.sampler = World_environment_sampler,
				.imageView = World_environment_view,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};

			VkDescriptorImageInfo World_environment_brdf_lut_info{
				.sampler = texture_sampler,
				.imageView = World_environment_brdf_lut_view,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};

			VkDescriptorImageInfo ShadowAtlas_info{
				.sampler = shadow_sampler,
				.imageView = Shadow_atlas_view,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};

			std::array< VkWriteDescriptorSet, 8 > writes{
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

				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.World_descriptors,
					.dstBinding = 3,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					.pBufferInfo = &SunLight_info,
				},

				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.World_descriptors,
					.dstBinding = 4,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					.pBufferInfo = &SphereLight_info,
				},

				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.World_descriptors,
					.dstBinding = 5,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					.pBufferInfo = &SpotLight_info,
				},

				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.World_descriptors,
					.dstBinding = 6,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.pImageInfo = &ShadowAtlas_info,
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

		if (scene.has_cloud) {// update cloud descriptors
			VkDescriptorBufferInfo Cloud_World_info{
				.buffer = workspace.Cloud_World.handle,
				.offset = 0,
				.range = workspace.Cloud_World.size,
			};

			VkDescriptorImageInfo Cloud_lightgrid_info{
				.sampler = cloud_sampler,
				.imageView = workspace.Cloud_lightgrid_view,
				.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
			};

			VkDescriptorImageInfo Cloud_lightgrid_sample_info{
				.sampler = cloud_sampler,
				.imageView = workspace.Cloud_lightgrid_view,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};

			std::array<VkWriteDescriptorSet, 4> writes = {

				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.Cloud_World_descriptors,
					.dstBinding = 1,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.pBufferInfo = &Cloud_World_info,
				},

				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.Cloud_LightGrid_World_descriptors,
					.dstBinding = 1,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.pBufferInfo = &Cloud_World_info,
				},

				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.Cloud_LightGrid_World_descriptors,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					.pImageInfo = &Cloud_lightgrid_info,
				},

				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.Cloud_World_descriptors,
					.dstBinding = 2,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.pImageInfo = &Cloud_lightgrid_sample_info,
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
	
	}
	

	{//create object vertices
		std::vector<PosNorTanTexVertex> vertices;
		vertices.resize(scene.vertices_count);
		uint32_t new_vertices_start = 0;
		mesh_vertices.assign(scene.meshes.size(), ObjectVertices());
		mesh_AABBs.assign(scene.meshes.size(),AABB());
		for (uint32_t i = 0; i < uint32_t(scene.meshes.size()); ++i) {
			Scene::Mesh& cur_mesh = scene.meshes[i];
			mesh_vertices[i].count = cur_mesh.count;
			mesh_vertices[i].first = new_vertices_start;  
			std::ifstream file(scene.scene_path + "/" + cur_mesh.attributes[0].source, std::ios::binary); // assuming the attribute layout holds
			if (!file.is_open()) throw std::runtime_error("Error opening file for mesh data: " + scene.scene_path + "/" + cur_mesh.attributes[0].source);
			if (!file.read(reinterpret_cast< char * >(&vertices[new_vertices_start]), cur_mesh.count * sizeof(PosNorTanTexVertex))) {
				throw std::runtime_error("Failed to read mesh data: " + scene.scene_path + "/" + cur_mesh.attributes[0].source);
			}
			//find OOB
			for (size_t vertex_i = mesh_vertices[i].first; vertex_i < (mesh_vertices[i].first + mesh_vertices[i].count); ++vertex_i) {
				glm::vec3 cur_vert_pos = {vertices[vertex_i].Position.x, vertices[vertex_i].Position.y, vertices[vertex_i].Position.z};
				mesh_AABBs[i].min = glm::min(mesh_AABBs[i].min, cur_vert_pos);
				mesh_AABBs[i].max = glm::max(mesh_AABBs[i].max, cur_vert_pos);
			}
			new_vertices_start += cur_mesh.count;
		}
		assert(new_vertices_start == scene.vertices_count);

		size_t bytes = vertices.size() * sizeof(vertices[0]);

		object_vertices = rtg.helpers.create_buffer(
			bytes,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped
		);

		//copy data to buffer:
		rtg.helpers.transfer_to_buffer(vertices.data(), bytes, object_vertices);
	}

	{//make some textures
		textures.reserve(scene.textures.size()); // index 0-4 is the default textures

		// all images loaded should be flipped as s72 file format has the image origin at bottom left while stbi load is top left
		stbi_set_flip_vertically_on_load(true);

		for (uint32_t i = 0; i < scene.textures.size(); ++i) {
			Scene::Texture& cur_texture = scene.textures[i];
			if (cur_texture.has_src) {
				int width,height,n;
				unsigned char* image;
				std::string source = std::get<std::string>(cur_texture.value);
				
				if (cur_texture.single_channel) { // just read the r value
					assert(cur_texture.format != Scene::Texture::RGBE);
					VkFormat format = cur_texture.format == Scene::Texture::Linear ? VK_FORMAT_R8_UNORM : VK_FORMAT_R8_SRGB;
					image = stbi_load((scene.scene_path +"/"+ source).c_str(), &width, &height, &n, 1);
					if (image == NULL) throw std::runtime_error("Error loading texture " + scene.scene_path + "/" + source);
					textures.emplace_back(rtg.helpers.create_image(
						VkExtent2D{ .width = uint32_t(width) , .height = uint32_t(height) }, //size of image
						format,
						VK_IMAGE_TILING_OPTIMAL,
						VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, //will sample and upload
						VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, //should be device-local
						Helpers::Unmapped
					));
					
					rtg.helpers.transfer_to_image(image, sizeof(image[0]) * width*height, textures.back());
				}
				else {
					image = stbi_load((scene.scene_path +"/"+ source).c_str(), &width, &height, &n, 4);
					if (image == NULL) throw std::runtime_error("Error loading texture " + scene.scene_path + "/" + source);		
					if (cur_texture.format == Scene::Texture::RGBE) {
						std::vector<uint32_t> converted_image(width * height);
						for (uint32_t pixel_i = 0; pixel_i< uint32_t(width * height); ++pixel_i) {
							glm::u8vec4 rgbe_pixel = glm::u8vec4(image[4*pixel_i], image[4*pixel_i + 1], image[4*pixel_i + 2], image[4*pixel_i + 3]);
							converted_image[pixel_i] = rgbe_to_E5B9G9R9(rgbe_pixel);
						}
						textures.emplace_back(rtg.helpers.create_image(
							VkExtent2D{ .width = uint32_t(width) , .height = uint32_t(height) }, //size of image
							VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,
							VK_IMAGE_TILING_OPTIMAL,
							VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, //will sample and upload
							VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, //should be device-local
							Helpers::Unmapped
						));
						
						rtg.helpers.transfer_to_image(converted_image.data(), sizeof(converted_image[0])*width*height, textures.back());
					}
					else {
						VkFormat format = cur_texture.format == Scene::Texture::sRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
						textures.emplace_back(rtg.helpers.create_image(
							VkExtent2D{ .width = uint32_t(width) , .height = uint32_t(height) }, //size of image
							format, 
							VK_IMAGE_TILING_OPTIMAL,
							VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, //will sample and upload
							VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, //should be device-local
							Helpers::Unmapped
						));
						
						rtg.helpers.transfer_to_image(image, sizeof(image[0]) * width*height*4, textures.back());
					}
				}
				//free image:
				stbi_image_free(image);
			}
			else {
				if (cur_texture.single_channel) {
					uint8_t value = uint8_t(std::get<float>(cur_texture.value) * 255.0f);
					textures.emplace_back(rtg.helpers.create_image(
						VkExtent2D{ .width = 1 , .height = 1 }, //size of image
						VK_FORMAT_R8_UNORM, //how to interpret image data (in this case, SRGB-encoded 8-bit RGBA)
						VK_IMAGE_TILING_OPTIMAL,
						VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, //will sample and upload
						VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, //should be device-local
						Helpers::Unmapped
					));

					//transfer data:
					rtg.helpers.transfer_to_image(&value, sizeof(uint8_t), textures.back());
				}
				else {
					glm::vec3 value = std::get<glm::vec3>(cur_texture.value);
					uint8_t data[4] = {uint8_t(value.x*255.0f), uint8_t(value.y*255.0f), uint8_t(value.z*255.0f),255};
					//make a place for the texture to live on the GPU:
					textures.emplace_back(rtg.helpers.create_image(
						VkExtent2D{ .width = 1 , .height = 1 }, //size of image
						VK_FORMAT_R8G8B8A8_UNORM, //how to interpret image data (in this case, SRGB-encoded 8-bit RGBA)
						VK_IMAGE_TILING_OPTIMAL,
						VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, //will sample and upload
						VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, //should be device-local
						Helpers::Unmapped
					));

					//transfer data:
					rtg.helpers.transfer_to_image(&data, sizeof(uint8_t) * 4, textures.back());
				}
			}
		}
	}

	{//make image views for the textures
		texture_views.reserve(textures.size());
		for (Helpers::AllocatedImage const &image : textures) {
			VkImageViewCreateInfo create_info{
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.flags = 0,
				.image = image.handle,
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.format = image.format,
				// .components sets swizzling and is fine when zero-initialized
				.subresourceRange{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
			};

			VkImageView image_view = VK_NULL_HANDLE;
			VK(vkCreateImageView(rtg.device, &create_info, nullptr, &image_view));

			texture_views.emplace_back(image_view);
		}
		assert(texture_views.size() == textures.size());
	}

	{//create the material descriptor pool
		uint32_t per_material = uint32_t(scene.materials.size());
		uint32_t per_pbr_material = uint32_t(scene.MatPBR_count);
		uint32_t per_lambertian_material = uint32_t(scene.MatLambertian_count);
		uint32_t per_envmirror_material = uint32_t(scene.MatEnvMirror_count);
		std::array< VkDescriptorPoolSize, 1> pool_sizes{
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1 * 5 * per_pbr_material + 1 * 3 * per_lambertian_material + 1 * 2 * per_envmirror_material,
			},
		};
		
		VkDescriptorPoolCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.flags = 0, //because CREATE_FREE_DESCRIPTOR_SET_BIT isn't included, *can't* free individual descriptors allocated from this pool
			.maxSets = 1 * per_material, //one set per material
			.poolSizeCount = uint32_t(pool_sizes.size()),
			.pPoolSizes = pool_sizes.data(),
		};

		VK(vkCreateDescriptorPool(rtg.device, &create_info, nullptr, &material_descriptor_pool));
		
	}

	{//allocate and write the texture descriptor sets
		//allocate the descriptors (use the material type's alloc info)
		VkDescriptorSetAllocateInfo mat_lambertian_alloc_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = material_descriptor_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &lambertian_pipeline.set2_TEXTURE,
		};
		VkDescriptorSetAllocateInfo mat_pbr_alloc_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = material_descriptor_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &pbr_pipeline.set2_TEXTURE,
		};
		VkDescriptorSetAllocateInfo mat_envmirror_alloc_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = material_descriptor_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &environment_pipeline.set2_TEXTURE,
		};
		material_descriptors.assign(scene.materials.size(), VK_NULL_HANDLE);

		for (uint32_t material_index = 0; material_index < scene.materials.size(); ++material_index) {
			VkDescriptorSet &descriptor_set = material_descriptors[material_index];
			if (scene.materials[material_index].material_type == Scene::Material::Lambertian) {
				VK(vkAllocateDescriptorSets(rtg.device, &mat_lambertian_alloc_info, &descriptor_set));
			}
			else if (scene.materials[material_index].material_type == Scene::Material::PBR) {
				VK(vkAllocateDescriptorSets(rtg.device, &mat_pbr_alloc_info, &descriptor_set));
			}
			else {
				VK(vkAllocateDescriptorSets(rtg.device, &mat_envmirror_alloc_info, &descriptor_set));
			}
		}
		//write descriptors for materials:
		std::vector< VkWriteDescriptorSet > writes(scene.materials.size());
		std::vector< std::array<VkDescriptorImageInfo,5> > infos(scene.materials.size());
		constexpr uint8_t pbr_tex_count = 5;
		constexpr uint8_t lambertian_tex_count = 3;
		constexpr uint8_t envmirror_tex_count = 2;

		for (uint32_t material_index = 0; material_index < scene.materials.size(); ++material_index) {
			const Scene::Material& material = scene.materials[material_index];
			uint8_t cur_material_tex_count = 0;
			std::array<VkDescriptorImageInfo,5>& cur_infos = infos[material_index];
			cur_infos[0] = VkDescriptorImageInfo{
				.sampler = texture_sampler,
				.imageView = texture_views[material.normal_index],
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};
			cur_infos[1] = VkDescriptorImageInfo{
				.sampler = texture_sampler,
				.imageView = texture_views[material.displacement_index],
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};
			if (material.material_type == Scene::Material::Lambertian) {
				cur_material_tex_count = lambertian_tex_count;
				cur_infos[2] = VkDescriptorImageInfo{
					.sampler = texture_sampler,
					.imageView = texture_views[std::get<Scene::Material::MatLambertian>(material.material_textures).albedo_index],
					.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				};
			}
			else if (material.material_type == Scene::Material::PBR) {
				cur_material_tex_count = pbr_tex_count;
				const Scene::Material::MatPBR& pbr_textures = std::get<Scene::Material::MatPBR>(material.material_textures);
				cur_infos[2] = VkDescriptorImageInfo{
					.sampler = texture_sampler,
					.imageView = texture_views[pbr_textures.albedo_index],
					.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				};
				cur_infos[3] = VkDescriptorImageInfo{
					.sampler = texture_sampler,
					.imageView = texture_views[pbr_textures.roughness_index],
					.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				};
				cur_infos[4] = VkDescriptorImageInfo{
					.sampler = texture_sampler,
					.imageView = texture_views[pbr_textures.metalness_index],
					.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				};
			}
			else {
				cur_material_tex_count = envmirror_tex_count;
			}

			writes[material_index] = VkWriteDescriptorSet{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = material_descriptors[material_index],
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = cur_material_tex_count,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &cur_infos[0],
			};
		}

		vkUpdateDescriptorSets(rtg.device, uint32_t(writes.size()), writes.data(), 0, nullptr);
	}

	{ //setup camera if no --camera in the command line, scene camera is set in update
		if (!rtg_.configuration.scene_camera.has_value()) {
			float x = user_camera.radius * std::sin(user_camera.elevation) * std::cos(user_camera.azimuth);
			float y = user_camera.radius * std::sin(user_camera.elevation) * std::sin(user_camera.azimuth);
			float z = user_camera.radius * std::cos(user_camera.elevation);
			//cache view and clip matrices
			view_from_world[1] = glm::make_mat4(look_at(
				x,y,z, //eye
				0.0f, 0.0f, 0.5f, //target
				0.0f, 0.0f, 1.0f //up
			).data());
			clip_from_view[1] = glm::make_mat4(perspective(
				60.0f * float(M_PI) / 180.0f, //vfov
				rtg.swapchain_extent.width / float(rtg.swapchain_extent.height), //aspect
				0.1f, //near
				1000.0f //far
			).data());
			view_from_world[2] = view_from_world[1];
			clip_from_view[2] = clip_from_view[1];
			CLIP_FROM_WORLD = clip_from_view[1] * view_from_world[1];
			
			cloud_world.CAMERA_POSITION = {x, y, z};
			cloud_world.HALF_TAN_FOV = tanf(60.0f * float(M_PI) / 360.0f);
			cloud_world.CAMERA_FAR = 1000.0f;
			cloud_world.CAMERA_NEAR = 0.1f;
			cloud_world.ASPECT_RATIO = rtg.swapchain_extent.width / float(rtg.swapchain_extent.height);

			view_camera = InSceneCamera::UserCamera;
			culling_camera = InSceneCamera::UserCamera;
		}
		else {
			Scene::Camera& cur_camera = scene.cameras[scene.requested_camera_index];
			glm::mat4x4 cur_camera_transform = scene.nodes[cur_camera.local_to_world[0]].transform.parent_from_local();
			for (int i = 1; i < cur_camera.local_to_world.size(); ++i) {
				cur_camera_transform *= scene.nodes[cur_camera.local_to_world[i]].transform.parent_from_local();
			}
			glm::vec3 eye = glm::vec3(cur_camera_transform[3]);
			glm::vec3 forward = -glm::vec3(cur_camera_transform[2]);
			glm::vec3 target = eye + forward;

			view_from_world[0] = glm::make_mat4(look_at(
				eye.x, eye.y, eye.z, //eye
				target.x, target.y, target.z, //target
				0.0f, 0.0f, 1.0f //up
			).data());

			clip_from_view[0] = glm::make_mat4(perspective(
				cur_camera.vfov, //vfov
				cur_camera.aspect, //aspect
				cur_camera.near, //near
				cur_camera.far //far
			).data());

			clip_from_view[1] = glm::make_mat4(perspective(
				60.0f * float(M_PI) / 180.0f, //vfov
				rtg.swapchain_extent.width / float(rtg.swapchain_extent.height), //aspect
				0.1f, //near
				1000.0f //far
			).data());

			clip_from_view[2] = clip_from_view[1];

			CLIP_FROM_WORLD = clip_from_view[0] * view_from_world[0];
			cloud_world.HALF_TAN_FOV = tanf(cur_camera.vfov * 0.5f);
			cloud_world.CAMERA_FAR = cur_camera.far;
			cloud_world.CAMERA_NEAR = cur_camera.near;
			cloud_world.CAMERA_POSITION = eye;
			cloud_world.ASPECT_RATIO = rtg.swapchain_extent.width / float(rtg.swapchain_extent.height);

			view_camera = InSceneCamera::SceneCamera;
		}

		user_camera.type = UserCamera;
		debug_camera.type = DebugCamera;

	}
}

RTGRenderer::~RTGRenderer() {
	//just in case rendering is still in flight, don't destroy resources:
	//(not using VK macro to avoid throw-ing in destructor)
	if (VkResult result = vkDeviceWaitIdle(rtg.device); result != VK_SUCCESS) {
		std::cerr << "Failed to vkDeviceWaitIdle in RTGRenderer::~RTGRenderer [" << string_VkResult(result) << "]; continuing anyway." << std::endl;
	}

	if (material_descriptor_pool) {
		vkDestroyDescriptorPool(rtg.device, material_descriptor_pool, nullptr);
		material_descriptor_pool = nullptr;

		//the above also frees the descriptor sets allocated from the pool:
		material_descriptors.clear();
	}

	if (World_environment_sampler) {
		vkDestroySampler(rtg.device, World_environment_sampler, nullptr);
		World_environment_sampler = VK_NULL_HANDLE;
	}

	if (World_environment_view) {
		vkDestroyImageView(rtg.device, World_environment_view, nullptr);
		World_environment_view = VK_NULL_HANDLE;
	}

	if (World_environment.handle) {
		rtg.helpers.destroy_image(std::move(World_environment));
	}


	if (Clouds_NVDF.field_data_view != VK_NULL_HANDLE) {
		vkDestroyImageView(rtg.device, Clouds_NVDF.field_data_view, nullptr);
		Clouds_NVDF.field_data_view = VK_NULL_HANDLE;
	}
	if (Clouds_NVDF.modeling_data_view != VK_NULL_HANDLE) {
		vkDestroyImageView(rtg.device, Clouds_NVDF.modeling_data_view, nullptr);
		Clouds_NVDF.modeling_data_view = VK_NULL_HANDLE;
	}

	if (Clouds_NVDF.field_data.handle)
		rtg.helpers.destroy_image_3D(std::move(Clouds_NVDF.field_data));
	if (Clouds_NVDF.modeling_data.handle)
		rtg.helpers.destroy_image_3D(std::move(Clouds_NVDF.modeling_data));
	

	if (Cloud_noise_view != VK_NULL_HANDLE) {
		vkDestroyImageView(rtg.device, Cloud_noise_view, nullptr);
		Cloud_noise_view = VK_NULL_HANDLE;
	}
	if (Cloud_noise.handle) {
		rtg.helpers.destroy_image_3D(std::move(Cloud_noise));
	}

	if (cloud_sampler != VK_NULL_HANDLE) {
		vkDestroySampler(rtg.device, cloud_sampler, nullptr);
		cloud_sampler = VK_NULL_HANDLE;
	}

	if (noise_3D_sampler != VK_NULL_HANDLE) {
		vkDestroySampler(rtg.device, noise_3D_sampler, nullptr);
		noise_3D_sampler = VK_NULL_HANDLE;
	}

	if (texture_sampler != VK_NULL_HANDLE) {
		vkDestroySampler(rtg.device, texture_sampler, nullptr);
		texture_sampler = VK_NULL_HANDLE;
	}

	if (World_environment_brdf_lut_view) {
		vkDestroyImageView(rtg.device, World_environment_brdf_lut_view, nullptr);
		World_environment_brdf_lut_view = VK_NULL_HANDLE;
	}

	if (World_environment_brdf_lut.handle) {
		rtg.helpers.destroy_image(std::move(World_environment_brdf_lut));
	}

	for (VkImageView &view : texture_views) {
		vkDestroyImageView(rtg.device, view, nullptr);
		view = VK_NULL_HANDLE;
	}
	texture_views.clear();

	for (auto &texture : textures) {
		rtg.helpers.destroy_image(std::move(texture));
	}
	textures.clear();

	background_pipeline.destroy(rtg);
	lines_pipeline.destroy(rtg);
	lambertian_pipeline.destroy(rtg);
	environment_pipeline.destroy(rtg);
	mirror_pipeline.destroy(rtg);
	pbr_pipeline.destroy(rtg);
	shadow_pipeline.destroy(rtg);
	cloud_pipeline.destroy(rtg);
	cloud_lightgrid_pipeline.destroy(rtg);
	
	rtg.helpers.destroy_buffer(std::move(object_vertices));

	if (shadow_sampler) {
		vkDestroySampler(rtg.device, shadow_sampler, nullptr);
		shadow_sampler = VK_NULL_HANDLE;
	}

	if (Shadow_atlas_view) {
		vkDestroyImageView(rtg.device, Shadow_atlas_view, nullptr);
		Shadow_atlas_view = VK_NULL_HANDLE;
	}

	if (shadow_atlas_image.handle) {
		rtg.helpers.destroy_image(std::move(shadow_atlas_image));
	}

	if (shadow_framebuffer) {
		vkDestroyFramebuffer(rtg.device, shadow_framebuffer, nullptr);
	}


	if (swapchain_depth_image.handle != VK_NULL_HANDLE) {
		destroy_framebuffers();
	}

	for (Workspace &workspace : workspaces) {
		if (workspace.command_buffer != VK_NULL_HANDLE) {
			vkFreeCommandBuffers(rtg.device, command_pool, 1, &workspace.command_buffer);
			workspace.command_buffer = VK_NULL_HANDLE;
		}

		if (workspace.lines_vertices_src.handle != VK_NULL_HANDLE) {
		rtg.helpers.destroy_buffer(std::move(workspace.lines_vertices_src));
		}
		if (workspace.lines_vertices.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.lines_vertices));
		}
		if (workspace.Camera_src.handle != VK_NULL_HANDLE) {
				rtg.helpers.destroy_buffer(std::move(workspace.Camera_src));
			}
		if (workspace.Camera.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Camera));
		}
		// Camera descriptors are freed when the pool is destroyed

		if (workspace.Cloud_World_src.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Cloud_World_src));
		}
		if (workspace.Cloud_World.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Cloud_World));
		}

		if (workspace.World_src.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.World_src));
		}
		if (workspace.World.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.World));
		}

		if (workspace.Light_src.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Light_src));
		}
		if (workspace.Light.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Light));
		}
		//World descriptors freed when pool is destroyed.

		if (workspace.Transforms_src.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Transforms_src));
		}
		if (workspace.Transforms.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Transforms));
		}
		//Transforms_descriptors freed when pool is destroyed.

		if (workspace.Cloud_lightgrid.handle) {
			rtg.helpers.destroy_image_3D(std::move(workspace.Cloud_lightgrid));
		}

		if (workspace.Cloud_lightgrid_view != VK_NULL_HANDLE) {
			vkDestroyImageView(rtg.device, workspace.Cloud_lightgrid_view, nullptr);
			workspace.Cloud_lightgrid_view = VK_NULL_HANDLE;
		}

		if (workspace.Cloud_target.handle) {
			rtg.helpers.destroy_image(std::move(workspace.Cloud_target));
		}

		if (workspace.Cloud_lightgrid_view != VK_NULL_HANDLE) {
			vkDestroyImageView(rtg.device, workspace.Cloud_target_view, nullptr);
			workspace.Cloud_target_view = VK_NULL_HANDLE;
		}
	}
	workspaces.clear();

	if (descriptor_pool) {
		vkDestroyDescriptorPool(rtg.device, descriptor_pool, nullptr);
		descriptor_pool = nullptr;
		//(this also frees the descriptor sets allocated from the pool)
	}

	if (command_pool != VK_NULL_HANDLE) {
		vkDestroyCommandPool(rtg.device, command_pool, nullptr);
		command_pool = VK_NULL_HANDLE;
	}

	if (shadow_atlas_pass != VK_NULL_HANDLE) {
		vkDestroyRenderPass(rtg.device, shadow_atlas_pass, nullptr);
		shadow_atlas_pass = VK_NULL_HANDLE;
	}

	if (render_pass != VK_NULL_HANDLE) {
		vkDestroyRenderPass(rtg.device, render_pass, nullptr);
		render_pass = VK_NULL_HANDLE;
	}
}

void RTGRenderer::on_swapchain(RTG &rtg_, RTG::SwapchainEvent const &swapchain) {
	// clean up existing framebuffers (and depth image):
	if (swapchain_depth_image.handle != VK_NULL_HANDLE) {
		destroy_framebuffers();
	}
	//Allocate depth image for framebuffers to share:
	swapchain_depth_image = rtg.helpers.create_image(
		swapchain.extent,
		depth_format,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		Helpers::Unmapped
	);
	{//create depth image view:
		VkImageViewCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = swapchain_depth_image.handle,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = depth_format,
			.subresourceRange{
				.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			},
		};

		VK(vkCreateImageView(rtg.device, &create_info, nullptr, &swapchain_depth_image_view));
	}
	//Make framebuffers for each swapchain image:
	swapchain_framebuffers.assign(swapchain.image_views.size(), VK_NULL_HANDLE);
	for (size_t i = 0; i < swapchain.image_views.size(); ++i) {
		std::array<VkImageView, 2> attachments{
			swapchain.image_views[i],
			swapchain_depth_image_view,
		};
		VkFramebufferCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = render_pass,
			.attachmentCount = uint32_t(attachments.size()),
			.pAttachments = attachments.data(),
			.width = swapchain.extent.width,
			.height = swapchain.extent.height,
			.layers = 1,
		};

		VK(vkCreateFramebuffer(rtg.device, &create_info, nullptr, &swapchain_framebuffers[i]));
	}
	std::cout<< "There are "<< swapchain.image_views.size() << " images in the swapchain" <<std::endl;

	// target image for cloud rendering
	for (auto& workspace : workspaces) {
		workspace.Cloud_target = rtg.helpers.create_image(
			swapchain.extent,
			VK_FORMAT_R8G8B8A8_SNORM,
			VK_IMAGE_TILING_LINEAR,
			VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, //will sample and upload
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, //should be device-local
			Helpers::Unmapped
		);

		VkImageViewCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.flags = 0,
			.image = workspace.Cloud_target.handle,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = workspace.Cloud_target.format,
			// .components sets swizzling and is fine when zero-initialized
			.subresourceRange{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
		};

		VK(vkCreateImageView(rtg.device, &create_info, nullptr, &workspace.Cloud_target_view));
	}
}

void RTGRenderer::destroy_framebuffers() {
	for (VkFramebuffer &framebuffer : swapchain_framebuffers) {
		assert(framebuffer != VK_NULL_HANDLE);
		vkDestroyFramebuffer(rtg.device, framebuffer, nullptr);
		framebuffer = VK_NULL_HANDLE;
	}
	swapchain_framebuffers.clear();

	assert(swapchain_depth_image_view != VK_NULL_HANDLE);
	vkDestroyImageView(rtg.device, swapchain_depth_image_view, nullptr);
	swapchain_depth_image_view = VK_NULL_HANDLE;

	rtg.helpers.destroy_image(std::move(swapchain_depth_image));

	for (auto& workspace : workspaces) {
		if (workspace.Cloud_target_view) {
			vkDestroyImageView(rtg.device, workspace.Cloud_target_view, nullptr);
			workspace.Cloud_target_view = VK_NULL_HANDLE;
		}
		if (workspace.Cloud_target.handle) {
			rtg.helpers.destroy_image(std::move(workspace.Cloud_target));
		}
	}
}


void RTGRenderer::render(RTG &rtg_, RTG::RenderParams const &render_params) {
	//assert that parameters are valid:
	assert(&rtg == &rtg_);
	assert(render_params.workspace_index < workspaces.size());
	assert(render_params.image_index < swapchain_framebuffers.size());

	// //prevent faulty attempt to render when the swapchain has no area
	// if (rtg.swapchain_extent.width == 0 || rtg.swapchain_extent.height == 0) return;

	//get more convenient names for the current workspace and target framebuffer:
	Workspace &workspace = workspaces[render_params.workspace_index];
	VkFramebuffer framebuffer = swapchain_framebuffers[render_params.image_index];

	//reset the command buffer (clear old commands):
	VK(vkResetCommandBuffer(workspace.command_buffer, 0));

	{//begin recording:
		VkCommandBufferBeginInfo begine_info{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, // records again every submit
		};
		VK(vkBeginCommandBuffer(workspace.command_buffer, &begine_info));
	}

	//copy transforms, needed for both shadow atlas pass and render pass
	if (!lambertian_instances.empty() || !environment_instances.empty() || !mirror_instances.empty() || !pbr_instances.empty()) { //upload object transforms:
		size_t needed_bytes = (lambertian_instances.size() + environment_instances.size() + mirror_instances.size() + pbr_instances.size()) * sizeof(Transform);
		if (workspace.Transforms_src.handle == VK_NULL_HANDLE || workspace.Transforms_src.size < needed_bytes) {
			//round to next multiple of 4k to avoid re-allocating continuously if vertex count grows slowly:
			size_t new_bytes = ((needed_bytes + 4096) / 4096) * 4096;
			if (workspace.Transforms_src.handle) {
				rtg.helpers.destroy_buffer(std::move(workspace.Transforms_src));
			}
			if (workspace.Transforms.handle) {
				rtg.helpers.destroy_buffer(std::move(workspace.Transforms));
			}
			workspace.Transforms_src = rtg.helpers.create_buffer(
				new_bytes,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT, //going to have GPU copy from this memory
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, //host-visible memory, coherent (no special sync needed)
				Helpers::Mapped //get a pointer to the memory
			);
			workspace.Transforms = rtg.helpers.create_buffer(
				new_bytes,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, //going to use as storage buffer, also going to have GPU into this memory
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, //GPU-local memory
				Helpers::Unmapped //don't get a pointer to the memory
			);

			//update the descriptor set:
			VkDescriptorBufferInfo Transforms_info{
				.buffer = workspace.Transforms.handle,
				.offset = 0,
				.range = workspace.Transforms.size,
			};

			std::array< VkWriteDescriptorSet, 1 > writes{
				VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = workspace.Transforms_descriptors,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					.pBufferInfo = &Transforms_info,
				},
			};

			vkUpdateDescriptorSets(
				rtg.device,
				uint32_t(writes.size()), writes.data(), //descriptorWrites count, data
				0, nullptr //descriptorCopies count, data
			);

			std::cout << "Re-allocated object transforms buffers to " << new_bytes << " bytes." << std::endl;
		}

		assert(workspace.Transforms_src.size == workspace.Transforms.size);
		assert(workspace.Transforms_src.size >= needed_bytes);

		{ //copy transforms into Transforms_src:
			assert(workspace.Transforms_src.allocation.mapped);
			LambertianPipeline::Transform *out = reinterpret_cast< LambertianPipeline::Transform * >(workspace.Transforms_src.allocation.data()); // Strict aliasing violation, but it doesn't matter
			for (ObjectInstance const &inst : lambertian_instances) {
				*out = inst.transform;
				++out;
			}
			for (ObjectInstance const &inst : environment_instances) {
				*out = inst.transform;
				++out;
			}
			for (ObjectInstance const &inst : mirror_instances) {
				*out = inst.transform;
				++out;
			}
			for (ObjectInstance const &inst : pbr_instances) {
				*out = inst.transform;
				++out;
			}
		}

		//device-side copy from Transforms_src -> Transforms:
		VkBufferCopy copy_region{
			.srcOffset = 0,
			.dstOffset = 0,
			.size = needed_bytes,
		};
		vkCmdCopyBuffer(workspace.command_buffer, workspace.Transforms_src.handle, workspace.Transforms.handle, 1, &copy_region);
	}

	VkBufferMemoryBarrier buffer_memory_barrier{
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
			.buffer = workspace.Transforms.handle,
			.size = VK_WHOLE_SIZE, 
	};
	vkCmdPipelineBarrier( workspace.command_buffer,
		VK_PIPELINE_STAGE_TRANSFER_BIT, //srcStageMask
		VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, //dstStageMask
		0, //dependencyFlags
		0, nullptr, //memoryBarriers (count, data)
		1, &buffer_memory_barrier, //bufferMemoryBarriers (count, data)
		0, nullptr //imageMemoryBarriers (count, data)
	);

	{//shadow atlas pass:
		std::array<VkClearValue, 1> clear_values{
			VkClearValue{.depthStencil{.depth = 1.0f, .stencil = 0}},
		};
		VkRenderPassBeginInfo begin_info{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.renderPass = shadow_atlas_pass,
			.framebuffer = shadow_framebuffer,
			.renderArea{
				.offset = {.x = 0, .y = 0},
				.extent = {.width = shadow_atlas_length, .height = shadow_atlas_length},
			},
			.clearValueCount = uint32_t(clear_values.size()),
			.pClearValues = clear_values.data(),
		};

		vkCmdBeginRenderPass(workspace.command_buffer, &begin_info, VK_SUBPASS_CONTENTS_INLINE);

		if (!lambertian_instances.empty() || !environment_instances.empty() || !mirror_instances.empty() || !pbr_instances.empty()) {
			//bind Transforms descriptor set:
			std::array< VkDescriptorSet, 1 > descriptor_sets{
				workspace.Transforms_descriptors, //1: Transforms
			};
			vkCmdBindDescriptorSets(
				workspace.command_buffer, //command buffer
				VK_PIPELINE_BIND_POINT_GRAPHICS, //pipeline bind point
				shadow_pipeline.layout, //pipeline layout
				0, //first set
				uint32_t(descriptor_sets.size()), descriptor_sets.data(), //descriptor sets count, ptr
				0, nullptr //dynamic offsets count, ptr
			);
		}
		if (!spot_lights.empty()) {
			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_pipeline.handle);

			{//use object_vertices (offset 0) as vertex buffer binding 0:
				std::array<VkBuffer, 1>vertex_buffers{object_vertices.handle};
				std::array< VkDeviceSize, 1 > offsets{ 0 };
				vkCmdBindVertexBuffers(workspace.command_buffer, 0, uint32_t(vertex_buffers.size()), vertex_buffers.data(), offsets.data());

			}
			for (uint32_t i = 0; i < scene.spot_lights_sorted_indices.size(); ++i) {
				uint32_t light_index = scene.spot_lights_sorted_indices[i].spot_lights_index;
				ShadowAtlas::Region& region = shadow_atlas.regions[light_index];
				if (region.size == 0) continue; // skip shadow of size 0
				spot_lights[light_index].LIGHT_FROM_WORLD = spot_light_from_world[i];
				spot_lights[light_index].ATLAS_COORD_FROM_WORLD = ShadowAtlas::calculate_shadow_atlas_matrix(spot_light_from_world[i],region,shadow_atlas_length);
				{//push light:
					ShadowAtlasPipeline::Light push{
						.LIGHT_FROM_WORLD = spot_light_from_world[i],
					};
					vkCmdPushConstants(workspace.command_buffer, shadow_pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
				}
				{// set viewport and scissors
					VkExtent2D extent = {region.size, region.size};
					VkOffset2D offset = {.x = int32_t(region.x), .y = int32_t(region.y)};

					{//set scissor rectangle:
						VkRect2D scissor{
							.offset = offset,
							.extent = extent,
						};
						vkCmdSetScissor(workspace.command_buffer, 0, 1, &scissor);
					}
					{//configure viewport transform:
						VkViewport viewport{
							.x = float(offset.x),
							.y = float(offset.y),
							.width = float(extent.width),
							.height = float(extent.height),
							.minDepth = 0.0f,
							.maxDepth = 1.0f,
						};
						vkCmdSetViewport(workspace.command_buffer, 0, 1, &viewport);
					}
				}

				//draw all instances:
				for (uint32_t index : in_spot_light_instances[i][static_cast<uint32_t>(Scene::Material::Lambertian)]) {
					ObjectInstance const &inst = lambertian_instances[index];

					vkCmdDraw(workspace.command_buffer, inst.vertices.count, 1, inst.vertices.first, index);
				}

				uint32_t index_offset = uint32_t(lambertian_instances.size());// account for lambertian size
				for (uint32_t index : in_spot_light_instances[i][static_cast<uint32_t>(Scene::Material::Environment)]) {
					ObjectInstance const &inst = environment_instances[index];
					index += index_offset; 
					vkCmdDraw(workspace.command_buffer, inst.vertices.count, 1, inst.vertices.first, index);
				}
				index_offset = uint32_t(lambertian_instances.size() + environment_instances.size());// account for lambertian and environment size
				for (uint32_t index : in_spot_light_instances[i][static_cast<uint32_t>(Scene::Material::Mirror)]) {
					ObjectInstance const &inst = mirror_instances[index];
					index += index_offset; 
					vkCmdDraw(workspace.command_buffer, inst.vertices.count, 1, inst.vertices.first, index);
				}
				index_offset = uint32_t(lambertian_instances.size() + environment_instances.size() + mirror_instances.size());// account for lambertian, environment, and mirror size
				for (uint32_t index : in_spot_light_instances[i][static_cast<uint32_t>(Scene::Material::PBR)]) {
					ObjectInstance const &inst = pbr_instances[index];
					index += index_offset; 
					vkCmdDraw(workspace.command_buffer, inst.vertices.count, 1, inst.vertices.first, index);
				}
			}
		}
		vkCmdEndRenderPass(workspace.command_buffer);
	}

	VkImageMemoryBarrier image_memory_barrier{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.image = shadow_atlas_image.handle,
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1
    	},
	};

	// Command for barrier between render passes
	vkCmdPipelineBarrier(
		workspace.command_buffer,
		VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, // srcStageMask
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, // dstStageMask
		0, // dependencyFlags
		0, nullptr, // memoryBarriers (count, data)
		0, nullptr, // bufferMemoryBarriers (count, data)
		1, &image_memory_barrier // imageMemoryBarriers (count, data)
	);


	if (!lines_vertices.empty()) { //upload lines vertices;
		//[re-]allocate lines buffer if needed:
		size_t needed_bytes = lines_vertices.size() * sizeof(lines_vertices[0]);
		if (workspace.lines_vertices_src.handle == VK_NULL_HANDLE || workspace.lines_vertices_src.size < needed_bytes) {
			//round to next multiple of 4k to avoid re-allocating continuously if vertex count grows slowly:
			size_t new_bytes = ((needed_bytes + 4096) / 4096) * 4096;

			if (workspace.lines_vertices_src.handle) {
				rtg.helpers.destroy_buffer(std::move(workspace.lines_vertices_src));
			}
			if (workspace.lines_vertices.handle) {
				rtg.helpers.destroy_buffer(std::move(workspace.lines_vertices));
			}
			
			workspace.lines_vertices_src = rtg.helpers.create_buffer(
				new_bytes,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT, //going to have GPU copy from this memory
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, //host-visible memory, coherent (no special sync needed)
				Helpers::Mapped //get a pointer to the memory
			);
			workspace.lines_vertices = rtg.helpers.create_buffer(
				new_bytes,
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, //going to use as vertex buffer, also going to have GPU into this memory
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, //GPU-local memory
				Helpers::Unmapped //don't get a pointer to the memory
			);

			std::cout << "Re-allocated lines buffers to " << new_bytes << " bytes." << std::endl;

		}

		assert(workspace.lines_vertices_src.size == workspace.lines_vertices.size);
		assert(workspace.lines_vertices_src.size >= needed_bytes);

		// host-side copy into lines_vertices_src:
		assert(workspace.lines_vertices_src.allocation.mapped);
		std::memcpy(workspace.lines_vertices_src.allocation.data(), lines_vertices.data(), needed_bytes);

		//device-side copy from lines_vertices_src -> lines_vertices:
		VkBufferCopy copy_region{
			.srcOffset = 0,
			.dstOffset = 0,
			.size = needed_bytes,
		};

		vkCmdCopyBuffer(workspace.command_buffer, workspace.lines_vertices_src.handle, workspace.lines_vertices.handle, 1, &copy_region);
	}

	{//upload camera info:
		LinesPipeline::Camera camera{
			.CLIP_FROM_WORLD = CLIP_FROM_WORLD
		};
		assert(workspace.Camera_src.size == sizeof(camera));

		//host-side copy into Camera_src:
		memcpy(workspace.Camera_src.allocation.data(), &camera, sizeof(camera));

		//add device-side copy from Camera_src -> Camera:
		assert(workspace.Camera_src.size == workspace.Camera.size);
		VkBufferCopy copy_region{
			.srcOffset = 0,
			.dstOffset = 0,
			.size = workspace.Camera_src.size,
		};
		vkCmdCopyBuffer(workspace.command_buffer, workspace.Camera_src.handle, workspace.Camera.handle, 1, &copy_region);
	}

	{ //upload world info:
		assert(workspace.World_src.size == sizeof(world));

		//host-side copy into World_src:
		memcpy(workspace.World_src.allocation.data(), &world, sizeof(world));

		//add device-side copy from World_src -> World:
		assert(workspace.World_src.size == workspace.World.size);
		VkBufferCopy copy_region{
			.srcOffset = 0,
			.dstOffset = 0,
			.size = workspace.World_src.size,
		};
		vkCmdCopyBuffer(workspace.command_buffer, workspace.World_src.handle, workspace.World.handle, 1, &copy_region);
	}

	{// upload cloud world info
		assert(workspace.Cloud_World_src.size == sizeof(cloud_world));

		//host-side copy into Cloud_World_src:
		memcpy(workspace.Cloud_World_src.allocation.data(), &cloud_world, sizeof(cloud_world));

		//add device-side copy from Cloud_World_src -> Cloud_World:
		assert(workspace.Cloud_World_src.size == workspace.Cloud_World.size);
		VkBufferCopy copy_region{
			.srcOffset = 0,
			.dstOffset = 0,
			.size = workspace.Cloud_World_src.size,
		};
		vkCmdCopyBuffer(workspace.command_buffer, workspace.Cloud_World_src.handle, workspace.Cloud_World.handle, 1, &copy_region);
	}

	if (!spot_lights.empty() || !sun_lights.empty() || !sphere_lights.empty()) {
		{ //copy lights into Light_src:
			assert(workspace.Light_src.allocation.mapped);
			char * lights_ptr = reinterpret_cast< char * >(workspace.Light_src.allocation.data());
			LambertianPipeline::SunLight *sun_out = reinterpret_cast< LambertianPipeline::SunLight * >(lights_ptr);
			for (LambertianPipeline::SunLight const &inst : sun_lights) {
				*sun_out = inst;
				++sun_out;
			}
			LambertianPipeline::SphereLight *sphere_out = reinterpret_cast< LambertianPipeline::SphereLight * >(lights_ptr + light_info.sun_light_alignment);
			for (LambertianPipeline::SphereLight const &inst : sphere_lights) {
				*sphere_out = inst;
				++sphere_out;
			}
			LambertianPipeline::SpotLight *spot_out = reinterpret_cast< LambertianPipeline::SpotLight * >(lights_ptr + light_info.sphere_light_alignment);
			for (LambertianPipeline::SpotLight const &inst : spot_lights) {
				*spot_out = inst;
				++spot_out;
			}
		}

		//device-side copy from Light_src -> Light:
		VkBufferCopy copy_region{
			.srcOffset = 0,
			.dstOffset = 0,
			.size = light_info.sphere_light_alignment + light_info.spot_light_size,
		};
		vkCmdCopyBuffer(workspace.command_buffer, workspace.Light_src.handle, workspace.Light.handle, 1, &copy_region);
		
	}

	{//memory barrier to make sure copies complete before rendering happens:
		VkMemoryBarrier memory_barrier{
			.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
		};
		vkCmdPipelineBarrier( workspace.command_buffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT, //srcStageMask
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, //dstStageMask
			0, //dependencyFlags
			1, &memory_barrier, //memoryBarriers (count, data)
			0, nullptr, //bufferMemoryBarriers (count, data)
			0, nullptr //imageMemoryBarriers (count, data)
		);
	}

	{//render pass:
		std::array<VkClearValue, 2> clear_values{
			VkClearValue{.color{.float32{0.0f, 0.0f, 0.0f, 1.0f}}},
			VkClearValue{.depthStencil{.depth = 1.0f, .stencil = 0}},
		};
		VkRenderPassBeginInfo begin_info{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.renderPass = render_pass,
			.framebuffer = framebuffer,
			.renderArea{
				.offset = {.x = 0, .y = 0},
				.extent = rtg.swapchain_extent,
			},
			.clearValueCount = uint32_t(clear_values.size()),
			.pClearValues = clear_values.data(),
		};
		vkCmdBeginRenderPass(workspace.command_buffer, &begin_info, VK_SUBPASS_CONTENTS_INLINE);

		{// set viewport and scissors
			VkExtent2D extent = rtg.swapchain_extent;
			VkOffset2D offset = {.x = 0, .y = 0};
			if (view_camera == SceneCamera) {
				float camera_aspect = scene.cameras[scene.requested_camera_index].aspect; // W / H
				float actual_aspect = rtg.swapchain_extent.width / float(rtg.swapchain_extent.height);
				if (actual_aspect < camera_aspect) {
					extent.height = uint32_t(float(extent.width) / camera_aspect);
					offset.y += (rtg.swapchain_extent.height - extent.height) / 2;
				}
				else if (actual_aspect > camera_aspect) {
					extent.width = uint32_t(float(extent.height) * camera_aspect);
					offset.x += (rtg.swapchain_extent.width - extent.width) / 2;
				}
			}

			{//set scissor rectangle:
				VkRect2D scissor{
					.offset = offset,
					.extent = extent,
				};
				vkCmdSetScissor(workspace.command_buffer, 0, 1, &scissor);
			}
			{//configure viewport transform:
				VkViewport viewport{
					.x = float(offset.x),
					.y = float(offset.y),
					.width = float(extent.width),
					.height = float(extent.height),
					.minDepth = 0.0f,
					.maxDepth = 1.0f,
				};
				vkCmdSetViewport(workspace.command_buffer, 0, 1, &viewport);
			}
		}

		// {//draw with the background pipeline:
		// 	vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, background_pipeline.handle);
			
		// 	{//push time:
		// 		BackgroundPipeline::Push push{
		// 			.time = float(time),
		// 		};
		// 		vkCmdPushConstants(workspace.command_buffer, background_pipeline.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
		// 	}
			
		// 	vkCmdDraw(workspace.command_buffer, 3, 1, 0, 0);
		// }

		if (!lines_vertices.empty()) {//draw with the lines pipeline:
			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, lines_pipeline.handle);

			{//use lines_vertices (offset 0) as vertex buffer binding 0:
				std::array< VkBuffer, 1 > vertex_buffers{ workspace.lines_vertices.handle };
				std::array< VkDeviceSize, 1 > offsets{ 0 };
				vkCmdBindVertexBuffers(workspace.command_buffer, 0, uint32_t(vertex_buffers.size()), vertex_buffers.data(), offsets.data());
			}

			{ //bind Camera descriptor set:
				std::array< VkDescriptorSet, 1 > descriptor_sets{
					workspace.Camera_descriptors, //0: Camera
				};
		
				vkCmdBindDescriptorSets(
					workspace.command_buffer, //command buffer
					VK_PIPELINE_BIND_POINT_GRAPHICS, //pipeline bind point
					lines_pipeline.layout, //pipeline layout
					0, //first set
					uint32_t(descriptor_sets.size()), descriptor_sets.data(), //descriptor sets count, ptr
					0, nullptr //dynamic offsets count, ptr
				);
			}

			//draw lines vertices:
			vkCmdDraw(workspace.command_buffer, uint32_t(lines_vertices.size()), 1, 0, 0);
		}

		if (!lambertian_instances.empty() || !environment_instances.empty() || !mirror_instances.empty() || !pbr_instances.empty()) {
			//bind World and Transforms descriptor set:
			std::array< VkDescriptorSet, 2 > descriptor_sets{
				workspace.World_descriptors, //0: World
				workspace.Transforms_descriptors, //1: Transforms
			};
			vkCmdBindDescriptorSets(
				workspace.command_buffer, //command buffer
				VK_PIPELINE_BIND_POINT_GRAPHICS, //pipeline bind point
				lambertian_pipeline.layout, //pipeline layout
				0, //first set
				uint32_t(descriptor_sets.size()), descriptor_sets.data(), //descriptor sets count, ptr
				0, nullptr //dynamic offsets count, ptr
			);
		}

		if (!lambertian_instances.empty()){//draw with the objects pipeline:
			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, lambertian_pipeline.handle);

			{//use object_vertices (offset 0) as vertex buffer binding 0:
				std::array<VkBuffer, 1>vertex_buffers{object_vertices.handle};
				std::array< VkDeviceSize, 1 > offsets{ 0 };
				vkCmdBindVertexBuffers(workspace.command_buffer, 0, uint32_t(vertex_buffers.size()), vertex_buffers.data(), offsets.data());

			}

			// set 1 and 2 still bound

			//draw all instances:
			for (uint32_t index : in_view_instances[static_cast<uint32_t>(Scene::Material::Lambertian)]) {
				ObjectInstance const &inst = lambertian_instances[index];
				//bind texture descriptor set:
				vkCmdBindDescriptorSets(
					workspace.command_buffer, //command buffer
					VK_PIPELINE_BIND_POINT_GRAPHICS, //pipeline bind point
					lambertian_pipeline.layout, //pipeline layout
					2, //second set
					1, &material_descriptors[inst.material_index], //descriptor sets count, ptr
					0, nullptr //dynamic offsets count, ptr
				);

				vkCmdDraw(workspace.command_buffer, inst.vertices.count, 1, inst.vertices.first, index);
			}

		}
	
		if (!environment_instances.empty()) {//draw with the objects pipeline:
			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, environment_pipeline.handle);

			{//use object_vertices as vertex buffer binding 0:
				std::array<VkBuffer, 1>vertex_buffers{object_vertices.handle};
				std::array< VkDeviceSize, 1 > offsets{ 0 };
				vkCmdBindVertexBuffers(workspace.command_buffer, 0, uint32_t(vertex_buffers.size()), vertex_buffers.data(), offsets.data());

			}

			//World descriptor still bound

			//draw all instances:
			uint32_t index_offset = uint32_t(lambertian_instances.size());// account for lambertian size
			for (uint32_t index : in_view_instances[static_cast<uint32_t>(Scene::Material::Environment)]) {
				ObjectInstance const &inst = environment_instances[index];
				index += index_offset;
				//bind texture descriptor set:
				vkCmdBindDescriptorSets(
					workspace.command_buffer, //command buffer
					VK_PIPELINE_BIND_POINT_GRAPHICS, //pipeline bind point
					environment_pipeline.layout, //pipeline layout
					2, //second set
					1, &material_descriptors[inst.material_index], //descriptor sets count, ptr
					0, nullptr //dynamic offsets count, ptr
				);
				vkCmdDraw(workspace.command_buffer, inst.vertices.count, 1, inst.vertices.first, index);
			}

		}

		if (!mirror_instances.empty()) {//draw with the objects pipeline:
			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mirror_pipeline.handle);

			{//use object_vertices as vertex buffer binding 0:
				std::array<VkBuffer, 1>vertex_buffers{object_vertices.handle};
				std::array< VkDeviceSize, 1 > offsets{ 0 };
				vkCmdBindVertexBuffers(workspace.command_buffer, 0, uint32_t(vertex_buffers.size()), vertex_buffers.data(), offsets.data());
			}

			//World descriptor still bound

			//draw all instances:
			uint32_t index_offset = uint32_t(lambertian_instances.size() + environment_instances.size());// account for lambertian and environment size
			for (uint32_t index : in_view_instances[static_cast<uint32_t>(Scene::Material::Mirror)]) {
				ObjectInstance const &inst = mirror_instances[index];
				index += index_offset;
				//bind texture descriptor set:
				vkCmdBindDescriptorSets(
					workspace.command_buffer, //command buffer
					VK_PIPELINE_BIND_POINT_GRAPHICS, //pipeline bind point
					mirror_pipeline.layout, //pipeline layout
					2, //second set
					1, &material_descriptors[inst.material_index], //descriptor sets count, ptr
					0, nullptr //dynamic offsets count, ptr
				);
				vkCmdDraw(workspace.command_buffer, inst.vertices.count, 1, inst.vertices.first, index);
			}

		}
		if (!pbr_instances.empty()) {//draw with the objects pipeline:
			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pbr_pipeline.handle);

			{//use object_vertices as vertex buffer binding 0:
				std::array<VkBuffer, 1>vertex_buffers{object_vertices.handle};
				std::array< VkDeviceSize, 1 > offsets{ 0 };
				vkCmdBindVertexBuffers(workspace.command_buffer, 0, uint32_t(vertex_buffers.size()), vertex_buffers.data(), offsets.data());
			}

			//World descriptor still bound

			//draw all instances:
			uint32_t index_offset = uint32_t(lambertian_instances.size() + environment_instances.size() + mirror_instances.size());// account for lambertian, environment, and mirror size
			for (uint32_t index : in_view_instances[static_cast<uint32_t>(Scene::Material::PBR)]) {
				ObjectInstance const &inst = pbr_instances[index];
				index += index_offset;
				//bind texture descriptor set:
				vkCmdBindDescriptorSets(
					workspace.command_buffer, //command buffer
					VK_PIPELINE_BIND_POINT_GRAPHICS, //pipeline bind point
					pbr_pipeline.layout, //pipeline layout
					2, //second set
					1, &material_descriptors[inst.material_index], //descriptor sets count, ptr
					0, nullptr //dynamic offsets count, ptr
				);
				vkCmdDraw(workspace.command_buffer, inst.vertices.count, 1, inst.vertices.first, index);
			}

		}
	
		vkCmdEndRenderPass(workspace.command_buffer);
	}

	if (scene.has_cloud){// cloud rendering
		VkImageSubresourceRange whole_image{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		};

		VkImageSubresourceRange whole_image_depth{
			.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		};
		{ // cloud light grid
			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, cloud_lightgrid_pipeline.handle);

			{// transfer target image to desired format: VK_IMAGE_LAYOUT_GENERAL
				std::array<VkImageMemoryBarrier, 1> barriers{
					VkImageMemoryBarrier{
						.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
						.srcAccessMask = 0,
						.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
						.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, //throw away old image
						.newLayout = VK_IMAGE_LAYOUT_GENERAL,
						.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
						.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
						.image = workspace.Cloud_lightgrid.handle,
						.subresourceRange = whole_image,
					},
				};

				vkCmdPipelineBarrier(
					workspace.command_buffer, //commandBuffer
					VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, //srcStageMask
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, //dstStageMask
					0, //dependencyFlags
					0, nullptr, //memory barrier count, pointer
					0, nullptr, //buffer memory barrier count, pointer
					uint32_t(barriers.size()), barriers.data() //image memory barrier count, pointer
				);
			}

			vkCmdBindDescriptorSets(
				workspace.command_buffer, //command buffer
				VK_PIPELINE_BIND_POINT_COMPUTE, //pipeline bind point
				cloud_pipeline.layout, //pipeline layout
				0, //first set
				1, &workspace.Cloud_LightGrid_World_descriptors, //descriptor sets count, ptr
				0, nullptr //dynamic offsets count, ptr
			);

			vkCmdBindDescriptorSets(
				workspace.command_buffer, //command buffer
				VK_PIPELINE_BIND_POINT_COMPUTE, //pipeline bind point
				cloud_pipeline.layout, //pipeline layout
				1, //second set
				1, &Cloud_descriptors, //descriptor sets count, ptr
				0, nullptr //dynamic offsets count, ptr
			);

			uint32_t groups_x = (workspace.Cloud_lightgrid.extent.width + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
			uint32_t groups_y = (workspace.Cloud_lightgrid.extent.height + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;

			vkCmdDispatch(workspace.command_buffer,
				groups_x,
				groups_y,
				workspace.Cloud_lightgrid.extent.depth
			);
		}

		{// transfer depth image to desired format
			std::array<VkImageMemoryBarrier, 1> barriers{
				VkImageMemoryBarrier{
					.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
					.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
					.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
					.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
					.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
					.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
					.image = swapchain_depth_image.handle,
					.subresourceRange = whole_image_depth,
				},
			};

			vkCmdPipelineBarrier(
				workspace.command_buffer, //commandBuffer
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, //srcStageMask
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, //dstStageMask
				0, //dependencyFlags
				0, nullptr, //memory barrier count, pointer
				0, nullptr, //buffer memory barrier count, pointer
				uint32_t(barriers.size()), barriers.data() //image memory barrier count, pointer
			);
		}

		VkDescriptorImageInfo Cloud_target_info{
			.sampler = texture_sampler,
			.imageView = workspace.Cloud_target_view,
			.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
		};

		VkDescriptorImageInfo render_pass_image_info{
			.sampler = VK_NULL_HANDLE,
			.imageView = rtg.swapchain_image_views[render_params.image_index],
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};

		VkDescriptorImageInfo render_pass_depth_image_info{
			.sampler = VK_NULL_HANDLE,
			.imageView = swapchain_depth_image_view,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};

		std::array<VkWriteDescriptorSet, 3> writes = {

			VkWriteDescriptorSet{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = workspace.Cloud_World_descriptors,
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.pImageInfo = &Cloud_target_info,
			},
			VkWriteDescriptorSet{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = workspace.Cloud_World_descriptors,
				.dstBinding = 3,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
				.pImageInfo = &render_pass_image_info,
			},
			VkWriteDescriptorSet{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = workspace.Cloud_World_descriptors,
				.dstBinding = 4,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
				.pImageInfo = &render_pass_depth_image_info,
			},

		};

		vkUpdateDescriptorSets(
			rtg.device, //device
			uint32_t(writes.size()), //descriptorWriteCount
			writes.data(), //pDescriptorWrites
			0, //descriptorCopyCount
			nullptr //pDescriptorCopies
		);

		std::array<VkImageMemoryBarrier, 1> lightgrid_barriers{
			VkImageMemoryBarrier{
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
				.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.image = workspace.Cloud_lightgrid.handle,
				.subresourceRange = whole_image,
			},
		};

		vkCmdPipelineBarrier(
			workspace.command_buffer, //commandBuffer
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, //srcStageMask
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, //dstStageMask
			0, //dependencyFlags
			0, nullptr, //memory barrier count, pointer
			0, nullptr, //buffer memory barrier count, pointer
			uint32_t(lightgrid_barriers.size()), lightgrid_barriers.data() //image memory barrier count, pointer
		);

		std::array<VkImageMemoryBarrier, 1> target_barriers{
			VkImageMemoryBarrier{
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = 0,
				.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, //throw away old image
				.newLayout = VK_IMAGE_LAYOUT_GENERAL,
				.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.image = workspace.Cloud_target.handle,
				.subresourceRange = whole_image,
			
			},
		};

		vkCmdPipelineBarrier(
			workspace.command_buffer, //commandBuffer
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, //srcStageMask
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, //dstStageMask
			0, //dependencyFlags
			0, nullptr, //memory barrier count, pointer
			0, nullptr, //buffer memory barrier count, pointer
			uint32_t(target_barriers.size()), target_barriers.data() //image memory barrier count, pointer
		);

		

		vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, cloud_pipeline.handle);

		vkCmdBindDescriptorSets(
			workspace.command_buffer, //command buffer
			VK_PIPELINE_BIND_POINT_COMPUTE, //pipeline bind point
			cloud_pipeline.layout, //pipeline layout
			0, //first set
			1, &workspace.Cloud_World_descriptors, //descriptor sets count, ptr
			0, nullptr //dynamic offsets count, ptr
		);

		const glm::ivec2 swapchain_dimensions(swapchain_depth_image.extent.width, swapchain_depth_image.extent.height);

		uint32_t groups_x = (swapchain_dimensions.x + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
		uint32_t groups_y = (swapchain_dimensions.y + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;

		vkCmdDispatch(workspace.command_buffer,
			groups_x,
			groups_y,
			1
		);

		{ // transfer to swapchain
			VkExtent3D image_extent = { workspace.Cloud_target.extent.width, workspace.Cloud_target.extent.height, 1 };
			VkImageMemoryBarrier barriers[2] = {
				// Barrier for compute storage image
				{
					.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
					.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, // From compute shader
					.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
					.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
					.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
					.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
					.image = workspace.Cloud_target.handle,
					.subresourceRange = {
						.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						.baseMipLevel = 0,
						.levelCount = 1,
						.baseArrayLayer = 0,
						.layerCount = 1,
					},
				},
				// Barrier for framebuffer image
				{
					.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
					.srcAccessMask = 0,
					.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
					.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
					.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
					.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
					.image = rtg.swapchain_images[render_params.image_index],
					.subresourceRange = {
						.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						.baseMipLevel = 0,
						.levelCount = 1,
						.baseArrayLayer = 0,
						.layerCount = 1,
					},
				},
			};

			vkCmdPipelineBarrier(
				workspace.command_buffer,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, // From compute shader
				VK_PIPELINE_STAGE_TRANSFER_BIT,      // For transfer operation
				0,
				0, nullptr,
				0, nullptr,
				2, barriers
			);


			VkImageBlit blitRegion = {
				.srcSubresource = {
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.mipLevel = 0,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
				.srcOffsets = {
					{0, 0, 0}, // srcOffset[0]
					{(int32_t)image_extent.width, (int32_t)image_extent.height, 1} // srcOffset[1]
				},
				.dstSubresource = {
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.mipLevel = 0,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
				.dstOffsets = {
					{0, 0, 0}, // dstOffset[0]
					{(int32_t)image_extent.width, (int32_t)image_extent.height, 1} // dstOffset[1]
				}
			};

			vkCmdBlitImage(
				workspace.command_buffer,
				workspace.Cloud_target.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				rtg.swapchain_images[render_params.image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1, &blitRegion,
				VK_FILTER_NEAREST // no filter required
			);

			// Transition framebuffer image for presentation
			VkImageMemoryBarrier framebuffer_barrier = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				.newLayout = rtg.configuration.headless_mode ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
				.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
				.image = rtg.swapchain_images[render_params.image_index],
				.subresourceRange = {
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
			};

			vkCmdPipelineBarrier(
				workspace.command_buffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				0,
				0, nullptr,
				0, nullptr,
				1, &framebuffer_barrier
			);
		}
	}
	
	


	//end recording:
	VK(vkEndCommandBuffer(workspace.command_buffer));

	{//submit `workspace.command buffer` for the GPU to run:
		std::array<VkSemaphore, 1> wait_semaphores{
			render_params.image_available
		};
		std::array<VkPipelineStageFlags,1> wait_stages{
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
		};
		static_assert(wait_semaphores.size() == wait_stages.size(), "every semaphore needs a stage");

		std::array<VkSemaphore, 1>signal_semaphores{
			render_params.image_done
		};
		VkSubmitInfo submit_info{
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount = uint32_t(wait_semaphores.size()),
			.pWaitSemaphores = wait_semaphores.data(),
			.pWaitDstStageMask = wait_stages.data(),
			.commandBufferCount = 1,
			.pCommandBuffers = &workspace.command_buffer,
			.signalSemaphoreCount = uint32_t(signal_semaphores.size()),
			.pSignalSemaphores = signal_semaphores.data(),
		};
		if (rtg.configuration.headless_mode) {
			VK(vkQueueSubmit(rtg.graphics_queue, 1, &submit_info, VK_NULL_HANDLE))
		}
		else {
			VK(vkQueueSubmit(rtg.graphics_queue, 1, &submit_info, render_params.workspace_available))
		}
	}
}


void RTGRenderer::update(float dt) {
	time = std::fmod(time + dt, 60.0f);

	{//update the animations according to the drivers
		scene.animation_setting = rtg.configuration.animation_settings;
		scene.update_drivers(dt);
	}

	// set scene camera for animation purposes
	if (view_camera == InSceneCamera::SceneCamera || culling_camera == InSceneCamera::SceneCamera){
		Scene::Camera& cur_camera = scene.cameras[scene.requested_camera_index];
		glm::mat4x4 cur_camera_transform = scene.nodes[cur_camera.local_to_world[0]].transform.parent_from_local();
		for (int i = 1; i < cur_camera.local_to_world.size(); ++i) {
			cur_camera_transform *= scene.nodes[cur_camera.local_to_world[i]].transform.parent_from_local();
		}
		glm::vec3 eye = glm::vec3(cur_camera_transform[3]);
		glm::vec3 forward = -glm::vec3(cur_camera_transform[2]);
		glm::vec3 target = eye + forward;
		glm::vec3 world_up = glm::vec3(0.0f, 0.0f, 1.0f);
		if (glm::abs(glm::dot(forward, world_up)) > 0.999f) {
			world_up = glm::vec3(0.0f, 1.0f, 0.0f);
		}
		glm::vec3 right = glm::normalize(glm::cross(forward, world_up));
		glm::vec3 up = glm::normalize(glm::cross(right, forward));

		view_from_world[0] = glm::make_mat4(look_at(
			eye.x, eye.y, eye.z, //eye
			target.x, target.y, target.z, //target
			up.x, up.y, up.z //up
		).data());

		clip_from_view[0] = glm::make_mat4(perspective(
			cur_camera.vfov, //vfov
			cur_camera.aspect, //aspect
			cur_camera.near, //near
			cur_camera.far //far
		).data());

		if (view_camera == InSceneCamera::SceneCamera) {
			CLIP_FROM_WORLD = clip_from_view[0] * view_from_world[0];
			world.CAMERA_POSITION = eye;
			cloud_world.CAMERA_POSITION = eye;
			cloud_world.HALF_TAN_FOV = tanf(cur_camera.vfov * 0.5f);
			cloud_world.CAMERA_FAR = cur_camera.far;
			cloud_world.CAMERA_NEAR = cur_camera.near;
			cloud_world.ASPECT_RATIO = cur_camera.aspect;
		}

	} 
	if (view_camera != InSceneCamera::SceneCamera) { // check if aspect ratio changed
		static float last_aspect = float(rtg.swapchain_extent.width) / float(rtg.swapchain_extent.height);

		if (last_aspect != float(rtg.swapchain_extent.width) / float(rtg.swapchain_extent.height)) {
			clip_from_view[1] = glm::make_mat4(perspective(
				60.0f * float(M_PI) / 180.0f, //vfov
				rtg.swapchain_extent.width / float(rtg.swapchain_extent.height), //aspect
				0.1f, //near
				1000.0f //far
			).data());

			clip_from_view[2] = clip_from_view[1];
			FreeCamera& cam = (view_camera == InSceneCamera::UserCamera) ? user_camera : debug_camera;
			update_free_camera(cam);
			last_aspect = float(rtg.swapchain_extent.width) / float(rtg.swapchain_extent.height);

			cloud_world.HALF_TAN_FOV = tanf(60.0f * float(M_PI) / 360.0f);
			cloud_world.CAMERA_FAR = 1000.0f;
			cloud_world.CAMERA_NEAR = 0.1f;
			cloud_world.ASPECT_RATIO = last_aspect;
		}

		FreeCamera& cur_camera = view_camera == InSceneCamera::UserCamera ? user_camera : debug_camera;
		world.CAMERA_POSITION = cur_camera.eye;
	}

	const std::array<glm::vec4, 8> clip_space_coordinates = {
		glm::vec4(1.0f,  1.0f, 0.0f, 1.0f),   // Near top right
		glm::vec4(-1.0f,  1.0f, 0.0f, 1.0f),  // Near top left
		glm::vec4(1.0f, -1.0f, 0.0f, 1.0f),   // Near bottom right
		glm::vec4(-1.0f, -1.0f, 0.0f, 1.0f),  // Near bottom left
		glm::vec4(1.0f,  1.0f, 1.0f, 1.0f),   // Far top right
		glm::vec4(-1.0f,  1.0f, 1.0f, 1.0f),  // Far top left
		glm::vec4(1.0f, -1.0f, 1.0f, 1.0f),   // Far bottom right
		glm::vec4(-1.0f, -1.0f, 1.0f, 1.0f)   // Far bottom left
	};
	std::vector<std::array<glm::vec3, 8>> light_frustums;

	{// get light frustums for shadow atlas
		spot_light_from_world.clear();
		light_frustums.resize(scene.spot_lights_sorted_indices.size());
		for (uint32_t i = 0; i < scene.spot_lights_sorted_indices.size(); ++i) {
			Scene::Light& cur_light = scene.lights[scene.spot_lights_sorted_indices[i].lights_index];
			assert(cur_light.light_type == Scene::Light::LightType::Spot); // only support spot for now
			total_shadow_size += cur_light.shadow * cur_light.shadow;
			glm::mat4x4 cur_light_transform = scene.nodes[scene.spot_lights_sorted_indices[i].local_to_world[0]].transform.parent_from_local();
			for (int j = 1; j < scene.spot_lights_sorted_indices[i].local_to_world.size(); ++j) {
				cur_light_transform *= scene.nodes[scene.spot_lights_sorted_indices[i].local_to_world[j]].transform.parent_from_local();
			}
			
			{//create light frustum and light from world matrices
				glm::vec3 eye = glm::vec3(cur_light_transform[3]);
				glm::vec3 forward = -glm::vec3(cur_light_transform[2]);
				glm::vec3 target = eye + forward;

				glm::vec3 world_up = glm::vec3(0.0f, 0.0f, 1.0f);
				if (glm::abs(glm::dot(forward, world_up)) > 0.999f) {
					world_up = glm::vec3(0.0f, 1.0f, 0.0f);
				}
				glm::vec3 right = glm::normalize(glm::cross(forward, world_up));
				glm::vec3 up = glm::normalize(glm::cross(right, forward));

				
				Scene::Light::ParamSpot spot_param = std::get<Scene::Light::ParamSpot>(cur_light.additional_params);
				float aspect = 1.0f; 
				float near = 0.02f;
				float far;
				if (spot_param.limit == 0.0f) {
					far = std::sqrt(glm::length(spot_param.power * cur_light.tint) / (float(M_PI) * 4.0f * 0.001f));
				}
				else {
					far = spot_param.limit;
				}

				glm::mat4 projection = glm::make_mat4(perspective(spot_param.fov, aspect, near, far).data());
				glm::mat4 view = glm::make_mat4(look_at(
					eye.x, eye.y, eye.z, //eye
					target.x, target.y, target.z, //target
					up.x, up.y, up.z //up
				).data());
				spot_light_from_world.emplace_back(projection * view);


				glm::mat4x4 world_from_clip = glm::inverse(spot_light_from_world.back());
				// Transform clip space to world space and apply perspective divide
				for (int j = 0; j < 8; ++j) {
					glm::vec4 world_space_vertex = world_from_clip * clip_space_coordinates[j];
					light_frustums[i][j] = glm::vec3(world_space_vertex) / world_space_vertex.w;
				}

			}
		}
	}

	lines_vertices.clear();
	std::array<glm::vec3, 8> frustum_vertices;

	if (rtg.configuration.culling_settings == 1) { // frustum culling is on
		glm::mat4x4 world_from_clip = glm::inverse(culling_camera == SceneCamera ? clip_from_view[0] * view_from_world[0] : clip_from_view[1]* view_from_world[1]);
		// Transform clip space to world space and apply perspective divide
		for (int j = 0; j < 8; ++j) {
			glm::vec4 world_space_vertex = world_from_clip * clip_space_coordinates[j];
			frustum_vertices[j] = glm::vec3(world_space_vertex) / world_space_vertex.w;
		}
	}
	{// render last active frustum if in debug mode
		if (view_camera == DebugCamera) {
			if (rtg.configuration.culling_settings != 1) {
				glm::mat4x4 world_from_clip = glm::inverse(culling_camera == SceneCamera ? clip_from_view[0] * view_from_world[0] : clip_from_view[1]* view_from_world[1]);
				// Transform clip space to world space and apply perspective divide
				for (int j = 0; j < 8; ++j) {
					glm::vec4 world_space_vertex = world_from_clip * clip_space_coordinates[j];
					frustum_vertices[j] = glm::vec3(world_space_vertex) / world_space_vertex.w;
				}
			}

			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = frustum_vertices[0].x, .y = frustum_vertices[0].y, .z = frustum_vertices[0].z},
				.Color{ .r = 0xff, .g = 0x00, .b = 0xc9, .a = 0xff},
			});
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = frustum_vertices[1].x, .y = frustum_vertices[1].y, .z = frustum_vertices[1].z},
				.Color{ .r = 0xff, .g = 0x00, .b = 0xc9, .a = 0xff},
			});
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = frustum_vertices[0].x, .y = frustum_vertices[0].y, .z = frustum_vertices[0].z},
				.Color{ .r = 0xff, .g = 0x00, .b = 0xc9, .a = 0xff},
			});
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = frustum_vertices[2].x, .y = frustum_vertices[2].y, .z = frustum_vertices[2].z},
				.Color{ .r = 0xff, .g = 0x00, .b = 0xc9, .a = 0xff},
			});
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = frustum_vertices[2].x, .y = frustum_vertices[2].y, .z = frustum_vertices[2].z},
				.Color{ .r = 0xff, .g = 0x00, .b = 0xc9, .a = 0xff},
			});
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = frustum_vertices[3].x, .y = frustum_vertices[3].y, .z = frustum_vertices[3].z},
				.Color{ .r = 0xff, .g = 0x00, .b = 0xc9, .a = 0xff},
			});
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = frustum_vertices[3].x, .y = frustum_vertices[3].y, .z = frustum_vertices[3].z},
				.Color{ .r = 0xff, .g = 0x00, .b = 0xc9, .a = 0xff},
			});
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = frustum_vertices[1].x, .y = frustum_vertices[1].y, .z = frustum_vertices[1].z},
				.Color{ .r = 0xff, .g = 0x00, .b = 0xc9, .a = 0xff},
			});
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = frustum_vertices[0].x, .y = frustum_vertices[0].y, .z = frustum_vertices[0].z},
				.Color{ .r = 0xff, .g = 0x00, .b = 0xc9, .a = 0xff},
			});
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = frustum_vertices[4].x, .y = frustum_vertices[4].y, .z = frustum_vertices[4].z},
				.Color{ .r = 0xff, .g = 0x00, .b = 0xc9, .a = 0xff},
			});
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = frustum_vertices[4].x, .y = frustum_vertices[4].y, .z = frustum_vertices[4].z},
				.Color{ .r = 0xff, .g = 0x00, .b = 0xc9, .a = 0xff},
			});
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = frustum_vertices[6].x, .y = frustum_vertices[6].y, .z = frustum_vertices[6].z},
				.Color{ .r = 0xff, .g = 0x00, .b = 0xc9, .a = 0xff},
			});
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = frustum_vertices[2].x, .y = frustum_vertices[2].y, .z = frustum_vertices[2].z},
				.Color{ .r = 0xff, .g = 0x00, .b = 0xc9, .a = 0xff},
			});
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = frustum_vertices[6].x, .y = frustum_vertices[6].y, .z = frustum_vertices[6].z},
				.Color{ .r = 0xff, .g = 0x00, .b = 0xc9, .a = 0xff},
			});
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = frustum_vertices[4].x, .y = frustum_vertices[4].y, .z = frustum_vertices[4].z},
				.Color{ .r = 0xff, .g = 0x00, .b = 0xc9, .a = 0xff},
			});
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = frustum_vertices[5].x, .y = frustum_vertices[5].y, .z = frustum_vertices[5].z},
				.Color{ .r = 0xff, .g = 0x00, .b = 0xc9, .a = 0xff},
			});
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = frustum_vertices[6].x, .y = frustum_vertices[6].y, .z = frustum_vertices[6].z},
				.Color{ .r = 0xff, .g = 0x00, .b = 0xc9, .a = 0xff},
			});
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = frustum_vertices[7].x, .y = frustum_vertices[7].y, .z = frustum_vertices[7].z},
				.Color{ .r = 0xff, .g = 0x00, .b = 0xc9, .a = 0xff},
			});
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = frustum_vertices[1].x, .y = frustum_vertices[1].y, .z = frustum_vertices[1].z},
				.Color{ .r = 0xff, .g = 0x00, .b = 0xc9, .a = 0xff},
			});
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = frustum_vertices[5].x, .y = frustum_vertices[5].y, .z = frustum_vertices[5].z},
				.Color{ .r = 0xff, .g = 0x00, .b = 0xc9, .a = 0xff},
			});
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = frustum_vertices[3].x, .y = frustum_vertices[3].y, .z = frustum_vertices[3].z},
				.Color{ .r = 0xff, .g = 0x00, .b = 0xc9, .a = 0xff},
			});
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = frustum_vertices[7].x, .y = frustum_vertices[7].y, .z = frustum_vertices[7].z},
				.Color{ .r = 0xff, .g = 0x00, .b = 0xc9, .a = 0xff},
			});
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = frustum_vertices[5].x, .y = frustum_vertices[5].y, .z = frustum_vertices[5].z},
				.Color{ .r = 0xff, .g = 0x00, .b = 0xc9, .a = 0xff},
			});
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = frustum_vertices[7].x, .y = frustum_vertices[7].y, .z = frustum_vertices[7].z},
				.Color{ .r = 0xff, .g = 0x00, .b = 0xc9, .a = 0xff},
			});
		}
	}

	{ //fill object instances with scene hiearchy, optionally draw debug lines when on debug camera, fill light information
		for (uint32_t i = 0; i < in_view_instances.size(); ++i) {
			in_view_instances[i].clear();
		}
		in_spot_light_instances.resize(scene.spot_lights_sorted_indices.size());
		for (uint32_t i = 0; i < in_spot_light_instances.size(); ++i) {
			for (uint32_t j = 0; j < in_spot_light_instances[0].size(); ++j) {
				in_spot_light_instances[i][j].clear();
			}
		}
		lambertian_instances.clear();
		environment_instances.clear();
		mirror_instances.clear();
		pbr_instances.clear();
		//clear lights
		sun_lights.clear();
		sphere_lights.clear();
		spot_lights.clear();
		spot_light_from_world.clear();
		// culling resources
		glm::mat4x4 frustum_view_from_world = culling_camera == SceneCamera ? view_from_world[0] : view_from_world[1];

		std::deque<glm::mat4x4> transform_stack;
		std::function<void(uint32_t)> collect_node_information = [&](uint32_t i) {
			Scene::Node& cur_node = scene.nodes[i];
			glm::mat4x4 cur_node_transform_in_parent = cur_node.transform.parent_from_local();
			if (transform_stack.empty()) {
				transform_stack.push_back(cur_node_transform_in_parent);
			}
			else {
				glm::mat4x4 parent_node_transform_in_world = transform_stack.back();
				transform_stack.push_back(parent_node_transform_in_world * cur_node_transform_in_parent);
			}
			// gather light information
			if (uint32_t cur_light_index = cur_node.light_index; cur_light_index != -1) {
				glm::mat4x4 WORLD_FROM_LOCAL = transform_stack.back();
				Scene::Light& cur_light = scene.lights[cur_light_index];
				
				glm::vec3 tint = cur_light.tint;
				if (cur_light.light_type == Scene::Light::Sun) {

					glm::vec3 light_direction = glm::mat3x3(WORLD_FROM_LOCAL) * glm::vec3(0.0f,0.0f,1.0f);
					Scene::Light::ParamSun sun_param = std::get<Scene::Light::ParamSun>(cur_light.additional_params);
					sun_lights.emplace_back(LambertianPipeline::SunLight{
						.DIRECTION = glm::vec4(light_direction, 0.0f),
						.ENERGY = sun_param.strength * tint / float(M_PI),
						.SIN_ANGLE = sin(sun_param.angle/2.0f)
					});
				}
				else if (cur_light.light_type == Scene::Light::Sphere) {

					glm::vec3 light_position = WORLD_FROM_LOCAL * glm::vec4(0.0f,0.0f,0.0f,1.0f);
					Scene::Light::ParamSphere sphere_param = std::get<Scene::Light::ParamSphere>(cur_light.additional_params);
					sphere_lights.emplace_back(LambertianPipeline::SphereLight{
						.POSITION = glm::vec4(light_position, 0.0f),
						.RADIUS = sphere_param.radius,
						.ENERGY = sphere_param.power * tint / float(M_PI),
						.LIMIT = sphere_param.limit,
					});
				}
				else if (cur_light.light_type == Scene::Light::Spot) {

					glm::vec3 light_position = WORLD_FROM_LOCAL * glm::vec4(0.0f,0.0f,0.0f,1.0f);
					glm::vec3 light_direction = glm::mat3x3(WORLD_FROM_LOCAL) * glm::vec3(0.0f,0.0f,1.0f);
					Scene::Light::ParamSpot spot_param = std::get<Scene::Light::ParamSpot>(cur_light.additional_params);
					
					float outer_angle = spot_param.fov / 2.0f;
					float inner_angle = (1.0f - spot_param.blend) * outer_angle;
					spot_lights.emplace_back(LambertianPipeline::SpotLight{
						.POSITION = glm::vec4(light_position, 0.0f),
						.shadow_size = cur_light.shadow,
						.DIRECTION = light_direction,
						.RADIUS = spot_param.radius,
						.ENERGY = spot_param.power * tint / float(M_PI),
						.LIMIT = spot_param.limit,
						.CONE_ANGLES = glm::vec4(inner_angle, outer_angle, 0.0f, 0.0f),
					});
				}
			}

			for (uint32_t child_index : cur_node.children) {
				collect_node_information(child_index);
			}
			// draw own mesh
			if (int32_t cur_mesh_index = cur_node.mesh_index; cur_mesh_index != -1) {
				glm::mat4x4 WORLD_FROM_LOCAL = transform_stack.back();
				glm::mat4x4 WORLD_FROM_LOCAL_NORMAL = glm::mat4x4(glm::inverse(glm::transpose(glm::mat3(WORLD_FROM_LOCAL))));
				OBB obb = AABB_transform_to_OBB(WORLD_FROM_LOCAL, mesh_AABBs[cur_mesh_index]);
				{//draw debug obb and frustum
					
					if (view_camera == DebugCamera) {//debug draw the OBBs
						std::array<glm::vec3,8> vertices = {
							obb.center + obb.extents[0] * obb.axes[0] + obb.extents[1]*obb.axes[1] + obb.extents[2]*obb.axes[2],
							obb.center + obb.extents[0] * obb.axes[0] + obb.extents[1]*obb.axes[1] - obb.extents[2]*obb.axes[2],
							obb.center + obb.extents[0] * obb.axes[0] - obb.extents[1]*obb.axes[1] + obb.extents[2]*obb.axes[2],
							obb.center + obb.extents[0] * obb.axes[0] - obb.extents[1]*obb.axes[1] - obb.extents[2]*obb.axes[2],
							obb.center - obb.extents[0] * obb.axes[0] + obb.extents[1]*obb.axes[1] + obb.extents[2]*obb.axes[2],
							obb.center - obb.extents[0] * obb.axes[0] + obb.extents[1]*obb.axes[1] - obb.extents[2]*obb.axes[2],
							obb.center - obb.extents[0] * obb.axes[0] - obb.extents[1]*obb.axes[1] + obb.extents[2]*obb.axes[2],
							obb.center - obb.extents[0] * obb.axes[0] - obb.extents[1]*obb.axes[1] - obb.extents[2]*obb.axes[2]
						};

						lines_vertices.emplace_back(PosColVertex{
							.Position{.x = vertices[0].x, .y = vertices[0].y, .z = vertices[0].z},
							.Color{ .r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff},
						});
						lines_vertices.emplace_back(PosColVertex{
							.Position{.x = vertices[1].x, .y = vertices[1].y, .z = vertices[1].z},
							.Color{ .r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff},
						});
						lines_vertices.emplace_back(PosColVertex{
							.Position{.x = vertices[0].x, .y = vertices[0].y, .z = vertices[0].z},
							.Color{ .r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff},
						});
						lines_vertices.emplace_back(PosColVertex{
							.Position{.x = vertices[2].x, .y = vertices[2].y, .z = vertices[2].z},
							.Color{ .r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff},
						});
						lines_vertices.emplace_back(PosColVertex{
							.Position{.x = vertices[2].x, .y = vertices[2].y, .z = vertices[2].z},
							.Color{ .r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff},
						});
						lines_vertices.emplace_back(PosColVertex{
							.Position{.x = vertices[3].x, .y = vertices[3].y, .z = vertices[3].z},
							.Color{ .r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff},
						});
						lines_vertices.emplace_back(PosColVertex{
							.Position{.x = vertices[3].x, .y = vertices[3].y, .z = vertices[3].z},
							.Color{ .r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff},
						});
						lines_vertices.emplace_back(PosColVertex{
							.Position{.x = vertices[1].x, .y = vertices[1].y, .z = vertices[1].z},
							.Color{ .r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff},
						});
						lines_vertices.emplace_back(PosColVertex{
							.Position{.x = vertices[0].x, .y = vertices[0].y, .z = vertices[0].z},
							.Color{ .r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff},
						});
						lines_vertices.emplace_back(PosColVertex{
							.Position{.x = vertices[4].x, .y = vertices[4].y, .z = vertices[4].z},
							.Color{ .r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff},
						});
						lines_vertices.emplace_back(PosColVertex{
							.Position{.x = vertices[4].x, .y = vertices[4].y, .z = vertices[4].z},
							.Color{ .r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff},
						});
						lines_vertices.emplace_back(PosColVertex{
							.Position{.x = vertices[6].x, .y = vertices[6].y, .z = vertices[6].z},
							.Color{ .r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff},
						});
						lines_vertices.emplace_back(PosColVertex{
							.Position{.x = vertices[2].x, .y = vertices[2].y, .z = vertices[2].z},
							.Color{ .r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff},
						});
						lines_vertices.emplace_back(PosColVertex{
							.Position{.x = vertices[6].x, .y = vertices[6].y, .z = vertices[6].z},
							.Color{ .r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff},
						});
						lines_vertices.emplace_back(PosColVertex{
							.Position{.x = vertices[4].x, .y = vertices[4].y, .z = vertices[4].z},
							.Color{ .r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff},
						});
						lines_vertices.emplace_back(PosColVertex{
							.Position{.x = vertices[5].x, .y = vertices[5].y, .z = vertices[5].z},
							.Color{ .r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff},
						});
						lines_vertices.emplace_back(PosColVertex{
							.Position{.x = vertices[6].x, .y = vertices[6].y, .z = vertices[6].z},
							.Color{ .r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff},
						});
						lines_vertices.emplace_back(PosColVertex{
							.Position{.x = vertices[7].x, .y = vertices[7].y, .z = vertices[7].z},
							.Color{ .r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff},
						});
						lines_vertices.emplace_back(PosColVertex{
							.Position{.x = vertices[1].x, .y = vertices[1].y, .z = vertices[1].z},
							.Color{ .r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff},
						});
						lines_vertices.emplace_back(PosColVertex{
							.Position{.x = vertices[5].x, .y = vertices[5].y, .z = vertices[5].z},
							.Color{ .r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff},
						});
						lines_vertices.emplace_back(PosColVertex{
							.Position{.x = vertices[3].x, .y = vertices[3].y, .z = vertices[3].z},
							.Color{ .r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff},
						});
						lines_vertices.emplace_back(PosColVertex{
							.Position{.x = vertices[7].x, .y = vertices[7].y, .z = vertices[7].z},
							.Color{ .r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff},
						});
						lines_vertices.emplace_back(PosColVertex{
							.Position{.x = vertices[5].x, .y = vertices[5].y, .z = vertices[5].z},
							.Color{ .r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff},
						});
						lines_vertices.emplace_back(PosColVertex{
							.Position{.x = vertices[7].x, .y = vertices[7].y, .z = vertices[7].z},
							.Color{ .r = 0xff, .g = 0x00, .b = 0x00, .a = 0xff},
						});
					}
					
				}

				if (uint32_t cur_material_index = scene.meshes[cur_mesh_index].material_index; cur_material_index != -1) { /// has some material
					const Scene::Material& cur_material = scene.materials[scene.meshes[cur_mesh_index].material_index];
					uint32_t instance_index = 0;
					if (cur_material.material_type == Scene::Material::MaterialType::Lambertian) {
						instance_index = uint32_t(lambertian_instances.size());
						lambertian_instances.emplace_back(ObjectInstance{
							.vertices = mesh_vertices[cur_mesh_index],
							.transform{
								.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL,
								.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL,
								.WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL_NORMAL,
							},
							.material_index = cur_material_index,
						});
					}
					else if (cur_material.material_type == Scene::Material::MaterialType::Environment) {
						instance_index = uint32_t(environment_instances.size());
						environment_instances.emplace_back(ObjectInstance{
							.vertices = mesh_vertices[cur_mesh_index],
							.transform{
								.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL,
								.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL,
								.WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL_NORMAL,
							},
							.material_index = cur_material_index,
						});
					}
					else if (cur_material.material_type == Scene::Material::MaterialType::Mirror) {
						instance_index = uint32_t(mirror_instances.size());
						mirror_instances.emplace_back(ObjectInstance{
							.vertices = mesh_vertices[cur_mesh_index],
							.transform{
								.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL,
								.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL,
								.WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL_NORMAL,
							},
							.material_index = cur_material_index,
						});
					}
					else if (cur_material.material_type == Scene::Material::MaterialType::PBR) {
						instance_index = uint32_t(pbr_instances.size());
						pbr_instances.emplace_back(ObjectInstance{
							.vertices = mesh_vertices[cur_mesh_index],
							.transform{
								.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL,
								.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL,
								.WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL_NORMAL,
							},
							.material_index = cur_material_index,
						});
					}
					if (rtg.configuration.culling_settings == 1 && check_frustum_obb_intersection(frustum_vertices, obb)) {
						in_view_instances[static_cast<uint32_t>(cur_material.material_type)].push_back(instance_index);
					}
					for (uint32_t frustum_i = 0; frustum_i < in_spot_light_instances.size(); ++frustum_i) {
						if (check_frustum_obb_intersection(light_frustums[frustum_i], obb)) {
							in_spot_light_instances[frustum_i][static_cast<uint32_t>(cur_material.material_type)].push_back(instance_index);
						}
					}
				}
				else {
					if (rtg.configuration.culling_settings == 1 && check_frustum_obb_intersection(frustum_vertices, obb)) {
						in_view_instances[0].push_back(uint32_t(lambertian_instances.size()));
					}
					for (uint32_t frustum_i = 0; frustum_i < in_spot_light_instances.size(); ++frustum_i) {
						if (check_frustum_obb_intersection(light_frustums[i], obb)) {
							in_spot_light_instances[frustum_i][0].push_back(uint32_t(lambertian_instances.size()));
						}
					}
					// use lambertian pipeline to render the default albedo, displacement and normal maps
					lambertian_instances.emplace_back(ObjectInstance{
						.vertices = mesh_vertices[cur_mesh_index],
						.transform{
							.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL,
							.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL,
							.WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL_NORMAL,
						},
						.material_index = 0,//default material
					});
				}
			}
			
			transform_stack.pop_back();
		};

		//traverse the scene hiearchy:
		for (uint32_t i = 0; i < scene.root_nodes.size(); ++i) {
			transform_stack.clear();
			collect_node_information(scene.root_nodes[i]);
		}
	}

	{// shadow map atlas organization

		// reduce shadow map size if requesting too many
		uint8_t reduction = 0;
		while (total_shadow_size > shadow_atlas_length * shadow_atlas_length) {
			total_shadow_size/= 4;
			++reduction;
		}
		shadow_atlas.update_regions(spot_lights, scene.spot_lights_sorted_indices, reduction);
		//reset total_shadow_size
		total_shadow_size = 0;
	}

	{ // cloud world information
		cloud_world.VIEW_FROM_WORLD = view_from_world[view_camera];
		cloud_world.TIME += dt;
		cloud_world.CLOUD_ANIMATE_OFFSET = glm::vec2(1);
		if (!sun_lights.empty()) {
			cloud_world.SUN_DIRECTION.x = sun_lights[0].DIRECTION.x;
			cloud_world.SUN_DIRECTION.y = sun_lights[0].DIRECTION.y;
			cloud_world.SUN_DIRECTION.z = sun_lights[0].DIRECTION.z;
		}
		else {
			cloud_world.SUN_DIRECTION = glm::vec3(0,0,1);
		}
	}

}


void RTGRenderer::on_input(InputEvent const &event) {
	bool update_camera = false;

	// switches camera mode from scene, view, and debug
	if (event.type == InputEvent::Type::KeyDown && (event.key.key == GLFW_KEY_1 || event.key.key == GLFW_KEY_2 || event.key.key == GLFW_KEY_3)) {
		
		if (event.key.key == GLFW_KEY_1) {
			if (scene.cameras.empty()) {
				std::cerr << "There are no scene camera in the scene, unable to switch\n";
			}
			else {
				view_camera = InSceneCamera::SceneCamera;
				culling_camera = InSceneCamera::SceneCamera;
			}
			return;
		}
		else if (event.key.key == GLFW_KEY_2) {
			view_camera = InSceneCamera::UserCamera;
			culling_camera = InSceneCamera::UserCamera;
		}
		else if (event.key.key == GLFW_KEY_3) {
			view_camera = InSceneCamera::DebugCamera;
		}
		update_camera = true;
	}

	if (view_camera == InSceneCamera::SceneCamera) {
		if (event.type == InputEvent::Type::KeyDown) {
			// Swapping between scene cameras
			if (event.key.key == GLFW_KEY_LEFT) {
				if (scene.cameras.size() == 1) {
					std::cout<<"Only one camera available, unable to switch to another scene camera"<<std::endl;
					return;
				}
				scene.requested_camera_index = (scene.requested_camera_index - 1 + int32_t(scene.cameras.size())) % scene.cameras.size();
				std::cout<< "Now viewing through camera: " + scene.cameras[scene.requested_camera_index].name<< " with index " << scene.requested_camera_index <<std::endl;
			} else if (event.key.key == GLFW_KEY_RIGHT) {
				if (scene.cameras.size() == 1) {
					std::cout<<"Only one camera available, unable to switch to another scene camera"<<std::endl;
					return;
				}
				scene.requested_camera_index = (scene.requested_camera_index + 1) % scene.cameras.size();
				std::cout<< "Now viewing through camera: " + scene.cameras[scene.requested_camera_index].name<< " with index " << scene.requested_camera_index <<std::endl;
			}
		}
		return;
	}

	FreeCamera& cam = (view_camera == InSceneCamera::UserCamera) ? user_camera : debug_camera;
	switch (event.type) {
		case InputEvent::Type::MouseMotion:
			//orbit
			if (event.motion.state && !shift_down) {
				if (previous_mouse_x != -1.0f) {
					update_camera = true;
					if (upside_down) {
						cam.azimuth += (event.motion.x - previous_mouse_x) * 3.0f;
					}
					else {
						cam.azimuth -= (event.motion.x - previous_mouse_x) * 3.0f;
					}
					cam.elevation += (event.motion.y - previous_mouse_y) * 3.0f;
					cam.azimuth = fmod(cam.azimuth, 2.0f * float(M_PI));
					cam.elevation = fmod(cam.elevation, 2.0f * float(M_PI));
				}

				previous_mouse_x = event.motion.x;
				previous_mouse_y = event.motion.y;
			} 
			else if (event.motion.state && shift_down) {
				//pan
				if (previous_mouse_x != -1.0f) {
					update_camera = true;
					glm::vec3 forward = glm::normalize(cam.target - cam.eye);  // Forward direction
					glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 0.0f, 1.0f))); // Right vector
					glm::vec3 up = glm::cross(right, forward);  // Up vector
		
					float pan_sensitivity = 2.0f * std::max(cam.radius,0.5f);
					if (upside_down) {
						cam.target += right * (event.motion.x - previous_mouse_x) * pan_sensitivity;
					}
					else {
						cam.target -= right * (event.motion.x - previous_mouse_x) * pan_sensitivity;
					}
					cam.target += up * (event.motion.y - previous_mouse_y) * pan_sensitivity;
				}
				previous_mouse_x = event.motion.x;
				previous_mouse_y = event.motion.y;
			}
			break;
		case InputEvent::Type::KeyDown:
			if (event.key.key == GLFW_KEY_LEFT_SHIFT) {
				shift_down = true;
			}
			break;
		case InputEvent::Type::KeyUp:
			if (event.key.key == GLFW_KEY_LEFT_SHIFT) {
				shift_down = false;
			}
			break;
		case InputEvent::Type::MouseButtonUp:
			previous_mouse_x = -1.0f;
			break;
		case InputEvent::Type::MouseButtonDown:
			upside_down = (int((abs(cam.elevation) + float(M_PI) / 2) / float(M_PI)) % 2 == 1);
			break;
		case InputEvent::Type::MouseWheel:
			cam.radius = std::max(cam.radius - event.wheel.y*5.0f, 0.001f);
			update_camera = true;
			break;
	}
	
	if (update_camera) {
		update_free_camera(cam);
	}

}

void RTGRenderer::set_animation_time(float t)
{
	scene.set_driver_time(t);
}

void RTGRenderer::update_free_camera(FreeCamera &cam)
{
	assert(cam.type != SceneCamera);
	float x = cam.radius * std::cos(cam.elevation) * std::cos(cam.azimuth);
	float y = cam.radius * std::cos(cam.elevation) * std::sin(cam.azimuth);
	float z = cam.radius * std::sin(cam.elevation);
	float up = 1.0f;
	// flip up axis when upside down
	if (int((abs(cam.elevation) + float(M_PI) / 2) / float(M_PI)) % 2 == 1 ) {
		up =-1.0f;
	}
	cam.eye = glm::vec3{x,y,z} + cam.target;
	uint8_t type = static_cast<uint8_t>(cam.type);
	view_from_world[type] = glm::make_mat4(look_at(
		cam.eye.x,cam.eye.y,cam.eye.z, //eye
		cam.target.x,cam.target.y,cam.target.z, //target
		0.0f, 0.0f, up //up
	).data());

	CLIP_FROM_WORLD = clip_from_view[type] * view_from_world[type];
	cloud_world.CAMERA_POSITION = cam.eye;
}
