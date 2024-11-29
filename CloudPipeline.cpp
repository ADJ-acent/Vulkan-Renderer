#include "RTGRenderer.hpp"

#include "Helpers.hpp"

#include "VK.hpp"

static uint32_t comp_code[] = 
#include "spv/cloud.comp.inl"
;

void RTGRenderer::CloudPipeline::create(RTG &rtg, VkRenderPass render_pass, uint32_t subpass) {
    VkShaderModule comp_module = rtg.helpers.create_shader_module(comp_code);

    {//the set0_World layout holds cloud information and sun information
		std::array<VkDescriptorSetLayoutBinding, 6> bindings{
			VkDescriptorSetLayoutBinding{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
			},
            VkDescriptorSetLayoutBinding{
				.binding = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
			},
            VkDescriptorSetLayoutBinding{
				.binding = 2,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
			},
            VkDescriptorSetLayoutBinding{// sun light
				.binding = 3,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
			},
            VkDescriptorSetLayoutBinding{// sphere light
				.binding = 4,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
			},
            VkDescriptorSetLayoutBinding{ // spot light
				.binding = 5,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
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


    {//create pipeline layout:
		std::array<VkDescriptorSetLayout, 1> layouts{
			set0_World
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

void RTGRenderer::CloudPipeline::destroy(RTG &rtg) {
    if (set0_World != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(rtg.device, set0_World, nullptr);
		set0_World = VK_NULL_HANDLE;
	}

    if (layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(rtg.device, layout, nullptr);
        layout = VK_NULL_HANDLE;
    }

    if (handle != VK_NULL_HANDLE) {
        vkDestroyPipeline(rtg.device, handle, nullptr);
        handle = VK_NULL_HANDLE;
    }
}