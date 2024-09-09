#ifdef _WIN32
//ensure we have M_PI
#define _USE_MATH_DEFINES
#endif

#include "Tutorial.hpp"
#include "refsol.hpp"

#include "VK.hpp"
#include "refsol.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

Tutorial::Tutorial(RTG &rtg_) : rtg(rtg_) {
	refsol::Tutorial_constructor(rtg, &depth_format, &render_pass, &command_pool);

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
		refsol::Tutorial_constructor_workspace(rtg, command_pool, &workspace.command_buffer);
	
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
		std::vector<PosNorTexVertex> vertices;

		{ //A [-1,1]x[-1,1]x{0} quadrilateral:
			plane_vertices.first = uint32_t(vertices.size());
			vertices.emplace_back(PosNorTexVertex{
				.Position{ .x = -1.0f, .y = -1.0f, .z = 0.0f },
				.Normal{ .x = 0.0f, .y = 0.0f, .z = 1.0f },
				.TexCoord{ .s = 0.0f, .t = 0.0f },
			});
			vertices.emplace_back(PosNorTexVertex{
				.Position{ .x = 1.0f, .y = -1.0f, .z = 0.0f },
				.Normal{ .x = 0.0f, .y = 0.0f, .z = 1.0f},
				.TexCoord{ .s = 1.0f, .t = 0.0f },
			});
			vertices.emplace_back(PosNorTexVertex{
				.Position{ .x = -1.0f, .y = 1.0f, .z = 0.0f },
				.Normal{ .x = 0.0f, .y = 0.0f, .z = 1.0f},
				.TexCoord{ .s = 0.0f, .t = 1.0f },
			});
			vertices.emplace_back(PosNorTexVertex{
				.Position{ .x = 1.0f, .y = 1.0f, .z = 0.0f },
				.Normal{ .x = 0.0f, .y = 0.0f, .z = 1.0f },
				.TexCoord{ .s = 1.0f, .t = 1.0f },
			});
			vertices.emplace_back(PosNorTexVertex{
				.Position{ .x = -1.0f, .y = 1.0f, .z = 0.0f },
				.Normal{ .x = 0.0f, .y = 0.0f, .z = 1.0f},
				.TexCoord{ .s = 0.0f, .t = 1.0f },
			});
			vertices.emplace_back(PosNorTexVertex{
				.Position{ .x = 1.0f, .y = -1.0f, .z = 0.0f },
				.Normal{ .x = 0.0f, .y = 0.0f, .z = 1.0f},
				.TexCoord{ .s = 1.0f, .t = 0.0f },
			});

			plane_vertices.count = uint32_t(vertices.size()) - plane_vertices.first;
		}

		{//A torus:
			torus_vertices.first = uint32_t(vertices.size());

			//will parameterize with (u,v) where:
			// - u is angle around main axis (+z)
			// - v is angle around the tube

			constexpr float R1 = 0.75f; //main radius
			constexpr float R2 = 0.15f; //tube radius

			constexpr uint32_t U_STEPS = 20;
			constexpr uint32_t V_STEPS = 16;

			//texture repeats around the torus:
			constexpr float V_REPEATS = 2.0f;
			float U_REPEATS = std::ceil(V_REPEATS / R2 * R1);

			auto emplace_vertex = [&](uint32_t ui, uint32_t vi) {
				//convert steps to angles:
				// (doing the mod since trig on 2 M_PI may not exactly match 0)
				float ua = (ui % U_STEPS) / float(U_STEPS) * 2.0f * float(M_PI);
				float va = (vi % V_STEPS) / float(V_STEPS) * 2.0f * float(M_PI);

				vertices.emplace_back( PosNorTexVertex{
					.Position{
						.x = (R1 + R2 * std::cos(va)) * std::cos(ua),
						.y = (R1 + R2 * std::cos(va)) * std::sin(ua),
						.z = R2 * std::sin(va),
					},
					.Normal{
						.x = std::cos(va) * std::cos(ua),
						.y = std::cos(va) * std::sin(ua),
						.z = std::sin(va),
					},
					.TexCoord{
						.s = ui / float(U_STEPS) * U_REPEATS,
						.t = vi / float(V_STEPS) * V_REPEATS,
					},
				});
			};

			for (uint32_t ui = 0; ui < U_STEPS; ++ui) {
				for (uint32_t vi = 0; vi < V_STEPS; ++vi) {
					emplace_vertex(ui, vi);
					emplace_vertex(ui+1, vi);
					emplace_vertex(ui, vi+1);

					emplace_vertex(ui, vi+1);
					emplace_vertex(ui+1, vi);
					emplace_vertex(ui+1, vi+1);
				}
			}


			torus_vertices.count = uint32_t(vertices.size()) - torus_vertices.first;
		}

		{//a sphere
			sphere_vertices.first = uint32_t(vertices.size());
			constexpr float R = .5f;
			constexpr uint32_t U_STEPS = 50;
			constexpr uint32_t V_STEPS = 50; // should only be even

			auto emplace_vertex = [&](uint32_t ui, uint32_t vi) {
				//convert steps to angles:
				// (doing the mod since trig on 2 M_PI may not exactly match 0)
				float height = float(ui) / float(U_STEPS) * 2 * R;
				float angle = float(vi) / float(V_STEPS) * 2.0f * float(M_PI);
				float height_from_center = height - R;

				vertices.emplace_back( PosNorTexVertex{
					.Position{
						.x = std::cos(angle) * std::sqrt(R*R - height_from_center * height_from_center),
						.y = std::sin(angle) * std::sqrt(R*R - height_from_center * height_from_center),
						.z = height,
					},
					.Normal{
						.x = std::cos(angle) * 
							std::sqrt(R*R - height_from_center * height_from_center) / R,
						.y = std::sin(angle) *
							std::sqrt(R*R - height_from_center * height_from_center) / R,
						.z = (height-R) / R,
					},
					.TexCoord{
						.s = float(ui) / float(U_STEPS),
						.t = float(vi) / float(V_STEPS),
					},
				});
			};


			for (uint32_t ui = 0; ui < U_STEPS; ++ui) {
				for (uint32_t vi = 0; vi < V_STEPS; ++vi) {
					if (ui == 0) {
						emplace_vertex(ui, vi);
						emplace_vertex(ui+1, vi+1);
						emplace_vertex(ui+1, vi);
					}
					else if (ui == U_STEPS - 1) {
						emplace_vertex(ui, vi);
						emplace_vertex(ui, vi+1);
						emplace_vertex(ui+1, vi);
					}
					else {
						emplace_vertex(ui, vi);
						emplace_vertex(ui, vi+1);
						emplace_vertex(ui+1, vi);

						emplace_vertex(ui, vi+1);
						emplace_vertex(ui+1, vi+1);
						emplace_vertex(ui+1, vi);
					}
				}
			}

			sphere_vertices.count = uint32_t(vertices.size()) - sphere_vertices.first;
		}

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
		textures.reserve(3);

		{ //texture 0 will be a dark grey / light grey checkerboard with a red square at the origin.
			//actually make the texture:
			uint32_t size = 128;
			std::vector< uint32_t > data;
			data.reserve(size * size);
			for (uint32_t y = 0; y < size; ++y) {
				float fy = (y + 0.5f) / float(size);
				for (uint32_t x = 0; x < size; ++x) {
					float fx = (x + 0.5f) / float(size);
					//highlight the origin:
					if      (fx < 0.05f && fy < 0.05f) data.emplace_back(0xff0000ff); //red
					else if ( (fx < 0.5f) == (fy < 0.5f)) data.emplace_back(0xff444444); //dark grey
					else data.emplace_back(0xffbbbbbb); //light grey
				}
			}
			assert(data.size() == size*size);

			//make a place for the texture to live on the GPU:
			textures.emplace_back(rtg.helpers.create_image(
				VkExtent2D{ .width = size , .height = size }, //size of image
				VK_FORMAT_R8G8B8A8_UNORM, //how to interpret image data (in this case, linearly-encoded 8-bit RGBA)
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, //will sample and upload
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, //should be device-local
				Helpers::Unmapped
			));

			//transfer data:
			rtg.helpers.transfer_to_image(data.data(), sizeof(data[0]) * data.size(), textures.back());
		}

		{ //texture 1 will be a classic 'xor' texture:
			//actually make the texture:
			uint32_t size = 256;
			std::vector< uint32_t > data;
			data.reserve(size * size);
			for (uint32_t y = 0; y < size; ++y) {
				for (uint32_t x = 0; x < size; ++x) {
					uint8_t r = uint8_t(x) ^ uint8_t(y);
					uint8_t g = uint8_t(x + 128) ^ uint8_t(y);
					uint8_t b = uint8_t(x) ^ uint8_t(y + 27);
					uint8_t a = 0xff;
					data.emplace_back( uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24) );
				}
			}
			assert(data.size() == size*size);

			//make a place for the texture to live on the GPU:
			textures.emplace_back(rtg.helpers.create_image(
				VkExtent2D{ .width = size , .height = size }, //size of image
				VK_FORMAT_R8G8B8A8_SRGB, //how to interpret image data (in this case, SRGB-encoded 8-bit RGBA)
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, //will sample and upload
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, //should be device-local
				Helpers::Unmapped
			));

			//transfer data:
			rtg.helpers.transfer_to_image(data.data(), sizeof(data[0]) * data.size(), textures.back());
		}

		{//texture 2 has diagonal pattern
			uint32_t size = 256;
			std::vector< uint32_t > data;
			data.reserve(size * size);
			for (uint32_t y = 0; y < size; ++y) {
				for (uint32_t x = 0; x < size; ++x) {
					uint8_t r = uint8_t((float((x + y) % 10) / 10.0f ) * 255.0f);
					uint8_t g = uint8_t((float((x + y + 5) % 10) / 10.0f ) * 255.0f);
					uint8_t b = uint8_t(x) ^ uint8_t(y + 27);
					uint8_t a = 0xff;
					data.emplace_back( uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24) );
				}
			}
			assert(data.size() == size*size);

			//make a place for the texture to live on the GPU:
			textures.emplace_back(rtg.helpers.create_image(
				VkExtent2D{ .width = size , .height = size }, //size of image
				VK_FORMAT_R8G8B8A8_SRGB, //how to interpret image data (in this case, SRGB-encoded 8-bit RGBA)
				VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, //will sample and upload
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, //should be device-local
				Helpers::Unmapped
			));

			//transfer data:
			rtg.helpers.transfer_to_image(data.data(), sizeof(data[0]) * data.size(), textures.back());
		
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
}

Tutorial::~Tutorial() {
	//just in case rendering is still in flight, don't destroy resources:
	//(not using VK macro to avoid throw-ing in destructor)
	if (VkResult result = vkDeviceWaitIdle(rtg.device); result != VK_SUCCESS) {
		std::cerr << "Failed to vkDeviceWaitIdle in Tutorial::~Tutorial [" << string_VkResult(result) << "]; continuing anyway." << std::endl;
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
		refsol::Tutorial_destructor_workspace(rtg, command_pool, &workspace.command_buffer);

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

	refsol::Tutorial_destructor(rtg, &render_pass, &command_pool);
}

void Tutorial::on_swapchain(RTG &rtg_, RTG::SwapchainEvent const &swapchain) {
	//[re]create framebuffers:
	refsol::Tutorial_on_swapchain(rtg, swapchain, depth_format, render_pass, &swapchain_depth_image, &swapchain_depth_image_view, &swapchain_framebuffers);
}

void Tutorial::destroy_framebuffers() {
	refsol::Tutorial_destroy_framebuffers(rtg, &swapchain_depth_image, &swapchain_depth_image_view, &swapchain_framebuffers);
}


void Tutorial::render(RTG &rtg_, RTG::RenderParams const &render_params) {
	//assert that parameters are valid:
	assert(&rtg == &rtg_);
	assert(render_params.workspace_index < workspaces.size());
	assert(render_params.image_index < swapchain_framebuffers.size());

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
			VkClearValue{.color{.float32{0.0f, 1.0f, .7f, 1.0f}}},
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

		{//set scissor rectangle:
			VkRect2D scissor{
				.offset = {.x = 0, .y = 0},
				.extent = rtg.swapchain_extent,
			};
			vkCmdSetScissor(workspace.command_buffer, 0, 1, &scissor);
		}
		{//configure viewport transform:
			VkViewport viewport{
				.x = 0.0f,
				.y = 0.0f,
				.width = float(rtg.swapchain_extent.width),
				.height = float(rtg.swapchain_extent.height),
				.minDepth = 0.0f,
				.maxDepth = 1.0f,
			};
			vkCmdSetViewport(workspace.command_buffer, 0, 1, &viewport);
		}

		{//draw with the background pipeline:
			vkCmdBindPipeline(workspace.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, background_pipeline.handle);
			
			{//push time:
				BackgroundPipeline::Push push{
					.time = float(time),
				};
				vkCmdPushConstants(workspace.command_buffer, background_pipeline.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
			}
			
			vkCmdDraw(workspace.command_buffer, 3, 1, 0, 0);
		}

		{//draw with the lines pipeline:
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

		{//draw with the objects pipeline:
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
			vkCmdEndRenderPass(workspace.command_buffer);
		}
	}

	//end recording:
	VK(vkEndCommandBuffer(workspace.command_buffer));

	//submit `workspace.command buffer` for the GPU to run:
	refsol::Tutorial_render_submit(rtg, render_params, workspace.command_buffer);
}


void Tutorial::update(float dt) {
	time = std::fmod(time + dt, 60.0f);

	{ //camera orbiting the origin:
		float ang = float(M_PI) * 2.0f * 10.0f;// * (time / 60.0f);
		CLIP_FROM_WORLD = perspective(
			60.0f / float(M_PI) * 180.0f, //vfov
			rtg.swapchain_extent.width / float(rtg.swapchain_extent.height), //aspect
			0.1f, //near
			1000.0f //far
		) * look_at(
			3.0f * std::cos(ang), 3.0f * std::sin(ang), 1.0f, //eye
			0.0f, 0.0f, 0.5f, //target
			0.0f, 0.0f, 1.0f //up
		);
	}

	{ //non static sun and static sky:
		world.SKY_DIRECTION.x = 0.0f;
		world.SKY_DIRECTION.y = 0.0f;
		world.SKY_DIRECTION.z = 1.0f;

		world.SKY_ENERGY.r = 0.1f;
		world.SKY_ENERGY.g = 0.1f;
		world.SKY_ENERGY.b = 0.2f;

		constexpr float rotation_speed = .01f;
		float angle = time * rotation_speed;
		static bool do_once = true;

		world.SUN_DIRECTION.x = 6.0f / 23.0f;
		if (do_once) {
			world.SUN_DIRECTION.y = 13.0f / 23.0f;
			world.SUN_DIRECTION.z = 18.0f / 23.0f;
			do_once = false;
		}
		else {
			world.SUN_DIRECTION.y = world.SUN_DIRECTION.y * std::cos(angle) - world.SUN_DIRECTION.z * std::sin(angle);
			world.SUN_DIRECTION.z = world.SUN_DIRECTION.y * std::sin(angle) + world.SUN_DIRECTION.z * std::cos(angle);
		}

		float length = sqrt(world.SUN_DIRECTION.x * world.SUN_DIRECTION.x + world.SUN_DIRECTION.y * world.SUN_DIRECTION.y + world.SUN_DIRECTION.z * world.SUN_DIRECTION.z);
		world.SKY_DIRECTION.x /= length;
		world.SKY_DIRECTION.y /= length;
		world.SKY_DIRECTION.z /= length;

		world.SUN_ENERGY.r = 1.0f;
		world.SUN_ENERGY.g = 1.0f;
		world.SUN_ENERGY.b = 0.9f;
	}

	{ //make a grid that is circular:
		lines_vertices.clear();
		constexpr size_t count = 2 * 101;
		lines_vertices.reserve(count);
		//horizontal lines at z = 0.5f:
		for (uint32_t i = 0; i < 101; ++i) {
			float x = 1.0f, y = 1.0f;
			if (i < 50) {
				y -= float(i) / 25.0f;
			}
			else {
				x -= float(i-50) / 25.0f;
			}
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = x, .y = y, .z = .1f + sin(time/20.0f)},
				.Color{ .r = uint8_t((x+1) * 255.0f/2.0f), .g = uint8_t((y+1) * 255.0f/2.0f), .b = 0xff, .a = 0xff},
			});
			lines_vertices.emplace_back(PosColVertex{
				.Position{.x = -x, .y = -y, .z = .1f + cos(time/10.0f)},
				.Color{ .r = uint8_t((-x+1) * 255.0f/2.0f), .g = uint8_t((-y+1) * 255.0f/2.0f), .b = 0x00, .a = 0xff},
			});
		}

		assert(lines_vertices.size() == count);
	}

	{ //make some objects:
		object_instances.clear();

		{ //plane translated +x by one unit:
			mat4 WORLD_FROM_LOCAL{
				1.0f, 0.0f, 0.0f, 0.0f,
				0.0f, 1.0f, 0.0f, 0.0f,
				0.0f, 0.0f, 1.0f, 0.0f,
				1.0f, 0.0f, 0.0f, 1.0f,
			};

			object_instances.emplace_back(ObjectInstance{
				.vertices = plane_vertices,
				.transform{
					.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL,
					.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL,
					.WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL,
				},
				.texture = 2,
			});
		}
		{ //torus translated -x by one unit and rotated CCW around +y:
			float ang = time / 60.0f * 2.0f * float(M_PI) * 10.0f;
			float ca = std::cos(ang);
			float sa = std::sin(ang);
			mat4 WORLD_FROM_LOCAL{
				  ca, 0.0f,  -sa, 0.0f,
				0.0f, 1.0f, 0.0f, 0.0f,
				  sa, 0.0f,   ca, 0.0f,
				-1.0f,0.0f, 0.0f, 1.0f,
			};

			object_instances.emplace_back(ObjectInstance{
				.vertices = torus_vertices,
				.transform{
					.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL,
					.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL,
					.WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL,
				},
			});
		}

		{ //small sphere stay in place;
			mat4 WORLD_FROM_LOCAL{
				.1f, 0.0f, 0.0f, 0.0f,
				0.0f, .1f, 0.0f, 0.0f,
				0.0f, 0.0f, .1f, 0.0f,
				0.0f, 0.0f, 0.0f, 1.0f,
			};

			object_instances.emplace_back(ObjectInstance{
				.vertices = sphere_vertices,
				.transform{
					.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL,
					.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL,
					.WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL,
				},
				.texture = 1,
			});
		}

		{ //mid sphere;
			mat4 WORLD_FROM_LOCAL{
				.2f, 0.0f, 0.0f, 0.0f,
				0.0f, .2f, 0.0f, 0.0f,
				0.0f, 0.0f, .2f, 0.0f,
				-0.3f, -0.3f, 0.0f, 1.0f,
			};

			object_instances.emplace_back(ObjectInstance{
				.vertices = sphere_vertices,
				.transform{
					.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL,
					.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL,
					.WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL,
				},
			});
		}

		{ //large sphere;
			mat4 WORLD_FROM_LOCAL{
				.5f, 0.0f, 0.0f, 0.0f,
				0.0f, .5f, 0.0f, 0.0f,
				0.0f, 0.0f, .5f, 0.0f,
				-1.0f, -1.0f, 0.0f, 1.0f,
			};

			object_instances.emplace_back(ObjectInstance{
				.vertices = sphere_vertices,
				.transform{
					.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL,
					.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL,
					.WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL,
				},
				.texture = 1,
			});
		}

		{ //extra large sphere;
			mat4 WORLD_FROM_LOCAL{
				1.0f, 0.0f, 0.0f, 0.0f,
				0.0f, 1.0f, 0.0f, 0.0f,
				0.0f, 0.0f, 1.0f, 0.0f,
				-2.0f, -2.0f, 0.0f, 1.0f,
			};

			object_instances.emplace_back(ObjectInstance{
				.vertices = sphere_vertices,
				.transform{
					.CLIP_FROM_LOCAL = CLIP_FROM_WORLD * WORLD_FROM_LOCAL,
					.WORLD_FROM_LOCAL = WORLD_FROM_LOCAL,
					.WORLD_FROM_LOCAL_NORMAL = WORLD_FROM_LOCAL,
				},
				.texture = 2,
			});
		}
	}
}


void Tutorial::on_input(InputEvent const &) {
}
