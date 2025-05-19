#include <windows.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <minhook.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <cmath>

#pragma warning(disable: 4996)

using json = nlohmann::json;

// Оффсеты для Rust Alkad 2588 (будут найдены дампером)
namespace Offsets {
    DWORD64 GWorld = 0x0;
    DWORD64 GNames = 0x0;
    DWORD64 LocalPlayer = 0x0;
    DWORD64 EntityList = 0x0;
    DWORD64 ViewMatrix = 0x0;
    DWORD64 Health = 0x320;
    DWORD64 Position = 0x1A0;
    DWORD64 Name = 0x4B0;
    DWORD64 BoneArray = 0x5A0; // Оффсет для костей (Aimbot)
}

// Глобальные переменные
HMODULE hModule;
bool bShowMenu = false;
bool bESP = false;
bool bAimbot = false;
bool bNoRecoil = false;

// Структура для игроков
struct Player {
    DWORD64 entity;
    float x, y, z;
    int health;
    std::string name;
    bool isVisible;
};

// Вектор для хранения игроков
std::vector<Player> playerList;

// Функция чтения памяти
template<typename T>
T ReadMemory(DWORD64 address) {
    if (!address) return T();
    return *(T*)address;
}

// Функция записи в память
template<typename T>
void WriteMemory(DWORD64 address, T value) {
    if (!address) return;
    *(T*)address = value;
}

// Сканирование памяти для поиска сигнатур
DWORD64 FindPattern(const char* pattern, const char* mask) {
    MODULEINFO modInfo;
    GetModuleInformation(GetCurrentProcess(), GetModuleHandle(NULL), &modInfo, sizeof(MODULEINFO));
    DWORD64 base = (DWORD64)modInfo.lpBaseOfDll;
    DWORD size = modInfo.SizeOfImage;

    size_t patternLength = strlen(mask);
    for (DWORD i = 0; i < size - patternLength; i++) {
        bool found = true;
        for (DWORD j = 0; j < patternLength; j++) {
            found &= mask[j] == '?' || pattern[j] == *(char*)(base + i + j);
        }
        if (found) {
            return base + i;
        }
    }
    return 0;
}

// Авто дампер оффсетов (улучшенный поиск)
void DumpOffsets() {
    const char* gWorldSig = "\x48\x8B\x05\x00\x00\x00\x00\x48\x85\xC0\x74\x00\x48\x8B\x80";
    const char* gWorldMask = "xxx????xxxx?xxx";
    const char* gNamesSig = "\x4C\x8D\x0D\x00\x00\x00\x00\x48\x8D\x15\x00\x00\x00\x00\xE8";
    const char* gNamesMask = "xxx????xxx????x";
    const char* localPlayerSig = "\x48\x8B\x0D\x00\x00\x00\x00\x48\x85\xC9\x0F\x84\x00\x00\x00\x00";
    const char* localPlayerMask = "xxx????xxxxxx????";
    const char* entityListSig = "\x48\x8B\x15\x00\x00\x00\x00\x48\x8D\x0D\x00\x00\x00\x00\xE8";
    const char* entityListMask = "xxx????xxx????x";
    const char* viewMatrixSig = "\x48\x8D\x0D\x00\x00\x00\x00\x48\x89\x01\x48\x8D\x54\x24";
    const char* viewMatrixMask = "xxx????xxxxxxx";
    const char* boneArraySig = "\x48\x8B\x80\x00\x00\x00\x00\x48\x85\xC0\x74\x00\x48\x8B\x88";
    const char* boneArrayMask = "xxx????xxxx?xxx";

    Offsets::GWorld = FindPattern(gWorldSig, gWorldMask);
    Offsets::GNames = FindPattern(gNamesSig, gNamesMask);
    Offsets::LocalPlayer = FindPattern(localPlayerSig, localPlayerMask);
    Offsets::EntityList = FindPattern(entityListSig, entityListMask);
    Offsets::ViewMatrix = FindPattern(viewMatrixSig, viewMatrixMask);
    Offsets::BoneArray = FindPattern(boneArraySig, boneArrayMask);

    std::ofstream file("offsets_dump.txt");
    file << "GWorld: 0x" << std::hex << Offsets::GWorld << "\n";
    file << "GNames: 0x" << std::hex << Offsets::GNames << "\n";
    file << "LocalPlayer: 0x" << std::hex << Offsets::LocalPlayer << "\n";
    file << "EntityList: 0x" << std::hex << Offsets::EntityList << "\n";
    file << "ViewMatrix: 0x" << std::hex << Offsets::ViewMatrix << "\n";
    file << "BoneArray: 0x" << std::hex << Offsets::BoneArray << "\n";
    file.close();
    std::cout << "[Auto Dumper] Offsets dumped to offsets_dump.txt\n";
}

// Авто апдейтер оффсетов
void UpdateOffsets() {
    std::ifstream file("offsets_dump.txt");
    if (!file.is_open()) {
        DumpOffsets();
        return;
    }

    json oldOffsets;
    file >> oldOffsets;
    file.close();

    DumpOffsets();

    std::ifstream newFile("offsets_dump.txt");
    json newOffsets;
    newFile >> newOffsets;
    newFile.close();

    if (oldOffsets != newOffsets) {
        std::cout << "[Auto Updater] Offsets updated!\n";
    }
    else {
        std::cout << "[Auto Updater] Offsets unchanged.\n";
    }
}

// Расшифровщик строк
std::string DecryptString(DWORD64 address) {
    if (!address) return "";
    char encrypted[256];
    strncpy(encrypted, (char*)address, sizeof(encrypted) - 1);
    encrypted[sizeof(encrypted) - 1] = '\0';

    std::string decrypted;
    for (size_t i = 0; i < strlen(encrypted); i++) {
        decrypted += encrypted[i] ^ 0x42; // Простой XOR, замени на реальный метод
    }
    return decrypted;
}

// Получение позиции локального игрока
struct Vector3 {
    float x, y, z;
};

Vector3 GetLocalPlayerPosition() {
    Vector3 pos = { 0, 0, 0 };
    if (!Offsets::LocalPlayer) return pos;

    DWORD64 localPlayer = ReadMemory<DWORD64>(Offsets::LocalPlayer);
    if (!localPlayer) return pos;

    pos.x = ReadMemory<float>(localPlayer + Offsets::Position);
    pos.y = ReadMemory<float>(localPlayer + Offsets::Position + 4);
    pos.z = ReadMemory<float>(localPlayer + Offsets::Position + 8);
    return pos;
}

// Обновление списка игроков
void UpdatePlayers() {
    playerList.clear();
    if (!Offsets::EntityList) return;

    DWORD64 entityList = ReadMemory<DWORD64>(Offsets::EntityList);
    if (!entityList) return;

    for (int i = 0; i < 100; i++) {
        DWORD64 entity = ReadMemory<DWORD64>(entityList + i * sizeof(DWORD64));
        if (!entity) continue;

        Player player;
        player.entity = entity;
        player.health = ReadMemory<int>(entity + Offsets::Health);
        player.x = ReadMemory<float>(entity + Offsets::Position);
        player.y = ReadMemory<float>(entity + Offsets::Position + 4);
        player.z = ReadMemory<float>(entity + Offsets::Position + 8);
        player.name = DecryptString(entity + Offsets::Name);
        player.isVisible = true; // Замени на реальную проверку
        playerList.push_back(player);
    }
}

// Aimbot: Наведение на ближайшего игрока
void RunAimbot() {
    if (!bAimbot || !Offsets::LocalPlayer || !Offsets::ViewMatrix || !Offsets::BoneArray) return;

    Vector3 localPos = GetLocalPlayerPosition();
    Player* target = nullptr;
    float closestDist = FLT_MAX;

    for (auto& player : playerList) {
        if (!player.isVisible || player.health <= 0) continue;

        float dist = sqrt(
            pow(player.x - localPos.x, 2) +
            pow(player.y - localPos.y, 2) +
            pow(player.z - localPos.z, 2)
        );

        if (dist < closestDist) {
            closestDist = dist;
            target = &player;
        }
    }

    if (!target) return;

    // Получаем кости головы (пример)
    DWORD64 boneArray = ReadMemory<DWORD64>(target->entity + Offsets::BoneArray);
    if (boneArray) {
        Vector3 headPos = { ReadMemory<float>(boneArray + 8 * 6), // Индекс кости головы (пример)
                            ReadMemory<float>(boneArray + 8 * 6 + 4),
                            ReadMemory<float>(boneArray + 8 * 6 + 8) };

        // Простая логика наведения (нужна ViewMatrix для точности)
        std::cout << "[Aimbot] Targeting head of " << target->name << " at distance: " << closestDist << "\n";
    }
}

// Window procedure для ImGui
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// Hook для No Recoil
typedef void (*OriginalRecoilFunction)();
OriginalRecoilFunction oRecoilFunction = nullptr;

void HookedRecoilFunction() {
    if (bNoRecoil) {
        return;
    }
    oRecoilFunction();
}

// Основной поток чита
DWORD WINAPI MainThread(LPVOID lpParam) {
    if (MH_Initialize() != MH_OK) return 1;

    void* recoilTarget = (void*)0xDEADBEEF; // Замени на реальный адрес функции отдачи
    if (MH_CreateHook(recoilTarget, &HookedRecoilFunction, reinterpret_cast<void**>(&oRecoilFunction)) != MH_OK) return 1;
    if (MH_EnableHook(recoilTarget) != MH_OK) return 1;

    DumpOffsets();
    UpdateOffsets();

    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0, 0, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, L"ImGui Cheat", NULL };
    RegisterClassEx(&wc);
    HWND hwnd = CreateWindow(wc.lpszClassName, L"Rust Alkad Cheat", WS_OVERLAPPEDWINDOW, 100, 100, 400, 600, NULL, NULL, wc.hInstance, NULL);
    ShowWindow(hwnd, SW_SHOWDEFAULT);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);

        UpdatePlayers();
        if (bAimbot) RunAimbot();

        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (bShowMenu) {
            ImGui::Begin("Rust Alkad Cheat 2588", &bShowMenu);
            ImGui::Text("LIXOR333 Cheat Menu");

            if (ImGui::CollapsingHeader("ESP")) {
                ImGui::Checkbox("Enable ESP", &bESP);
                if (bESP) {
                    for (auto& player : playerList) {
                        char label[64];
                        sprintf(label, "%s [HP: %d]", player.name.c_str(), player.health);
                        ImGui::Text(label);
                        ImGui::SameLine();
                        ImGui::Checkbox("Visible", &player.isVisible);
                    }
                }
            }

            if (ImGui::CollapsingHeader("Aimbot")) {
                ImGui::Checkbox("Enable Aimbot", &bAimbot);
            }

            if (ImGui::CollapsingHeader("Weapon")) {
                ImGui::Checkbox("No Recoil", &bNoRecoil);
            }

            if (ImGui::CollapsingHeader("Tools")) {
                if (ImGui::Button("Dump Offsets")) DumpOffsets();
                if (ImGui::Button("Update Offsets")) UpdateOffsets();
            }

            ImGui::End();
        }

        ImGui::Render();
    }

    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    MH_Uninitialize();
    return 0;
}

// Entry point
BOOL APIENTRY DllMain(HMODULE hMod, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hMod);
        hModule = hMod;
        CreateThread(NULL, 0, MainThread, NULL, 0, NULL);
    }
    return TRUE;
}
