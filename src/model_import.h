#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace previewmodel
{
struct Vertex
{
    std::array<float,3> position{0,0,0};
    std::array<float,3> normal{0,1,0};
    std::array<float,4> tangent{1,0,0,1};
    std::array<float,2> uv{0,0};
};

struct Mesh
{
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::string sourceFormat;
    std::string sourcePath;
    std::string warning;
};

bool loadModel(const std::string& path, Mesh& mesh, std::string& error);
}
