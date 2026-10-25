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
    void draw(const engine::LevelGeometry& geometry, const std::vector<std::uint32_t>& textures,
              const std::vector<std::uint32_t>& lightmaps, std::size_t model_lightmap_offset,
              const engine::Mat4& view_projection, engine::MaterialMode material_mode,
              bool use_lightmap);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace bo2::platform::sdl::detail
