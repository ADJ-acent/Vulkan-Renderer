#include "RTGRenderer.hpp"
#include <iostream>

//referenced https://lisyarus.github.io/blog/posts/texture-packing.html
void RTGRenderer::ShadowAtlas::update_regions(std::vector<RTGRenderer::LambertianPipeline::SpotLight> & spot_lights, uint8_t reduction)
{
    regions.clear();
    regions.resize(spot_lights.size());
    struct point { uint32_t x, y; } pen = {0,0};
    std::vector<point> ladder;
    for (size_t i = 0; i < spot_lights.size(); ++i) {
        
        const uint32_t texture_size = spot_lights[i].shadow_size >> reduction;

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
