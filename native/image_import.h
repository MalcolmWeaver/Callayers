#pragma once
#include "generated_card.h"
#include <filesystem>
namespace callayers {
GeneratedCard ImportImage(const std::filesystem::path& path);
}
