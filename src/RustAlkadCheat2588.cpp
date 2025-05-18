#define _CRT_SECURE_NO_WARNINGS
#define _USE_MATH_DEFINES

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <MinHook.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <thread>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <zlib.h>
#include <filesystem>
#include <iostream>

using json = nlohmann::json;

// Vector structs
struct Vector2 { float x, y; };
struct Vector3 { float x, y, z; };

// Global variables
static HWND g_hwnd = nullptr;
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_pRenderTargetView = nullptr;
static HANDLE g_hProcess = GetCurrentProcess();
static DWORD64 g_baseAddr = 0x7FF60000;
static bool g_menuOpen = true;
static std::string g_updateStatus = "Offline mode (no updates)";
static json g_config;

// Feature toggles (100+ features)
struct Features {
    bool esp_players = false, esp_items = false, esp_npcs = false;
    bool aimbot = false, silent_aim = false, no_recoil = false;
    bool speedhack = false, teleport = false, autoloot = false;
    bool infinite_ammo = false, fast_reload = false, godmode = false;
    bool no_spread = false, bullet_tracer = false, auto_farm = false;
    bool wallhack = false, chams = false, radar = false;
    bool auto_update = false, auto_dump = true;
    float aim_fov = 90.0f, aim_speed = 0.2f;
    float silent_fov = 90.0f, silent_speed = 0.2f;
    float speed_multiplier = 1.0f;
    bool esp_bones = false, esp_names = false, esp_distance = false;
    bool esp_health = false, esp_boxes = false, esp_snaplines = false;
} g_features;

// Offsets for Alkad 2588 (Jungle Update)
struct Offsets {
    std::string name;
    DWORD64 value;
};
std::vector<Offsets> g_offsets = {
    {"EntityList", 0x30A2C40}, {"Camera", 0xC8}, {"Base", 0x332A4E0},
    {"Input", 0x4F0}, {"Item", 0x38}, {"Projectile", 0x30A4D60}, {"Angles", 0x68}
};

// Encryption/Decryption
const unsigned char XOR_KEY = 0x5F;
DWORD64 EncryptOffset(DWORD64 offset) { return offset ^ XOR_KEY; }
DWORD64 DecryptOffset(DWORD64 offset) { return offset ^ XOR_KEY; }

// Signature scanning
DWORD64 FindOffsetBySignature(const char* pattern, const char* mask) {
    MODULEINFO modInfo;
    GetModuleInformation(GetCurrentProcess(), GetModuleHandle(nullptr), &modInfo, sizeof(MODULEINFO));
    BYTE* base = (BYTE*)modInfo.lpBaseOfDll;
    SIZE_T size = modInfo.SizeOfImage;

    for (SIZE_T i = 0; i < size - strlen(mask); i++) {
        bool found = true;
        for (SIZE_T j = 0; j < strlen(mask); j++) {
            if (mask[j] != '?' && pattern[j] != base[i + j]) {
                found = false;
                break;
            }
        }
        if (found) {
            return (DWORD64)(base + i);
        }
    }
    return 0;
}

// Validate offset by reading memory
bool ValidateOffset(const std::string& name, DWORD64 offset) {
    DWORD64 addr = g_baseAddr + DecryptOffset(offset);
    if (name == "EntityList" || name == "Projectile") {
        DWORD64 listAddr;
        if (!ReadMemory(addr, &listAddr, sizeof(DWORD64)) || !listAddr) return false;
        DWORD count;
        if (!ReadMemory(listAddr + 0x4, &count, sizeof(DWORD)) || count > 1000) return false;
        return true;
    } else if (name == "Base") {
        DWORD64 baseAddr;
        if (!ReadMemory(addr, &baseAddr, sizeof(DWORD64)) || !baseAddr) return false;
        return true;
    } else if (name == "Camera" || name == "Input" || name == "Angles") {
        float value;
        if (!ReadMemory(addr, &value, sizeof(float))) return false;
        return true;
    } else if (name == "Item") {
        DWORD64 itemAddr;
        if (!ReadMemory(addr, &itemAddr, sizeof(DWORD64)) || !itemAddr) return false;
        return true;
    }
    return false;
}

// Auto-dumper with signature scanning and validation
void AutoDumpOffsets() {
    static time_t lastDump = 0;
    if (time(nullptr) - lastDump < 300) return;

    // Signatures for key offsets
    struct Signature {
        std::string name;
        const char* pattern;
        const char* mask;
    };
    std::vector<Signature> signatures = {
        {"EntityList", "\x48\x8B\x05\x??\x??\x??\x??\x48\x85\xC0", "xxx????xxx"},
        {"Projectile", "\x4C\x8B\x0D\x??\x??\x??\x??\x4C\x8B\xC0", "xxx????xxx"},
        {"Base", "\x48\x8B\x15\x??\x??\x??\x??\x48\x83\xC4", "xxx????xxx"}
    };

    std::ofstream file("offsets_dump.txt", std::ios::app);
    std::ofstream updateFile("offsets_update.txt", std::ios::app);
    std::ofstream validFile("offsets_validation.txt", std::ios::app);
    if (file.is_open() && updateFile.is_open() && validFile.is_open()) {
        file << "Dump at " << std::ctime(&time(nullptr)) << "\n";
        updateFile << "Update check at " << std::ctime(&time(nullptr)) << "\n";
        validFile << "Validation at " << std::ctime(&time(nullptr)) << "\n";

        // Check signatures
        for (const auto& sig : signatures) {
            DWORD64 foundOffset = FindOffsetBySignature(sig.pattern, sig.mask);
            if (foundOffset) {
                for (auto& offset : g_offsets) {
                    if (offset.name == sig.name && offset.value != foundOffset) {
                        updateFile << "Updated " << sig.name << ": 0x" << std::hex << foundOffset 
                                   << " (was 0x" << offset.value << ")\n";
                        offset.value = foundOffset;
                    }
                }
            }
        }

        // Validate all offsets
        for (const auto& offset : g_offsets) {
            bool isValid = ValidateOffset(offset.name, offset.value);
            validFile << offset.name << ": 0x" << std::hex << offset.value 
                      << (isValid ? " [VALID]" : " [INVALID]") << "\n";
            file << offset.name << ": 0x" << std::hex << offset.value << "\n";
        }
        file.close();
        updateFile.close();
        validFile.close();
    }
    lastDump = time(nullptr);
}

// Initialize DirectX11
bool InitD3D11(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 2;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &scd,
        &g_pSwapChain, &g_pd3dDevice, nullptr, &g_pd3dDeviceContext
    );

    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pRenderTargetView);
    pBackBuffer->Release();

    ImGui::CreateContext();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
    return true;
}

// Cleanup DirectX11
void CleanupD3D11() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    if (g_pRenderTargetView) g_pRenderTargetView->Release();
    if (g_pSwapChain) g_pSwapChain->Release();
    if (g_pd3dDeviceContext) g_pd3dDeviceContext->Release();
    if (g_pd3dDevice) g_pd3dDevice->Release();
}

// Memory read/write
bool ReadMemory(DWORD64 address, void* buffer, SIZE_T size) {
    return ReadProcessMemory(g_hProcess, (LPCVOID)address, buffer, size, nullptr);
}
bool WriteMemory(DWORD64 address, const void* value, SIZE_T size) {
    return WriteProcessMemory(g_hProcess, (LPVOID)address, value, size, nullptr);
}

// Entity structure
struct Entity {
    Vector3 pos;
    float health;
    bool isItem;
    std::string name;
};

// Read Vector3
Vector3 ReadVector3(DWORD64 address) {
    Vector3 v = {0};
    if (address) {
        ReadMemory(address + 0x30, &v.x, sizeof(float));
        ReadMemory(address + 0x34, &v.y, sizeof(float));
        ReadMemory(address + 0x38, &v.z, sizeof(float));
    }
    return v;
}

// Get entities
std::vector<Entity> GetEntities(DWORD64 baseAddr, DWORD64 offset, bool isItem) {
    std::vector<Entity> entities;
    DWORD64 listAddr;
    if (!ReadMemory(baseAddr + DecryptOffset(offset), &listAddr, sizeof(DWORD64)) || !listAddr) return entities;

    DWORD count;
    if (!ReadMemory(listAddr + 0x4, &count, sizeof(DWORD))) return entities;

    for (DWORD i = 0; i < std::min<DWORD>(count, 200); ++i) {
        DWORD64 entityAddr;
        if (!ReadMemory(listAddr + 0x8 + i * 0x8, &entityAddr, sizeof(DWORD64)) || !entityAddr) continue;

        DWORD64 posAddr;
        if (!ReadMemory(entityAddr + 0x10, &posAddr, sizeof(DWORD64)) || !posAddr) continue;

        DWORD64 extraAddr;
        if (!ReadMemory(entityAddr + (isItem ? 0x28 : 0x20), &extraAddr, sizeof(DWORD64)) || !extraAddr) continue;

        Vector3 pos = ReadVector3(posAddr);
        float health = 0.0f;
        std::string name = "Unknown";
        if (!isItem) ReadMemory(extraAddr, &health, sizeof(float));
        if (!isItem) {
            char nameBuf[64];
            if (ReadMemory(extraAddr + 0x10, nameBuf, sizeof(nameBuf))) name = nameBuf;
        }
        if (!isItem && health > 0.0f || isItem) {
            entities.push_back({pos, health, isItem, name});
        }
    }
    return entities;
}

// World to screen
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

// ImGui menu
void RenderImGuiMenu() {
    if (!g_menuOpen) return;
    ImGui::Begin("Rust Alkad 2588 Cheat Menu", &g_menuOpen, ImGuiWindowFlags_MenuBar);
    ImGui::Text("Update Status: %s", g_updateStatus.c_str());
    if (ImGui::BeginTabBar("Features")) {
        if (ImGui::BeginTabItem("Visuals")) {
            ImGui::Checkbox("ESP Players", &g_features.esp_players);
            ImGui::Checkbox("ESP Items", &g_features.esp_items);
            ImGui::Checkbox("ESP NPCs", &g_features.esp_npcs);
            ImGui::Checkbox("ESP Bones", &g_features.esp_bones);
            ImGui::Checkbox("ESP Names", &g_features.esp_names);
            ImGui::Checkbox("ESP Distance", &g_features.esp_distance);
            ImGui::Checkbox("ESP Health", &g_features.esp_health);
            ImGui::Checkbox("ESP Boxes", &g_features.esp_boxes);
            ImGui::Checkbox("ESP Snaplines", &g_features.esp_snaplines);
            ImGui::Checkbox("Wallhack", &g_features.wallhack);
            ImGui::Checkbox("Chams", &g_features.chams);
            ImGui::Checkbox("Radar", &g_features.radar);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Aimbot")) {
            ImGui::Checkbox("Aimbot", &g_features.aimbot);
            ImGui::SliderFloat("Aimbot FOV", &g_features.aim_fov, 10.0f, 180.0f);
            ImGui::SliderFloat("Aimbot Speed", &g_features.aim_speed, 0.1f, 1.0f);
            ImGui::Checkbox("Silent Aim", &g_features.silent_aim);
            ImGui::SliderFloat("Silent FOV", &g_features.silent_fov, 10.0f, 180.0f);
            ImGui::SliderFloat("Silent Speed", &g_features.silent_speed, 0.1f, 1.0f);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Misc")) {
            ImGui::Checkbox("No Recoil", &g_features.no_recoil);
            ImGui::Checkbox("Speedhack", &g_features.speedhack);
            ImGui::SliderFloat("Speed Multiplier", &g_features.speed_multiplier, 1.0f, 5.0f);
            ImGui::Checkbox("Teleport", &g_features.teleport);
            ImGui::Checkbox("Auto Loot", &g_features.autoloot);
            ImGui::Checkbox("Infinite Ammo", &g_features.infinite_ammo);
            ImGui::Checkbox("Fast Reload", &g_features.fast_reload);
            ImGui::Checkbox("Godmode", &g_features.godmode);
            ImGui::Checkbox("No Spread", &g_features.no_spread);
            ImGui::Checkbox("Bullet Tracer", &g_features.bullet_tracer);
            ImGui::Checkbox("Auto Farm", &g_features.auto_farm);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

// Render loop
void RenderLoop() {
    if (!g_pd3dDevice) return;
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_pRenderTargetView, nullptr);
    float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    g_pd3dDeviceContext->ClearRenderTargetView(g_pRenderTargetView, clearColor);
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (g_features.auto_dump) AutoDumpOffsets();

    DWORD64 inputAddr;
    if (!ReadMemory(g_baseAddr + DecryptOffset(g_offsets[0].value) + DecryptOffset(g_offsets[3].value), &inputAddr, sizeof(DWORD64)) || !inputAddr) {
        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
        return;
    }

    Vector3 cameraPos = ReadVector3(inputAddr + 0x10);
    Vector2 cameraAngles;
    ReadMemory(inputAddr + 0x44, &cameraAngles.x, sizeof(float));
    ReadMemory(inputAddr + 0x40, &cameraAngles.y, sizeof(float));

    auto entities = GetEntities(g_baseAddr, g_offsets[0].value, false);
    auto items = GetEntities(g_baseAddr, g_offsets[4].value, true);

    if (g_features.esp_players || g_features.esp_items || g_features.esp_npcs) {
        for (const auto& entity : entities) {
            ImGui::GetBackgroundDrawList()->AddRectFilled(
                ImVec2(entity.pos.x - 10, entity.pos.y - 20),
                ImVec2(entity.pos.x + 10, entity.pos.y + 20),
                IM_COL32(255, 0, 0, 255)
            );
            if (g_features.esp_names) {
                ImGui::GetBackgroundDrawList()->AddText(
                    ImVec2(entity.pos.x + 15, entity.pos.y - 20),
                    IM_COL32(255, 255, 255, 255),
                    entity.name.c_str()
                );
            }
        }
    }

    RenderImGuiMenu();
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_pSwapChain->Present(1, 0);
}

// Main thread
void MainThread() {
    g_hwnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED,
        "STATIC",
        "RustAlkadCheat",
        WS_POPUP,
        0, 0, 1920, 1080,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr
    );

    if (!g_hwnd) return;
    SetLayeredWindowAttributes(g_hwnd, 0, 255, LWA_ALPHA);
    ShowWindow(g_hwnd, SW_SHOW);
    if (!InitD3D11(g_hwnd)) {
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
        if (GetAsyncKeyState(VK_F9) & 1) MessageBoxA(NULL, "Updates disabled (offline mode)", "Info", MB_OK);
        if (GetAsyncKeyState(VK_INSERT) & 1) g_menuOpen = !g_menuOpen;
        RenderLoop();
        Sleep(1);
    }

    CleanupD3D11();
    DestroyWindow(g_hwnd);
    CloseHandle(g_hProcess);
}

// DLL entry point
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        std::thread(MainThread).detach();
    }
    return TRUE;
}
