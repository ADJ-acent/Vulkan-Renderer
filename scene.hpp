#pragma once
#include "VK.hpp"
#include "GLM.hpp"
#include <string>
#include <vector>
#include <optional>
#include <variant>

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
        std::variant<float, glm::vec3, std::string> value;
        bool is_2D = true; // if not 2D, it is Cube
        bool has_src = false;
        bool single_channel = false;
        enum Format {
            Linear,
            SRGB,
            RGBE,
        } format = Format::Linear;
        Texture(std::string value_, bool single_channel_, Format format_ = Linear) : value(value_), is_2D(true), has_src(true), single_channel(single_channel_), format(format_) {};
        Texture(float value_, Format format_ = Linear) : value(value_), is_2D(true), has_src(false), single_channel(true), format(format_) {};
        Texture(std::string value_, Format format_ = Linear) : value(value_), is_2D(true), has_src(true), single_channel(false), format(format_) {};
        Texture(glm::vec3 value_, Format format_ = Linear) : value(value_), is_2D(true), has_src(false), single_channel(false), format(format_) {};
        Texture() : value(glm::vec3{1,1,1}), is_2D(true), has_src(false), single_channel(false), format(Linear) {};

        enum struct DefaultTexture: uint8_t {
            DefaultAlbedo = 0,
            DefaultRoughness = 1,
            DefaultMetalness = 2,
            DefaultNormal = 3,
            DefaultDisplacement = 4,
        };

    };

    struct Material {        
        enum MaterialType : uint8_t {
            Lambertian,
            PBR,
            Mirror,
            Environment
        } material_type;
        std::string name;
        uint32_t normal_index = 3;
        uint32_t displacement_index = 4;
        struct MatLambertian {
            uint32_t albedo_index = 0;
        };

        struct MatPBR {
            uint32_t albedo_index = 0;
            uint32_t roughness_index = 1;
            uint32_t metalness_index = 2;
        };
        
        std::variant<std::monostate, MatLambertian, MatPBR> material_textures;
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
        uint32_t material_index = 0; // default material at index 0
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

    struct Environment {
        std::string name;
        std::string source = "";
    };

    // Node 
    struct Node {
        std::string name;
        Transform transform;
        std::vector<uint32_t> children;
        int32_t cameras_index = -1;
        int32_t mesh_index = -1;
        int32_t light_index = -1;
        bool environment = false;
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
    uint32_t MatPBR_count;
    uint32_t MatLambertian_count;
    uint32_t MatEnvMirror_count; // both environment and mirror just need normal and displacement

    std::vector<Texture> textures;

    std::vector<Driver> drivers;
    uint8_t animation_setting;
    Environment environment = Environment();

    std::vector<uint32_t> root_nodes;
    std::string scene_path;

    Scene(std::string filename, std::optional<std::string> camera, uint8_t animation_setting);

    void load(std::string file_path, std::optional<std::string> requested_camera);

    void debug();

    void update_drivers(float dt);

    void set_driver_time(float time);
};
