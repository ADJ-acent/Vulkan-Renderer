#include "Cloud.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <vector>
#include <iostream>

namespace Cloud {
    Helpers::AllocatedImage3D Cloud::load_noise(RTG &rtg) {
        std::vector<unsigned char*> images(noise_count);
        uint32_t width = 0, height = 0;
        for (uint16_t i = 0; i < noise_count; ++i) {
            char buffer[4];
            _itoa_s(i+1, buffer, 10);
            std::string number(buffer);
            while (number.length() < 3) {
                number = '0' + number;
            }
            
            int x, y, comp;
            images[i] = static_cast<unsigned char*>(stbi_load((noise_path + number + ".tga").c_str(), &x, &y, &comp, 0));
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
        std::vector<unsigned char> merged_images(total_image_size);
        for (size_t i = 0; i < noise_count; ++i) {
            std::memcpy(merged_images.data() + i * image_size, images[i], image_size);
        }
        
        Helpers::AllocatedImage3D noise = rtg.helpers.create_image_3D(
			VkExtent3D{ .width = uint32_t(width), .height = uint32_t(height), .depth = uint32_t(noise_count) }, // size of each face
			VK_FORMAT_R8G8B8A8_UNORM,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped
		);
        rtg.helpers.transfer_to_image_3D(merged_images.data(), total_image_size, noise);
        
        for (unsigned char* image : images) {
            stbi_image_free(image);
        }
        return noise;
    }

    NVDF load_cloud(RTG &rtg, std::string directory)
    // assuming 64 layers, file names is either field_data.number.tga or modeling_data.number.tga
    {
        NVDF cloud_nvdf;
        std::string cloud_paths[2] = {std::string(directory + "field_data."), std::string(directory + "modeling_data.")};

        { // field data
            Helpers::AllocatedImage3D &field_data_image = cloud_nvdf.field_data;
            const std::string& cloud_path = data_path(std::string(directory + "field_data."));
            std::vector<unsigned char*> images(cloud_voxel_layers);
            uint32_t width = 0, height = 0;
            for (uint16_t i = 0; i < cloud_voxel_layers; ++i) {
                char buffer[4];
                _itoa_s(i+1, buffer, 10);
                std::string number(buffer);
                while (number.length() < 3) {
                    number = '0' + number;
                }
                
                int x, y, comp;
                images[i] = static_cast<unsigned char*>(stbi_load((cloud_path + number + ".tga").c_str(), &x, &y, &comp, 0));
                assert(images[i] != NULL);
                assert(comp == 4);
                if (width == 0 && height == 0) {
                    width = x;
                    height = y;
                }
                assert(uint32_t(x) == width);
                assert(uint32_t(y) == height);
                
            }
            uint32_t image_size = width * height * 4;
            uint32_t total_image_size = image_size * cloud_voxel_layers;
            std::vector<unsigned char> merged_images(total_image_size);
            for (size_t i = 0; i < cloud_voxel_layers; ++i) {
                std::memcpy(merged_images.data() + i * image_size, images[i], image_size);
            }
            
            field_data_image = rtg.helpers.create_image_3D(
                VkExtent3D{ .width = uint32_t(width), .height = uint32_t(height), .depth = uint32_t(cloud_voxel_layers)  },
                VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                Helpers::Unmapped
            );
            rtg.helpers.transfer_to_image_3D(merged_images.data(), total_image_size, field_data_image);
            
            for (unsigned char* image : images) {
                stbi_image_free(image);
            }
        }

        { // model data
            Helpers::AllocatedImage3D &model_data_image = cloud_nvdf.modeling_data;
            const std::string& cloud_path = data_path(std::string(directory + "field_data."));
            std::vector<unsigned char*> images(cloud_voxel_layers);
            uint32_t width = 0, height = 0;
            for (uint16_t i = 0; i < cloud_voxel_layers; ++i) {
                char buffer[4];
                _itoa_s(i+1, buffer, 10);
                std::string number(buffer);
                while (number.length() < 3) {
                    number = '0' + number;
                }
                
                int x, y, comp;
                images[i] = static_cast<unsigned char*>(stbi_load((cloud_path + number + ".tga").c_str(), &x, &y, &comp, 0));
                assert(images[i] != NULL);
                assert(comp == 4);
                if (width == 0 && height == 0) {
                    width = x;
                    height = y;
                }
                assert(uint32_t(x) == width);
                assert(uint32_t(y) == height);
                
            }
            uint32_t image_size = width * height * 4;
            uint32_t total_image_size = image_size * cloud_voxel_layers;
            std::vector<unsigned char> merged_images(total_image_size);
            for (size_t i = 0; i < cloud_voxel_layers; ++i) {
                std::memcpy(merged_images.data() + i * image_size, images[i], image_size);
            }
            
            model_data_image = rtg.helpers.create_image_3D(
                VkExtent3D{ .width = uint32_t(width), .height = uint32_t(height), .depth = uint32_t(cloud_voxel_layers)  },
                VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                Helpers::Unmapped
            );
            rtg.helpers.transfer_to_image_3D(merged_images.data(), total_image_size, model_data_image);
            
            for (unsigned char* image : images) {
                stbi_image_free(image);
            }
        }
        
        return cloud_nvdf;
    }
}