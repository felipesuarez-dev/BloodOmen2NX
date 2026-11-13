#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "bo2/engine/cell_group.hpp"
#include "bo2/engine/math.hpp"

namespace bo2::platform::sdl::detail {

class LevelPipeline {
public:
    LevelPipeline();
    ~LevelPipeline();
    LevelPipeline(const LevelPipeline&) = delete;
    LevelPipeline& operator=(const LevelPipeline&) = delete;

    void upload(const engine::LevelGeometry& geometry);
    // Rewrites the positions and normals of `positions.size()` vertices from
    // `first` on; the other attributes come from `geometry`. This is how a
    // skinned actor moves: the level stays in the buffer and only the actor's
    // slice is sent again each frame. `normals` may be empty.
    void update_vertices(const engine::LevelGeometry& geometry, std::uint32_t first,
                         const std::vector<engine::Vec3>& positions,
                         const std::vector<engine::Vec3>& normals);
    void draw(const engine::LevelGeometry& geometry, const std::vector<std::uint32_t>& textures,
              const std::vector<std::uint32_t>& lightmaps, std::size_t model_lightmap_offset,
              const engine::Mat4& view_projection, engine::MaterialMode material_mode,
              bool use_lightmap);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace bo2::platform::sdl::detail
