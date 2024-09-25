#pragma once

#include "PosColVertex.hpp"
#include "PosNorTanTexVertex.hpp"
#include "mat4.hpp"

#include "RTG.hpp"
#include "Scene.hpp"
#include "frustum_culling.hpp"

#include "GLM.hpp"

struct RTGRenderer : RTG::Application {

	RTGRenderer(RTG &, Scene &);
	RTGRenderer(RTGRenderer const &) = delete; //you shouldn't be copying this object
	~RTGRenderer();

	//kept for use in destructor:
	RTG &rtg;

	//scene that contains nodes, camera, light, material and texture information
	Scene &scene;
	//--------------------------------------------------------------------
	//Resources that last the lifetime of the application:

	//chosen format for depth buffer:
	VkFormat depth_format{};
	//Render passes describe how pipelines write to images:
	VkRenderPass render_pass = VK_NULL_HANDLE;

	//Pipelines:

	struct BackgroundPipeline {
		// no descriptor set layouts

		struct Push {
			float time;
		};

		VkPipelineLayout layout = VK_NULL_HANDLE;

		// no vertex bindings

		VkPipeline handle = VK_NULL_HANDLE;

		void create(RTG &, VkRenderPass render_pass, uint32_t subpass);
		void destroy(RTG &);
	} background_pipeline;

	struct LinesPipeline {
		//descriptor set layouts:
		VkDescriptorSetLayout set0_Camera = VK_NULL_HANDLE;

		//types for descriptors:
		struct Camera {
			glm::mat4x4 CLIP_FROM_WORLD;
		};

		static_assert(sizeof(Camera) == 16*4, "camera buffer structure is packed");

		// no push constants

		VkPipelineLayout layout = VK_NULL_HANDLE;

		using Vertex = PosColVertex;

		VkPipeline handle = VK_NULL_HANDLE;

		void create(RTG &, VkRenderPass render_pass, uint32_t subpass);
		void destroy(RTG &);
	} lines_pipeline;

	struct ObjectsPipeline {
		//descriptor set layouts:
		VkDescriptorSetLayout set0_World = VK_NULL_HANDLE;
        VkDescriptorSetLayout set1_Transforms = VK_NULL_HANDLE;
        VkDescriptorSetLayout set2_TEXTURE = VK_NULL_HANDLE;

		//types for descriptors:

        struct World {
            struct { float x, y, z, padding_; } SKY_DIRECTION;
            struct { float r, g, b, padding_; } SKY_ENERGY;
            struct { float x, y, z, padding_; } SUN_DIRECTION;
            struct { float r, g, b, padding_; } SUN_ENERGY;
        };
        static_assert(sizeof(World) == 4*4 + 4*4 + 4*4 + 4*4, "World is the expected size.");
		
        struct Transform {
            glm::mat4x4 CLIP_FROM_LOCAL;
            glm::mat4x4 WORLD_FROM_LOCAL;
            glm::mat4x4 WORLD_FROM_LOCAL_NORMAL;
        };
        static_assert(sizeof(Transform) == 16*4 + 16*4 + 16*4, "Transform is the expected size.");

		//no push constants

		VkPipelineLayout layout = VK_NULL_HANDLE;

		using Vertex = PosNorTanTexVertex;

		VkPipeline handle = VK_NULL_HANDLE;

		void create(RTG &, VkRenderPass render_pass, uint32_t subpass);
		void destroy(RTG &);
	} objects_pipeline;

	//pools from which per-workspace things are allocated:
	VkCommandPool command_pool = VK_NULL_HANDLE;
	
	//descriptor pool
	VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;

	//workspaces hold per-render resources:
	struct Workspace {
		VkCommandBuffer command_buffer = VK_NULL_HANDLE; //from the command pool above; reset at the start of every render.
		
		//location for lines data: (streamed to GPU per-frame)
		Helpers::AllocatedBuffer lines_vertices_src; //host coherent; mapped
		Helpers::AllocatedBuffer lines_vertices; //device-local

		//location for LinesPipeline::Camera data: (streamed to GPU per-frame)
		Helpers::AllocatedBuffer Camera_src; //host coherent; mapped
		Helpers::AllocatedBuffer Camera; //device-local
		VkDescriptorSet Camera_descriptors; //references Camera

        //location for ObjectsPipeline::World data: (streamed to GPU per-frame)
        Helpers::AllocatedBuffer World_src; //host coherent; mapped
        Helpers::AllocatedBuffer World; //device-local
        VkDescriptorSet World_descriptors; //references World

        // locations for ObjectsPipeline::Transforms data: (streamed to GPU per-frame):
        Helpers::AllocatedBuffer Transforms_src; //host coherent; mapped
        Helpers::AllocatedBuffer Transforms; //device-local
        VkDescriptorSet Transforms_descriptors; //references Transforms
	};
	std::vector< Workspace > workspaces;

	//-------------------------------------------------------------------
	//static scene resources:

    Helpers::AllocatedBuffer object_vertices;
    struct ObjectVertices {
		uint32_t first = 0;
		uint32_t count = 0;
	};
	std::vector<ObjectVertices> mesh_vertices; // indexed the same as scene.meshes
	std::vector<AABB> mesh_AABBs; // also indexed the same as scene.meshes

    std::vector< Helpers::AllocatedImage > textures;
	std::vector< VkImageView > texture_views;
	VkSampler texture_sampler = VK_NULL_HANDLE;
	VkDescriptorPool texture_descriptor_pool = VK_NULL_HANDLE;
	std::vector< VkDescriptorSet > texture_descriptors; //allocated from texture_descriptor_pool

	//--------------------------------------------------------------------
	//Resources that change when the swapchain is resized:

	virtual void on_swapchain(RTG &, RTG::SwapchainEvent const &) override;

	Helpers::AllocatedImage swapchain_depth_image;
	VkImageView swapchain_depth_image_view = VK_NULL_HANDLE;
	std::vector< VkFramebuffer > swapchain_framebuffers;
	//used from on_swapchain and the destructor: (framebuffers are created in on_swapchain)
	void destroy_framebuffers();

	//--------------------------------------------------------------------
	//Resources that change when time passes or the user interacts:
	struct FreeCamera;

	virtual void update(float dt) override;
	virtual void on_input(InputEvent const &) override;
	void update_free_camera(FreeCamera& camera);

	float time = 0.0f;

	glm::mat4x4 CLIP_FROM_WORLD;

	std::vector<LinesPipeline::Vertex> lines_vertices;

    ObjectsPipeline::World world;
    
    struct ObjectInstance {
		ObjectVertices vertices;
		ObjectsPipeline::Transform transform;
        uint32_t texture = 0;
	};
	std::vector< ObjectInstance > object_instances;

	struct FreeCamera
	{
		glm::vec3 target = {0.0f, 0.0f, 0.5f};
		float radius = 10.0f;
		float azimuth = 0.0f;
		float elevation = 0.785398163f; //Pi/4
		glm::vec3 eye = {
			radius * std::cos(elevation) * std::cos(azimuth),
			radius * std::cos(elevation) * std::sin(azimuth),
			radius * std::sin(elevation)
		};
	} user_camera, debug_camera;

	float previous_mouse_x = -1.0f, previous_mouse_y = -1.0f;
	bool shift_down = false;
	bool upside_down = false;

	enum InSceneCamera{
		SceneCamera = 0,
		UserCamera = 1,
		DebugCamera = 2
	};
	InSceneCamera view_camera = InSceneCamera::SceneCamera;

	// used for free and debug cam
	glm::mat4x4 perspective_mat = glm::make_mat4(perspective(
		60.0f * 3.14159265358979323846f / 180.0f, //vfov
		rtg.swapchain_extent.width / float(rtg.swapchain_extent.height), //aspect
		0.1f, //near
		1000.0f //far
	).data());

	// used for frustum culling
	glm::mat4x4 culling_view_mat;
	CullingFrustum scene_cam_frustum, user_cam_frustum;

	InSceneCamera culling_camera = InSceneCamera::SceneCamera;	
	//--------------------------------------------------------------------
	//Rendering function, uses all the resources above to queue work to draw a frame:

	virtual void render(RTG &, RTG::RenderParams const &) override;
};
