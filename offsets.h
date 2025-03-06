#pragma once

#include <vector>

// Offsets for the "Hide Name" feature
const DWORD hideNameOffset = 0x1C0044;
const BYTE originalHideNameBytes[] = { 0xC6, 0x86, 0x24, 0x12, 0x00, 0x00, 0x00 };
const BYTE injectedHideNameBytes[] = { 0xC6, 0x86, 0x24, 0x12, 0x00, 0x00, 0x01 };

// FOV offsets
const DWORD offsetGameToBaseAddressFOV = 0x002F7A30;
const std::vector<DWORD> pointsOffsets{ 0x30, 0x38, 0x298, 0x264, 0x10C, 0x3C, 0x4F4 };

// Jump offsets
const DWORD offsetGameToBaseAddressJump = 0x002FFE34;
const std::vector<DWORD> customOffsets{ 0x19C, 0x1D8, 0x1AC, 0x1A4, 0x198, 0x34C, 0x478 };

// Fast Menu offset
const DWORD fastMenuOffset = 0x00C7EF88;

// Noclip offsets
const DWORD offsetGameToBaseAddressNoclip = 0x002F7A28;
const DWORD noclipOffset = 0x4AC;

// Perm offsets
const DWORD offsetGameToBaseAddressPerm = 0x002FFDEC;
const std::vector<DWORD> permOffset{ 0x70, 0xA8, 0x264, 0x1B4, 0x1A0, 0x40, 0x394 };
