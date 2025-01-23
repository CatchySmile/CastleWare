#include <Windows.h>
#include <TlHelp32.h>
#include <iostream>
#include <tchar.h>
#include <vector>
#include <fstream>
#include <thread>
#include "offsets.h" // Include the new header file

// get the base address of a module
DWORD GetModuleBaseAddress(TCHAR* lpszModuleName, DWORD pID) {
    DWORD dwModuleBaseAddress = 0;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pID);
    MODULEENTRY32 ModuleEntry32 = { 0 };
    ModuleEntry32.dwSize = sizeof(MODULEENTRY32);

    if (Module32First(hSnapshot, &ModuleEntry32)) {
        do {
            if (_tcscmp(ModuleEntry32.szModule, lpszModuleName) == 0) {
                dwModuleBaseAddress = (DWORD)ModuleEntry32.modBaseAddr;
                break;
            }
        } while (Module32Next(hSnapshot, &ModuleEntry32));
    }
    CloseHandle(hSnapshot);
    return dwModuleBaseAddress;
}

// get the address with offsets
DWORD getAddressWithOffsets(HANDLE processHandle, DWORD baseAddress, const std::vector<DWORD>& offsets) {
    DWORD address = baseAddress;
    for (size_t i = 0; i < offsets.size() - 1; ++i) {
        ReadProcessMemory(processHandle, (LPVOID)(address + offsets[i]), &address, sizeof(address), NULL);
    }
    return address + offsets.back();
}

void injectMemory(HANDLE processHandle, DWORD baseAddress, bool& injectEnabled) {
    DWORD injectionAddress = baseAddress + hideNameOffset;

    while (injectEnabled) {
        WriteProcessMemory(processHandle, (LPVOID)(injectionAddress), injectedHideNameBytes, sizeof(injectedHideNameBytes), 0);
        Sleep(100);
    }

    // Restore original bytes when disabled
    WriteProcessMemory(processHandle, (LPVOID)(injectionAddress), originalHideNameBytes, sizeof(originalHideNameBytes), 0);
}

// Unused footer
void displayFooter() {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
    GetConsoleScreenBufferInfo(hConsole, &consoleInfo);

    COORD footerPosition;
    footerPosition.X = 0;
    footerPosition.Y = consoleInfo.srWindow.Bottom - 1;

    SetConsoleCursorPosition(hConsole, footerPosition);
    std::cout << "https://github.com/CatchySmile\n";
}

// display the logo
void displayLogo() {
    std::cout << R"(
+---------------------------------------------------+
| ____ ____ ____ ___ _    ____ _ _ _ ____ ____ ____ |
| |    |__| [__   |  |    |___ | | | |__| |__/ |___ |
| |___ |  | ___]  |  |___ |___ |_|_| |  | |  \ |___ |
|                                                   |
+---------------------------------------------------+


Created by https://github.com/CatchySmile
)";
    Sleep(2000);
    system("cls");
}

// display the menu
void displayMenu(bool freezeEnabled, bool fovEnabled, bool fastMenuEnabled, bool hideNameEnabled, float currentFOV) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
    WORD saved_attributes;

    // Save current attributes
    GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
    saved_attributes = consoleInfo.wAttributes;
    std::cout << R"(
+----------------+----+
|   CastleWare   |v1.0|
+----------------+----+
FOV & Infinite Jump must be re-enabled after switching realms.
)";
    std::cout << "\n+----------------+----+" << std::endl;

    if (fovEnabled) {
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN);
        std::cout << "| FOV Changer    | F1 | [Enabled] FOV: " << currentFOV << std::endl;
    }
    else {
        SetConsoleTextAttribute(hConsole, FOREGROUND_INTENSITY);
        std::cout << "| FOV Changer    | F1 | [Disabled]" << std::endl;
    }

    if (freezeEnabled) {
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN);
        std::cout << "| Infinite Jump  | F2 | [Enabled]" << std::endl;
    }
    else {
        SetConsoleTextAttribute(hConsole, FOREGROUND_INTENSITY);
        std::cout << "| Infinite Jump  | F2 | [Disabled]" << std::endl;
    }

    if (fastMenuEnabled) {
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN);
        std::cout << "| Fast Menus     | F3 | [Enabled]" << std::endl;
    }
    else {
        SetConsoleTextAttribute(hConsole, FOREGROUND_INTENSITY);
        std::cout << "| Fast Menus     | F3 | [Disabled]" << std::endl;
    }

    if (hideNameEnabled) {
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN);
        std::cout << "| Hide Name      | F4 | [Enabled]" << std::endl;
    }
    else {
        SetConsoleTextAttribute(hConsole, FOREGROUND_INTENSITY);
        std::cout << "| Hide Name      | F4 | [Disabled]" << std::endl;
    }

    // Restore original attributes
    SetConsoleTextAttribute(hConsole, saved_attributes);
    std::cout << "+----------------+----+" << std::endl;
    std::cout << "| Show Info      | F7 |" << std::endl;
    std::cout << "| Quit           | F8 |" << std::endl;
    std::cout << "+----------------+----+" << std::endl;
}

// jump value freeze
void freezeValue(HANDLE processHandle, DWORD address, int value, bool& freeze) {
    if (freeze) {
        freeze = false;
        Sleep(100);
        freeze = true;
    }

    while (freeze) {
        WriteProcessMemory(processHandle, (LPVOID)(address), &value, sizeof(value), 0);
        Sleep(100);
    }
}

// maintain the FOV value in memory
void maintainFOV(HANDLE processHandle, DWORD address, float value, bool& fovEnabled) {
    while (fovEnabled) {
        WriteProcessMemory(processHandle, (LPVOID)(address), &value, sizeof(value), 0);
        Sleep(100);
    }
}

// maintain the fast menu value in memory
void maintainFastMenu(HANDLE processHandle, DWORD address, bool& fastMenuEnabled) {
    int zeroValue = 0;
    while (fastMenuEnabled) {
        WriteProcessMemory(processHandle, (LPVOID)(address), &zeroValue, sizeof(zeroValue), 0);
        Sleep(100);
    }
}

// display addresses and offsets
void displayAddresses(DWORD gameBaseAddress, DWORD pointsAddress, DWORD customAddress) {
    std::cout << "Last updated January 22nd, 2025" << std::endl;

    std::cout << R"(
+---   SCROLL FOR MORE INFO  ---+
+-------------------------------+
|              FOV              |
+-------------------------------+
)";
    std::cout << "Description: Simple fov changer, what did you expect?" << std::endl;
    std::cout << "Base Offset: 0x002F7A30" << std::endl;
    std::cout << "Offsets: { 0x30, 0x38, 0x298, 0x264, 0x10C, 0x3C, 0x4F4 }" << std::endl;

    std::cout << R"(
+-------------------------------+
|        Jump Potential         |
+-------------------------------+
)";
    std::cout << "Description: Infinite jump works by freezing our jump potential allowing infinite jumps." << std::endl;
    std::cout << "Base Offset: 0x002FFE34" << std::endl;
    std::cout << "Offsets: { 0x19C, 0x1D8, 0x1AC, 0x1A4, 0x198, 0x34C, 0x478 }" << std::endl;
    std::cout << R"(
+-------------------------------+
|           Fast Menu           |
+-------------------------------+
)";
    std::cout << "Description: Allows you to immediantly open any menu after switching realms." << std::endl;
    std::cout << "Address: Cubic.exe" << std::endl;
    std::cout << "Offsets: { 0x00C7EF88 }" << std::endl;
    std::cout << R"(
+-------------------------------+
|           Name Hide           |
+-------------------------------+
)";
    std::cout << "Description: Automatically hide your name when entering a new realm." << std::endl;
    std::cout << "Address: Cubic.exe" << std::endl;
    std::cout << "Offsets: { 0x1C00448 }" << std::endl;

}

// reload addresses and offsets
void reloadAddresses(HANDLE processHandle, DWORD gameBaseAddress, DWORD& pointsAddress, DWORD& customAddress, bool& freeze, std::thread& freezeThread) {
    DWORD baseAddress = NULL;
    ReadProcessMemory(processHandle, (LPVOID)(gameBaseAddress + offsetGameToBaseAddressFOV), &baseAddress, sizeof(baseAddress), NULL);
    pointsAddress = getAddressWithOffsets(processHandle, baseAddress, pointsOffsets);

    DWORD baseAddress2 = NULL;
    ReadProcessMemory(processHandle, (LPVOID)(gameBaseAddress + offsetGameToBaseAddressJump), &baseAddress2, sizeof(baseAddress2), NULL);
    customAddress = getAddressWithOffsets(processHandle, baseAddress2, customOffsets);

    if (freeze) {
        if (freezeThread.joinable()) {
            freezeThread.join();
        }
        freezeThread = std::thread(freezeValue, processHandle, customAddress, 6400, std::ref(freeze));
        freezeThread.detach();
    }
}

int main() {
    displayLogo();
    std::ofstream logFile("debug.log");

    HWND hGameWindow = FindWindow(NULL, "Cubic");
    if (hGameWindow == NULL) {
        logFile << "Start the game! No valid window found" << std::endl;
        std::cout << "Window not found..." << std::endl;
        return 0;
    }

    DWORD pID = NULL;
    GetWindowThreadProcessId(hGameWindow, &pID);
    HANDLE processHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pID);
    if (processHandle == INVALID_HANDLE_VALUE || processHandle == NULL) {
        logFile << "Failed to open process" << std::endl;
        return 0;
    }

    char gameName[] = "Cubic.exe";
    DWORD gameBaseAddress = GetModuleBaseAddress(_T(gameName), pID);
    if (gameBaseAddress == 0) {
        logFile << "Failed to get game base address" << std::endl;
        return 0;
    }

    DWORD pointsAddress = NULL;
    DWORD customAddress = NULL;
    bool freeze = false;
    bool fovEnabled = false;
    bool fastMenuEnabled = false;
    bool injectEnabled = false;
    bool hideNameEnabled = false;
    float desiredFOV = 0.0f;
    int freezeValueToSet = 6400;
    std::thread freezeThread;
    std::thread fovThread;
    std::thread fastMenuThread;
    std::thread injectThread;

    reloadAddresses(processHandle, gameBaseAddress, pointsAddress, customAddress, freeze, freezeThread);
    displayMenu(freeze, fovEnabled, fastMenuEnabled, hideNameEnabled, desiredFOV);

    while (true) {
        Sleep(50);
        if (GetAsyncKeyState(VK_F8)) {
            system("cls");
            logFile << "Exiting..." << std::endl;
            std::cout << "Exiting..." << std::endl;
            fovThread.detach();
            Sleep(40);
            freezeThread.detach();
            Sleep(40);
            fastMenuThread.detach();
            Sleep(40);
            injectThread.detach();
            Sleep(40);
            break;
        }
        if (GetAsyncKeyState(VK_F1)) {
            fovEnabled = !fovEnabled;
            if (fovEnabled) {
                reloadAddresses(processHandle, gameBaseAddress, pointsAddress, customAddress, freeze, freezeThread);
                std::cout << "Enter desired FOV: ";
                std::cin >> desiredFOV;
                WriteProcessMemory(processHandle, (LPVOID)(pointsAddress), &desiredFOV, sizeof(desiredFOV), 0);
                fovThread = std::thread(maintainFOV, processHandle, pointsAddress, desiredFOV, std::ref(fovEnabled));
                fovThread.detach();
                std::cout << "FOV Changer enabled with FOV: " << desiredFOV << std::endl;
            }
            else {
                std::cout << "FOV Changer disabled" << std::endl;
            }
            Sleep(700);
            system("cls");
            displayMenu(freeze, fovEnabled, fastMenuEnabled, hideNameEnabled, desiredFOV);
        }
        if (GetAsyncKeyState(VK_F2)) {
            freeze = !freeze;
            if (freeze) {
                reloadAddresses(processHandle, gameBaseAddress, pointsAddress, customAddress, freeze, freezeThread);
                std::cout << "Enabled infinite jump" << std::endl;
            }
            else {
                std::cout << "Disabled infinite jump" << std::endl;
            }
            Sleep(700);
            system("cls");
            displayMenu(freeze, fovEnabled, fastMenuEnabled, hideNameEnabled, desiredFOV);
        }
        if (GetAsyncKeyState(VK_F3)) {
            fastMenuEnabled = !fastMenuEnabled;
            if (fastMenuEnabled) {
                DWORD fastMenuAddress = gameBaseAddress + fastMenuOffset;
                fastMenuThread = std::thread(maintainFastMenu, processHandle, fastMenuAddress, std::ref(fastMenuEnabled));
                fastMenuThread.detach();
                std::cout << "Fast Menu enabled" << std::endl;
            }
            else {
                std::cout << "Fast Menu disabled" << std::endl;
            }
            Sleep(700);
            system("cls");
            displayMenu(freeze, fovEnabled, fastMenuEnabled, hideNameEnabled, desiredFOV);
        }
        if (GetAsyncKeyState(VK_F4)) {
            hideNameEnabled = !hideNameEnabled;
            injectEnabled = hideNameEnabled;
            if (injectEnabled) {
                injectThread = std::thread(injectMemory, processHandle, gameBaseAddress, std::ref(injectEnabled));
                injectThread.detach();
                std::cout << "Hide Name Enabled" << std::endl;
            }
            else {
                std::cout << "Hide Name Disabled" << std::endl;
            }
            Sleep(700);
            system("cls");
            displayMenu(freeze, fovEnabled, fastMenuEnabled, hideNameEnabled, desiredFOV);
        }

        if (GetAsyncKeyState(VK_F7)) {
            system("cls");
            displayAddresses(gameBaseAddress, pointsAddress, customAddress);
            Sleep(4500);
            system("cls");
            displayMenu(freeze, fovEnabled, fastMenuEnabled, hideNameEnabled, desiredFOV);
        }
    }

    freeze = false;
    fovEnabled = false;
    fastMenuEnabled = false;
    injectEnabled = false;
    hideNameEnabled = false;
    if (freezeThread.joinable()) {
        freezeThread.join();
    }
    if (fovThread.joinable()) {
        fovThread.join();
    }
    if (fastMenuThread.joinable()) {
        fastMenuThread.join();
    }
    if (injectThread.joinable()) {
        injectThread.join();
    }

    logFile.close();
    return 0;
}
