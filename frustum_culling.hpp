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
    glm::vec3 extents;
    glm::vec3 axes[3];
};

struct CullingFrustum
{
    float near_right;
    float near_top;
    float near_plane;
    float far_plane;
};

CullingFrustum make_frustum(float vfov, float aspect, float z_near, float z_far);

bool object_in_frustum_check(const glm::mat4x4& transform_mat, const AABB& aabb, const CullingFrustum& frustum);

OBB AABB_transform_to_OBB(const glm::mat4x4& transform_mat, const AABB& aabb);

bool SAT_visibility_test(const OBB& obb, const CullingFrustum& frustum);