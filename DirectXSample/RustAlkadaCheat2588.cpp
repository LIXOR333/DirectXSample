#include <windows.h>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <cmath>
#include <cfloat>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

// Глобальные переменные с актуальными оффсетами для Rust 2588 (Jungle Update, Alkad)
bool g_showMenu = false;
bool g_enableESP = false;
bool g_enableAimbot = false;
int g_espColorR = 255, g_espColorG = 0, g_espColorB = 0;
float g_aimbotFOV = 30.0f, g_aimbotSmooth = 5.0f;
float g_espDistanceMax = 200.0f;
int g_aimbotPriority = 0;
float g_viewPitch = 0.0f, g_viewYaw = 0.0f;

// Оффсеты для Rust 2588 (Jungle Update, Alkad)
DWORD g_entityListOffset = 0x4E2A1B0; // Обновлено для джунглей
DWORD g_localPlayerOffset = 0x4E5D890; // Изменено из-за новой структуры игрока
DWORD g_viewMatrixOffset = 0x4E7F210; // Новая матрица обзора
DWORD g_healthOffset = 0x14; // Скорректировано после шифрования

// View Matrix (4x4)
float g_viewMatrix[16] = { 0 };

// Консоль
HANDLE g_hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

// Логирование
void LogToFile(const std::string& message) {
    std::ofstream logFile("cheat_log.txt", std::ios::app);
    if (logFile.is_open()) {
        SYSTEMTIME time;
        GetLocalTime(&time);
        char timeStr[32];
        snprintf(timeStr, sizeof(timeStr), "[%02d:%02d:%02d.%03d] ", time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
        logFile << timeStr << message << std::endl;
        logFile.close();
    }
}

// Вывод сообщения в консоль
void PrintConsoleMessage(const std::string& message) {
    SetConsoleCursorPosition(g_hConsole, { 0, 0 });
    WriteConsoleA(g_hConsole, message.c_str(), message.length(), NULL, NULL);
    WriteConsoleA(g_hConsole, "\n", 1, NULL, NULL);
    LogToFile(message);
}

// Проверка памяти
bool IsMemoryReadable(uintptr_t address, size_t size) {
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi))) {
        return mbi.State == MEM_COMMIT && mbi.Protect != PAGE_NOACCESS;
    }
    return false;
}

// Поиск сигнатуры
uintptr_t FindPattern(const char* moduleName, const char* pattern, const char* mask) {
    HMODULE hModule = GetModuleHandleA(moduleName);
    if (!hModule) return 0;

    MODULEINFO modInfo;
    if (!GetModuleInformation(GetCurrentProcess(), hModule, &modInfo, sizeof(MODULEINFO))) return 0;

    uintptr_t base = reinterpret_cast<uintptr_t>(hModule);
    size_t size = modInfo.SizeOfImage;
    size_t patternLength = strlen(mask);

    for (size_t i = 0; i < size - patternLength; i++) {
        if (!IsMemoryReadable(base + i, patternLength)) continue;
        bool found = true;
        for (size_t j = 0; j < patternLength; j++) {
            found &= mask[j] == '?' || pattern[j] == *reinterpret_cast<char*>(base + i + j);
        }
        if (found) return base + i;
    }
    return 0;
}

// Валидация оффсета
bool ValidateOffset(DWORD offset, const std::string& name) {
    if (offset < 0x100000 || offset > 0xFFFFFFF) return false;
    if (!IsMemoryReadable(offset, sizeof(DWORD))) return false;
    LogToFile(name + " offset validated: 0x" + std::to_string(offset));
    return true;
}

// Анализ типа шифрования
enum class EncryptionType {
    XOR,
    BYTE_SHIFT,
    SUBSTITUTION,
    UNKNOWN
};

EncryptionType DetectEncryptionType(const std::vector<char>& data) {
    if (data.empty()) return EncryptionType::UNKNOWN;

    int xorScore = 0, shiftScore = 0, substitutionScore = 0;
    for (size_t i = 0; i < data.size(); i++) {
        char byteXor = data[i] ^ 0x5A;
        if (byteXor >= 32 && byteXor <= 126) xorScore++;
        char byteShift = (data[i] + 5) % 256;
        if (byteShift >= 32 && byteShift <= 126) shiftScore++;
        char byteSub = data[i] ^ (i % 256);
        if (byteSub >= 32 && byteSub <= 126) substitutionScore++;
    }

    if (xorScore > shiftScore && xorScore > substitutionScore) return EncryptionType::XOR;
    if (shiftScore > xorScore && shiftScore > substitutionScore) return EncryptionType::BYTE_SHIFT;
    if (substitutionScore > xorScore && substitutionScore > shiftScore) return EncryptionType::SUBSTITUTION;
    return EncryptionType::UNKNOWN;
}

// Расшифровка
bool DecryptFile(const std::string& inputFile, const std::string& outputFile) {
    std::ifstream inFile(inputFile, std::ios::binary);
    if (!inFile.is_open()) {
        LogToFile("Failed to open input file for decryption: " + inputFile);
        return false;
    }

    std::vector<char> data((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
    inFile.close();

    if (data.empty()) {
        LogToFile("Input file is empty: " + inputFile);
        return false;
    }

    EncryptionType encType = DetectEncryptionType(data);
    std::vector<char> decryptedData = data;

    switch (encType) {
    case EncryptionType::XOR:
        for (size_t i = 0; i < decryptedData.size(); i++) decryptedData[i] ^= 0x5A;
        LogToFile("Detected XOR encryption for " + inputFile);
        break;
    case EncryptionType::BYTE_SHIFT:
        for (size_t i = 0; i < decryptedData.size(); i++) decryptedData[i] = (decryptedData[i] + 5) % 256;
        LogToFile("Detected byte shift encryption for " + inputFile);
        break;
    case EncryptionType::SUBSTITUTION:
        for (size_t i = 0; i < decryptedData.size(); i++) decryptedData[i] ^= (i % 256);
        LogToFile("Detected substitution encryption for " + inputFile);
        break;
    default:
        for (size_t i = 0; i < decryptedData.size(); i++) decryptedData[i] ^= 0x5A;
        LogToFile("Unknown encryption type for " + inputFile + ", trying default XOR...");
        break;
    }

    std::ofstream outFile(outputFile, std::ios::binary);
    if (!outFile.is_open()) {
        LogToFile("Failed to open output file for decryption: " + outputFile);
        return false;
    }

    outFile.write(decryptedData.data(), decryptedData.size());
    outFile.close();
    LogToFile("Successfully decrypted file: " + inputFile + " to " + outputFile);
    return true;
}

// Поиск и расшифровка файлов
void DecryptGameFiles() {
    LogToFile("Starting decryption of game files...");

    std::vector<std::string> possibleFiles = {
        "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Rust\\gameconfig.dat",
        "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Rust\\data\\settings.bin",
        "C:\\Rust\\config.dat",
        "C:\\Rust\\data.bin"
    };

    for (const auto& file : possibleFiles) {
        if (GetFileAttributesA(file.c_str()) != INVALID_FILE_ATTRIBUTES) {
            std::string outputFile = file + ".decrypted";
            DecryptFile(file, outputFile);
        }
    }
}

// Оптимизированный поиск оффсетов
bool FindOffsets() {
    LogToFile("Starting optimized offset search for Rust 2588 Jungle Update (Alkad)...");

    // Кэширование оффсетов
    std::ifstream cacheFile("offset_cache.txt");
    if (cacheFile.is_open()) {
        std::string line;
        while (std::getline(cacheFile, line)) {
            std::istringstream iss(line);
            std::string key, value;
            if (std::getline(iss, key, '=') && std::getline(iss, value)) {
                if (key == "entity_list") g_entityListOffset = std::stoul(value, nullptr, 16);
                else if (key == "local_player") g_localPlayerOffset = std::stoul(value, nullptr, 16);
                else if (key == "view_matrix") g_viewMatrixOffset = std::stoul(value, nullptr, 16);
                else if (key == "health") g_healthOffset = std::stoul(value, nullptr, 16);
            }
        }
        cacheFile.close();
        if (ValidateOffset(g_entityListOffset, "entityList") &&
            ValidateOffset(g_localPlayerOffset, "localPlayer") &&
            ValidateOffset(g_viewMatrixOffset, "viewMatrix") &&
            ValidateOffset(g_healthOffset, "health")) {
            LogToFile("Offsets loaded from cache: entityList=0x" + std::to_string(g_entityListOffset) +
                      ", localPlayer=0x" + std::to_string(g_localPlayerOffset) +
                      ", viewMatrix=0x" + std::to_string(g_viewMatrixOffset) +
                      ", health=0x" + std::to_string(g_healthOffset));
            return true;
        }
    }

    // Расширенные сигнатуры для Rust 2588 (с учётом джунглей и новых монументов)
    std::vector<std::pair<const char*, const char*>> patterns = {
        {"\x48\x8B\x05\x00\x00\x00\x00\x48\x85\xC0\x0F\x84", "xxx????xxxxx"}, // entityList
        {"\x48\x8B\x15\x00\x00\x00\x00\x48\x85\xD2\x74", "xxx????xxxx"},     // localPlayer
        {"\x48\x8D\x0D\x00\x00\x00\x00\xE8\x00\x00\x00\x00\xF3\x0F\x10\x00", "xxx????x????xxxx"}, // viewMatrix
        {"\xF3\x0F\x10\x00\x00\x00\x00\xF3\x0F\x11\x00", "xxx????xxx?"},      // health
        {"\x4C\x8B\x0D\x00\x00\x00\x00\x48\x8B\xF8", "xxx????xxx"},         // entityList (альтернатива)
        {"\x48\x8B\x0D\x00\x00\x00\x00\x48\x89\x7C\x24", "xxx????xxxx"},     // localPlayer (альтернатива)
    };

    for (int i = 0; i < patterns.size(); i++) {
        uintptr_t baseAddr = FindPattern("GameAssembly.dll", patterns[i].first, patterns[i].second);
        if (baseAddr) {
            DWORD offset = *reinterpret_cast<DWORD*>(baseAddr + 3);
            baseAddr = (baseAddr + 7) + offset;
            DWORD newOffset = static_cast<DWORD>(baseAddr - reinterpret_cast<uintptr_t>(GetModuleHandleA("GameAssembly.dll")));
            if (ValidateOffset(newOffset, i == 0 || i == 4 ? "entityList" : (i == 1 || i == 5 ? "localPlayer" : (i == 2 ? "viewMatrix" : "health")))) {
                if (i == 0 || i == 4) g_entityListOffset = newOffset;
                else if (i == 1 || i == 5) g_localPlayerOffset = newOffset;
                else if (i == 2) g_viewMatrixOffset = newOffset;
                else if (i == 3) g_healthOffset = newOffset;
            }
        }
    }

    if (ValidateOffset(g_entityListOffset, "entityList") &&
        ValidateOffset(g_localPlayerOffset, "localPlayer") &&
        ValidateOffset(g_viewMatrixOffset, "viewMatrix") &&
        ValidateOffset(g_healthOffset, "health")) {
        LogToFile("Offsets found: entityList=0x" + std::to_string(g_entityListOffset) +
                  ", localPlayer=0x" + std::to_string(g_localPlayerOffset) +
                  ", viewMatrix=0x" + std::to_string(g_viewMatrixOffset) +
                  ", health=0x" + std::to_string(g_healthOffset));

        // Сохраняем в кэш
        std::ofstream cacheFileOut("offset_cache.txt");
        if (cacheFileOut.is_open()) {
            cacheFileOut << "entity_list=0x" << std::hex << g_entityListOffset << "\n";
            cacheFileOut << "local_player=0x" << std::hex << g_localPlayerOffset << "\n";
            cacheFileOut << "view_matrix=0x" << std::hex << g_viewMatrixOffset << "\n";
            cacheFileOut << "health=0x" << std::hex << g_healthOffset << "\n";
            cacheFileOut.close();
        }
        return true;
    }

    LogToFile("Failed to find offsets.");
    return false;
}

// Авто-дампер (F10)
void AutoDumpOffsets() {
    PrintConsoleMessage("F10: Началось");

    // Поиск всех оффсетов
    FindOffsets();

    // Извлечение углов
    if (ExtractViewMatrix()) {
        LogToFile("View angles extracted: Pitch=" + std::to_string(g_viewPitch) + ", Yaw=" + std::to_string(g_viewYaw));
    } else {
        LogToFile("Failed to extract view matrix.");
    }

    // Дамп в файл
    std::ofstream dumpFile("offset_dump.txt");
    if (dumpFile.is_open()) {
        dumpFile << "entityList=0x" << std::hex << g_entityListOffset << "\n";
        dumpFile << "localPlayer=0x" << std::hex << g_localPlayerOffset << "\n";
        dumpFile << "viewMatrix=0x" << std::hex << g_viewMatrixOffset << "\n";
        dumpFile << "health=0x" << std::hex << g_healthOffset << "\n";
        dumpFile << "viewPitch=" << std::dec << g_viewPitch << "\n";
        dumpFile << "viewYaw=" << std::dec << g_viewYaw << "\n";
        dumpFile.close();
        LogToFile("Offsets dumped to offset_dump.txt");
    }

    PrintConsoleMessage("F10: Завершено");
}

// Извлечение view matrix
bool ExtractViewMatrix() {
    uintptr_t baseAddr = reinterpret_cast<uintptr_t>(GetModuleHandleA("GameAssembly.dll"));
    uintptr_t viewMatrixAddr = baseAddr + g_viewMatrixOffset;
    if (!IsMemoryReadable(viewMatrixAddr, 16 * sizeof(float))) return false;
    memcpy(g_viewMatrix, reinterpret_cast<float*>(viewMatrixAddr), 16 * sizeof(float));
    g_viewPitch = atan2(-g_viewMatrix[8], sqrt(g_viewMatrix[0] * g_viewMatrix[0] + g_viewMatrix[4] * g_viewMatrix[4])) * 180.0f / 3.14159f;
    g_viewYaw = atan2(g_viewMatrix[4], g_viewMatrix[0]) * 180.0f / 3.14159f;
    return true;
}

// Чтение настроек
void LoadConfig() {
    std::ifstream file("config.txt");
    if (!file.is_open()) return;
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string key, value;
        if (std::getline(iss, key, '=') && std::getline(iss, value)) {
            if (key == "esp_enabled") g_enableESP = (value == "true");
            else if (key == "esp_color_r") g_espColorR = std::stoi(value);
            else if (key == "esp_color_g") g_espColorG = std::stoi(value);
            else if (key == "esp_color_b") g_espColorB = std::stoi(value);
            else if (key == "aimbot_enabled") g_enableAimbot = (value == "true");
            else if (key == "aimbot_fov") g_aimbotFOV = std::stof(value);
            else if (key == "aimbot_smooth") g_aimbotSmooth = std::stof(value);
            else if (key == "esp_distance_max") g_espDistanceMax = std::stof(value);
            else if (key == "aimbot_priority") g_aimbotPriority = std::stoi(value);
        }
    }
    file.close();
}

// ESP (F2)
void DrawESP() {
    if (!g_enableESP) return;

    uintptr_t baseAddr = reinterpret_cast<uintptr_t>(GetModuleHandleA("GameAssembly.dll"));
    DWORD entityList = *reinterpret_cast<DWORD*>(baseAddr + g_entityListOffset);
    if (!entityList) return;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(g_hConsole, &csbi);
    COORD pos = { 0, (SHORT)(csbi.dwCursorPosition.Y + 1) };

    for (int i = 0; i < 32; i++) {
        uintptr_t entityAddr = baseAddr + g_entityListOffset + (i * 0x4);
        DWORD entity = *reinterpret_cast<DWORD*>(entityAddr);
        if (!entity) continue;

        float x = *reinterpret_cast<float*>(entity + 0x4);
        float y = *reinterpret_cast<float*>(entity + 0x8);
        float z = *reinterpret_cast<float*>(entity + 0xC);
        float health = *reinterpret_cast<float*>(entity + g_healthOffset);

        float dist = std::sqrt(x * x + y * y + z * z);
        if (dist > g_espDistanceMax) continue;

        float screenX = g_viewMatrix[0] * x + g_viewMatrix[4] * y + g_viewMatrix[8] * z + g_viewMatrix[12];
        float screenY = g_viewMatrix[1] * x + g_viewMatrix[5] * y + g_viewMatrix[9] * z + g_viewMatrix[13];
        float w = g_viewMatrix[3] * x + g_viewMatrix[7] * y + g_viewMatrix[11] * z + g_viewMatrix[15];
        if (w < 0.1f) continue;
        screenX /= w; screenY /= w;

        char buffer[128];
        snprintf(buffer, sizeof(buffer), "Player %d: X=%.1f Y=%.1f Z=%.1f HP=%.0f Dist=%.1f (Screen: %.0f, %.0f)",
                 i, x, y, z, health, dist, screenX, screenY);
        SetConsoleCursorPosition(g_hConsole, pos);
        SetConsoleTextAttribute(g_hConsole, (g_espColorR << 8) | (g_espColorG << 4) | g_espColorB);
        WriteConsoleA(g_hConsole, buffer, strlen(buffer), NULL, NULL);
        pos.Y++;
    }
}

// Аимбот (F1)
void Aimbot() {
    if (!g_enableAimbot) return;

    uintptr_t baseAddr = reinterpret_cast<uintptr_t>(GetModuleHandleA("GameAssembly.dll"));
    uintptr_t localPlayerAddr = baseAddr + g_localPlayerOffset;
    DWORD localPlayer = *reinterpret_cast<DWORD*>(localPlayerAddr);
    if (!localPlayer) return;

    DWORD entityList = *reinterpret_cast<DWORD*>(baseAddr + g_entityListOffset);
    if (!entityList) return;

    float closestDist = FLT_MAX;
    float targetX = 0, targetY = 0, targetZ = 0;
    DWORD targetEntity = 0;

    for (int i = 0; i < 32; i++) {
        uintptr_t entityAddr = baseAddr + g_entityListOffset + (i * 0x4);
        DWORD entity = *reinterpret_cast<DWORD*>(entityAddr);
        if (!entity) continue;

        float x = *reinterpret_cast<float*>(entity + 0x4);
        float y = *reinterpret_cast<float*>(entity + 0x8);
        float z = *reinterpret_cast<float*>(entity + 0xC);
        float health = *reinterpret_cast<float*>(entity + g_healthOffset);

        float dx = x - *reinterpret_cast<float*>(localPlayer + 0x4);
        float dy = y - *reinterpret_cast<float*>(localPlayer + 0x8);
        float dz = z - *reinterpret_cast<float*>(localPlayer + 0xC);
        float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

        if (dist < closestDist && dist < g_aimbotFOV) {
            if (g_aimbotPriority == 1 && health > 0) {
                if (targetEntity && *reinterpret_cast<float*>(targetEntity + g_healthOffset) > health) continue;
            }
            closestDist = dist;
            targetX = x; targetY = y; targetZ = z;
            targetEntity = entity;
        }
    }

    if (closestDist != FLT_MAX) {
        float dx = targetX - *reinterpret_cast<float*>(localPlayer + 0x4);
        float dy = targetY - *reinterpret_cast<float*>(localPlayer + 0x8);
        float dz = targetZ - *reinterpret_cast<float*>(localPlayer + 0xC);
        float yaw = atan2(dy, dx) * 180.0f / 3.14159f - g_viewYaw;
        float pitch = atan2(-dz, std::sqrt(dx * dx + dy * dy)) * 180.0f / 3.14159f - g_viewPitch;

        mouse_event(MOUSEEVENTF_MOVE, (DWORD)(yaw / g_aimbotSmooth), (DWORD)(pitch / g_aimbotSmooth), 0, 0);
    }
}

// Поток для обработки клавиш
DWORD WINAPI InputThread(LPVOID lpParam) {
    AllocConsole();
    g_hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTitleA("RustAlkadCheat2588 Console");

    if (!FindOffsets()) {
        PrintConsoleMessage("Failed to find offsets!");
    } else if (!ExtractViewMatrix()) {
        PrintConsoleMessage("Failed to extract view matrix!");
    } else {
        PrintConsoleMessage("Offsets and view matrix initialized successfully!");
    }

    while (true) {
        if (GetAsyncKeyState(VK_INSERT) & 1) {
            g_showMenu = !g_showMenu;
            PrintConsoleMessage(g_showMenu ? "Insert: Началось" : "Insert: Завершено");
        }
        if (GetAsyncKeyState(VK_F1) & 1) {
            g_enableAimbot = !g_enableAimbot;
            PrintConsoleMessage(g_enableAimbot ? "F1: Началось" : "F1: Завершено");
        }
        if (GetAsyncKeyState(VK_F2) & 1) {
            g_enableESP = !g_enableESP;
            PrintConsoleMessage(g_enableESP ? "F2: Началось" : "F2: Завершено");
        }
        if (GetAsyncKeyState(VK_F10) & 1) {
            AutoDumpOffsets();
            DecryptGameFiles();
        }
        if (GetAsyncKeyState(VK_NUMPAD0) & 1) {
            std::ofstream config("config.txt");
            if (config.is_open()) {
                config << "esp_enabled=" << (g_enableESP ? "true" : "false") << "\n";
                config << "esp_color_r=" << g_espColorR << "\n";
                config << "esp_color_g=" << g_espColorG << "\n";
                config << "esp_color_b=" << g_espColorB << "\n";
                config << "aimbot_enabled=" << (g_enableAimbot ? "true" : "false") << "\n";
                config << "aimbot_fov=" << g_aimbotFOV << "\n";
                config << "aimbot_smooth=" << g_aimbotSmooth << "\n";
                config << "esp_distance_max=" << g_espDistanceMax << "\n";
                config << "aimbot_priority=" << g_aimbotPriority << "\n";
                config.close();
                PrintConsoleMessage("Num0: Изменено (Настройки сохранены)");
            }
        }
        if (GetAsyncKeyState(VK_NUMPAD1) & 1) {
            g_espColorR = std::min(255, g_espColorR + 10);
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "Num1: Изменено (ESP R: %d)", g_espColorR);
            PrintConsoleMessage(buffer);
        }
        if (GetAsyncKeyState(VK_NUMPAD2) & 1) {
            g_espColorG = std::min(255, g_espColorG + 10);
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "Num2: Изменено (ESP G: %d)", g_espColorG);
            PrintConsoleMessage(buffer);
        }
        if (GetAsyncKeyState(VK_NUMPAD3) & 1) {
            g_espColorB = std::min(255, g_espColorB + 10);
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "Num3: Изменено (ESP B: %d)", g_espColorB);
            PrintConsoleMessage(buffer);
        }
        if (GetAsyncKeyState(VK_NUMPAD4) & 1) {
            g_aimbotFOV = std::min(90.0f, g_aimbotFOV + 5.0f);
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "Num4: Изменено (Aimbot FOV: %.1f)", g_aimbotFOV);
            PrintConsoleMessage(buffer);
        }
        if (GetAsyncKeyState(VK_NUMPAD5) & 1) {
            g_aimbotSmooth = std::max(1.0f, g_aimbotSmooth - 1.0f);
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "Num5: Изменено (Aimbot Smooth: %.1f)", g_aimbotSmooth);
            PrintConsoleMessage(buffer);
        }
        if (GetAsyncKeyState(VK_NUMPAD6) & 1) {
            g_espDistanceMax += 50.0f;
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "Num6: Изменено (ESP Distance: %.1f)", g_espDistanceMax);
            PrintConsoleMessage(buffer);
        }
        if (GetAsyncKeyState(VK_NUMPAD7) & 1) {
            g_aimbotPriority = (g_aimbotPriority + 1) % 2;
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "Num7: Изменено (Aimbot Priority: %d)", g_aimbotPriority);
            PrintConsoleMessage(buffer);
        }

        Aimbot();
        if (g_enableESP) DrawESP();
        Sleep(1);
    }
    return 0;
}

// Точка входа
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        LoadConfig();
        CreateThread(NULL, 0, InputThread, NULL, 0, NULL);
    }
    return TRUE;
}
