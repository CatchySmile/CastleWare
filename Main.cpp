#include <Windows.h>
#include <TlHelp32.h>
#include <iostream>
#include <tchar.h>
#include <vector>
#include <fstream>
#include <thread>
#include "offsets.h"

// Function prototypes
DWORD GetModuleBaseAddress(TCHAR* lpszModuleName, DWORD pID);
DWORD getAddressWithOffsets(HANDLE processHandle, DWORD baseAddress, const std::vector<DWORD>& offsets);
void injectMemory(HANDLE processHandle, DWORD baseAddress, bool& injectEnabled);
void maintainFOV(HANDLE processHandle, DWORD address, bool& fovEnabled);
void maintainFastMenu(HANDLE processHandle, DWORD address, bool& fastMenuEnabled);
void maintainNoclip(HANDLE processHandle, DWORD address, bool& noclipEnabled);
void maintainPlayerSize(HANDLE processHandle, DWORD address, bool& playerSizeEnabled, int& playerSizeValue);
void freezeValue(HANDLE processHandle, DWORD address, int value, bool& freeze);
void reloadAddresses(HANDLE processHandle, DWORD gameBaseAddress, DWORD& pointsAddress, DWORD& customAddress, bool& freeze, std::thread& freezeThread);
void reattachAndReload(HANDLE& processHandle, DWORD& gameBaseAddress, DWORD& pointsAddress, DWORD& customAddress, bool& freeze, std::thread& freezeThread);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void ToggleOption(bool& option, HWND checkbox);
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

// Global variables
HINSTANCE hInst;
HWND hWndMain, hFreezeCheckbox, hFovCheckbox, hFastMenuCheckbox, hHideNameCheckbox, hNoclipCheckbox, hPlayerSizeCheckbox, hFovInput, hPlayerSizeDropdown, hFooter;
bool freeze = false, fovEnabled = false, fastMenuEnabled = false, hideNameEnabled = false, noclipEnabled = false, playerSizeEnabled = false;
int playerSizeValue = 1065353216; // Default to Normal size
DWORD gameBaseAddress = 0, pointsAddress = 0, customAddress = 0;
HANDLE processHandle = NULL;
std::thread freezeThread, fovThread, fastMenuThread, injectThread, noclipThread, playerSizeThread;
HHOOK hKeyboardHook;

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

void maintainFOV(HANDLE processHandle, DWORD address, bool& fovEnabled) {
    while (fovEnabled) {
        char buffer[10];
        GetWindowText(hFovInput, buffer, 10);
        float fovValue = static_cast<float>(atof(buffer));
        WriteProcessMemory(processHandle, (LPVOID)(address), &fovValue, sizeof(fovValue), 0);
        Sleep(100);
    }
}

void maintainFastMenu(HANDLE processHandle, DWORD address, bool& fastMenuEnabled) {
    int zeroValue = 0;
    while (fastMenuEnabled) {
        WriteProcessMemory(processHandle, (LPVOID)(address), &zeroValue, sizeof(zeroValue), 0);
        Sleep(100);
    }
}

void maintainNoclip(HANDLE processHandle, DWORD address, bool& noclipEnabled) {
    int noclipValue = 1008000000; // This is just our player size fr
    int normalValue = 1065353216; // tiny mode: 1056964608
    while (noclipEnabled) {
        WriteProcessMemory(processHandle, (LPVOID)(address), &noclipValue, sizeof(noclipValue), 0);
        Sleep(100);
    }
    WriteProcessMemory(processHandle, (LPVOID)(address), &normalValue, sizeof(normalValue), 0);
}

void maintainPlayerSize(HANDLE processHandle, DWORD address, bool& playerSizeEnabled, int& playerSizeValue) {
    while (playerSizeEnabled) {
        int selectedIndex = SendMessage(hPlayerSizeDropdown, CB_GETCURSEL, 0, 0);
        switch (selectedIndex) {
        case 0:
            playerSizeValue = 1056964608; // Tiny
            break;
        case 1:
            playerSizeValue = 1065353216; // Normal
            break;
        case 2:
            playerSizeValue = 1070000000; // Large
            break;
        case 3:
            playerSizeValue = 1085000000; // Titan
            break;
        case 4:
            playerSizeValue = 1125000000; // Max
            break;
        }
        WriteProcessMemory(processHandle, (LPVOID)(address), &playerSizeValue, sizeof(playerSizeValue), 0);
        Sleep(100);
    }
    int normalValue = 1065353216;
    WriteProcessMemory(processHandle, (LPVOID)(address), &normalValue, sizeof(normalValue), 0);
}

void ToggleOption(bool& option, HWND checkbox) {
    option = !option;
    SendMessage(checkbox, BM_SETCHECK, option ? BST_CHECKED : BST_UNCHECKED, 0);
    // Add your logic to enable/disable the feature here
    if (option) {
        if (checkbox == hFovCheckbox) {
            fovThread = std::thread(maintainFOV, processHandle, pointsAddress, std::ref(fovEnabled));
            fovThread.detach();
        }
        else if (checkbox == hFreezeCheckbox) {
            reloadAddresses(processHandle, gameBaseAddress, pointsAddress, customAddress, freeze, freezeThread);
        }
        else if (checkbox == hFastMenuCheckbox) {
            DWORD fastMenuAddress = gameBaseAddress + fastMenuOffset;
            fastMenuThread = std::thread(maintainFastMenu, processHandle, fastMenuAddress, std::ref(fastMenuEnabled));
            fastMenuThread.detach();
        }
        else if (checkbox == hHideNameCheckbox) {
            injectThread = std::thread(injectMemory, processHandle, gameBaseAddress, std::ref(hideNameEnabled));
            injectThread.detach();
        }
        else if (checkbox == hNoclipCheckbox) {
            DWORD noclipBaseAddress = NULL;
            ReadProcessMemory(processHandle, (LPVOID)(gameBaseAddress + offsetGameToBaseAddressNoclip), &noclipBaseAddress, sizeof(noclipBaseAddress), NULL);
            DWORD noclipAddress = noclipBaseAddress + noclipOffset;
            noclipThread = std::thread(maintainNoclip, processHandle, noclipAddress, std::ref(noclipEnabled));
            noclipThread.detach();
        }
        else if (checkbox == hPlayerSizeCheckbox) {
            DWORD playerSizeBaseAddress = NULL;
            ReadProcessMemory(processHandle, (LPVOID)(gameBaseAddress + offsetGameToBaseAddressNoclip), &playerSizeBaseAddress, sizeof(playerSizeBaseAddress), NULL);
            DWORD playerSizeAddress = playerSizeBaseAddress + noclipOffset;
            playerSizeThread = std::thread(maintainPlayerSize, processHandle, playerSizeAddress, std::ref(playerSizeEnabled), std::ref(playerSizeValue));
            playerSizeThread.detach();
        }
    }
}


void freezeValue(HANDLE processHandle, DWORD address, int value, bool& freeze) {
    while (freeze) {
        WriteProcessMemory(processHandle, (LPVOID)(address), &value, sizeof(value), 0);
        Sleep(100);
    }
}

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

void reattachAndReload(HANDLE& processHandle, DWORD& gameBaseAddress, DWORD& pointsAddress, DWORD& customAddress, bool& freeze, std::thread& freezeThread) {
    while (true) {
        // Detach from everything
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
        if (playerSizeThread.joinable()) {
            playerSizeThread.join();
        }

        // Reattach and reload addresses
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

        // Re-enable options if they were enabled before
        if (fovEnabled) {
            fovThread = std::thread(maintainFOV, processHandle, pointsAddress, std::ref(fovEnabled));
            fovThread.detach();
        }
        if (freeze) {
            freezeThread = std::thread(freezeValue, processHandle, customAddress, 6400, std::ref(freeze));
            freezeThread.detach();
        }
        if (fastMenuEnabled) {
            DWORD fastMenuAddress = gameBaseAddress + fastMenuOffset;
            fastMenuThread = std::thread(maintainFastMenu, processHandle, fastMenuAddress, std::ref(fastMenuEnabled));
            fastMenuThread.detach();
        }
        if (hideNameEnabled) {
            injectThread = std::thread(injectMemory, processHandle, gameBaseAddress, std::ref(hideNameEnabled));
            injectThread.detach();
        }
        if (noclipEnabled) {
            DWORD noclipBaseAddress = NULL;
            ReadProcessMemory(processHandle, (LPVOID)(gameBaseAddress + offsetGameToBaseAddressNoclip), &noclipBaseAddress, sizeof(noclipBaseAddress), NULL);
            DWORD noclipAddress = noclipBaseAddress + noclipOffset;
            noclipThread = std::thread(maintainNoclip, processHandle, noclipAddress, std::ref(noclipEnabled));
            noclipThread.detach();
        }
        if (playerSizeEnabled) {
            DWORD playerSizeBaseAddress = NULL;
            ReadProcessMemory(processHandle, (LPVOID)(gameBaseAddress + offsetGameToBaseAddressNoclip), &playerSizeBaseAddress, sizeof(playerSizeBaseAddress), NULL);
            DWORD playerSizeAddress = playerSizeBaseAddress + noclipOffset;
            playerSizeThread = std::thread(maintainPlayerSize, processHandle, playerSizeAddress, std::ref(playerSizeEnabled), std::ref(playerSizeValue));
            playerSizeThread.detach();
        }

        Sleep(333); // Sleep for a third a second before rechecking
    }
}


LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static HBRUSH hbrBkgnd = CreateSolidBrush(RGB(0, 0, 0)); // Black background
    static HBRUSH hbrGray = CreateSolidBrush(RGB(30, 30, 30)); // gray background for text
    static HBRUSH hbrBtnGray = CreateSolidBrush(RGB(30, 30, 30)); //
    static HBRUSH hbrWhite = CreateSolidBrush(RGB(255, 255, 255)); //

    switch (message) {
    case WM_CREATE:
        hFreezeCheckbox = CreateWindow(TEXT("button"), TEXT("Infinite Jump"), WS_VISIBLE | WS_CHILD | BS_CHECKBOX, 20, 20, 150, 20, hWnd, (HMENU)1, hInst, NULL);
        hFovCheckbox = CreateWindow(TEXT("button"), TEXT("FOV Changer"), WS_VISIBLE | WS_CHILD | BS_CHECKBOX, 20, 50, 150, 20, hWnd, (HMENU)2, hInst, NULL);
        hFovInput = CreateWindow(TEXT("edit"), TEXT("90"), WS_VISIBLE | WS_CHILD | WS_BORDER | ES_NUMBER, 180, 50, 50, 20, hWnd, NULL, hInst, NULL);
        hFastMenuCheckbox = CreateWindow(TEXT("button"), TEXT("Fast Menus"), WS_VISIBLE | WS_CHILD | BS_CHECKBOX, 20, 80, 150, 20, hWnd, (HMENU)3, hInst, NULL);
        hHideNameCheckbox = CreateWindow(TEXT("button"), TEXT("Hide Names"), WS_VISIBLE | WS_CHILD | BS_CHECKBOX, 20, 110, 150, 20, hWnd, (HMENU)4, hInst, NULL);
        hNoclipCheckbox = CreateWindow(TEXT("button"), TEXT("Noclip"), WS_VISIBLE | WS_CHILD | BS_CHECKBOX, 20, 140, 150, 20, hWnd, (HMENU)5, hInst, NULL);
        hPlayerSizeCheckbox = CreateWindow(TEXT("button"), TEXT("Player Size"), WS_VISIBLE | WS_CHILD | BS_CHECKBOX, 20, 170, 150, 20, hWnd, (HMENU)6, hInst, NULL);
        hPlayerSizeDropdown = CreateWindow(TEXT("combobox"), NULL, WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST, 180, 170, 100, 160, hWnd, (HMENU)7, hInst, NULL);
        SendMessage(hPlayerSizeDropdown, CB_ADDSTRING, 0, (LPARAM)TEXT("Tiny"));
        SendMessage(hPlayerSizeDropdown, CB_ADDSTRING, 0, (LPARAM)TEXT("Normal"));
        SendMessage(hPlayerSizeDropdown, CB_ADDSTRING, 0, (LPARAM)TEXT("Large"));
        SendMessage(hPlayerSizeDropdown, CB_ADDSTRING, 0, (LPARAM)TEXT("Titan"));
        SendMessage(hPlayerSizeDropdown, CB_ADDSTRING, 0, (LPARAM)TEXT("Gigantic"));
        SendMessage(hPlayerSizeDropdown, CB_SETCURSEL, 1, 0); // Default to Normal size
        hFooter = CreateWindow(TEXT("static"), TEXT("INS to toggle"), WS_VISIBLE | WS_CHILD, 20, 220, 250, 20, hWnd, NULL, hInst, NULL);
        hFooter = CreateWindow(TEXT("static"), TEXT("Github.com/CatchySmile/CastleWare"), WS_VISIBLE | WS_CHILD, 20, 236, 250, 20, hWnd, NULL, hInst, NULL);
        break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC hdcStatic = (HDC)wParam;
        SetTextColor(hdcStatic, RGB(255, 255, 255)); // White text
        SetBkColor(hdcStatic, RGB(30, 30, 30)); // Gray background
        return (INT_PTR)hbrGray;
    }
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hWnd, &rc);
        HBRUSH hbrTransparent = CreateSolidBrush(RGB(0, 0, 0));
        SetBkMode(hdc, TRANSPARENT);
        FillRect(hdc, &rc, hbrTransparent);
        DeleteObject(hbrTransparent);
        return 1;
    }
    case WM_CTLCOLORDLG:
        return (INT_PTR)hbrBkgnd;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case 1:
            ToggleOption(freeze, hFreezeCheckbox);
            break;
        case 2:
            ToggleOption(fovEnabled, hFovCheckbox);
            break;
        case 3:
            ToggleOption(fastMenuEnabled, hFastMenuCheckbox);
            break;
        case 4:
            ToggleOption(hideNameEnabled, hHideNameCheckbox);
            break;
        case 5:
            ToggleOption(noclipEnabled, hNoclipCheckbox);
            break;
        case 6:
            ToggleOption(playerSizeEnabled, hPlayerSizeCheckbox);
            int selectedIndex = SendMessage(hPlayerSizeDropdown, CB_GETCURSEL, 0, 0);
            switch (selectedIndex) {
            case 0:
                playerSizeValue = 1056964608; // Tiny
                break;
            case 1:
                playerSizeValue = 1065353216; // Normal
                break;
            case 2:
                playerSizeValue = 1073000000; // Large
                break;
            case 3:
                playerSizeValue = 1085000000; // Titan
                break;
            case 4:
                playerSizeValue = 1125000000; // Max
                break;
            }
            if (playerSizeEnabled) {
                DWORD playerSizeBaseAddress = NULL;
                ReadProcessMemory(processHandle, (LPVOID)(gameBaseAddress + offsetGameToBaseAddressNoclip), &playerSizeBaseAddress, sizeof(playerSizeBaseAddress), NULL);
                DWORD playerSizeAddress = playerSizeBaseAddress + noclipOffset;
                playerSizeThread = std::thread(maintainPlayerSize, processHandle, playerSizeAddress, std::ref(playerSizeEnabled), std::ref(playerSizeValue));
                playerSizeThread.detach();
            }
            break;
        }
        break;
    case WM_DESTROY:
        DeleteObject(hbrBkgnd);
        DeleteObject(hbrGray);
        DeleteObject(hbrBtnGray);
        DeleteObject(hbrWhite);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && wParam == WM_KEYDOWN) {
        KBDLLHOOKSTRUCT* pKeyboard = (KBDLLHOOKSTRUCT*)lParam;
        if (pKeyboard->vkCode == VK_INSERT) {
            if (IsWindowVisible(hWndMain)) {
                ShowWindow(hWndMain, SW_HIDE);
            }
            else {
                ShowWindow(hWndMain, SW_SHOW);
                SetWindowPos(hWndMain, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            }
        }
    }
    return CallNextHookEx(hKeyboardHook, nCode, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    hInst = hInstance;
    WNDCLASSEX wcex;
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = TEXT("MainWindowClass");
    wcex.hIconSm = LoadIcon(wcex.hInstance, IDI_APPLICATION);

    RegisterClassEx(&wcex);

    hWndMain = CreateWindow(TEXT("MainWindowClass"), TEXT("CastleWare v1.0"), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, 300, 300, NULL, NULL, hInstance, NULL);
    if (!hWndMain) {
        return FALSE;
    }

    ShowWindow(hWndMain, nCmdShow);
    UpdateWindow(hWndMain);

    // Set the window to always be on top
    SetWindowPos(hWndMain, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

    // Set the keyboard hook
    hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, hInstance, 0);

    // Start the reattach and reload thread
    std::thread reattachThread(reattachAndReload, std::ref(processHandle), std::ref(gameBaseAddress), std::ref(pointsAddress), std::ref(customAddress), std::ref(freeze), std::ref(freezeThread));
    reattachThread.detach();

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Unhook the keyboard hook
    UnhookWindowsHookEx(hKeyboardHook);

    freeze = false;
    fovEnabled = false;
    fastMenuEnabled = false;
    hideNameEnabled = false;
    noclipEnabled = false;
    playerSizeEnabled = false;
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
    if (playerSizeThread.joinable()) {
        playerSizeThread.join();
    }

    return (int)msg.wParam;
}
