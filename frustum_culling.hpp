#pragma once

#include "GLM.hpp"
#include <limits>
#include <array>
// concept and code adapted from https://bruop.github.io/improved_frustum_culling/

struct AABB // axis aligned bounding box
{
    glm::vec3 min = glm::vec3(std::numeric_limits<float>::infinity());
    glm::vec3 max = glm::vec3(-std::numeric_limits<float>::infinity());
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

OBB AABB_transform_to_OBB(const glm::mat4x4& transform_mat, const AABB& aabb);

float project_point_onto_axis(const glm::vec3& point, const glm::vec3& axis);

void project_obb_onto_axis(const OBB& obb, const glm::vec3& axis, float& min_proj, float& max_proj);

void project_frustum_onto_axis(const std::array<glm::vec3, 8>& frustum_vertices, const glm::vec3& axis, float& min_proj, float& max_proj);

bool overlap_on_axis(const std::array<glm::vec3, 8>& frustum_vertices, const OBB& obb, const glm::vec3& axis);

//frustum has to be in the following order
// Near top right,
// Near top left,
// Near bottom right,
// Near bottom left,
// Far top right,
// Far top left,
// Far bottom right,
// Far bottom left
bool check_frustum_obb_intersection(const std::array<glm::vec3, 8>& frustum_vertices, const OBB& obb);