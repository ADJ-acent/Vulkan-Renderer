#include "RTGRenderer.hpp"

#include "Helpers.hpp"

#include "VK.hpp"

static uint32_t comp_code[] = 
#include "spv/cloud_lightgrid.comp.inl"
;

void RTGRenderer::CloudLightGirdPipeline::create(RTG &rtg) {
    VkShaderModule comp_module = rtg.helpers.create_shader_module(comp_code);

    {//the set0_World layout holds the output image and world information
		std::array<VkDescriptorSetLayoutBinding, 3> bindings{
			VkDescriptorSetLayoutBinding{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
			},
			VkDescriptorSetLayoutBinding{
				.binding = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
			},
			VkDescriptorSetLayoutBinding{
				.binding = 2,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
			},
		};
		
		VkDescriptorSetLayoutCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.bindingCount = uint32_t(bindings.size()),
			.pBindings = bindings.data(),
		};

		VK(vkCreateDescriptorSetLayout(rtg.device, &create_info, nullptr, &set0_World));
	}

	{//the set1_Cloud layout holds all the cloud voxel data
		std::array<VkDescriptorSetLayoutBinding, 5> bindings{
			// ParkouringCloud Model Data
			VkDescriptorSetLayoutBinding{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
			},
			// ParkouringCloud Field Data
			VkDescriptorSetLayoutBinding{
				.binding = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
			},
			// StormbirdCloud Model Data
			VkDescriptorSetLayoutBinding{
				.binding = 2,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
			},
			// StormbirdCloud Field Data
			VkDescriptorSetLayoutBinding{
				.binding = 3,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
			},
			// Noise
			VkDescriptorSetLayoutBinding{
				.binding = 4,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
			},
		};
		
		VkDescriptorSetLayoutCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.bindingCount = uint32_t(bindings.size()),
			.pBindings = bindings.data(),
		};

		VK(vkCreateDescriptorSetLayout(rtg.device, &create_info, nullptr, &set1_Cloud));
	}

	// {//the set2_World layout holds all the world information such as environment map
	// 	std::array<VkDescriptorSetLayoutBinding, 1> bindings{
	// 		// camera information
	// 		VkDescriptorSetLayoutBinding{
	// 			.binding = 0,
	// 			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	// 			.descriptorCount = 1,
	// 			.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
	// 		},
			
	// 	};
		
	// 	VkDescriptorSetLayoutCreateInfo create_info{
	// 		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
	// 		.bindingCount = uint32_t(bindings.size()),
	// 		.pBindings = bindings.data(),
	// 	};

	// 	VK(vkCreateDescriptorSetLayout(rtg.device, &create_info, nullptr, &set2_World));
	// }


    {//create pipeline layout:
		std::array<VkDescriptorSetLayout, 2> layouts{
			set0_World,
			set1_Cloud,
			// set2_World,
        };

		VkPipelineLayoutCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = uint32_t(layouts.size()),
			.pSetLayouts = layouts.data(),
			.pushConstantRangeCount = 0,
			.pPushConstantRanges = nullptr,
		};
        VK(vkCreatePipelineLayout(rtg.device, &create_info, nullptr, &layout));
    }
    { //create pipeline:
        VkPipelineShaderStageCreateInfo shader_stage{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = comp_module,
            .pName = "main",
        };

        VkComputePipelineCreateInfo create_info{
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = shader_stage,
            .layout = layout,
        };

        VK(vkCreateComputePipelines(rtg.device, VK_NULL_HANDLE, 1, &create_info, nullptr, &handle));

        // Destroy shader module after pipeline creation
        vkDestroyShaderModule(rtg.device, comp_module, nullptr);
    }
}

void RTGRenderer::CloudLightGirdPipeline::destroy(RTG &rtg) {
    if (set0_World != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(rtg.device, set0_World, nullptr);
		set0_World = VK_NULL_HANDLE;
	}

	if (set1_Cloud != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(rtg.device, set1_Cloud, nullptr);
		set1_Cloud = VK_NULL_HANDLE;
	}

	// if (set2_World != VK_NULL_HANDLE) {
	// 	vkDestroyDescriptorSetLayout(rtg.device, set2_World, nullptr);
	// 	set2_World = VK_NULL_HANDLE;
	// }

    if (layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(rtg.device, layout, nullptr);
        layout = VK_NULL_HANDLE;
    }

    if (handle != VK_NULL_HANDLE) {
        vkDestroyPipeline(rtg.device, handle, nullptr);
        handle = VK_NULL_HANDLE;
    }
}