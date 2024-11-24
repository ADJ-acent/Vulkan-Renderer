#pragma once
#include "Helpers.hpp"
#include "data_path.hpp"
#include "RTG.hpp"
#include <string>


namespace Cloud {
    struct NVDF {
        Helpers::AllocatedImage field_data;
        Helpers::AllocatedImage modeling_data;
    };

    static const std::string noise_path = data_path("../resource/NubisVoxelCloudsPack/Noise/Examples/TGA/NubisVoxelCloudNoise.");
    static constexpr uint16_t noise_count = 128;
    Helpers::AllocatedImage load_noise(RTG &);

}