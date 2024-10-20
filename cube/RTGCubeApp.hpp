#include "../RTG.hpp"

struct RTGCubeApp : RTG::Application {
    RTGCubeApp(RTG &);
	RTGCubeApp(RTGCubeApp const &) = delete; //you shouldn't be copying this object
	~RTGCubeApp();
    RTG &rtg;

    struct CubeComputePipeline {
		//descriptor set layouts:
		VkDescriptorSetLayout set0_CUBE = VK_NULL_HANDLE;

		VkPipelineLayout layout = VK_NULL_HANDLE;

		VkPipeline handle = VK_NULL_HANDLE;

		void create(RTG &, VkRenderPass render_pass, uint32_t subpass);
		void destroy(RTG &);
	} compute_pipeline;

    
};