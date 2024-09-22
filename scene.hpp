#pragma once
#include "VK.hpp"
#include "GLM.hpp"
#include <string>
#include <vector>

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
            std::string source;
            uint32_t offset;
            uint32_t stride;
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

    std::vector<Node> nodes;
    std::vector<Camera> cameras;
    std::vector<Light> lights;
    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    std::vector<Texture> textures;
    std::vector<uint32_t> root_nodes;

    Scene(std::string filename);

    void load(std::string file_path);


    void debug();
};
