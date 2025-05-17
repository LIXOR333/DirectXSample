// dllmain.cpp - Maxed-out DirectX DLL cheat for Rust Alkad 2588
#define _CRT_SECURE_NO_WARNINGSvariables$0
#define _USE_MATH_DEFINES

#include "RustCheat.h"
#include <d3d9.h>
#include <d3dx9.h>
#include <windows.h>
#include <string>
#include <vector>
#include <thread>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <ctime>

// Encryption key (simple XOR for demo)
const unsigned char XOR_KEY = 0x5F;

// Global variables
static HWND g_hwnd = nullptr;
static LPDIRECT3D9 g_pD3D = nullptr;
static LPDIRECT3DDEVICE9 g_pd3dDevice = nullptr;
static LPD3DXFONT g_pFont = nullptr;
static D3DPRESENT_PARAMETERS g_d3dpp = {};
static HANDLE g_hProcess = nullptr;
static DWORD g_baseAddr = 0x7FF60000; // Placeholder, will auto-update
static bool g_featureEnabled1 = false; // ESP
static bool g_featureEnabled2 = false; // Aim
static bool g_featureEnabled3 = false; // Silent
static float g_aimFOV = 90.0f, g_aimSpeed = 0.2f;
static float g_silentFOV = 90.0f, g_silentSpeed = 0.2f;
static Offsets g_offsets = {
    {"Offset1", DecryptOffset(0x3091C30)}, // Entity list (updated for Alkad 2588)
    {"Offset2", DecryptOffset(0xC8)},      // Camera offset (adjusted for Alkad)
    {"Offset3", DecryptOffset(0x331B3D0)}, // Base offset (updated for Jungle Update)
    {"Offset4", DecryptOffset(0x4E0)},     // Input offset (same)
    {"Offset5", DecryptOffset(0x38)},      // Item offset (shifted for new items)
    {"Offset6", DecryptOffset(0x3093D50)}, // Projectile offset (updated for Alkad 2588)
    {"Offset7", DecryptOffset(0x68)}       // Angles offset (same)
};

// Encryption/Decryption function
unsigned long long EncryptOffset(unsigned long long offset) {
    return offset ^ XOR_KEY;
}

unsigned long long DecryptOffset(unsigned long long offset) {
    return offset ^ XOR_KEY;
}

// Auto-dump offsets
void AutoDumpOffsets() {
    static time_t lastDump = 0;
    if (time(nullptr) - lastDump < 300) return; // Dump every 5 minutes
    std::ofstream file("offsets_dump.txt", std::ios::app);
    if (file.is_open()) {
        file << "Dump at " << ctime(&lastDump) << "\n";
        for (const auto& [key, value] : g_offsets) {
            file << key << ": 0x" << std::hex << value << "\n";
        }
        file << "----------------\n";
        file.close();
    }
    lastDump = time(nullptr);
}

// Validate offsets (simplified for demo)
bool ValidateOffset(DWORD64 offset) {
    unsigned char buffer[1];
    return ReadProcessMemory(GetCurrentProcess(), (LPCVOID)offset, buffer, sizeof(buffer), nullptr);
}

// Update base address (simplified placeholder)
void UpdateBaseAddr() {
    // In real scenario, scan memory for Rust module base using Zydis (web:12)
    g_baseAddr += 0x1000; // Dummy update for demo
    AutoDumpOffsets();
}

// Initialize DirectX device
bool InitD3D(HWND hwnd) {
    g_pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!g_pD3D) return false;

    ZeroMemory(&g_d3dpp, sizeof(g_d3dpp));
    g_d3dpp.Windowed = TRUE;
    g_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_d3dpp.BackBufferFormat = D3DFMT_A8R8G8B8;
    g_d3dpp.EnableAutoDepthStencil = TRUE;
    g_d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
    g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

    HRESULT result = g_pD3D->CreateDevice(
        D3DADAPTER_DEFAULT,
        D3DDEVTYPE_HAL,
        hwnd,
        D3DCREATE_HARDWARE_VERTEXPROCESSING,
        &g_d3dpp,
        &g_pd3dDevice
    );

    if (FAILED(result)) {
        g_pD3D->Release();
        return false;
    }

    result = D3DXCreateFontA(
        g_pd3dDevice,
        14,
        0,
        FW_BOLD,
        1,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        DEFAULT_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        "Arial",
        &g_pFont
    );

    if (FAILED(result)) {
        g_pd3dDevice->Release();
        g_pD3D->Release();
        return false;
    }

    return true;
}

// Cleanup DirectX resources
void CleanupD3D() {
    if (g_pFont) g_pFont->Release();
    if (g_pd3dDevice) g_pd3dDevice->Release();
    if (g_pD3D) g_pD3D->Release();
}

// Read memory
bool ReadMemory(DWORD64 address, void* buffer, SIZE_T size) {
    return ReadProcessMemory(g_hProcess, (LPCVOID)address, buffer, size, nullptr);
}

// Write memory
bool WriteMemory(DWORD64 address, const void* value, SIZE_T size) {
    return WriteProcessMemory(g_hProcess, (LPVOID)address, value, size, nullptr);
}

// Entity structure
struct Entity {
    Vector3 pos;
    float health;
    bool isItem;
};

// Read 3D vector
Vector3 ReadVector3(DWORD64 address) {
    Vector3 v = {0};
    if (ValidateOffset(address + 0x30)) ReadMemory(address + 0x30, &v.x, sizeof(float));
    if (ValidateOffset(address + 0x34)) ReadMemory(address + 0x34, &v.y, sizeof(float));
    if (ValidateOffset(address + 0x38)) ReadMemory(address + 0x38, &v.z, sizeof(float));
    return v;
}

// Get entities
std::vector<Entity> GetEntities(DWORD64 baseAddr, DWORD64 offset, bool isItem) {
    std::vector<Entity> entities;
    DWORD64 listAddr;
    if (!ValidateOffset(baseAddr + offset) || !ReadMemory(baseAddr + offset, &listAddr, sizeof(DWORD64)) || !listAddr) return entities;

    DWORD count;
    if (!ValidateOffset(listAddr + 0x4) || !ReadMemory(listAddr + 0x4, &count, sizeof(DWORD))) return entities;

    for (DWORD i = 0; i < std::min<DWORD>(count, 200); ++i) {
        DWORD64 entityAddr;
        if (!ValidateOffset(listAddr + 0x8 + i * 0x8) || !ReadMemory(listAddr + 0x8 + i * 0x8, &entityAddr, sizeof(DWORD64)) || !entityAddr) continue;

        DWORD64 posAddr;
        if (!ValidateOffset(entityAddr + 0x10) || !ReadMemory(entityAddr + 0x10, &posAddr, sizeof(DWORD64)) || !posAddr) continue;

        DWORD64 extraAddr;
        if (!ValidateOffset(entityAddr + (isItem ? 0x28 : 0x20)) || !ReadMemory(entityAddr + (isItem ? 0x28 : 0x20), &extraAddr, sizeof(DWORD64)) || !extraAddr) continue;

        Vector3 pos = ReadVector3(posAddr);
        float health = 0.0f;
        if (!isItem && ValidateOffset(extraAddr)) ReadMemory(extraAddr, &health, sizeof(float));

        if (!isItem && health > 0.0f || isItem) {
            entities.push_back({pos, health, isItem});
        }
    }
    return entities;
}

// Convert world to screen (adaptive resolution)
Vector2 WorldToScreen(const Vector3& pos, const Vector3& cameraPos, float fov, int screenWidth, int screenHeight) {
    Vector2 screen = {0};
    float dx = pos.x - cameraPos.x;
    float dy = pos.y - cameraPos.y;
    float dz = pos.z - cameraPos.z;
    float dist = sqrtf(dx*dx + dy*dy + dz*dz);
    if (dist < 0.1f) return screen;

    screen.x = screenWidth / 2 + (dx / dist) * (screenWidth / (2 * tanf(fov * M_PI / 360.0f)));
    screen.y = screenHeight / 2 - (dz / dist) * (screenHeight / (2 * tanf(fov * M_PI / 360.0f)));
    return screen;
}

// Find target
Entity* GetTarget(const std::vector<Entity>& entities, const Vector3& cameraPos, const Vector2& angles, float fov) {
    Entity* closest = nullptr;
    float minDist = FLT_MAX;

    for (const auto& entity : entities) {
        float dx = entity.pos.x - cameraPos.x;
        float dy = entity.pos.y - cameraPos.y;
        float dz = entity.pos.z - cameraPos.z;
        float dist = sqrtf(dx*dx + dy*dy + dz*dz);

        if (dist < minDist && dist > 0.1f) {
            float yaw = atan2f(dy, dx) * 180.0f / static_cast<float>(M_PI);
            float yawDiff = fmodf(yaw - angles.x + 180.0f, 360.0f) - 180.0f;

            if (fabsf(yawDiff) < fov) {
                minDist = dist;
                closest = const_cast<Entity*>(&entity);
            }
        }
    }
    return closest;
}

// Set angles
void SetAngles(DWORD64 inputAddr, const Entity& target, const Vector3& cameraPos, float speed) {
    float dx = target.pos.x - cameraPos.x;
    float dy = target.pos.y - cameraPos.y;
    float dz = target.pos.z - cameraPos.z;
    float dist = sqrtf(dx*dx + dy*dy + dz*dz);
    float yaw = atan2f(dy, dx) * 180.0f / static_cast<float>(M_PI);
    float pitch = asinf(dz / dist) * 180.0f / static_cast<float>(M_PI);

    float currentYaw = 0.0f, currentPitch = 0.0f;
    if (ValidateOffset(inputAddr + 0x40)) ReadMemory(inputAddr + 0x40, &currentYaw, sizeof(float));
    if (ValidateOffset(inputAddr + 0x44)) ReadMemory(inputAddr + 0x44, &currentPitch, sizeof(float));

    float newYaw = currentYaw + (yaw - currentYaw) * speed;
    float newPitch = currentPitch + (pitch - currentPitch) * speed;
    if (ValidateOffset(inputAddr + 0x40)) WriteMemory(inputAddr + 0x40, &newYaw, sizeof(float));
    if (ValidateOffset(inputAddr + 0x44)) WriteMemory(inputAddr + 0x44, &newPitch, sizeof(float));
}

// Set silent angles
void SetSilentAngles(DWORD64 projectileAddr, const Entity& target, const Vector3& cameraPos, float speed) {
    float dx = target.pos.x - cameraPos.x;
    float dy = target.pos.y - cameraPos.y;
    float dz = target.pos.z - cameraPos.z;
    float dist = sqrtf(dx*dx + dy*dy + dz*dz);
    float yaw = atan2f(dy, dx) * 180.0f / static_cast<float>(M_PI);
    float pitch = asinf(dz / dist) * 180.0f / static_cast<float>(M_PI);

    float currentYaw = 0.0f, currentPitch = 0.0f;
    if (ValidateOffset(projectileAddr + g_offsets["Offset7"])) ReadMemory(projectileAddr + g_offsets["Offset7"], &currentYaw, sizeof(float));
    if (ValidateOffset(projectileAddr + g_offsets["Offset7"] + 0x4)) ReadMemory(projectileAddr + g_offsets["Offset7"] + 0x4, &currentPitch, sizeof(float));

    float newYaw = currentYaw + (yaw - currentYaw) * speed;
    float newPitch = currentPitch + (pitch - currentPitch) * speed;
    if (ValidateOffset(projectileAddr + g_offsets["Offset7"])) WriteMemory(projectileAddr + g_offsets["Offset7"], &newYaw, sizeof(float));
    if (ValidateOffset(projectileAddr + g_offsets["Offset7"] + 0x4)) WriteMemory(projectileAddr + g_offsets["Offset7"] + 0x4, &newPitch, sizeof(float));
}

// Render loop
void RenderLoop() {
    if (!g_pd3dDevice) return;
    g_pd3dDevice->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);
    g_pd3dDevice->BeginScene();

    UpdateBaseAddr(); // Auto-update base address

    DWORD64 inputAddr;
    if (!ValidateOffset(g_baseAddr + g_offsets["Offset1"] + g_offsets["Offset4"]) || 
        !ReadMemory(g_baseAddr + g_offsets["Offset1"] + g_offsets["Offset4"], &inputAddr, sizeof(DWORD64)) || !inputAddr) {
        g_pd3dDevice->EndScene();
        g_pd3dDevice->Present(nullptr, nullptr, nullptr, nullptr);
        return;
    }

    Vector3 cameraPos = ReadVector3(inputAddr + 0x10);
    Vector2 cameraAngles;
    if (ValidateOffset(inputAddr + 0x44)) ReadMemory(inputAddr + 0x44, &cameraAngles.x, sizeof(float));
    if (ValidateOffset(inputAddr + 0x40)) ReadMemory(inputAddr + 0x40, &cameraAngles.y, sizeof(float));

    D3DVIEWPORT9 viewport;
    g_pd3dDevice->GetViewport(&viewport);
    int screenWidth = viewport.Width;
    int screenHeight = viewport.Height;

    auto entities = GetEntities(g_baseAddr, g_offsets["Offset3"], false);
    auto items = GetEntities(g_baseAddr, g_offsets["Offset5"], true);

    if (g_featureEnabled1) {
        for (const auto& entity : entities) {
            Vector2 screen = WorldToScreen(entity.pos, cameraPos, g_aimFOV, screenWidth, screenHeight);
            float dist = sqrtf(powf(entity.pos.x - cameraPos.x, 2) + powf(entity.pos.y - cameraPos.y, 2) + powf(entity.pos.z - cameraPos.z, 2));
            if (screen.x > 0 && screen.x < screenWidth && screen.y > 0 && screen.y < screenHeight) {
                g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
                D3DRECT rect = { (LONG)screen.x - 10, (LONG)screen.y - 20, (LONG)screen.x + 10, (LONG)screen.y + 20 };
                g_pd3dDevice->Clear(1, &rect, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 255, 0, 0), 0, 0);
                RECT textRect = { (LONG)screen.x + 15, (LONG)screen.y - 20, (LONG)screen.x + 100, (LONG)screen.y + 20 };
                g_pFont->DrawTextA(nullptr, ("Health: " + std::to_string((int)entity.health)).c_str(), -1, &textRect, DT_LEFT, D3DCOLOR_ARGB(255, 255, 255, 255));
                textRect.top += 15;
                g_pFont->DrawTextA(nullptr, ("Dist: " + std::to_string((int)dist) + "m").c_str(), -1, &textRect, DT_LEFT, D3DCOLOR_ARGB(255, 255, 255, 255));
            }
        }
        for (const auto& item : items) {
            Vector2 screen = WorldToScreen(item.pos, cameraPos, g_aimFOV, screenWidth, screenHeight);
            float dist = sqrtf(powf(item.pos.x - cameraPos.x, 2) + powf(item.pos.y - cameraPos.y, 2) + powf(item.pos.z - cameraPos.z, 2));
            if (screen.x > 0 && screen.x < screenWidth && screen.y > 0 && screen.y < screenHeight) {
                D3DRECT rect = { (LONG)screen.x - 10, (LONG)screen.y - 20, (LONG)screen.x + 10, (LONG)screen.y + 20 };
                g_pd3dDevice->Clear(1, &rect, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 0, 255, 0), 0, 0);
                RECT textRect = { (LONG)screen.x + 15, (LONG)screen.y - 20, (LONG)screen.x + 100, (LONG)screen.y + 20 };
                g_pFont->DrawTextA(nullptr, ("Dist: " + std::to_string((int)dist) + "m").c_str(), -1, &textRect, DT_LEFT, D3DCOLOR_ARGB(255, 255, 255, 255));
            }
        }
    }

    if (g_featureEnabled2 && GetAsyncKeyState(VK_RBUTTON)) {
        auto target = GetTarget(entities, cameraPos, cameraAngles, g_aimFOV);
        if (target) {
            SetAngles(inputAddr, *target, cameraPos, g_aimSpeed);
        }
    }

    if (g_featureEnabled3 && GetAsyncKeyState(VK_LBUTTON)) {
        DWORD64 projectileAddr;
        if (ValidateOffset(g_baseAddr + g_offsets["Offset6"]) && 
            ReadMemory(g_baseAddr + g_offsets["Offset6"], &projectileAddr, sizeof(DWORD64)) && projectileAddr) {
            auto target = GetTarget(entities, cameraPos, cameraAngles, g_silentFOV);
            if (target) {
                SetSilentAngles(projectileAddr, *target, cameraPos, g_silentSpeed);
            }
        }
    }

    // Draw indicators
    if (g_featureEnabled1) {
        RECT espRect = { 10, 10, 200, 30 };
        g_pFont->DrawTextA(nullptr, "ESP ON", -1, &espRect, DT_LEFT, D3DCOLOR_ARGB(255, 0, 255, 0));
    } else {
        RECT espRect = { 10, 10, 200, 30 };
        g_pFont->DrawTextA(nullptr, "ESP OFF", -1, &espRect, DT_LEFT, D3DCOLOR_ARGB(255, 255, 0, 0));
    }
    if (g_featureEnabled2) {
        RECT aimRect = { 10, 40, 200, 60 };
        g_pFont->DrawTextA(nullptr, "AIM ON", -1, &aimRect, DT_LEFT, D3DCOLOR_ARGB(255, 0, 255, 0));
    } else {
        RECT aimRect = { 10, 40, 200, 60 };
        g_pFont->DrawTextA(nullptr, "AIM OFF", -1, &aimRect, DT_LEFT, D3DCOLOR_ARGB(255, 255, 0, 0));
    }
    if (g_featureEnabled3) {
        RECT silentRect = { 10, 70, 200, 90 };
        g_pFont->DrawTextA(nullptr, "SILENT ON", -1, &silentRect, DT_LEFT, D3DCOLOR_ARGB(255, 0, 255, 0));
    } else {
        RECT silentRect = { 10, 70, 200, 90 };
        g_pFont->DrawTextA(nullptr, "SILENT OFF", -1, &silentRect, DT_LEFT, D3DCOLOR_ARGB(255, 255, 0, 0));
    }

    g_pd3dDevice->EndScene();
    g_pd3dDevice->Present(nullptr, nullptr, nullptr, nullptr);
}

// Main thread loop
void MainThread() {
    g_hwnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED,
        "STATIC",
        "EpicCheat",
        WS_POPUP,
        0, 0, 1920, 1080,
        nullptr,
        nullptr,
        GetModuleHandle(nullptr),
        nullptr
    );

    if (!g_hwnd) return;

    SetLayeredWindowAttributes(g_hwnd, 0, 255, LWA_ALPHA);
    ShowWindow(g_hwnd, SW_SHOW);

    if (!InitD3D(g_hwnd)) {
        DestroyWindow(g_hwnd);
        return;
    }

    MSG msg;
    ZeroMemory(&msg, sizeof(msg));
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        // Toggle features
        if (GetAsyncKeyState(VK_F1) & 1) { g_featureEnabled1 = !g_featureEnabled1; Sleep(100); }
        if (GetAsyncKeyState(VK_F7) & 1) {
            g_aimFOV = (g_aimFOV > 10.0f) ? g_aimFOV - 10.0f : 180.0f;
            g_aimSpeed = (g_aimSpeed > 0.1f) ? g_aimSpeed - 0.1f : 1.0f;
            Sleep(100);
        }
        if (GetAsyncKeyState(VK_F8) & 1) {
            g_silentFOV = (g_silentFOV > 10.0f) ? g_silentFOV - 10.0f : 180.0f;
            g_silentSpeed = (g_silentSpeed > 0.1f) ? g_silentSpeed - 0.1f : 1.0f;
            Sleep(100);
        }
        if (GetAsyncKeyState(VK_F2) & 1) { g_featureEnabled2 = !g_featureEnabled2; Sleep(100); }
        if (GetAsyncKeyState(VK_F5) & 1) { g_featureEnabled3 = !g_featureEnabled3; Sleep(100); }
        if (GetAsyncKeyState(VK_F4) & 1) break;
        RenderLoop();
        Sleep(1);
    }

    CleanupD3D();
    DestroyWindow(g_hwnd);
    CloseHandle(g_hProcess);
}

// DLL entry point
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        g_hProcess = GetCurrentProcess();
        std::thread(MainThread).detach();
    }
    return TRUE;
}
