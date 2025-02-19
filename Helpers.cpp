#include "Helpers.hpp"

#include "RTG.hpp"
#include "VK.hpp"

#include <vulkan/utility/vk_format_utils.h> //useful for byte counting
#include <utility>
#include <cassert>
#include <cstring>
#include <iostream>

Helpers::Allocation::Allocation(Allocation &&from) {
	assert(handle == VK_NULL_HANDLE && offset == 0 && size == 0 && mapped == nullptr);

	std::swap(handle, from.handle);
	std::swap(size, from.size);
	std::swap(offset, from.offset);
	std::swap(mapped, from.mapped);
}

Helpers::Allocation &Helpers::Allocation::operator=(Allocation &&from) {
	if (!(handle == VK_NULL_HANDLE && offset == 0 && size == 0 && mapped == nullptr)) {
		//not fatal, just sloppy, so complain but don't throw:
		std::cerr << "Replacing a non-empty allocation; device memory will leak." << std::endl;
	}

	std::swap(handle, from.handle);
	std::swap(size, from.size);
	std::swap(offset, from.offset);
	std::swap(mapped, from.mapped);

	return *this;
}

Helpers::Allocation::~Allocation() {
	if (!(handle == VK_NULL_HANDLE && offset == 0 && size == 0 && mapped == nullptr)) {
		std::cerr << "Destructing a non-empty Allocation; device memory will leak." << std::endl;
	}
}

//----------------------------

Helpers::Allocation Helpers::allocate(VkDeviceSize size, VkDeviceSize alignment, uint32_t memory_type_index, MapFlag map) {
	Helpers::Allocation allocation;

	VkMemoryAllocateInfo alloc_info{
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = size,
		.memoryTypeIndex = memory_type_index
	};

	VK(vkAllocateMemory(rtg.device, &alloc_info, nullptr, &allocation.handle));

	allocation.size = size;
	allocation.offset = 0;

	if (map == Mapped) {
		VK(vkMapMemory(rtg.device, allocation.handle, 0, allocation.size, 0, &allocation.mapped));
	}

	return allocation;
}

Helpers::Allocation Helpers::allocate(VkMemoryRequirements const &req, VkMemoryPropertyFlags properties, MapFlag map) {
	return allocate(req.size, req.alignment, find_memory_type(req.memoryTypeBits, properties), map);
}

void Helpers::free(Helpers::Allocation &&allocation) {
	if (allocation.mapped != nullptr) {
		vkUnmapMemory(rtg.device, allocation.handle);
		allocation.mapped = nullptr;
	}

	vkFreeMemory(rtg.device, allocation.handle, nullptr);

	allocation.handle = VK_NULL_HANDLE;
	allocation.offset = 0;
	allocation.size = 0;
}


//----------------------------

Helpers::AllocatedBuffer Helpers::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, MapFlag map) {
	AllocatedBuffer buffer;
	VkBufferCreateInfo create_info{
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	VK(vkCreateBuffer(rtg.device, &create_info, nullptr, &buffer.handle));
	buffer.size = size;

	//determine memory requirements:
	VkMemoryRequirements req;
	vkGetBufferMemoryRequirements(rtg.device, buffer.handle, &req);

	//allocate memory:
	buffer.allocation = allocate(req, properties, map);

	//bind memory:
	VK(vkBindBufferMemory(rtg.device, buffer.handle, buffer.allocation.handle, buffer.allocation.offset));
	return buffer;
}

void Helpers::destroy_buffer(AllocatedBuffer &&buffer) {
	vkDestroyBuffer(rtg.device, buffer.handle, nullptr);
	buffer.handle = VK_NULL_HANDLE;
	buffer.size = 0;

	this->free(std::move(buffer.allocation));
}


Helpers::AllocatedImage Helpers::create_image(VkExtent2D const &extent, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, MapFlag map, uint32_t layers, uint32_t mip_levels) {
	AllocatedImage image;
	image.extent = extent;
	image.format = format;

	VkImageCreateFlags flag = (layers == 6 ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : VkImageCreateFlags(0));

	VkImageCreateInfo create_info{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.flags = flag,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent{
			.width = extent.width,
			.height = extent.height,
			.depth = 1
		},
		.mipLevels = mip_levels,
		.arrayLayers = layers,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = tiling,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	VK( vkCreateImage(rtg.device, &create_info, nullptr, &image.handle) );

	VkMemoryRequirements req;
	vkGetImageMemoryRequirements(rtg.device, image.handle, &req);

	image.allocation = allocate(req, properties, map);

	VK(vkBindImageMemory(rtg.device, image.handle, image.allocation.handle, image.allocation.offset));
	return image;
}

Helpers::AllocatedImage3D Helpers::create_image_3D(VkExtent3D const &extent, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, MapFlag map)
{
    AllocatedImage3D image;
	image.extent = extent;
	image.format = format;


	VkImageCreateInfo create_info{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_3D,
		.format = format,
		.extent{
			.width = extent.width,
			.height = extent.height,
			.depth = extent.depth,
		},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = tiling,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	VK( vkCreateImage(rtg.device, &create_info, nullptr, &image.handle) );

	VkMemoryRequirements req;
	vkGetImageMemoryRequirements(rtg.device, image.handle, &req);

	image.allocation = allocate(req, properties, map);

	VK(vkBindImageMemory(rtg.device, image.handle, image.allocation.handle, image.allocation.offset));
	return image;
}

void Helpers::destroy_image(AllocatedImage &&image) {
	vkDestroyImage(rtg.device, image.handle, nullptr);

	image.handle = VK_NULL_HANDLE;
	image.extent = VkExtent2D{.width = 0, .height = 0};
	image.format = VK_FORMAT_UNDEFINED;

	this->free(std::move(image.allocation));
}

void Helpers::destroy_image_3D(AllocatedImage3D &&image)
{
	vkDestroyImage(rtg.device, image.handle, nullptr);

	image.handle = VK_NULL_HANDLE;
	image.extent = VkExtent3D{.width = 0, .height = 0, .depth = 0};
	image.format = VK_FORMAT_UNDEFINED;

	this->free(std::move(image.allocation));
}

//----------------------------

void Helpers::transfer_to_buffer(void *data, size_t size, AllocatedBuffer &target) {
	//NOTE: could let this stick around and use it for all uploads, but this function isn't for performant transfers:
	AllocatedBuffer transfer_src = create_buffer(
		size,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		Mapped
	);
	//copy data to transfer buffer
	std::memcpy(transfer_src.allocation.data(), data, size);


	{ //record command buffer that does CPU->GPU transfer:
		VK(vkResetCommandBuffer(transfer_command_buffers[0], 0));

		VkCommandBufferBeginInfo begin_info{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, //will record again every submit
		};

		VK(vkBeginCommandBuffer(transfer_command_buffers[0], &begin_info));

		VkBufferCopy copy_region{
			.srcOffset = 0,
			.dstOffset = 0,
			.size = size
		};
		vkCmdCopyBuffer(transfer_command_buffers[0], transfer_src.handle, target.handle, 1, &copy_region);

		VK(vkEndCommandBuffer(transfer_command_buffers[0]));
	}

	{//run command buffer
		VkSubmitInfo submit_info{
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.commandBufferCount = 1,
			.pCommandBuffers = &transfer_command_buffers[0]
		};

		VK(vkQueueSubmit(rtg.graphics_queue, 1, &submit_info, VK_NULL_HANDLE));
	}

	//wait for command buffer to finish
	VK(vkQueueWaitIdle(rtg.graphics_queue));

	//don't leak buffer memory:
	destroy_buffer(std::move(transfer_src));
}

void Helpers::transfer_to_image(void *data, size_t size, AllocatedImage &target) {
	assert(target.handle); //target image should be allocated already
	//check data is the right size:
	size_t bytes_per_pixel = vkuFormatElementSize(target.format);
	assert(size == target.extent.width * target.extent.height * bytes_per_pixel);

	//create a host-coherent source buffer
	AllocatedBuffer transfer_src = create_buffer(
		size,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		Mapped
	);

	//copy image data into the source buffer
	std::memcpy(transfer_src.allocation.data(), data, size);

	//begin recording a command buffer
	VK(vkResetCommandBuffer(transfer_command_buffers[0], 0));

	VkCommandBufferBeginInfo begin_info{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, //will record again every submit
	};

	VK(vkBeginCommandBuffer(transfer_command_buffers[0], &begin_info));

	VkImageSubresourceRange whole_image{
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel = 0,
		.levelCount = 1,
		.baseArrayLayer = 0,
		.layerCount = 1,
	};

	{ //put the receiving image in destination-optimal layout
		VkImageMemoryBarrier barrier{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, //throw away old image
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = target.handle,
			.subresourceRange = whole_image,
		};

		vkCmdPipelineBarrier(
			transfer_command_buffers[0], //commandBuffer
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, //srcStageMask
			VK_PIPELINE_STAGE_TRANSFER_BIT, //dstStageMask
			0, //dependencyFlags
			0, nullptr, //memory barrier count, pointer
			0, nullptr, //buffer memory barrier count, pointer
			1, &barrier //image memory barrier count, pointer
		);
	}

	{ // copy the source buffer to the image
		VkBufferImageCopy region{
			.bufferOffset = 0,
			.bufferRowLength = target.extent.width,
			.bufferImageHeight = target.extent.height,
			.imageSubresource{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = 0,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
			.imageOffset{ .x = 0, .y = 0, .z = 0 },
			.imageExtent{
				.width = target.extent.width,
				.height = target.extent.height,
				.depth = 1
			},
		};

		vkCmdCopyBufferToImage(
			transfer_command_buffers[0],
			transfer_src.handle,
			target.handle,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &region
		);

		//NOTE: if image has mip levels, need to copy additional regions here
	}

	{ // transition the image memory to shader-read-only-optimal layout:
		VkImageMemoryBarrier barrier{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = target.handle,
			.subresourceRange = whole_image,
		};

		vkCmdPipelineBarrier(
			transfer_command_buffers[0], //commandBuffer
			VK_PIPELINE_STAGE_TRANSFER_BIT, //srcStageMask
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, //dstStageMask
			0, //dependencyFlags
			0, nullptr, //memory barrier count, pointer
			0, nullptr, //buffer memory barrier count, pointer
			1, &barrier //image memory barrier count, pointer
		);
	}

	//end and submit the command buffer
	VK( vkEndCommandBuffer(transfer_command_buffers[0]) );

	VkSubmitInfo submit_info{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &transfer_command_buffers[0]
	};

	VK(vkQueueSubmit(rtg.graphics_queue, 1, &submit_info, VK_NULL_HANDLE));

	//wait for command buffer to finish executing
	VK(vkQueueWaitIdle(rtg.graphics_queue));

	//destroy the source buffer
	destroy_buffer(std::move(transfer_src));
}

void Helpers::transfer_to_image_3D(void *data, size_t size, AllocatedImage3D &target)
{
	assert(target.handle); //target image should be allocated already
	//check data is the right size:
	size_t bytes_per_pixel = vkuFormatElementSize(target.format);
	assert(size == target.extent.width * target.extent.height * target.extent.depth * bytes_per_pixel);

	//create a host-coherent source buffer
	AllocatedBuffer transfer_src = create_buffer(
		size,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		Mapped
	);

	//copy image data into the source buffer
	std::memcpy(transfer_src.allocation.data(), data, size);

	//begin recording a command buffer
	VK(vkResetCommandBuffer(transfer_command_buffers[0], 0));

	VkCommandBufferBeginInfo begin_info{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, //will record again every submit
	};

	VK(vkBeginCommandBuffer(transfer_command_buffers[0], &begin_info));

	VkImageSubresourceRange whole_image{
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel = 0,
		.levelCount = 1,
		.baseArrayLayer = 0,
		.layerCount = 1,
	};

	{ //put the receiving image in destination-optimal layout
		VkImageMemoryBarrier barrier{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, //throw away old image
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = target.handle,
			.subresourceRange = whole_image,
		};

		vkCmdPipelineBarrier(
			transfer_command_buffers[0], //commandBuffer
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, //srcStageMask
			VK_PIPELINE_STAGE_TRANSFER_BIT, //dstStageMask
			0, //dependencyFlags
			0, nullptr, //memory barrier count, pointer
			0, nullptr, //buffer memory barrier count, pointer
			1, &barrier //image memory barrier count, pointer
		);
	}

	{ // copy the source buffer to the image
		VkBufferImageCopy region{
			.bufferOffset = 0,
			.bufferRowLength = target.extent.width,
			.bufferImageHeight = target.extent.height,
			.imageSubresource{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = 0,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
			.imageOffset{ .x = 0, .y = 0, .z = 0 },
			.imageExtent{
				.width = target.extent.width,
				.height = target.extent.height,
				.depth = target.extent.depth,
			},
		};

		vkCmdCopyBufferToImage(
			transfer_command_buffers[0],
			transfer_src.handle,
			target.handle,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &region
		);

		//NOTE: if image has mip levels, need to copy additional regions here
	}

	{ // transition the image memory to shader-read-only-optimal layout:
		VkImageMemoryBarrier barrier{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = target.handle,
			.subresourceRange = whole_image,
		};

		vkCmdPipelineBarrier(
			transfer_command_buffers[0], //commandBuffer
			VK_PIPELINE_STAGE_TRANSFER_BIT, //srcStageMask
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, //dstStageMask
			0, //dependencyFlags
			0, nullptr, //memory barrier count, pointer
			0, nullptr, //buffer memory barrier count, pointer
			1, &barrier //image memory barrier count, pointer
		);
	}

	//end and submit the command buffer
	VK( vkEndCommandBuffer(transfer_command_buffers[0]) );

	VkSubmitInfo submit_info{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &transfer_command_buffers[0]
	};

	VK(vkQueueSubmit(rtg.graphics_queue, 1, &submit_info, VK_NULL_HANDLE));

	//wait for command buffer to finish executing
	VK(vkQueueWaitIdle(rtg.graphics_queue));

	//destroy the source buffer
	destroy_buffer(std::move(transfer_src));
}

void Helpers::transfer_to_image_layered(void *data, size_t size, AllocatedImage &image, uint32_t layer_count)
{
	assert(image.handle); 

    size_t bytes_per_pixel = vkuFormatElementSize(image.format);

    AllocatedBuffer transfer_src = create_buffer(
        size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        Mapped
    );

    std::memcpy(transfer_src.allocation.data(), data, size);

    VK(vkResetCommandBuffer(transfer_command_buffers[0], 0));

    VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
 
    };
    VK(vkBeginCommandBuffer(transfer_command_buffers[0], &begin_info));

    VkImageSubresourceRange all_layers{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,   // Start from layer 0
        .layerCount = layer_count,
    };

    { // Image memory barrier for all layers
        VkImageMemoryBarrier barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, 
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image.handle,
            .subresourceRange = all_layers,
        };

        vkCmdPipelineBarrier(
            transfer_command_buffers[0], 
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 
            VK_PIPELINE_STAGE_TRANSFER_BIT, 
            0, 
            0, nullptr, 
            0, nullptr, 
            1, &barrier 
        );
    }

    { // Copy buffer to all layers of the image
        std::vector<VkBufferImageCopy> regions(layer_count); // Array of layers

        for (uint8_t layer = 0; layer < layer_count; ++layer) {
			
			regions[layer] = {
				.bufferOffset = image.extent.width * image.extent.height * bytes_per_pixel * layer,
				.bufferRowLength = image.extent.width,
				.bufferImageHeight = image.extent.height,
				.imageSubresource{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.mipLevel = 0,
					.baseArrayLayer = layer,   // Layer index
					.layerCount = 1,       // One layer per region
				},
				.imageOffset{ .x = 0, .y = 0, .z = 0 },
				.imageExtent{
					.width = image.extent.width,
					.height = image.extent.height,
					.depth = 1
				},
			};
			
        }

        vkCmdCopyBufferToImage(
            transfer_command_buffers[0],
            transfer_src.handle,
            image.handle,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            uint32_t(regions.size()), regions.data() // Copy all regions
        );
    }

    { // Image memory barrier for all layers
        VkImageMemoryBarrier barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image.handle,
            .subresourceRange = all_layers,
        };

        vkCmdPipelineBarrier(
            transfer_command_buffers[0], 
            VK_PIPELINE_STAGE_TRANSFER_BIT, 
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
            0, 
            0, nullptr,
            0, nullptr,
            1, &barrier 
        );
    }

	//end and submit the command buffer
	VK( vkEndCommandBuffer(transfer_command_buffers[0]) );

	VkSubmitInfo submit_info{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &transfer_command_buffers[0]
	};

	VK(vkQueueSubmit(rtg.graphics_queue, 1, &submit_info, VK_NULL_HANDLE));

	//wait for command buffer to finish executing
	VK(vkQueueWaitIdle(rtg.graphics_queue));

	//destroy the source buffer
	destroy_buffer(std::move(transfer_src));
}

void Helpers::transfer_to_image_cube(void* data, size_t size, AllocatedImage& target, uint8_t mip_level) {
    assert(target.handle); 

    size_t bytes_per_pixel = vkuFormatElementSize(target.format);

    AllocatedBuffer transfer_src = create_buffer(
        size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        Mapped
    );

    std::memcpy(transfer_src.allocation.data(), data, size);

    VK(vkResetCommandBuffer(transfer_command_buffers[0], 0));

    VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
 
    };

    VK(vkBeginCommandBuffer(transfer_command_buffers[0], &begin_info));

    VkImageSubresourceRange all_layers{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = mip_level,
        .baseArrayLayer = 0,   // Start from layer 0
        .layerCount = 6,       // Include all 6 layers
    };

    { // Image memory barrier for all layers
        VkImageMemoryBarrier barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, 
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = target.handle,
            .subresourceRange = all_layers,
        };

        vkCmdPipelineBarrier(
            transfer_command_buffers[0], 
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 
            VK_PIPELINE_STAGE_TRANSFER_BIT, 
            0, 
            0, nullptr, 
            0, nullptr, 
            1, &barrier 
        );
    }

    { // Copy buffer to all layers of the image
        std::vector<VkBufferImageCopy> regions(6 * mip_level); // Array of regions for each layer

        for (uint8_t face = 0; face < 6; ++face) {
			for (uint8_t level = 0; level < mip_level; ++level) {
				regions[face*mip_level + level] = {
					.bufferOffset = get_cube_buffer_offset(target.extent.width, target.extent.height, face, level, bytes_per_pixel), // Offset for each layer
					.bufferRowLength = target.extent.width >> level,
					.bufferImageHeight = target.extent.height >> level,
					.imageSubresource{
						.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						.mipLevel = level,
						.baseArrayLayer = face,   // Layer index
						.layerCount = 1,       // One layer per region
					},
					.imageOffset{ .x = 0, .y = 0, .z = 0 },
					.imageExtent{
						.width = target.extent.width >> level,
						.height = target.extent.height >> level,
						.depth = 1
					},
				};
			}
        }

        vkCmdCopyBufferToImage(
            transfer_command_buffers[0],
            transfer_src.handle,
            target.handle,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            uint32_t(regions.size()), regions.data() // Copy all regions
        );
    }

    { // Image memory barrier for all layers
        VkImageMemoryBarrier barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = target.handle,
            .subresourceRange = all_layers,
        };

        vkCmdPipelineBarrier(
            transfer_command_buffers[0], 
            VK_PIPELINE_STAGE_TRANSFER_BIT, 
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
            0, 
            0, nullptr,
            0, nullptr,
            1, &barrier 
        );
    }

	//end and submit the command buffer
	VK( vkEndCommandBuffer(transfer_command_buffers[0]) );

	VkSubmitInfo submit_info{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &transfer_command_buffers[0]
	};

	VK(vkQueueSubmit(rtg.graphics_queue, 1, &submit_info, VK_NULL_HANDLE));

	//wait for command buffer to finish executing
	VK(vkQueueWaitIdle(rtg.graphics_queue));

	//destroy the source buffer
	destroy_buffer(std::move(transfer_src));

}

VkDeviceSize Helpers::get_cube_buffer_offset(uint32_t base_width, uint32_t base_height, uint32_t face, uint32_t level, size_t bytes_per_pixel)
{
	uint64_t cur_mip_face_offset = face * (base_width >> level) * (base_height >> level) * bytes_per_pixel;
	uint64_t mip_offset = 0;
	for (uint8_t l = 0; l < level; ++l) {
		mip_offset += 6 * (base_width>>l)*(base_height>>l) * bytes_per_pixel;
	}
    return VkDeviceSize(mip_offset + cur_mip_face_offset);
}

void Helpers::gpu_image_transfer_to_buffer(AllocatedBuffer &target, AllocatedImage &image, 
	VkSemaphore image_available, VkSemaphore image_done, VkFence workspace_available, uint8_t workspace_index)
{
	assert(target.handle); //target buffer should be allocated already
	assert(image.handle); //image should be allocated
	//check target is the right size:
	size_t bytes_per_pixel = vkuFormatElementSize(image.format);
	assert(target.size == image.extent.width * image.extent.height * bytes_per_pixel);

	//begin recording a command buffer
	VK(vkResetCommandBuffer(transfer_command_buffers[workspace_index], 0));

	VkCommandBufferBeginInfo begin_info{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, //will record again every submit
	};

	VK(vkBeginCommandBuffer(transfer_command_buffers[workspace_index], &begin_info));

	VkImageSubresourceRange whole_image{
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel = 0,
		.levelCount = 1,
		.baseArrayLayer = 0,
		.layerCount = 1,
	};

	{ // copy the source buffer to the image
		VkBufferImageCopy region{
			.bufferOffset = 0,
			.bufferRowLength = 0,
			.bufferImageHeight = 0,
			.imageSubresource{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = 0,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
			.imageOffset{ .x = 0, .y = 0, .z = 0 },
			.imageExtent{
				.width = image.extent.width,
				.height = image.extent.height,
				.depth = 1
			},
		};
		vkCmdCopyImageToBuffer(
			transfer_command_buffers[workspace_index],
			image.handle,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			target.handle,
			1,
			&region
		);

	}

	//end and submit the command buffer
	VK( vkEndCommandBuffer(transfer_command_buffers[workspace_index]) );
	
	std::array<VkSemaphore, 1> wait_semaphores{
		image_done
	};
	std::array<VkPipelineStageFlags,1> wait_stages{
		VK_PIPELINE_STAGE_TRANSFER_BIT
	};
	static_assert(wait_semaphores.size() == wait_stages.size(), "every semaphore needs a stage");

	std::array<VkSemaphore, 1>signal_semaphores{
		image_available
	};
	VkSubmitInfo submit_info{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = uint32_t(wait_semaphores.size()),
		.pWaitSemaphores = wait_semaphores.data(),
		.pWaitDstStageMask = wait_stages.data(),
		.commandBufferCount = 1,
		.pCommandBuffers = &transfer_command_buffers[workspace_index],
		.signalSemaphoreCount = uint32_t(signal_semaphores.size()),
		.pSignalSemaphores = signal_semaphores.data(),
	};

	VK(vkQueueSubmit(rtg.graphics_queue, 1, &submit_info, workspace_available));

}

//----------------------------

uint32_t Helpers::find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags flags) const {
	for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
		VkMemoryType const &type = memory_properties.memoryTypes[i];
		if ((type_filter & (1 << i)) != 0
		 && (type.propertyFlags & flags) == flags) {
			return i;
		}
	}
	throw std::runtime_error("No suitable memory type found.");
}

VkFormat Helpers::find_image_format(std::vector< VkFormat > const &candidates, VkImageTiling tiling, VkFormatFeatureFlags features) const {
	for (VkFormat format : candidates) {
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(rtg.physical_device, format, &props);
		if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
			return format;
		} else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
			return format;
		}
	}
	throw std::runtime_error("No supported format matches request.");
}

VkShaderModule Helpers::create_shader_module(uint32_t const *code, size_t bytes) const {
	VkShaderModule shader_module = VK_NULL_HANDLE;
	VkShaderModuleCreateInfo create_info{
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = bytes,
		.pCode = code
	};
	VK(vkCreateShaderModule(rtg.device, &create_info, nullptr, &shader_module));
	return shader_module;
}

void Helpers::signal_a_semaphore(VkSemaphore to_signal, uint8_t workspace_index)
{
	//begin recording a command buffer
	VK(vkResetCommandBuffer(transfer_command_buffers[workspace_index], 0));

	VkCommandBufferBeginInfo begin_info{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};

	VK(vkBeginCommandBuffer(transfer_command_buffers[workspace_index], &begin_info));
	VK(vkEndCommandBuffer(transfer_command_buffers[workspace_index]));

	VkSubmitInfo submit_info{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = 0,
		.pWaitSemaphores = nullptr,
		.pWaitDstStageMask = nullptr,
		.commandBufferCount = 1,
		.pCommandBuffers = &transfer_command_buffers[workspace_index],
		.signalSemaphoreCount = 1,
		.pSignalSemaphores = &to_signal,
	};

	VK(vkQueueSubmit(rtg.graphics_queue, 1, &submit_info, VK_NULL_HANDLE));

}

size_t Helpers::align_buffer_size(size_t current_buffer_size, size_t alignment)
{
    return (current_buffer_size + alignment - 1) / alignment * alignment;
}

//----------------------------

Helpers::Helpers(RTG const &rtg_) : rtg(rtg_)
{
}

Helpers::~Helpers() {
}

void Helpers::create() {
	transfer_command_buffers.resize(rtg.configuration.workspaces);
	VkCommandPoolCreateInfo create_info{
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = rtg.graphics_queue_family.value(),
	};
	VK(vkCreateCommandPool(rtg.device, &create_info, nullptr, &transfer_command_pool));

	VkCommandBufferAllocateInfo alloc_info{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = transfer_command_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = rtg.configuration.workspaces,
	};
	VK(vkAllocateCommandBuffers(rtg.device, &alloc_info, transfer_command_buffers.data()));
	vkGetPhysicalDeviceMemoryProperties(rtg.physical_device, &memory_properties);
	if (rtg.configuration.debug) {
		std::cout << "Memory types:\n";
		for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
			VkMemoryType const &type = memory_properties.memoryTypes[i];
			std::cout << " [" << i << "] heap " << type.heapIndex << ", flags: " << string_VkMemoryPropertyFlags(type.propertyFlags) << '\n';
		}
		std::cout << "Memory heaps:\n";
		for (uint32_t i = 0; i < memory_properties.memoryHeapCount; ++i) {
			VkMemoryHeap const &heap = memory_properties.memoryHeaps[i];
			std::cout << " [" << i << "] " << heap.size << " bytes, flags: " << string_VkMemoryHeapFlags( heap.flags ) << '\n';
		}
		std::cout.flush();
	}
}

void Helpers::destroy() {
	//technically not needed since freeing the pool frees the buffers contained
	for (VkCommandBuffer& transfer_command_buffer : transfer_command_buffers) {
		if (transfer_command_buffer != VK_NULL_HANDLE) {
			vkFreeCommandBuffers(rtg.device, transfer_command_pool, 1, &transfer_command_buffer);
			transfer_command_buffer = VK_NULL_HANDLE;
		}
	}
	transfer_command_buffers.clear();

	if (transfer_command_pool != VK_NULL_HANDLE) {
		vkDestroyCommandPool(rtg.device, transfer_command_pool, nullptr);
		transfer_command_pool = VK_NULL_HANDLE;
	}
}
