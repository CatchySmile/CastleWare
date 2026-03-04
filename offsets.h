#pragma once

// Offsets for Cubic Castles (external editor)
// "Cubic.exe" + 0x002F9A28 -> pointer P
// P + 0x4AC = Player Size
// P + 0x478 = Jump Potential

#include <cstdint>
#include <string>

static const std::wstring targetProcessName = L"Cubic.exe";
static constexpr uintptr_t offsetGameToBaseAddress = 0x002F9A28;
static constexpr uintptr_t playerSizeOffset = 0x4AC;
static constexpr uintptr_t jumpPotentialOffset = 0x478;
