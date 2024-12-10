#include "Cloud.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <vector>
#include <iostream>

namespace Cloud {
    Helpers::AllocatedImage3D Cloud::load_noise(RTG &rtg) {
        std::vector<float*> images(noise_count);
        uint32_t width = 0, height = 0;
        for (uint16_t i = 0; i < noise_count; ++i) {
            char buffer[4];
            _itoa_s(i+1, buffer, 10);
            std::string number(buffer);
            while (number.length() < 3) {
                number = '0' + number;
            }
            
            int x, y, comp;
            images[i] = stbi_loadf((noise_path + number + ".tga").c_str(), &x, &y, &comp, 0);
            assert(comp == 4);
            assert(images[i] != NULL);
            if (width == 0 && height == 0) {
                width = x;
                height = y;
            }
            assert(uint32_t(x) == width);
            assert(uint32_t(y) == height);
            
        }
        uint32_t image_size = width * height * 4;
        uint32_t total_image_size = image_size * noise_count;
        std::vector<float> merged_images(total_image_size);
        for (size_t i = 0; i < noise_count; ++i) {
            std::memcpy(merged_images.data() + i * image_size, images[i], image_size * 4);
        }
        
        Helpers::AllocatedImage3D noise = rtg.helpers.create_image_3D(
			VkExtent3D{ .width = uint32_t(width), .height = uint32_t(height), .depth = uint32_t(noise_count) }, // size of each face
			VK_FORMAT_R32G32B32A32_SFLOAT,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped
		);
        rtg.helpers.transfer_to_image_3D(merged_images.data(), merged_images.size() * sizeof(merged_images[0]), noise);
        
        for (float* image : images) {
            stbi_image_free(image);
        }
        return noise;
    }

    NVDF load_cloud(RTG &rtg, std::string directory)
    // assuming 64 layers, file names is either field_data.number.tga or modeling_data.number.tga
    {
       NVDF cloud_nvdf;
        std::string cloud_paths[2] = {
            directory + "field_data.",
            directory + "modeling_data."
        };

        auto load_image_stack = [&](const std::string &path_prefix, Helpers::AllocatedImage3D &image_output, VkFormat format, bool use_float) {
            uint32_t width = 0, height = 0;
            std::vector<void*> images(cloud_voxel_layers); // Use void* to store either float* or unsigned char*
            for (uint16_t i = 0; i < cloud_voxel_layers; ++i) {
                char buffer[4];
                _itoa_s(i + 1, buffer, 10);
                std::string number(buffer);
                while (number.length() < 3) {
                    number = '0' + number;
                }

                int x, y, comp;
                if (use_float) {
                    images[i] = stbi_loadf((path_prefix + number + ".tga").c_str(), &x, &y, &comp, 0);
                } else {
                    images[i] = stbi_load((path_prefix + number + ".tga").c_str(), &x, &y, &comp, 0);
                }

                assert(images[i] != nullptr); // Ensure the image is loaded
                assert(comp == 4);           // Ensure RGBA format
                if (width == 0 && height == 0) {
                    width = x;
                    height = y;
                }
                assert(uint32_t(x) == width);
                assert(uint32_t(y) == height);
            }

            uint32_t image_size = width * height * 4;
            uint32_t total_image_size = image_size * cloud_voxel_layers;

            if (use_float) {
                std::vector<float> merged_images(total_image_size);
                for (size_t i = 0; i < cloud_voxel_layers; ++i) {
                    std::memcpy(merged_images.data() + i * image_size, images[i], image_size * sizeof(float));
                }

                image_output = rtg.helpers.create_image_3D(
                    VkExtent3D{width, height, uint32_t(cloud_voxel_layers)},
                    format,
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    Helpers::Unmapped
                );

                rtg.helpers.transfer_to_image_3D(merged_images.data(), merged_images.size() * sizeof(float), image_output);
            } else {
                std::vector<unsigned char> merged_images(total_image_size);
                for (size_t i = 0; i < cloud_voxel_layers; ++i) {
                    std::memcpy(merged_images.data() + i * image_size, images[i], image_size);
                }

                image_output = rtg.helpers.create_image_3D(
                    VkExtent3D{width, height, uint32_t(cloud_voxel_layers)},
                    format,
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    Helpers::Unmapped
                );

                rtg.helpers.transfer_to_image_3D(merged_images.data(), merged_images.size(), image_output);
            }

            for (void* image : images) {
                stbi_image_free(image); // Free loaded images
            }
        };
        
        // Load field data
        load_image_stack(
            directory + "field_data.",
            cloud_nvdf.field_data,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            true
        );

        // Load modeling data
        load_image_stack(
            directory + "modeling_data.",
            cloud_nvdf.modeling_data,
            VK_FORMAT_R8G8B8A8_UNORM,
            false
        );



        
        return cloud_nvdf;
    }
}