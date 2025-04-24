#include "RTGRenderer.hpp"

#include "Helpers.hpp"

#include "VK.hpp"

static uint32_t comp_code[] = 
#include "spv/cluster_selection.comp.inl"
;

void RTGRenderer::ClusterSelectionPipeline::create(RTG &rtg) {
    VkShaderModule comp_module = rtg.helpers.create_shader_module(comp_code);

    {//the set0_World layout holds the output image and world information
		std::array<VkDescriptorSetLayoutBinding, 7> bindings{
			// Binding 0: Cluster Nodes
			VkDescriptorSetLayoutBinding{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
			},
			// Binding 1: Cluster Groups
			VkDescriptorSetLayoutBinding{
				.binding = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
			},
			// Binding 2: Cluster Object Offsets
			VkDescriptorSetLayoutBinding{
				.binding = 2,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
			},
			// Binding 3: Transforms
			VkDescriptorSetLayoutBinding{
				.binding = 3,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
			},
			// Binding 4: Result List
			VkDescriptorSetLayoutBinding{
				.binding = 4,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
			},
			// Binding 5: World
			VkDescriptorSetLayoutBinding{
				.binding = 5,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
			},
			// Binding 6 : Root Node Indices / Atomic Stack
			VkDescriptorSetLayoutBinding{
				.binding = 6,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
			},
		};
		
		VkDescriptorSetLayoutCreateInfo create_info{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.bindingCount = static_cast<uint32_t>(bindings.size()),
			.pBindings = bindings.data(),
		};
		
		VK(vkCreateDescriptorSetLayout(rtg.device, &create_info, nullptr, &set0_Resources));
	}

    {//create pipeline layout:
		std::array<VkDescriptorSetLayout, 1> layouts{
			set0_Resources,
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

void RTGRenderer::ClusterSelectionPipeline::destroy(RTG &rtg) {
    if (set0_Resources != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(rtg.device, set0_Resources, nullptr);
		set0_Resources = VK_NULL_HANDLE;
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