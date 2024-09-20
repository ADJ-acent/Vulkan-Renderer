#pragma once
#include <vulkan.h>
#include <glm/glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

/**
 *  Loads from .s72 format and manages a hiearchy of transformations
 * 
 *  partially referenced from https://github.com/15-466/15-466-f23-base6/blob/main/Scene.hpp
 */

struct Scene
{
    // local transformation
    struct Transform {
		glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f);
		glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		glm::vec3 scale = glm::vec3(1.0f, 1.0f, 1.0f);

        // should not copy construct transforms
        Transform(Transform const &) = delete;
        Transform() = default;
    };


    struct Camera {
        std::string name;
        float aspect;
        float vfov;
        float near;
        float far;
    };

    struct Texture {
        std::string source;
        bool is_2D = true;
        //TODO: store texture here
    };

    struct Material { // assuming all material is lambertian
        std::string name;
        union Lambertian
        {
            glm::vec3 value_albedo;
            Texture* texture_albedo;
        };
        
    };

    struct Mesh {
        std::string source = "";
        VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        uint32_t count = 0;
        Material* material;
        // assuming all has the format of PosNorTanTex
    };

    struct Light {
        // assuming only sun type
        glm::vec3 tint = glm::vec3(1.0f); // multiplied with energy
        float shadow = 0.0f;
        float angle = 0.0f;
        float strength = 1.0f;

    };

    // Node 
    struct Node {
        std::string name;
        Transform* Transform;
        std::vector<Node*> children;
        Camera* cameras;
        Mesh* mesh;
        Light* light;
        // ignoring environment
    };

    std::vector<Node> root_nodes;

    void load(std::string filename);

};
