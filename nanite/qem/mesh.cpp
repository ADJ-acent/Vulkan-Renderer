#include "mesh.h"

#include <cassert>



namespace gfx {

Mesh::Mesh(
           const std::vector<Vertex>& vertices,
           const std::vector<std::uint32_t>& indices,
           const glm::mat4& transform)
    : vertices_{vertices},
      indices_{indices},
      transform_{transform} {
  assert(indices_.size() % 3 == 0);
}

}  // namespace gfx
