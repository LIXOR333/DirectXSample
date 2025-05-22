#include <windows.h>
#include <d3d9.h>
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <random>
#include <map>
#include <algorithm>

// ImGui includes
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"

// Определение экспортируемых функций
#define RUST_CHEAT_API extern "C" __declspec(dllexport)

// Forward declare message handler from imgui_impl_win32.h
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// XOR шифрование для обхода EAC
std::string XorCrypt(const std::string& data, const std::string& key = "FuckEAC2588") {
    std::string result = data;
    for (size_t i = 0; i < data.size(); ++i) {
        result[i] = data[i] ^ key[i % key.size()];
    }
    return result;
}

// Структура смещений
struct Offset {
    std::string name;
    uintptr_t address;
    bool active;
    std::vector<uintptr_t> backups;

    Offset(const std::string& n, uintptr_t a, bool act) : name(n), address(a), active(act), backups() {}
};

// Структура объекта ESP
struct EspObject {
    std::string name;
    float x, y, z;
    float health;
    std::string type;
    float lastUpdateTime;

    EspObject(const std::string& n, float x_, float y_, float z_, float h, const std::string& t)
        : name(n), x(x_), y(y_), z(z_), health(h), type(t), lastUpdateTime(0.0f) {}
};

// Конфигурация чита
struct CheatConfig {
    bool aimbotEnabled = false;
    float aimFov = 90.0f;
    float aimSmoothness = 5.0f;
    bool aimSilent = false;
    bool aimTargetPlayers = true;
    bool aimTargetNPCs = false;
    bool espEnabled = false;
    bool espPlayers = true;
    bool espItems = false;
    bool noRecoil = false;
    float bulletDamageMultiplier = 1.0f;
    float bulletSpeed = 300.0f;
    bool collisionEnabled = true;
    std::vector<Offset> offsets = {
        {"BulletMatrix", 0xD2E3F4B5, true},
        {"BulletDamage", 0xD2E3F4B9, true},
        {"BulletSpeed", 0xD2E3F4BD, true},
        {"BlowpipeMatrix", 0xE1F2A3C4, true},
        {"PlayerBase", 0x1A2B3C4D, true},
        {"EntityList", 0xB4C5D6E7, true}
    };
    std::vector<EspObject> cachedObjects;

    ~CheatConfig() {
        cachedObjects.clear();
        for (auto& offset : offsets) {
            offset.backups.clear();
        }
    }
};

// Глобальные переменные
static CheatConfig g_config;
static bool g_initialized = false;
static bool g_menuVisible = false;
static HWND g_gameWindow = nullptr;
static IDirect3D9* g_pD3D = nullptr;
static IDirect3DDevice9* g_pd3dDevice = nullptr;
static D3DPRESENT_PARAMETERS g_d3dpp = {};
static WNDPROC g_originalWndProc = nullptr;

// Цвета для ImGui
namespace Colors {
    ImVec4 Red = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
    ImVec4 Green = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
    ImVec4 Blue = ImVec4(0.0f, 0.4f, 1.0f, 1.0f);
    ImVec4 Yellow = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
    ImVec4 Orange = ImVec4(1.0f, 0.5f, 0.0f, 1.0f);
    ImVec4 Purple = ImVec4(0.7f, 0.0f, 0.7f, 1.0f);
    ImVec4 Cyan = ImVec4(0.0f, 1.0f, 1.0f, 1.0f);
    ImVec4 White = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    ImVec4 Black = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    ImVec4 Gray = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    ImVec4 DarkGray = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
    ImVec4 LightGray = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
    ImVec4 Transparent = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
}

// Генерация резервных смещений
void GenerateBackupOffsets(Offset& offset) {
    offset.backups.clear();
    for (int i = 1; i <= 1500; i++) {
        offset.backups.push_back(offset.address + i * 0x10);
    }
    for (int i = 1; i <= 50; i++) {
        offset.backups.push_back(offset.address + i * 0x20);
    }
}

// Проверка валидности смещения
bool IsValidOffset(uintptr_t offset) {
    DWORD oldProtect;
    if (!VirtualProtect((LPVOID)offset, sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
    char buffer[4] = {0};
    SIZE_T bytesRead;
    return ReadProcessMemory(GetCurrentProcess(), (LPCVOID)offset, buffer, sizeof(buffer), &bytesRead) && bytesRead == sizeof(buffer);
}

// Сканирование AOB (упрощенно)
uintptr_t ScanAOBForOffset() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 0x1000);
    return 0xD2E3F4B5 + dis(gen); // Заглушка
}

// Попытка использования резервных смещений
bool TryOffset(uintptr_t& offset, const std::vector<uintptr_t>& backups) {
    if (IsValidOffset(offset)) return true;
    for (const auto& backup : backups) {
        if (IsValidOffset(backup)) {
            offset = backup;
            return true;
        }
    }
    offset = ScanAOBForOffset();
    return IsValidOffset(offset);
}

// Сканирование GameAssembly.dll
bool ScanGameAssembly(const std::string& offsetName, uintptr_t& address) {
    static const std::map<std::string, uintptr_t> offsetMap = {
        {"BulletMatrix", 0xD2E3F4B5},
        {"BulletDamage", 0xD2E3F4B9},
        {"BulletSpeed", 0xD2E3F4BD},
        {"BlowpipeMatrix", 0xE1F2A3C4},
        {"PlayerBase", 0x1A2B3C4D},
        {"EntityList", 0xB4C5D6E7}
    };
    auto it = offsetMap.find(offsetName);
    if (it != offsetMap.end()) {
        address = it->second;
        return IsValidOffset(address);
    }
    return false;
}

// Структура матрицы пули
struct BulletMatrix {
    float damage;
    float speed;
    float gravity;

    BulletMatrix() : damage(0.0f), speed(0.0f), gravity(0.0f) {}
};

// Чтение матрицы пули
BulletMatrix ReadBulletMatrix(uintptr_t matrixOffset) {
    BulletMatrix matrix;
    char buffer[12] = {0};
    SIZE_T bytesRead;
    if (ReadProcessMemory(GetCurrentProcess(), (LPCVOID)matrixOffset, buffer, sizeof(buffer), &bytesRead) && bytesRead == sizeof(buffer)) {
        matrix.damage = *(float*)buffer;
        matrix.speed = *(float*)(buffer + 4);
        matrix.gravity = *(float*)(buffer + 8);
    }
    return matrix;
}

// Применение модификаторов пули
void ApplyBulletMods(const CheatConfig& config) {
    if (config.bulletDamageMultiplier != 1.0f) {
        BulletMatrix matrix = ReadBulletMatrix(config.offsets[0].address);
        float damage = matrix.damage * config.bulletDamageMultiplier;
        SIZE_T bytesWritten;
        WriteProcessMemory(GetCurrentProcess(), (LPVOID)config.offsets[1].address, &damage, sizeof(float), &bytesWritten);
    }
    if (config.bulletSpeed != 300.0f) {
        float speed = config.bulletSpeed;
        SIZE_T bytesWritten;
        WriteProcessMemory(GetCurrentProcess(), (LPVOID)config.offsets[2].address, &speed, sizeof(float), &bytesWritten);
    }
}

// Линейная интерполяция
float Lerp(float a, float b, float t) {
    return a + (b - a) * (t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t));
}

// Чтение данных ESP
std::vector<EspObject> ReadEspData(CheatConfig& config) {
    std::vector<EspObject> objects;
    auto currentTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count() / 1000.0f;

    if (!config.espEnabled) {
        config.cachedObjects.clear();
        return objects;
    }

    std::vector<EspObject> newCache;
    if (config.espPlayers && IsValidOffset(config.offsets[4].address)) {
        char buffer[64] = {0};
        SIZE_T bytesRead;
        if (ReadProcessMemory(GetCurrentProcess(), (LPCVOID)config.offsets[4].address, buffer, sizeof(buffer), &bytesRead) && bytesRead == sizeof(buffer)) {
            EspObject player("Player1", *(float*)(buffer + 0x10), *(float*)(buffer + 0x14), *(float*)(buffer + 0x18), *(float*)(buffer + 0x20), "");
            player.lastUpdateTime = currentTime;
            newCache.push_back(player);
        }
    }

    if (config.espItems && IsValidOffset(config.offsets[5].address)) {
        char buffer[64] = {0};
        SIZE_T bytesRead;
        if (ReadProcessMemory(GetCurrentProcess(), (LPCVOID)config.offsets[5].address, buffer, sizeof(buffer), &bytesRead) && bytesRead == sizeof(buffer)) {
            EspObject item("Stone", *(float*)(buffer + 0x10), *(float*)(buffer + 0x14), *(float*)(buffer + 0x18), 0.0f, "Resource");
            item.lastUpdateTime = currentTime;
            newCache.push_back(item);
        }
    }

    for (auto& cached : config.cachedObjects) {
        bool found = false;
        for (auto& newObj : newCache) {
            if (cached.name == newObj.name && cached.type == newObj.type) {
                float t = 0.1f;
                cached.x = Lerp(cached.x, newObj.x, t);
                cached.y = Lerp(cached.y, newObj.y, t);
                cached.z = Lerp(cached.z, newObj.z, t);
                cached.health = newObj.health;
                cached.lastUpdateTime = currentTime;
                objects.push_back(cached);
                found = true;
                break;
            }
        }
        if (!found && (currentTime - cached.lastUpdateTime) < 1.0f) {
            objects.push_back(cached);
        }
    }

    config.cachedObjects = newCache;
    return objects;
}

// Сохранение конфигурации
void SaveConfig(const CheatConfig& config) {
    std::string fullPath = "./RustCheatConfig.cfg";
    std::ofstream file(fullPath, std::ios::binary);
    if (file.is_open()) {
        std::ostringstream oss;
        oss << "AimbotEnabled=" << (config.aimbotEnabled ? 1 : 0) << "\n"
            << "AimFov=" << config.aimFov << "\n"
            << "AimSmoothness=" << config.aimSmoothness << "\n"
            << "AimSilent=" << (config.aimSilent ? 1 : 0) << "\n"
            << "AimTargetPlayers=" << (config.aimTargetPlayers ? 1 : 0) << "\n"
            << "AimTargetNPCs=" << (config.aimTargetNPCs ? 1 : 0) << "\n"
            << "EspEnabled=" << (config.espEnabled ? 1 : 0) << "\n"
            << "EspPlayers=" << (config.espPlayers ? 1 : 0) << "\n"
            << "EspItems=" << (config.espItems ? 1 : 0) << "\n"
            << "NoRecoil=" << (config.noRecoil ? 1 : 0) << "\n"
            << "BulletDamageMultiplier=" << config.bulletDamageMultiplier << "\n"
            << "BulletSpeed=" << config.bulletSpeed << "\n"
            << "CollisionEnabled=" << (config.collisionEnabled ? 1 : 0) << "\n"
            << "[Offsets]\n";
        for (const auto& offset : config.offsets) {
            oss << offset.name << "=0x" << std::hex << offset.address << "\n"
                << offset.name << "_Active=" << (offset.active ? 1 : 0) << "\n";
            for (size_t i = 0; i < offset.backups.size(); ++i) {
                oss << offset.name << "_Backup" << i << "=0x" << std::hex << offset.backups[i] << "\n";
            }
        }
        std::string data = XorCrypt(oss.str());
        file.write(data.c_str(), data.size());
        file.close();
    }
}

// Загрузка конфигурации
void LoadConfig(CheatConfig& config) {
    std::string fullPath = "./RustCheatConfig.cfg";
    std::ifstream file(fullPath, std::ios::binary);
    if (file.is_open()) {
        std::stringstream ss;
        ss << file.rdbuf();
        std::string data = XorCrypt(ss.str());
        std::istringstream iss(data);
        std::string line;
        while (std::getline(iss, line)) {
            try {
                if (line.find("AimbotEnabled=") == 0) config.aimbotEnabled = std::stoi(line.substr(14)) != 0;
                else if (line.find("AimFov=") == 0) config.aimFov = std::stof(line.substr(7));
                else if (line.find("AimSmoothness=") == 0) config.aimSmoothness = std::stof(line.substr(14));
                else if (line.find("AimSilent=") == 0) config.aimSilent = std::stoi(line.substr(10)) != 0;
                else if (line.find("AimTargetPlayers=") == 0) config.aimTargetPlayers = std::stoi(line.substr(16)) != 0;
                else if (line.find("AimTargetNPCs=") == 0) config.aimTargetNPCs = std::stoi(line.substr(13)) != 0;
                else if (line.find("EspEnabled=") == 0) config.espEnabled = std::stoi(line.substr(11)) != 0;
                else if (line.find("EspPlayers=") == 0) config.espPlayers = std::stoi(line.substr(11)) != 0;
                else if (line.find("EspItems=") == 0) config.espItems = std::stoi(line.substr(9)) != 0;
                else if (line.find("NoRecoil=") == 0) config.noRecoil = std::stoi(line.substr(9)) != 0;
                else if (line.find("BulletDamageMultiplier=") == 0) config.bulletDamageMultiplier = std::stof(line.substr(22));
                else if (line.find("BulletSpeed=") == 0) config.bulletSpeed = std::stof(line.substr(12));
                else if (line.find("CollisionEnabled=") == 0) config.collisionEnabled = std::stoi(line.substr(16)) != 0;
                else {
                    for (auto& offset : config.offsets) {
                        if (line.find(offset.name + "=") == 0) {
                            offset.address = std::stoul(line.substr(offset.name.length() + 1), nullptr, 16);
                        } else if (line.find(offset.name + "_Active=") == 0) {
                            offset.active = std::stoi(line.substr(offset.name.length() + 8)) != 0;
                        } else if (line.find(offset.name + "_Backup") == 0) {
                            size_t pos = line.find("=0x");
                            if (pos != std::string::npos) {
                                offset.backups.push_back(std::stoul(line.substr(pos + 3), nullptr, 16));
                            }
                        }
                    }
                }
            } catch (const std::exception& e) {
                // Игнорируем ошибки парсинга
            }
        }
        file.close();
    } else {
        SaveConfig(config);
    }
}

// Применение патча для отсутствия отдачи
void ApplyNoRecoilPatch(const CheatConfig& config) {
    if (config.noRecoil) {
        unsigned char patch[] = {0x90, 0x90};
        SIZE_T bytesWritten;
        WriteProcessMemory(GetCurrentProcess(), (LPVOID)config.offsets[2].address, patch, sizeof(patch), &bytesWritten);
    }
}

// Инициализация DirectX9
bool InitD3D(HWND hWnd) {
    if ((g_pD3D = Direct3DCreate9(D3D_SDK_VERSION)) == NULL)
        return false;

    // Настройка параметров D3D
    ZeroMemory(&g_d3dpp, sizeof(g_d3dpp));
    g_d3dpp.Windowed = TRUE;
    g_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
    g_d3dpp.EnableAutoDepthStencil = TRUE;
    g_d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
    g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE; // Включаем V-Sync

    // Создание D3D устройства
    if (g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_HARDWARE_VERTEXPROCESSING,
        &g_d3dpp, &g_pd3dDevice) < 0)
        return false;

    return true;
}

// Очистка DirectX9
void CleanupD3D() {
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = NULL; }
    if (g_pD3D) { g_pD3D->Release(); g_pD3D = NULL; }
}

// Настройка стиля ImGui
void SetupImGuiStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    
    // Цвета
    style.Colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.10f, 0.94f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.15f, 0.15f, 0.15f, 0.00f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
    style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.20f, 0.20f, 0.54f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.40f, 0.40f, 0.40f, 0.40f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.65f, 0.65f, 0.65f, 0.67f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.29f, 0.29f, 0.29f, 1.00f);
    style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
    style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.94f, 0.00f, 0.00f, 1.00f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.86f, 0.86f, 0.86f, 1.00f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.44f, 0.00f, 0.00f, 0.62f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.75f, 0.00f, 0.00f, 0.62f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(1.00f, 0.00f, 0.00f, 0.62f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.26f, 0.26f, 0.26f, 0.35f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.45f, 0.00f, 0.00f, 0.80f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.75f, 0.00f, 0.00f, 0.80f);
    style.Colors[ImGuiCol_Separator] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
    style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.10f, 0.40f, 0.75f, 0.78f);
    style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.10f, 0.40f, 0.75f, 1.00f);
    style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.26f, 0.26f, 0.25f);
    style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.10f, 0.40f, 0.75f, 0.67f);
    style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.10f, 0.40f, 0.75f, 0.95f);
    style.Colors[ImGuiCol_Tab] = ImVec4(0.44f, 0.00f, 0.00f, 0.62f);
    style.Colors[ImGuiCol_TabHovered] = ImVec4(0.75f, 0.00f, 0.00f, 0.62f);
    style.Colors[ImGuiCol_TabActive] = ImVec4(1.00f, 0.00f, 0.00f, 0.62f);
    style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.36f, 0.36f, 0.36f, 0.54f);
    
    // Стиль
    style.WindowPadding = ImVec2(8, 8);
    style.FramePadding = ImVec2(5, 2);
    style.CellPadding = ImVec2(6, 6);
    style.ItemSpacing = ImVec2(6, 6);
    style.ItemInnerSpacing = ImVec2(6, 6);
    style.TouchExtraPadding = ImVec2(0, 0);
    style.IndentSpacing = 25;
    style.ScrollbarSize = 15;
    style.GrabMinSize = 10;
    
    // Закругления
    style.WindowRounding = 7;
    style.ChildRounding = 4;
    style.FrameRounding = 3;
    style.PopupRounding = 4;
    style.ScrollbarRounding = 9;
    style.GrabRounding = 3;
    style.LogSliderDeadzone = 4;
    style.TabRounding = 4;
}

// Отрисовка вкладки Aimbot
void RenderAimbotTab(CheatConfig& config) {
    ImGui::TextColored(Colors::Red, "Настройки аимбота");
    ImGui::Separator();
    
    ImGui::Checkbox("Включить аимбот", &config.aimbotEnabled);
    ImGui::Checkbox("Тихий аим", &config.aimSilent);
    ImGui::Checkbox("Цель: игроки", &config.aimTargetPlayers);
    ImGui::Checkbox("Цель: NPC", &config.aimTargetNPCs);
    
    ImGui::SliderFloat("FOV", &config.aimFov, 0.0f, 180.0f, "%.1f");
    ImGui::SliderFloat("Плавность", &config.aimSmoothness, 1.0f, 10.0f, "%.1f");
    
    ImGui::Separator();
    if (ImGui::Button("Применить настройки", ImVec2(200, 30))) {
        // Применение настроек аимбота
    }
}

// Отрисовка вкладки ESP
void RenderESPTab(CheatConfig& config) {
    ImGui::TextColored(Colors::Green, "Настройки ESP");
    ImGui::Separator();
    
    ImGui::Checkbox("Включить ESP", &config.espEnabled);
    ImGui::Checkbox("Показывать игроков", &config.espPlayers);
    ImGui::Checkbox("Показывать предметы", &config.espItems);
    
    ImGui::Separator();
    
    if (config.espEnabled) {
        ImGui::TextColored(Colors::Yellow, "Активные объекты ESP:");
        std::vector<EspObject> objects = ReadEspData(config);
        
        if (objects.empty()) {
            ImGui::Text("Объекты не найдены");
        } else {
            for (const auto& obj : objects) {
                if (obj.health > 0) {
                    ImGui::Text("%s: (%.1f, %.1f, %.1f) HP: %.1f", 
                        obj.name.c_str(), obj.x, obj.y, obj.z, obj.health);
                } else {
                    ImGui::Text("%s (%s): (%.1f, %.1f, %.1f)", 
                        obj.name.c_str(), obj.type.c_str(), obj.x, obj.y, obj.z);
                }
            }
        }
    }
}

// Отрисовка вкладки Offsets
void RenderOffsetsTab(CheatConfig& config) {
    ImGui::TextColored(Colors::Blue, "Управление смещениями");
    ImGui::Separator();
    
    for (size_t i = 0; i < config.offsets.size(); ++i) {
        auto& offset = config.offsets[i];
        ImGui::Text("%s: 0x%X [%s]", offset.name.c_str(), offset.address, 
            offset.active ? "Активно" : "Неактивно");
        
        ImGui::SameLine();
        std::string btnLabel = "Сканировать##" + std::to_string(i);
        if (ImGui::Button(btnLabel.c_str())) {
            ScanGameAssembly(offset.name, offset.address);
            offset.active = IsValidOffset(offset.address);
        }
        
        if (!offset.active) {
            ImGui::SameLine();
            std::string btnLabel2 = "Резерв##" + std::to_string(i);
            if (ImGui::Button(btnLabel2.c_str())) {
                offset.active = TryOffset(offset.address, offset.backups);
            }
        }
    }
    
    ImGui::Separator();
    if (ImGui::Button("Сканировать все", ImVec2(200, 30))) {
        for (auto& offset : config.offsets) {
            ScanGameAssembly(offset.name, offset.address);
            offset.active = IsValidOffset(offset.address);
        }
    }
}

// Отрисовка вкладки Weapon Mods
void RenderWeaponModsTab(CheatConfig& config) {
    ImGui::TextColored(Colors::Orange, "Модификации оружия");
    ImGui::Separator();
    
    ImGui::Checkbox("Отключить отдачу", &config.noRecoil);
    
    ImGui::SliderFloat("Множитель урона", &config.bulletDamageMultiplier, 0.5f, 5.0f, "%.1f");
    ImGui::SliderFloat("Скорость пули", &config.bulletSpeed, 100.0f, 1000.0f, "%.0f");
    
    ImGui::Separator();
    if (ImGui::Button("Применить модификации", ImVec2(200, 30))) {
        ApplyBulletMods(config);
        ApplyNoRecoilPatch(config);
    }
}

// Отрисовка вкладки Config
void RenderConfigTab(CheatConfig& config) {
    ImGui::TextColored(Colors::Purple, "Управление конфигурацией");
    ImGui::Separator();
    
    if (ImGui::Button("Сохранить конфигурацию", ImVec2(200, 30))) {
        SaveConfig(config);
        ImGui::OpenPopup("SavedPopup");
    }
    
    if (ImGui::Button("Загрузить конфигурацию", ImVec2(200, 30))) {
        LoadConfig(config);
        ImGui::OpenPopup("LoadedPopup");
    }
    
    // Всплывающие окна для уведомлений
    if (ImGui::BeginPopupModal("SavedPopup", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Конфигурация успешно сохранена!");
        if (ImGui::Button("OK", ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    
    if (ImGui::BeginPopupModal("LoadedPopup", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Конфигурация успешно загружена!");
        if (ImGui::Button("OK", ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    
    ImGui::Separator();
    ImGui::TextColored(Colors::Yellow, "Информация о чите");
    ImGui::Text("Rust Alcatraz Cheat - Версия 2.0");
    ImGui::Text("Разработано для Rust");
    ImGui::Text("Нажмите Insert для открытия/закрытия меню");
}

// Отрисовка главного меню
void RenderCheatMenu(CheatConfig& config, bool menuVisible) {
    if (!menuVisible) return;
    
    // Настройка окна
    ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);
    
    // Начало окна
    if (ImGui::Begin("Rust Alcatraz Cheat", &menuVisible, ImGuiWindowFlags_NoCollapse)) {
        // Вкладки
        if (ImGui::BeginTabBar("CheatTabs", ImGuiTabBarFlags_None)) {
            if (ImGui::BeginTabItem("Aimbot")) {
                RenderAimbotTab(config);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("ESP")) {
                RenderESPTab(config);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Offsets")) {
                RenderOffsetsTab(config);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Weapon Mods")) {
                RenderWeaponModsTab(config);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Config")) {
                RenderConfigTab(config);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

// Отрисовка ESP на экране
void RenderESPOverlay(CheatConfig& config) {
    if (!config.espEnabled) return;
    
    // Получение объектов ESP
    std::vector<EspObject> objects = ReadEspData(config);
    
    // Начало окна оверлея
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::SetNextWindowBgAlpha(0.0f);
    
    if (ImGui::Begin("ESP Overlay", nullptr, 
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | 
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | 
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus)) {
        
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        
        // Отрисовка объектов ESP
        for (const auto& obj : objects) {
            // Преобразование 3D координат в 2D (упрощенно)
            // В реальном чите здесь должно быть правильное преобразование мировых координат в экранные
            float screenX = obj.x + ImGui::GetIO().DisplaySize.x / 2;
            float screenY = obj.y + ImGui::GetIO().DisplaySize.y / 2;
            
            // Отрисовка бокса и информации
            if (obj.health > 0) {
                // Игрок
                drawList->AddRect(
                    ImVec2(screenX - 20, screenY - 40),
                    ImVec2(screenX + 20, screenY + 40),
                    ImColor(Colors::Red), 0.0f, 0, 2.0f
                );
                
                // Имя и здоровье
                drawList->AddText(
                    ImVec2(screenX - 20, screenY - 55),
                    ImColor(Colors::White),
                    (obj.name + " [" + std::to_string(int(obj.health)) + " HP]").c_str()
                );
            } else {
                // Предмет
                drawList->AddCircle(
                    ImVec2(screenX, screenY),
                    10.0f,
                    ImColor(Colors::Green), 0, 2.0f
                );
                
                // Название предмета
                drawList->AddText(
                    ImVec2(screenX - 20, screenY - 25),
                    ImColor(Colors::Green),
                    obj.name.c_str()
                );
            }
        }
    }
    ImGui::End();
}

// Обработчик сообщений окна
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    // Передача сообщений в ImGui
    if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
        return true;
    
    switch (message) {
    case WM_KEYDOWN:
        if (wParam == VK_INSERT) {
            g_menuVisible = !g_menuVisible;
            return 0;
        }
        break;
    }
    
    // Вызов оригинального обработчика
    return CallWindowProc(g_originalWndProc, hWnd, message, wParam, lParam);
}

// Инициализация чита
RUST_CHEAT_API bool InitializeCheat() {
    if (g_initialized) return true;
    
    // Поиск окна игры
    g_gameWindow = FindWindowA(NULL, "Rust");
    if (!g_gameWindow) {
        MessageBoxA(NULL, "Rust не найден! Запустите игру сначала.", "Ошибка", MB_ICONERROR);
        return false;
    }
    
    // Инициализация DirectX9
    if (!InitD3D(g_gameWindow)) {
        MessageBoxA(NULL, "Не удалось инициализировать DirectX9!", "Ошибка", MB_ICONERROR);
        return false;
    }
    
    // Инициализация ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    
    // Настройка стиля
    SetupImGuiStyle();
    
    // Инициализация ImGui для DirectX9
    ImGui_ImplWin32_Init(g_gameWindow);
    ImGui_ImplDX9_Init(g_pd3dDevice);
    
    // Подмена обработчика сообщений окна
    g_originalWndProc = (WNDPROC)SetWindowLongPtr(g_gameWindow, GWLP_WNDPROC, (LONG_PTR)WndProc);
    
    // Инициализация конфигурации
    for (auto& offset : g_config.offsets) {
        GenerateBackupOffsets(offset);
    }
    LoadConfig(g_config);
    
    g_initialized = true;
    return true;
}

// Обновление чита
RUST_CHEAT_API void UpdateCheat() {
    if (!g_initialized) return;
    
    // Начало нового кадра ImGui
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    
    // Отрисовка меню и ESP
    RenderCheatMenu(g_config, g_menuVisible);
    RenderESPOverlay(g_config);
    
    // Применение модификаций
    if (g_config.noRecoil) {
        ApplyNoRecoilPatch(g_config);
    }
    
    // Применение модификаторов пули
    if (g_config.bulletDamageMultiplier != 1.0f || g_config.bulletSpeed != 300.0f) {
        ApplyBulletMods(g_config);
    }
    
    // Завершение кадра ImGui
    ImGui::EndFrame();
    ImGui::Render();
    
    // Очистка устройства
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    g_pd3dDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    g_pd3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    
    // Отрисовка данных ImGui
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    
    // Отображение кадра
    g_pd3dDevice->Present(NULL, NULL, NULL, NULL);
}

// Завершение работы чита
RUST_CHEAT_API void ShutdownCheat() {
    if (!g_initialized) return;
    
    // Восстановление оригинального обработчика сообщений
    if (g_gameWindow && g_originalWndProc) {
        SetWindowLongPtr(g_gameWindow, GWLP_WNDPROC, (LONG_PTR)g_originalWndProc);
    }
    
    // Сохранение конфигурации
    SaveConfig(g_config);
    
    // Очистка ImGui
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    
    // Очистка DirectX9
    CleanupD3D();
    
    g_initialized = false;
}

// Получение указателя на конфигурацию
RUST_CHEAT_API CheatConfig* GetCheatConfig() {
    return &g_config;
}

// Точка входа DLL
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        // Инициализация при загрузке DLL
        DisableThreadLibraryCalls(hModule);
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)InitializeCheat, NULL, 0, NULL);
        break;
    case DLL_THREAD_ATTACH:
        break;
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
        // Очистка при выгрузке DLL
        if (g_initialized) {
            ShutdownCheat();
        }
        break;
    }
    return TRUE;
}
