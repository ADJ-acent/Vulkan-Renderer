#pragma once
#include "VK.hpp"
#include "GLM.hpp"
#include <string>
#include <vector>
#include <optional>

/**
 *  Loads from .s72 format and manages a hiearchy of transformations
 * 
 *  Transform implementation partially referenced from https://github.com/15-466/15-466-f23-base6/blob/main/Scene.hpp
 */

struct Scene
{
    // local transformation
    struct Transform {
		glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f);
		glm::quat rotation = glm::quat(0.0f, 0.0f, 0.0f, 1.0f);
		glm::vec3 scale = glm::vec3(1.0f, 1.0f, 1.0f);

		glm::mat4x4 parent_from_local() const;
		glm::mat4x4 local_from_parent() const;

    };

    struct Camera {
        std::string name;
        float aspect;
        float vfov;
        float near;
        float far = -1.0f; // -1 means infinite perspective matrix
        std::vector<uint32_t> local_to_world; // list of node indices to get from local to world (index 0 is a root node)
    };

    struct Texture {
        std::string source = "";
        glm::vec3 value;
        bool is_2D = true;
        bool has_src = false;
        //TODO: store texture here
        Texture(std::string source_) : source(source_), is_2D(true), has_src(true) {};
        Texture(glm::vec3 value_) : value(value_), is_2D(true), has_src(false) {};
        Texture() : value({1,1,1}), is_2D(true), has_src(false) {};

    };

    struct Material { // assuming all material is lambertian
        std::string name;
        uint32_t texture_index;
    };

    struct Mesh {
        std::string name;
        struct Attribute {
            std::string source = "";
            uint32_t offset;
            uint32_t stride;
            VkFormat format;
        };
        Attribute attributes[4]; // Position, Normal, Tangent, TexCoord
        VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        uint32_t count = 0;
        int32_t material_index = -1;
        // assuming all has the format of PosNorTanTex
    };

    struct Light {
        // assuming only sun type
        std::string name;
        glm::vec3 tint = glm::vec3(1.0f); // multiplied with energy
        float shadow = 0.0f;
        float angle = 0.0f;
        float strength = 1.0f;
        std::vector<uint32_t> local_to_world; // list of node indices to get from local to world (index 0 is a root node)
    };

    // Node 
    struct Node {
        std::string name;
        Transform transform;
        std::vector<uint32_t> children;
        int32_t cameras_index = -1;
        int32_t mesh_index = -1;
        int32_t light_index = -1;
        // ignoring environment
    };

    struct Driver {
        std::string name;
        uint32_t node_index;
        enum Channel {
            Translation = 0,
            Scale = 1,
            Rotation = 2,
        } channel;
        std::vector<float> times;
        std::vector<float> values;
        enum InterpolationMode {
            STEP,
            LINEAR,
            SLERP,
        } interpolation = LINEAR;
        uint32_t cur_time_index = 0;
        float cur_time = 0.0f;
    };

    std::vector<Node> nodes;
    std::vector<Camera> cameras;
    int32_t requested_camera_index = -1;

    std::vector<Light> lights;
    std::vector<Mesh> meshes;
    uint32_t vertices_count = 0;

    std::vector<Material> materials;
    std::vector<Texture> textures;
    std::vector<Driver> drivers;
    uint8_t animation_setting;

    std::vector<uint32_t> root_nodes;
    std::string scene_path;

    Scene(std::string filename, std::optional<std::string> camera, uint8_t animation_setting);

    void load(std::string file_path, std::optional<std::string> requested_camera);

    void debug();

    void update_drivers(float dt);
};
