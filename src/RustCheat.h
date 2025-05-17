// RustCheat.h - Header file for a DirectX-based application
#ifndef RUST_CHEAT_H
#define RUST_CHEAT_H

#include <vector>
#include <map>
#include <string>

// 2D Vector structure for screen coordinates
struct Vector2 {
    float x, y;
};

// 3D Vector structure for world coordinates
struct Vector3 {
    float x, y, z;
};

// Signature map for memory parsing (simplified)
using Signatures = std::map<std::string, std::vector<unsigned char>>;

// Offset map for memory addresses
using Offsets = std::map<std::string, unsigned long long>;

#endif
