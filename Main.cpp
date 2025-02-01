#include <Windows.h>
#include <TlHelp32.h>
#include <iostream>
#include <tchar.h>
#include <vector>
#include <fstream>
#include <thread>
#include "offsets.h"

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
        ReadProcessMemory(processHandle, (LPCVOID)(address + offsets[i]), &address, sizeof(address), NULL);
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
void displayMenu(bool freezeEnabled, bool fovEnabled, bool fastMenuEnabled, bool hideNameEnabled, bool noclipEnabled, bool noCollideEnabled, float currentFOV) {
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
FOV changer must be re-enabled after switching realms.
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
        std::cout << "| Hide Names     | F4 | [Enabled]" << std::endl;
    }
    else {
        SetConsoleTextAttribute(hConsole, FOREGROUND_INTENSITY);
        std::cout << "| Hide Names     | F4 | [Disabled]" << std::endl;
    }

    if (noclipEnabled) {
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN);
        std::cout << "| Noclip         | F5 | [Enabled]" << std::endl;
    }
    else {
        SetConsoleTextAttribute(hConsole, FOREGROUND_INTENSITY);
        std::cout << "| Noclip         | F5 | [Disabled]" << std::endl;
    }

    if (noCollideEnabled) {
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN);
        std::cout << "| NoCollide      | F6 | [Enabled]" << std::endl;
    }
    else {
        SetConsoleTextAttribute(hConsole, FOREGROUND_INTENSITY);
        std::cout << "| NoCollide      | F6 | [Disabled]" << std::endl;
    }

    // Restore original attributes
    SetConsoleTextAttribute(hConsole, saved_attributes);
    std::cout << "+----------------+----+" << std::endl;
    std::cout << "| Show Info      | F7 |" << std::endl;
    std::cout << "| Quit           | F8 |" << std::endl;
    std::cout << "+----------------+----+" << std::endl;
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

// maintain the noclip value in memory
void maintainNoclip(HANDLE processHandle, DWORD address, bool& noclipEnabled) {
    int noclipValue = 1010000000;
    int normalValue = 1065353216; // Commented out value for tiny mode: 1056964608
    while (noclipEnabled) {
        WriteProcessMemory(processHandle, (LPVOID)(address), &noclipValue, sizeof(noclipValue), 0);
        Sleep(100);
    }
    WriteProcessMemory(processHandle, (LPVOID)(address), &normalValue, sizeof(normalValue), 0);
}

// jlkhasdfjkhgrteidshkjsdbkjfgbnkgbndfkjgnbdfkmlg

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
    std::cout << "YOU MAY NEED TO DISABLE/ENABLE MULTIPLE TIMES TO MAKE INFINITE JUMP WORK" << std::endl;
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
    std::cout << "Description: Automatically hides names when entering a new realm." << std::endl;
    std::cout << "Address: Cubic.exe" << std::endl;
    std::cout << "Offsets: { 0x1C00448 }" << std::endl;

}

// jump value freeze
void freezeValue(HANDLE processHandle, DWORD address, int value, bool& freeze) {
    while (freeze) {
        WriteProcessMemory(processHandle, (LPVOID)(address), &value, sizeof(value), 0);
        Sleep(100);
    }
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

// Refetch shit automatically (took me way to long to add this shit)
void reattachAndReload(HANDLE& processHandle, DWORD& gameBaseAddress, DWORD& pointsAddress, DWORD& customAddress, bool& freeze, std::thread& freezeThread) {
    while (true) {
        HWND hGameWindow = FindWindow(NULL, "Cubic");
        if (hGameWindow != NULL) {
            DWORD pID = NULL;
            GetWindowThreadProcessId(hGameWindow, &pID);
            HANDLE newProcessHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pID);
            if (newProcessHandle != INVALID_HANDLE_VALUE && newProcessHandle != NULL) {
                if (processHandle != newProcessHandle) {
                    processHandle = newProcessHandle;
                    char gameName[] = "Cubic.exe";
                    gameBaseAddress = GetModuleBaseAddress(_T(gameName), pID);
                    reloadAddresses(processHandle, gameBaseAddress, pointsAddress, customAddress, freeze, freezeThread);
                }
            }
        }
        else {
            processHandle = NULL;
        }
        Sleep(100);
    }
}

// maintain the NoCollide value in memory
void maintainNoCollide(HANDLE processHandle, DWORD address, bool& noCollideEnabled) {
    int noCollideValue = 0;
    int normalValue = 1065353216;
    while (noCollideEnabled) {
        WriteProcessMemory(processHandle, (LPVOID)(address), &noCollideValue, sizeof(noCollideValue), 0);
        Sleep(100);
    }
    WriteProcessMemory(processHandle, (LPVOID)(address), &normalValue, sizeof(normalValue), 0);
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
    bool noclipEnabled = false;
    bool noCollideEnabled = false;
    float desiredFOV = 0.0f;
    int freezeValueToSet = 6400;
    std::thread freezeThread;
    std::thread fovThread;
    std::thread fastMenuThread;
    std::thread injectThread;
    std::thread noclipThread;
    std::thread noCollideThread;

    reloadAddresses(processHandle, gameBaseAddress, pointsAddress, customAddress, freeze, freezeThread);
    displayMenu(freeze, fovEnabled, fastMenuEnabled, hideNameEnabled, noclipEnabled, noCollideEnabled, desiredFOV);

    // Start the reattach and reload thread
    std::thread reattachThread(reattachAndReload, std::ref(processHandle), std::ref(gameBaseAddress), std::ref(pointsAddress), std::ref(customAddress), std::ref(freeze), std::ref(freezeThread));
    reattachThread.detach();

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
            noclipThread.detach();
            Sleep(40);
            noCollideThread.detach();
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
            displayMenu(freeze, fovEnabled, fastMenuEnabled, hideNameEnabled, noclipEnabled, noCollideEnabled, desiredFOV);
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
            displayMenu(freeze, fovEnabled, fastMenuEnabled, hideNameEnabled, noclipEnabled, noCollideEnabled, desiredFOV);
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
            displayMenu(freeze, fovEnabled, fastMenuEnabled, hideNameEnabled, noclipEnabled, noCollideEnabled, desiredFOV);
        }
        if (GetAsyncKeyState(VK_F4)) {
            hideNameEnabled = !hideNameEnabled;
            injectEnabled = hideNameEnabled;
            if (injectEnabled) {
                injectThread = std::thread(injectMemory, processHandle, gameBaseAddress, std::ref(injectEnabled));
                injectThread.detach();
                std::cout << "Hide Names Enabled" << std::endl;
            }
            else {
                std::cout << "Hide Names Disabled" << std::endl;
            }
            Sleep(700);
            system("cls");
            displayMenu(freeze, fovEnabled, fastMenuEnabled, hideNameEnabled, noclipEnabled, noCollideEnabled, desiredFOV);
        }
        if (GetAsyncKeyState(VK_F5)) {
            noclipEnabled = !noclipEnabled;
            if (noclipEnabled) {
                DWORD noclipBaseAddress = NULL;
                ReadProcessMemory(processHandle, (LPVOID)(gameBaseAddress + offsetGameToBaseAddressNoclip), &noclipBaseAddress, sizeof(noclipBaseAddress), NULL);
                DWORD noclipAddress = noclipBaseAddress + noclipOffset;
                noclipThread = std::thread(maintainNoclip, processHandle, noclipAddress, std::ref(noclipEnabled));
                noclipThread.detach();
                std::cout << "Noclip enabled" << std::endl;
            }
            else {
                std::cout << "Noclip disabled" << std::endl;
            }
            Sleep(700);
            system("cls");
            displayMenu(freeze, fovEnabled, fastMenuEnabled, hideNameEnabled, noclipEnabled, noCollideEnabled, desiredFOV);
        }
        if (GetAsyncKeyState(VK_F6)) {
            noCollideEnabled = !noCollideEnabled;
            if (noCollideEnabled) {
                DWORD noCollideBaseAddress = NULL;
                ReadProcessMemory(processHandle, (LPVOID)(gameBaseAddress + offsetGameToBaseAddressNoclip), &noCollideBaseAddress, sizeof(noCollideBaseAddress), NULL);
                DWORD noCollideAddress = noCollideBaseAddress + noclipOffset;
                noCollideThread = std::thread(maintainNoCollide, processHandle, noCollideAddress, std::ref(noCollideEnabled));
                noCollideThread.detach();
                std::cout << "NoCollide enabled" << std::endl;
            }
            else {
                std::cout << "NoCollide disabled" << std::endl;
            }
            Sleep(700);
            system("cls");
            displayMenu(freeze, fovEnabled, fastMenuEnabled, hideNameEnabled, noclipEnabled, noCollideEnabled, desiredFOV);
        }

        if (GetAsyncKeyState(VK_F7)) {
            system("cls");
            displayAddresses(gameBaseAddress, pointsAddress, customAddress);
            Sleep(6500);
            system("cls");
            displayMenu(freeze, fovEnabled, fastMenuEnabled, hideNameEnabled, noclipEnabled, noCollideEnabled, desiredFOV);
        }
    }

    freeze = false;
    fovEnabled = false;
    fastMenuEnabled = false;
    injectEnabled = false;
    hideNameEnabled = false;
    noclipEnabled = false;
    noCollideEnabled = false;
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
    if (noclipThread.joinable()) {
        noclipThread.join();
    }
    if (noCollideThread.joinable()) {
        noCollideThread.join();
    }

    logFile.close();
    return 0;
}
