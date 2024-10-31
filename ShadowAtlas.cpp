#include "RTGRenderer.hpp"
#include <iostream>

//referenced https://lisyarus.github.io/blog/posts/texture-packing.html
void RTGRenderer::ShadowAtlas::update_regions(std::vector<RTGRenderer::LambertianPipeline::SpotLight> & spot_lights, std::vector<uint32_t> &sorted_indices, uint8_t reduction)
{
    regions.clear();
    regions.resize(spot_lights.size());
    struct point { uint32_t x, y; } pen = {0,0};
    std::vector<point> ladder;
    for (uint32_t i : sorted_indices) {
        
        const uint32_t texture_size = spot_lights[i].SHADOW_SIZE >> reduction;

        // allocate a texture region
        regions[i] = {pen.x, pen.y, texture_size};
        // shift the pen to the right
        pen.x += texture_size;

        // update the ladder
        if (!ladder.empty() && ladder.back().y == pen.y + texture_size)
            ladder.back().x = pen.x;
        else
            ladder.push_back({pen.x, pen.y + texture_size});

        if (pen.x == size)
        {
            // the pen hit the right edge of the atlas
            ladder.pop_back();
            pen.y += texture_size;
            if (!ladder.empty())
                pen.x = ladder.back().x;
            else
                pen.x = 0;
        }
    }
}

void RTGRenderer::ShadowAtlas::debug()
{
    std::cout<<"\nShadow Atlas, Size: " <<size<<std::endl;
    for (const auto& region : regions) {
        std::cout << "Region(x: " << region.x 
                  << ", y: " << region.y 
                  << ", size: " << region.size << ")\n";
    
    }
}

glm::mat4 RTGRenderer::ShadowAtlas::calculate_shadow_atlas_matrix(const glm::mat4& light_from_world, const Region& region, const int atlas_size) {
    // int shadow_size = region.size;
    // int shadow_x = region.x;
    // int shadow_y = region.y;
    glm::mat4 shadow_matrix = light_from_world;

    //mapping x and y [-w, w] to [0, w]
    glm::mat4 ndc_to_tex_coord = glm::mat4(
        0.5f, 0.0f, 0.0f, 0.5f,   
        0.0f, 0.5f, 0.0f, 0.5f,   
        0.0f, 0.0f, 1.0f, 0.0f,   
        0.0f, 0.0f, 0.0f, 1.0f    
    );
    shadow_matrix = ndc_to_tex_coord * shadow_matrix;
    return shadow_matrix;
    // //Scale by shadow map size relative to atlas size
    // float shadow_scale = static_cast<float>(shadow_size) / static_cast<float>(atlas_size);
    // glm::mat4 scale_to_shadow_map = glm::scale(glm::mat4(1.0f), glm::vec3(shadow_scale, shadow_scale, 1.0f));
    // shadow_matrix = scale_to_shadow_map * shadow_matrix;

    // //Offset for atlas placement
    // glm::vec2 offset = glm::vec2(shadow_x, shadow_y) / static_cast<float>(atlas_size);
    // glm::mat4 offset_to_atlas = glm::translate(glm::mat4(1.0f), glm::vec3(offset, 0.0f));
    // shadow_matrix = offset_to_atlas * shadow_matrix;

    // return shadow_matrix;
}