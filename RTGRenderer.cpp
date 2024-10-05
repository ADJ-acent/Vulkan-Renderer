#ifdef _WIN32
//ensure we have M_PI
#define _USE_MATH_DEFINES
#endif
#include <GLFW/glfw3.h>
#include "RTGRenderer.hpp"

#include "VK.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <deque>
#include <iostream>
#include <fstream>

RTGRenderer::RTGRenderer(RTG &rtg_, Scene &scene_) : rtg(rtg_), scene(scene_) {
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
				.finalLayout = rtg.configuration.headless_mode ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			},
			VkAttachmentDescription{//1 - depth attachment:
				.format = depth_format,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
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

	background_pipeline.create(rtg, render_pass, 0);
	lines_pipeline.create(rtg, render_pass, 0);
	objects_pipeline.create(rtg, render_pass, 0);

	{//create descriptor pool:
		uint32_t per_workspace = uint32_t(rtg.workspaces.size()); //for easier-to-read counting

		std::array< VkDescriptorPoolSize, 2> pool_sizes{
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.descriptorCount = 2 * per_workspace, //one descriptor per set, two sets per workspace
			},
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1 * per_workspace, //one descriptor per set, one set per workspace
			},
		};
		
		VkDescriptorPoolCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.flags = 0, //because CREATE_FREE_DESCRIPTOR_SET_BIT isn't included, *can't* free individual descriptors allocated from this pool
			.maxSets = 3 * per_workspace, //three sets per workspace
			.poolSizeCount = uint32_t(pool_sizes.size()),
			.pPoolSizes = pool_sizes.data(),
		};

		VK(vkCreateDescriptorPool(rtg.device, &create_info, nullptr, &descriptor_pool));
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
			sizeof(ObjectsPipeline::World),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			Helpers::Mapped
		);
		workspace.World = rtg.helpers.create_buffer(
			sizeof(ObjectsPipeline::World),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped
		);

		{ //allocate descriptor set for World descriptor
			VkDescriptorSetAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &objects_pipeline.set0_World,
			};

			VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &workspace.World_descriptors));
			//NOTE: will actually fill in this descriptor set just a bit lower
		}

		{//allocate descriptor set for Transforms descriptor
			VkDescriptorSetAllocateInfo alloc_info{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = descriptor_pool,
				.descriptorSetCount = 1,
				.pSetLayouts = &objects_pipeline.set1_Transforms,
			};

			VK( vkAllocateDescriptorSets(rtg.device, &alloc_info, &workspace.Transforms_descriptors) );
			//NOTE: will fill in this descriptor set in render when buffers are [re-]allocated
		}

		{//point descriptor to Camera buffer:
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

			std::array< VkWriteDescriptorSet, 2 > writes{
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
		textures.reserve(scene.textures.size() + 1); // index 0 is the default texture
		{//default material
			uint8_t data[4] = {255,255,255,255};
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

		for (uint32_t i = 0; i < scene.textures.size(); ++i) {
			Scene::Texture& cur_texture = scene.textures[i];
			if (cur_texture.has_src) {
				int width,height,n;
				// Flip the image vertically in-place as s72 file format has the image origin at bottom left while stbi load is top left
				stbi_set_flip_vertically_on_load(true);
				unsigned char *image = stbi_load((scene.scene_path +"/"+ cur_texture.source).c_str(), &width, &height, &n, 4);
				if (image == NULL) throw std::runtime_error("Error loading texture " + scene.scene_path + cur_texture.source);		
				//make a place for the texture to live on the GPU:
				textures.emplace_back(rtg.helpers.create_image(
					VkExtent2D{ .width = uint32_t(width) , .height = uint32_t(height) }, //size of image
					VK_FORMAT_R8G8B8A8_UNORM, //how to interpret image data (in this case, linearly-encoded 8-bit RGBA) TODO: double check format
					VK_IMAGE_TILING_OPTIMAL,
					VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, //will sample and upload
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, //should be device-local
					Helpers::Unmapped
				));
				std::cout<<width<<", "<<height<<", "<< n <<std::endl;
				rtg.helpers.transfer_to_image(image, sizeof(image[0]) * width*height*4, textures.back());
				//free image:
				stbi_image_free(image);
			}
			else {
				uint8_t data[4] = {uint8_t(cur_texture.value.x*255.0f), uint8_t(cur_texture.value.y*255.0f), uint8_t(cur_texture.value.z*255.0f),255};
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

	{//make a sampler for the textures
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
		
	{//create the texture descriptor pool
		uint32_t per_texture = uint32_t(textures.size()); //for easier-to-read counting

		std::array< VkDescriptorPoolSize, 1> pool_sizes{
			VkDescriptorPoolSize{
				.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1 * 1 * per_texture, //one descriptor per set, one set per texture
			},
		};
		
		VkDescriptorPoolCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.flags = 0, //because CREATE_FREE_DESCRIPTOR_SET_BIT isn't included, *can't* free individual descriptors allocated from this pool
			.maxSets = 1 * per_texture, //one set per texture
			.poolSizeCount = uint32_t(pool_sizes.size()),
			.pPoolSizes = pool_sizes.data(),
		};

		VK(vkCreateDescriptorPool(rtg.device, &create_info, nullptr, &texture_descriptor_pool));
	}

	{//allocate and write the texture descriptor sets
		//allocate the descriptors (using the same alloc_info):
		VkDescriptorSetAllocateInfo alloc_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = texture_descriptor_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &objects_pipeline.set2_TEXTURE,
		};
		texture_descriptors.assign(textures.size(), VK_NULL_HANDLE);

		for (VkDescriptorSet &descriptor_set : texture_descriptors) {
			VK(vkAllocateDescriptorSets(rtg.device, &alloc_info, &descriptor_set));
		}

		//write descriptors for textures:
		std::vector< VkDescriptorImageInfo > infos(textures.size());
		std::vector< VkWriteDescriptorSet > writes(textures.size());

		for (Helpers::AllocatedImage const &image : textures) {
			size_t i = &image - &textures[0];
			
			infos[i] = VkDescriptorImageInfo{
				.sampler = texture_sampler,
				.imageView = texture_views[i],
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};
			writes[i] = VkWriteDescriptorSet{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = texture_descriptors[i],
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.pImageInfo = &infos[i],
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

			scene_cam_frustum = make_frustum(
				cur_camera.vfov, //vfov
				cur_camera.aspect, //aspect
				cur_camera.near, //near
				cur_camera.far //far
			);

			clip_from_view[1] = glm::make_mat4(perspective(
				60.0f * float(M_PI) / 180.0f, //vfov
				rtg.swapchain_extent.width / float(rtg.swapchain_extent.height), //aspect
				0.1f, //near
				1000.0f //far
			).data());

			clip_from_view[2] = clip_from_view[1];

			CLIP_FROM_WORLD = clip_from_view[0] * view_from_world[0];

			view_camera = InSceneCamera::SceneCamera;
		}

		user_camera.type = UserCamera;
		debug_camera.type = DebugCamera;
		user_cam_frustum = make_frustum(
			60.0f * float(M_PI) / 180.0f, //vfov
			rtg.swapchain_extent.width / float(rtg.swapchain_extent.height), //aspect
			0.1f, //near
			1000.0f //far
		);

	}
}

RTGRenderer::~RTGRenderer() {
	//just in case rendering is still in flight, don't destroy resources:
	//(not using VK macro to avoid throw-ing in destructor)
	if (VkResult result = vkDeviceWaitIdle(rtg.device); result != VK_SUCCESS) {
		std::cerr << "Failed to vkDeviceWaitIdle in RTGRenderer::~RTGRenderer [" << string_VkResult(result) << "]; continuing anyway." << std::endl;
	}

	if (texture_descriptor_pool) {
		vkDestroyDescriptorPool(rtg.device, texture_descriptor_pool, nullptr);
		texture_descriptor_pool = nullptr;

		//this also frees the descriptor sets allocated from the pool:
		texture_descriptors.clear();
	}

	if (texture_sampler) {
		vkDestroySampler(rtg.device, texture_sampler, nullptr);
		texture_sampler = VK_NULL_HANDLE;
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
	objects_pipeline.destroy(rtg);

	rtg.helpers.destroy_buffer(std::move(object_vertices));

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

		if (workspace.World_src.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.World_src));
		}
		if (workspace.World.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.World));
		}
		//World descriptors freed when pool is destroyed.

		if (workspace.Transforms_src.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Transforms_src));
		}
		if (workspace.Transforms.handle != VK_NULL_HANDLE) {
			rtg.helpers.destroy_buffer(std::move(workspace.Transforms));
		}
		//Transforms_descriptors freed when pool is destroyed.
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
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
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
		assert(workspace.Camera_src.size == sizeof(world));

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

	if (!object_instances.empty()) { //upload object transforms:
		size_t needed_bytes = object_instances.size() * sizeof(ObjectsPipeline::Transform);
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
			ObjectsPipeline::Transform *out = reinterpret_cast< ObjectsPipeline::Transform * >(workspace.Transforms_src.allocation.data()); // Strict aliasing violation, but it doesn't matter
			for (ObjectInstance const &inst : object_instances) {
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

	{//memory barrier to make sure copies complete before rendering happens:
		VkMemoryBarrier memory_barrier{
			.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
		};
		//TODO: change to 2 pipeline barrier, buffer
		vkCmdPipelineBarrier( workspace.command_buffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT, //srcStageMask
			VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, //dstStageMask
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

		if (!object_instances.empty()){//draw with the objects pipeline:
			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, objects_pipeline.handle);

			{//use object_vertices (offset 0) as vertex buffer binding 0:
				std::array<VkBuffer, 1>vertex_buffers{object_vertices.handle};
				std::array< VkDeviceSize, 1 > offsets{ 0 };
				vkCmdBindVertexBuffers(workspace.command_buffer, 0, uint32_t(vertex_buffers.size()), vertex_buffers.data(), offsets.data());

			}

			{ //bind Transforms descriptor set:
				std::array< VkDescriptorSet, 2 > descriptor_sets{
					workspace.World_descriptors, //0: World
					workspace.Transforms_descriptors, //1: Transforms
				};
				vkCmdBindDescriptorSets(
					workspace.command_buffer, //command buffer
					VK_PIPELINE_BIND_POINT_GRAPHICS, //pipeline bind point
					objects_pipeline.layout, //pipeline layout
					0, //first set
					uint32_t(descriptor_sets.size()), descriptor_sets.data(), //descriptor sets count, ptr
					0, nullptr //dynamic offsets count, ptr
				);
			}

			//Camera descriptor set is still bound, but unused

			//draw all instances:
			for (ObjectInstance const &inst : object_instances) {
				uint32_t index = uint32_t(&inst - &object_instances[0]);
				//bind texture descriptor set:
				vkCmdBindDescriptorSets(
					workspace.command_buffer, //command buffer
					VK_PIPELINE_BIND_POINT_GRAPHICS, //pipeline bind point
					objects_pipeline.layout, //pipeline layout
					2, //second set
					1, &texture_descriptors[inst.texture], //descriptor sets count, ptr
					0, nullptr //dynamic offsets count, ptr
				);

				vkCmdDraw(workspace.command_buffer, inst.vertices.count, 1, inst.vertices.first, index);
			}

		}
	
		vkCmdEndRenderPass(workspace.command_buffer);
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

	{ //static sun and static sky:
		//guaranteed to have at most 2 lights
		assert(scene.lights.size() <= 2);
		bool sun_defined = false;
		bool sky_defined = false;
		glm::vec3 default_directional_light_dir = {0.0f, 0.0f, 1.0f};
		for (const Scene::Light& light : scene.lights) {
			//TODO: create method to only extract the rotation instead of needing to normalize
			glm::mat4x4 cur_light_transform = scene.nodes[light.local_to_world[0]].transform.parent_from_local();
			for (int i = 1; i < light.local_to_world.size(); ++i) {
				cur_light_transform *= scene.nodes[light.local_to_world[i]].transform.parent_from_local();
			}
			glm::mat3 rotation_matrix = glm::mat3(cur_light_transform);
			rotation_matrix[0] = glm::normalize(rotation_matrix[0]);
			rotation_matrix[1] = glm::normalize(rotation_matrix[1]);
			rotation_matrix[2] = glm::normalize(rotation_matrix[2]);
			glm::vec3 new_direction = rotation_matrix * default_directional_light_dir;

			if (abs(light.angle - 0.0f) < 0.001f) {
				sun_defined = true;
				glm::vec3 energy = light.strength * light.tint;
				world.SUN_ENERGY.r = energy.r;
				world.SUN_ENERGY.g = energy.g;
				world.SUN_ENERGY.b = energy.b;
				world.SUN_DIRECTION.x = new_direction.x;
				world.SUN_DIRECTION.y = new_direction.y;
				world.SUN_DIRECTION.z = new_direction.z;
			}
			else if (abs(light.angle - float(M_PI)) < 0.001f) {
				sky_defined = true;
				glm::vec3 energy = light.strength * light.tint;
				world.SKY_ENERGY.r = energy.r;
				world.SKY_ENERGY.g = energy.g;
				world.SKY_ENERGY.b = energy.b;
				world.SKY_DIRECTION.x = new_direction.x;
				world.SKY_DIRECTION.y = new_direction.y;
				world.SKY_DIRECTION.z = new_direction.z;
			}
		}
		if (!sky_defined && !sun_defined) {
			world.SKY_ENERGY.r = .1f;
			world.SKY_ENERGY.g = .1f;
			world.SKY_ENERGY.b = .2f;

			world.SUN_ENERGY.r = 1.0f;
			world.SUN_ENERGY.g = 1.0f;
			world.SUN_ENERGY.b = 0.9f;

			world.SKY_DIRECTION.x = 0.0f;
			world.SKY_DIRECTION.y = 0.0f;
			world.SKY_DIRECTION.z = 1.0f;

			world.SUN_DIRECTION.x = 0.0f;
			world.SUN_DIRECTION.y = 0.0f;
			world.SUN_DIRECTION.z = 1.0f;

		}
		else if (!sky_defined) {
			world.SKY_ENERGY.r = 0.0f;
			world.SKY_ENERGY.g = 0.0f;
			world.SKY_ENERGY.b = 0.0f;
			world.SKY_DIRECTION.x = 0.0f;
			world.SKY_DIRECTION.y = 0.0f;
			world.SKY_DIRECTION.z = 1.0f;
		}
		else if (!sun_defined) {
			world.SUN_ENERGY.r = 0.0f;
			world.SUN_ENERGY.g = 0.0f;
			world.SUN_ENERGY.b = 0.0f;
			world.SUN_DIRECTION.x = 0.0f;
			world.SUN_DIRECTION.y = 0.0f;
			world.SUN_DIRECTION.z = 1.0f;
		}

		float length = sqrt(world.SUN_DIRECTION.x * world.SUN_DIRECTION.x + world.SUN_DIRECTION.y * world.SUN_DIRECTION.y + world.SUN_DIRECTION.z * world.SUN_DIRECTION.z);
		world.SKY_DIRECTION.x /= length;
		world.SKY_DIRECTION.y /= length;
		world.SKY_DIRECTION.z /= length;

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

		scene_cam_frustum = make_frustum(
			cur_camera.vfov, //vfov
			cur_camera.aspect, //aspect
			cur_camera.near, //near
			cur_camera.far //far
		);

		if (view_camera == InSceneCamera::SceneCamera)
			CLIP_FROM_WORLD = clip_from_view[0] * view_from_world[0];

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

			user_cam_frustum = make_frustum(
				60.0f * float(M_PI) / 180.0f, //vfov
				rtg.swapchain_extent.width / float(rtg.swapchain_extent.height), //aspect
				0.1f, //near
				1000.0f //far
			);
			clip_from_view[2] = clip_from_view[1];
			FreeCamera& cam = (view_camera == InSceneCamera::UserCamera) ? user_camera : debug_camera;
			update_free_camera(cam);
			last_aspect = float(rtg.swapchain_extent.width) / float(rtg.swapchain_extent.height);
		}
	}

	lines_vertices.clear();
	std::array<glm::vec3, 8> frustum_vertices;
	if (rtg.configuration.culling_settings == 1) { // frustum culling is on
		glm::mat4x4 world_from_clip = glm::inverse(culling_camera == SceneCamera ? clip_from_view[0] * view_from_world[0] : clip_from_view[1]* view_from_world[1]);
		std::array<glm::vec4, 8> clip_space_coordinates = {
			glm::vec4(1.0f,  1.0f, 0.0f, 1.0f),   // Near top right
			glm::vec4(-1.0f,  1.0f, 0.0f, 1.0f),  // Near top left
			glm::vec4(1.0f, -1.0f, 0.0f, 1.0f),   // Near bottom right
			glm::vec4(-1.0f, -1.0f, 0.0f, 1.0f),  // Near bottom left
			glm::vec4(1.0f,  1.0f, 1.0f, 1.0f),   // Far top right
			glm::vec4(-1.0f,  1.0f, 1.0f, 1.0f),  // Far top left
			glm::vec4(1.0f, -1.0f, 1.0f, 1.0f),   // Far bottom right
			glm::vec4(-1.0f, -1.0f, 1.0f, 1.0f)   // Far bottom left
		};
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
				std::array<glm::vec4, 8> clip_space_coordinates = {
					glm::vec4(1.0f,  1.0f, 0.0f, 1.0f),   // Near top right
					glm::vec4(-1.0f,  1.0f, 0.0f, 1.0f),  // Near top left
					glm::vec4(1.0f, -1.0f, 0.0f, 1.0f),   // Near bottom right
					glm::vec4(-1.0f, -1.0f, 0.0f, 1.0f),  // Near bottom left
					glm::vec4(1.0f,  1.0f, 1.0f, 1.0f),   // Far top right
					glm::vec4(-1.0f,  1.0f, 1.0f, 1.0f),  // Far top left
					glm::vec4(1.0f, -1.0f, 1.0f, 1.0f),   // Far bottom right
					glm::vec4(-1.0f, -1.0f, 1.0f, 1.0f)   // Far bottom left
				};
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

	{ //fill object instances with scene hiearchy, optionally draw debug lines when on debug camera
		object_instances.clear();
		// culling resources
		glm::mat4x4 frustum_view_from_world = culling_camera == SceneCamera ? view_from_world[0] : view_from_world[1];

		std::deque<glm::mat4x4> transform_stack;
		std::function<void(uint32_t)> draw_node = [&](uint32_t i) {
			Scene::Node& cur_node = scene.nodes[i];
			glm::mat4x4 cur_node_transform_in_parent = cur_node.transform.parent_from_local();
			if (transform_stack.empty()) {
				transform_stack.push_back(cur_node_transform_in_parent);
			}
			else {
				glm::mat4x4 parent_node_transform_in_world = transform_stack.back();
				transform_stack.push_back(parent_node_transform_in_world * cur_node_transform_in_parent);
			}
			// draw children mesh
			for (uint32_t child_index : cur_node.children) {
				draw_node(child_index);
			}
			// draw own mesh
			if (int32_t cur_mesh_index = cur_node.mesh_index; cur_mesh_index != -1) {
				glm::mat4x4 WORLD_FROM_LOCAL = transform_stack.back();
				{//debug draws and frustum culling
					
					OBB obb = AABB_transform_to_OBB(WORLD_FROM_LOCAL, mesh_AABBs[cur_mesh_index]);
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
					
					if (rtg.configuration.culling_settings == 1 && !check_frustum_obb_intersection(frustum_vertices, obb)) {
						transform_stack.pop_back();
						return;
					}
				}

				uint32_t texture_index = 0;
				if (scene.meshes[cur_mesh_index].material_index != -1) {
					texture_index = scene.materials[scene.meshes[cur_mesh_index].material_index].texture_index + 1;
				}

				object_instances.emplace_back(ObjectInstance{
					.vertices = mesh_vertices[cur_mesh_index],
					.transform{
						.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL,
						.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL,
						.WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL,
					},
					.texture = texture_index,
				});
			}
			transform_stack.pop_back();
		};

		//traverse the scene hiearchy:
		for (uint32_t i = 0; i < scene.root_nodes.size(); ++i) {
			transform_stack.clear();
			draw_node(scene.root_nodes[i]);
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
			cam.radius = std::max(cam.radius - event.wheel.y*0.5f, 0.001f);
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
	
}
