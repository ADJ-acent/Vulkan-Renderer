#pragma once
#include "Helpers.hpp"
#include "data_path.hpp"
#include "RTG.hpp"
#include <string>
#include <array>

namespace Cloud {
    struct NVDF {
        Helpers::AllocatedImage3D field_data;
        Helpers::AllocatedImage3D modeling_data;
        VkImageView field_data_view = VK_NULL_HANDLE;
        VkImageView modeling_data_view = VK_NULL_HANDLE;
    };

    static const std::string noise_path = data_path("../resource/NubisVoxelCloudsPack/Noise/Examples/TGA/NubisVoxelCloudNoise.");
    static constexpr uint16_t noise_count = 128;
    static constexpr uint16_t cloud_voxel_layers = 64;
    Helpers::AllocatedImage3D load_noise(RTG &);
    
    NVDF load_cloud(RTG &, std::string directory);
}