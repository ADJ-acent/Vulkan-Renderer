#ifndef GEOMETRY_MESH_SIMPLIFIER_H_
#define GEOMETRY_MESH_SIMPLIFIER_H_

namespace gfx {
class Mesh;

namespace mesh {

/**
 * \brief Reduces the number of triangles in a mesh.
 * \param mesh The mesh to simplify.
 * \param rate The percentage of triangles to be removed (e.g., .95 indicates 95% of triangles should be removed).
 * \return A triangle mesh with \p rate percent of triangles removed from \p mesh.
 * \see docs/surface_simplification for a description of this mesh simplification algorithm.
 */
Mesh Simplify(const Mesh& mesh, const float rate);

}  // namespace mesh
}  // namespace gfx

#endif  // GEOMETRY_MESH_SIMPLIFIER_H_
