#include "frustum_culling.hpp"
#include <iostream>


CullingFrustum make_frustum(float vfov, float aspect, float z_near, float z_far)
{
    float half_height = z_near * tan(vfov / 2.0f);
    float half_width = half_height * aspect;
    return CullingFrustum{
        .near_right = half_width,
        .near_top = half_height, 
        .near_plane = z_near,
        .far_plane = z_far,
    };
}

OBB AABB_transform_to_OBB(const glm::mat4x4 &transform_mat, const AABB &aabb)
{
    // Consider four adjacent corners of the ABB
    glm::vec4 corners_aabb[4] = {
                {aabb.min.x, aabb.min.y, aabb.min.z, 1},
                {aabb.max.x, aabb.min.y, aabb.min.z, 1},
                {aabb.min.x, aabb.max.y, aabb.min.z, 1},
                {aabb.min.x, aabb.min.y, aabb.max.z, 1},
    };
    glm::vec3 corners[4];

    // Transform corners
    for (size_t corner_idx = 0; corner_idx < 4; corner_idx++) {
        glm::vec4 point = (transform_mat * corners_aabb[corner_idx]);
        corners[corner_idx] = {point.x, point.y, point.z};
    }

    // Use transformed corners to calculate center, axes and extents
    OBB obb = {
        .axes = {
            corners[1] - corners[0],
            corners[2] - corners[0],
            corners[3] - corners[0]
        },
    };
    obb.center = corners[0] + 0.5f * (obb.axes[0] + obb.axes[1] + obb.axes[2]);
    obb.extents = glm::vec3{ length(obb.axes[0]), length(obb.axes[1]), length(obb.axes[2]) };
    obb.axes[0] = obb.axes[0] / obb.extents.x;
    obb.axes[1] = obb.axes[1] / obb.extents.y;
    obb.axes[2] = obb.axes[2] / obb.extents.z;
    obb.extents *= 0.5f;

    return obb;
}

// Utility to project a point onto an axis (returns the scalar projection)
float project_point_onto_axis(const glm::vec3& point, const glm::vec3& axis) {
    return glm::dot(point, glm::normalize(axis));
}

// Project OBB onto the axis
void project_obb_onto_axis(const OBB& obb, const glm::vec3& axis, float& min_proj, float& max_proj) {
    glm::vec3 obb_vertices[8];

    // Create the OBB vertices based on the center, extents, and axes
    glm::vec3 extents_x = obb.extents.x * obb.axes[0];
    glm::vec3 extents_y = obb.extents.y * obb.axes[1];
    glm::vec3 extents_z = obb.extents.z * obb.axes[2];

    obb_vertices[0] = obb.center + extents_x + extents_y + extents_z;
    obb_vertices[1] = obb.center + extents_x + extents_y - extents_z;
    obb_vertices[2] = obb.center + extents_x - extents_y + extents_z;
    obb_vertices[3] = obb.center + extents_x - extents_y - extents_z;
    obb_vertices[4] = obb.center - extents_x + extents_y + extents_z;
    obb_vertices[5] = obb.center - extents_x + extents_y - extents_z;
    obb_vertices[6] = obb.center - extents_x - extents_y + extents_z;
    obb_vertices[7] = obb.center - extents_x - extents_y - extents_z;

    // Project all 8 vertices and find the min/max projection values
    min_proj = project_point_onto_axis(obb_vertices[0], axis);
    max_proj = min_proj;

    for (int i = 1; i < 8; ++i) {
        float proj = project_point_onto_axis(obb_vertices[i], axis);
        if (proj < min_proj) min_proj = proj;
        if (proj > max_proj) max_proj = proj;
    }
}

// Project frustum vertices onto the axis
void project_frustum_onto_axis(const std::array<glm::vec3, 8>& frustum_vertices, const glm::vec3& axis, float& min_proj, float& max_proj) {
    min_proj = project_point_onto_axis(frustum_vertices[0], axis);
    max_proj = min_proj;

    for (int i = 1; i < 8; ++i) {
        float proj = project_point_onto_axis(frustum_vertices[i], axis);
        if (proj < min_proj) min_proj = proj;
        if (proj > max_proj) max_proj = proj;
    }
}

// Test for overlap on a given axis
bool overlap_on_axis(const std::array<glm::vec3, 8>& frustum_vertices, const OBB& obb, const glm::vec3& axis) {
    float min_proj_frustum, max_proj_frustum;
    float min_proj_obb, max_proj_obb;

    // Project both the frustum and OBB onto the axis
    project_frustum_onto_axis(frustum_vertices, axis, min_proj_frustum, max_proj_frustum);
    project_obb_onto_axis(obb, axis, min_proj_obb, max_proj_obb);

    // Check for overlap (if there's no overlap, they are separated on this axis)
    return !(max_proj_obb < min_proj_frustum || max_proj_frustum < min_proj_obb);
}

// Main function to check if the OBB intersects with the frustum
bool check_frustum_obb_intersection(const std::array<glm::vec3, 8>& frustum_vertices, const OBB& obb) {
    // Test axes: OBB axes (3)
    for (int i = 0; i < 3; ++i) {
        if (!overlap_on_axis(frustum_vertices, obb, obb.axes[i])) {
            return false; // Separation found, so no intersection
        }
    }

    std::array<glm::vec3, 8> frustum_edges = {
        frustum_vertices[4] - frustum_vertices[0],
        frustum_vertices[2] - frustum_vertices[0],
        frustum_vertices[1] - frustum_vertices[0],
        frustum_vertices[2] - frustum_vertices[3],
        frustum_vertices[1] - frustum_vertices[3],
        frustum_vertices[7] - frustum_vertices[3],
        frustum_vertices[5] - frustum_vertices[1],
        frustum_vertices[6] - frustum_vertices[2]
    };
    // Test axes: Frustum face normals (6)

    std::array<glm::vec3, 5> frustum_normals = {
        glm::cross(frustum_edges[1], frustum_edges[0]),
        glm::cross(frustum_edges[0], frustum_edges[2]),
        glm::cross(frustum_edges[2], frustum_edges[1]),
        glm::cross(frustum_edges[5], frustum_edges[3]),
        glm::cross(frustum_edges[4], frustum_edges[5])
    };
    for (glm::vec3& face_normal : frustum_normals) {
        if (!overlap_on_axis(frustum_vertices, obb, face_normal)) {
            return false; // Separation found, so no intersection
        }
    }

    // Test axes: Cross products of OBB axes and frustum edges (9)
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 8; ++j) {
            glm::vec3 axis = glm::cross(obb.axes[i], frustum_edges[j]);
            if (!overlap_on_axis(frustum_vertices, obb, axis)) {
                return false; // Separation found, so no intersection
            }
        }
    }

    // If no separating axis is found, the OBB and frustum intersect
    return true;
}
