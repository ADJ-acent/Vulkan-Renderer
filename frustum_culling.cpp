#include "frustum_culling.hpp"

OBB AABB_transform_to_OBB(const glm::mat4x4 &transform_mat, const AABB &aabb)
{
    //calculate the center and extent of the original AABB
    glm::vec3 aabb_center = (aabb.min + aabb.max) / 2.0f;
    glm::vec3 aabb_extent = (aabb.max - aabb.min) / 2.0f;

    //transform the center of the AABB
    glm::vec4 transformed_center = transform_mat * glm::vec4(aabb_center, 1.0f);

    //extract the axis directions from the transformation matrix (rotation and scale)
    glm::vec3 axis_x = glm::vec3(transform_mat[0]);
    glm::vec3 axis_y = glm::vec3(transform_mat[1]);
    glm::vec3 axis_z = glm::vec3(transform_mat[2]);

    //scale the extents based on the axis lengths
    glm::vec3 new_extent;
    new_extent.x = aabb_extent.x * glm::length(axis_x);
    new_extent.y = aabb_extent.y * glm::length(axis_y);
    new_extent.z = aabb_extent.z * glm::length(axis_z);

    //normalize the axes to remove scaling
    axis_x = glm::normalize(axis_x);
    axis_y = glm::normalize(axis_y);
    axis_z = glm::normalize(axis_z);

    // 6. Create the OBB
    OBB obb;
    obb.center = glm::vec3(transformed_center);
    obb.extent = new_extent;
    obb.axis[0] = axis_x;
    obb.axis[1] = axis_y;
    obb.axis[2] = axis_z;

    return obb;
}