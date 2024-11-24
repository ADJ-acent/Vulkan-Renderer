#include "Cloud.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <vector>
#include <iostream>

namespace Cloud {
    Helpers::AllocatedImage Cloud::load_noise(RTG &rtg) {
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
        
        Helpers::AllocatedImage noise = rtg.helpers.create_image(
			VkExtent2D{ .width = uint32_t(width), .height = uint32_t(height) }, // size of each face
			VK_FORMAT_R8G8B8A8_UNORM,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			Helpers::Unmapped, noise_count, 1
		);
        std::cout<<"WH: "<<noise.extent.width<<", "<<noise.extent.height<<", "<<merged_images.size()<<std::endl;
        rtg.helpers.transfer_to_image_layered(merged_images.data(), total_image_size, noise, noise_count);
        
        for (unsigned char* image : images) {
            stbi_image_free(image);
        }
        return noise;
    }
}