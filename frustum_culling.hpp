#pragma once

#include "GLM.hpp"
#include <limits>
// concept and code adapted from https://bruop.github.io/improved_frustum_culling/

struct AABB // axis aligned bounding box
{
    glm::vec3 min = glm::vec3(std::numeric_limits<float>::max());
    glm::vec3 max = glm::vec3(std::numeric_limits<float>::min());
};

struct OBB // Oriented bounding boxs
{
    glm::vec3 center;
    glm::vec3 extent;
    glm::vec3 axis[3];
};

OBB AABB_transform_to_OBB(const glm::mat4x4& transform_mat, const AABB& aabb);