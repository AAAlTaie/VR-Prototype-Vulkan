#include "scene/SplatScene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace scene {
namespace {

constexpr float kSphericalHarmonicDC = 0.28209479177387814f;

const std::unordered_map<std::string, size_t>& scalarSizes() {
    static const std::unordered_map<std::string, size_t> sizes{
        {"char", 1},  {"uchar", 1},  {"int8", 1},   {"uint8", 1},  {"short", 2}, {"ushort", 2},
        {"int16", 2}, {"uint16", 2}, {"int", 4},    {"uint", 4},   {"int32", 4}, {"uint32", 4},
        {"float", 4}, {"float32", 4}, {"double", 8}, {"float64", 8}};
    return sizes;
}

struct Property {
    std::string name;
    std::string type;
    size_t offset = 0;
};

struct Header {
    std::vector<Property> properties;
    std::unordered_map<std::string, const Property*> byName;
    size_t vertexCount = 0;
    size_t stride = 0;
    std::streamoff dataOffset = 0;
};

float readFloat(const std::byte* record, const Property& property) {
    if (property.type == "double" || property.type == "float64") {
        double value = 0.0;
        std::memcpy(&value, record + property.offset, sizeof(value));
        return static_cast<float>(value);
    }
    float value = 0.0f;
    std::memcpy(&value, record + property.offset, sizeof(value));
    return value;
}

float sigmoid(float value) {
    return 1.0f / (1.0f + std::exp(-value));
}

core::Result<Header> parseHeader(std::ifstream& stream, const std::string& path) {
    std::string line;
    if (!std::getline(stream, line) || line.rfind("ply", 0) != 0) {
        return core::Error{path + ": not a PLY file"};
    }

    Header header;
    bool binaryLittleEndian = false;
    bool inVertexElement = false;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        std::istringstream tokens(line);
        std::string keyword;
        tokens >> keyword;

        if (keyword == "format") {
            std::string encoding;
            tokens >> encoding;
            binaryLittleEndian = encoding == "binary_little_endian";
            if (!binaryLittleEndian) {
                return core::Error{path + ": unsupported PLY format '" + encoding +
                                   "', expected binary_little_endian"};
            }
        } else if (keyword == "element") {
            std::string name;
            tokens >> name >> header.vertexCount;
            inVertexElement = name == "vertex";
            if (!inVertexElement) {
                header.vertexCount = 0;
            }
        } else if (keyword == "property" && inVertexElement) {
            Property property;
            tokens >> property.type;
            if (property.type == "list") {
                return core::Error{path + ": list properties are not supported on vertex elements"};
            }
            tokens >> property.name;

            const auto size = scalarSizes().find(property.type);
            if (size == scalarSizes().end()) {
                return core::Error{path + ": unknown property type '" + property.type + "'"};
            }
            property.offset = header.stride;
            header.stride += size->second;
            header.properties.push_back(property);
        } else if (keyword == "end_header") {
            header.dataOffset = stream.tellg();
            for (const Property& property : header.properties) {
                header.byName.emplace(property.name, &property);
            }
            if (header.vertexCount == 0) {
                return core::Error{path + ": no vertex element found"};
            }
            return header;
        }
    }

    return core::Error{path + ": header ended without end_header"};
}

core::Result<std::array<const Property*, 14>> requiredProperties(const Header& header,
                                                                 const std::string& path) {
    static constexpr std::array<const char*, 14> names{"x",       "y",       "z",       "f_dc_0",
                                                       "f_dc_1",  "f_dc_2",  "opacity", "scale_0",
                                                       "scale_1", "scale_2", "rot_0",   "rot_1",
                                                       "rot_2",   "rot_3"};
    std::array<const Property*, 14> resolved{};
    for (size_t index = 0; index < names.size(); ++index) {
        const auto found = header.byName.find(names[index]);
        if (found == header.byName.end()) {
            return core::Error{path + ": missing required property '" + names[index] + "'"};
        }
        resolved[index] = found->second;
    }
    return resolved;
}

uint32_t countSphericalHarmonics(const Header& header) {
    uint32_t count = 0;
    for (const Property& property : header.properties) {
        if (property.name.rfind("f_rest_", 0) == 0) {
            ++count;
        }
    }
    return count;
}

}

core::Result<SplatScene> loadSplatPly(const std::filesystem::path& file) {
    const std::string path = file.string();
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return core::Error{path + ": cannot open"};
    }

    auto header = parseHeader(stream, path);
    if (!header) {
        return core::Error{header.error()};
    }

    auto properties = requiredProperties(header.value(), path);
    if (!properties) {
        return core::Error{properties.error()};
    }

    const Header& layout = header.value();
    const auto& fields = properties.value();

    std::vector<std::byte> raw(layout.stride * layout.vertexCount);
    stream.seekg(layout.dataOffset);
    stream.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
    if (static_cast<size_t>(stream.gcount()) != raw.size()) {
        return core::Error{path + ": truncated vertex data, expected " + std::to_string(raw.size()) +
                           " bytes, read " + std::to_string(stream.gcount())};
    }

    SplatScene result;
    result.splats.resize(layout.vertexCount);
    result.sphericalHarmonicCoefficients = countSphericalHarmonics(layout);

    glm::vec3 minimum(std::numeric_limits<float>::max());
    glm::vec3 maximum(std::numeric_limits<float>::lowest());

    for (size_t index = 0; index < layout.vertexCount; ++index) {
        const std::byte* record = raw.data() + index * layout.stride;
        Splat& splat = result.splats[index];

        splat.position = {readFloat(record, *fields[0]), readFloat(record, *fields[1]),
                          readFloat(record, *fields[2])};
        splat.colour = glm::vec3(0.5f) + kSphericalHarmonicDC * glm::vec3{readFloat(record, *fields[3]),
                                                                          readFloat(record, *fields[4]),
                                                                          readFloat(record, *fields[5])};
        splat.opacity = sigmoid(readFloat(record, *fields[6]));
        splat.scale = {std::exp(readFloat(record, *fields[7])), std::exp(readFloat(record, *fields[8])),
                       std::exp(readFloat(record, *fields[9]))};

        const glm::vec4 rotation{readFloat(record, *fields[10]), readFloat(record, *fields[11]),
                                 readFloat(record, *fields[12]), readFloat(record, *fields[13])};
        const float length = glm::length(rotation);
        splat.rotation = length > 0.0f ? rotation / length : glm::vec4{1.0f, 0.0f, 0.0f, 0.0f};

        splat.padding = 0.0f;
        splat.padding2 = 0.0f;

        minimum = glm::min(minimum, splat.position);
        maximum = glm::max(maximum, splat.position);
    }

    result.bounds = {minimum, maximum};
    return core::Result<SplatScene>(std::move(result));
}

}
