#include "core/Config.h"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <stdexcept>
#include <string_view>

#include <toml++/toml.hpp>

namespace core {
namespace {

struct SchemaError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

[[noreturn]] void fail(const std::string& message) {
    throw SchemaError(message);
}

void requireOnlyKeys(const toml::table& table, std::string_view scope,
                     std::initializer_list<std::string_view> allowed) {
    for (const auto& entry : table) {
        const std::string_view key = entry.first.str();
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) {
            fail(std::string("unknown key '").append(key).append("' in ").append(scope));
        }
    }
}

const toml::table& requireSection(const toml::table& root, std::string_view name) {
    const auto* section = root.get_as<toml::table>(name);
    if (!section) {
        fail(std::string("missing section [").append(name).append("]"));
    }
    return *section;
}

template <typename T>
T requireField(const toml::table& table, std::string_view scope, std::string_view key) {
    const std::optional<T> value = table[key].value<T>();
    if (!value) {
        fail(std::string("missing or mistyped key '").append(key).append("' in ").append(scope));
    }
    return *value;
}

uint32_t requirePositive(const toml::table& table, std::string_view scope, std::string_view key,
                         uint32_t maximum) {
    const auto value = requireField<int64_t>(table, scope, key);
    if (value < 1 || value > static_cast<int64_t>(maximum)) {
        fail(std::string("key '").append(key).append("' in ").append(scope).append(" must be between 1 and ") +
             std::to_string(maximum));
    }
    return static_cast<uint32_t>(value);
}

std::array<float, 4> requireNormalizedColor(const toml::table& table, std::string_view scope,
                                           std::string_view key) {
    const toml::array* values = table[key].as_array();
    if (!values || values->size() != 4) {
        fail(std::string("key '").append(key).append("' in ").append(scope).append(" must be 4 numbers"));
    }

    std::array<float, 4> color{};
    for (size_t index = 0; index < color.size(); ++index) {
        const auto component = values->get(index)->value<double>();
        if (!component || *component < 0.0 || *component > 1.0) {
            fail(std::string("key '").append(key).append("' in ").append(scope).append(" components must be in [0, 1]"));
        }
        color[index] = static_cast<float>(*component);
    }
    return color;
}

float requireFloat(const toml::table& table, std::string_view scope, std::string_view key, float minimum,
                   float maximum) {
    const auto value = table[key].value<double>();
    if (!value || *value < static_cast<double>(minimum) || *value > static_cast<double>(maximum)) {
        fail(std::string("key '").append(key).append("' in ").append(scope).append(" must be between ") +
             std::to_string(minimum) + " and " + std::to_string(maximum));
    }
    return static_cast<float>(*value);
}

WindowConfig parseWindow(const toml::table& root) {
    const toml::table& table = requireSection(root, "window");
    requireOnlyKeys(table, "[window]", {"width", "height", "title", "vsync"});
    return {
        requirePositive(table, "[window]", "width", 16384),
        requirePositive(table, "[window]", "height", 16384),
        requireField<std::string>(table, "[window]", "title"),
        requireField<bool>(table, "[window]", "vsync"),
    };
}

RendererConfig parseRenderer(const toml::table& root) {
    const toml::table& table = requireSection(root, "renderer");
    requireOnlyKeys(table, "[renderer]",
                    {"validation", "preferred_device", "frames_in_flight", "clear_color", "log_statistics"});
    return {
        requireField<bool>(table, "[renderer]", "validation"),
        requireField<std::string>(table, "[renderer]", "preferred_device"),
        requirePositive(table, "[renderer]", "frames_in_flight", 3),
        requireNormalizedColor(table, "[renderer]", "clear_color"),
        requireField<bool>(table, "[renderer]", "log_statistics"),
    };
}

TonemapConfig parseTonemap(const toml::table& root) {
    const toml::table& table = requireSection(root, "tonemap");
    requireOnlyKeys(table, "[tonemap]", {"exposure", "operator"});

    const auto name = requireField<std::string>(table, "[tonemap]", "operator");
    uint32_t index = 0;
    if (name == "none") {
        index = 0;
    } else if (name == "reinhard") {
        index = 1;
    } else if (name == "aces") {
        index = 2;
    } else {
        fail("key 'operator' in [tonemap] must be one of: none, reinhard, aces");
    }

    return {requireFloat(table, "[tonemap]", "exposure", 0.01f, 100.0f), index};
}

CameraConfig parseCamera(const toml::table& root) {
    const toml::table& table = requireSection(root, "camera");
    requireOnlyKeys(table, "[camera]",
                    {"field_of_view_degrees", "near_plane", "far_plane", "orbit_degrees_per_second",
                     "elevation_degrees", "distance_multiplier"});
    return {
        requireFloat(table, "[camera]", "field_of_view_degrees", 1.0f, 179.0f),
        requireFloat(table, "[camera]", "near_plane", 0.0001f, 1000.0f),
        requireFloat(table, "[camera]", "far_plane", 0.001f, 100000.0f),
        requireFloat(table, "[camera]", "orbit_degrees_per_second", -360.0f, 360.0f),
        requireFloat(table, "[camera]", "elevation_degrees", -89.0f, 89.0f),
        requireFloat(table, "[camera]", "distance_multiplier", 0.1f, 100.0f),
    };
}

SceneConfig parseScene(const toml::table& root) {
    const toml::table& table = requireSection(root, "scene");
    requireOnlyKeys(table, "[scene]", {"path"});
    return {requireField<std::string>(table, "[scene]", "path")};
}

}

Result<Config> loadConfig(const std::filesystem::path& file) {
    try {
        const toml::table root = toml::parse_file(file.string());
        requireOnlyKeys(root, "root", {"window", "renderer", "tonemap", "camera", "scene"});
        return Config{parseWindow(root), parseRenderer(root), parseTonemap(root), parseCamera(root),
                      parseScene(root)};
    } catch (const toml::parse_error& error) {
        return Error{file.string() + ": " + std::string(error.description())};
    } catch (const SchemaError& error) {
        return Error{file.string() + ": " + error.what()};
    }
}

}
