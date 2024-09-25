#include "frustum_culling.hpp"
#include<iostream>

CullingFrustum make_frustum(float vfov, float aspect, float z_near, float z_far)
{
    float half_height = z_near * tan(glm::radians(vfov) / 2.0f);
    float half_width = half_height * aspect;
    return CullingFrustum{
        .near_right = half_width,
        .near_top = half_height, 
        .near_plane = z_near,
        .far_plane = z_far,
    };
}

bool object_in_frustum_check(const glm::mat4x4 &transform_mat, const AABB &aabb, const CullingFrustum &frustum)
{
    OBB obb = AABB_transform_to_OBB(transform_mat, aabb);
    return SAT_visibility_test(obb, frustum);
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
    // Note: I think this approach is only sufficient if our transform is non-shearing (affine)
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

bool SAT_visibility_test(const OBB &obb, const CullingFrustum& frustum)
{
    float z_near = frustum.near_plane;
    float z_far = frustum.far_plane;
    float x_near = frustum.near_right;
    float y_near = frustum.near_top;
    {
        // Projected center of our OBB
        float MoC = obb.center.z;
        // Projected size of OBB
        float radius = 0.0f;
        for (uint8_t i = 0; i < 3; i++) {
            // dot(M, axes[i]) == axes[i].z;
            radius += fabsf(obb.axes[i].z) * obb.extents[i];
        }
        float obb_min = MoC - radius;
        float obb_max = MoC + radius;
        // We can skip calculating the projection here, it's known
        float m0 = z_far; // Since the frustum's direction is negative z, far is smaller than near
        float m1 = z_near;

        if (obb_min > m1 || obb_max < m0) {

            
            std::cout<<"obb_min: "<<obb_min<<", obb_max: "<<obb_max<<", m1: "<<m1 <<", m0"<<m0<<std::endl;
            return false;
        }
    }
    return true;

    {
        // Frustum normals
        const glm::vec3 M[] = {
            { 0.0, -z_near, y_near }, // Top plane
            { 0.0, z_near, y_near }, // Bottom plane
            { -z_near, 0.0f, x_near }, // Right plane
            { z_near, 0.0f, x_near }, // Left Plane
        };
        for (uint8_t m = 0; m < 4; m++) {
            float MoX = fabsf(M[m].x);
            float MoY = fabsf(M[m].y);
            float MoZ = M[m].z;
            float MoC = dot(M[m], obb.center);

            float obb_radius = 0.0f;
            for (uint8_t i = 0; i < 3; i++) {
                obb_radius += fabsf(dot(M[m], obb.axes[i])) * obb.extents[i];
            }
            float obb_min = MoC - obb_radius;
            float obb_max = MoC + obb_radius;

            float p = x_near * MoX + y_near * MoY;

            float tau_0 = z_near * MoZ - p;
            float tau_1 = z_near * MoZ + p;

            if (tau_0 < 0.0f) {
                tau_0 *= z_far / z_near;
            }
            if (tau_1 > 0.0f) {
                tau_1 *= z_far / z_near;
            }

            if (obb_min > tau_1 || obb_max < tau_0) {
                return false;
            }
        }
    }
    return true;
}
