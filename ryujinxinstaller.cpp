
#ifndef UNICODE
#define UNICODE
#endif
#define _WIN32_WINNT 0x0601

#include <windows.h>
#include <wininet.h>
#include <commctrl.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>
#include <string>
#include <unordered_map>
#include <thread>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "comctl32.lib")

namespace fs = std::filesystem;

// Control-IDs
constexpr int ID_BUTTON = 101;
constexpr int ID_LOG = 102;
constexpr int ID_PROGRESS = 103;

// GUI-Handles & Farben
HWND     hButton = nullptr;
HWND     hLog = nullptr;
HWND     hProgress = nullptr;
HFONT    hFont = nullptr;
HBRUSH   hBrushBG = nullptr;
COLORREF colText = RGB(230, 230, 230);

// thread-sicheres Logging
std::mutex logMutex;
void AppendLog(const std::wstring& text) {
    std::lock_guard<std::mutex> lk(logMutex);
    SendMessageW(hLog, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    SendMessageW(hLog, EM_REPLACESEL, FALSE,
        (LPARAM)(text + L"\r\n").c_str());
}

// Haupt-Routine: Download & Entpacken mit globalem Fortschritt
void DownloadAndExtract() {
    const std::vector<std::wstring> urls = {
        L"https://github.com/Ryubing/Stable-Releases/releases/download/1.3.1/ryujinx-1.3.1-win_x64.zip",
        L"https://github.com/THZoria/NX_Firmware/releases/download/19.0.1/Firmware.19.0.1.zip",
        L"https://files.prodkeys.net/ProdKeys.net-v19.0.1.zip"
    };
    const int totalFiles = (int)urls.size();

    // Desktop\ryujinx anlegen
    size_t len = 0;
    _wgetenv_s(&len, nullptr, 0, L"USERPROFILE");
    std::vector<wchar_t> buf(len);
    _wgetenv_s(&len, buf.data(), len, L"USERPROFILE");
    fs::path baseDir = fs::path(buf.data()) / L"Desktop" / L"ryujinx";
    std::error_code ec;
    fs::create_directories(baseDir, ec);
    if (ec) {
        AppendLog(L"[FEHLER] Konnte Ziel­ordner nicht erstellen.");
        EnableWindow(hButton, TRUE);
        return;
    }

    // WinINet-Session starten
    HINTERNET hInet = InternetOpenW(
        L"RyujinxInstaller",
        INTERNET_OPEN_TYPE_PRECONFIG,
        nullptr, nullptr, 0
    );
    if (!hInet) {
        AppendLog(L"[FEHLER] WinINet-Initialisierung fehlgeschlagen.");
        EnableWindow(hButton, TRUE);
        return;
    }

    // 1) Gesamtgröße aller Dateien ermitteln
    std::vector<DWORD> sizes(totalFiles);
    DWORD64 totalSize = 0;
    for (int i = 0; i < totalFiles; ++i) {
        HINTERNET hUrl = InternetOpenUrlW(
            hInet, urls[i].c_str(),
            nullptr, 0,
            INTERNET_FLAG_RELOAD | INTERNET_FLAG_KEEP_CONNECTION,
            0
        );
        if (hUrl) {
            DWORD lenBytes = 0, lenSize = sizeof(lenBytes);
            if (HttpQueryInfoW(
                hUrl,
                HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER,
                &lenBytes, &lenSize, nullptr))
                sizes[i] = lenBytes;
            else
                sizes[i] = 0;
            InternetCloseHandle(hUrl);
        }
        else {
            sizes[i] = 0;
        }
        totalSize += sizes[i];
    }

    // 2) ProgressBar auf Gesamt­größe setzen
    SendMessageW(hProgress, PBM_SETRANGE32,
        0, static_cast<LPARAM>(totalSize));
    SendMessageW(hProgress, PBM_SETPOS, 0, 0);

    // 3) Tatsächlicher Download mit globalem Fortschritt
    DWORD64 downloaded = 0;
    std::unordered_map<std::wstring, int> folderCount;

    for (int i = 0; i < totalFiles; ++i) {
        const auto& url = urls[i];
        std::wstring fn = url.substr(url.find_last_of(L'/') + 1);
        std::wstring folder = fn.substr(0, fn.size() - 4);
        if (++folderCount[folder] > 1)
            folder += L"_" + std::to_wstring(folderCount[folder]);

        fs::path zipPath = baseDir / fn;
        fs::path extractPath = baseDir / folder;
        fs::create_directories(extractPath, ec);

        // Log: Download­start
        AppendLog(L"[INFO] (" +
            std::to_wstring(i + 1) + L"/" +
            std::to_wstring(totalFiles) + L") Starte Download: " + fn);

        HINTERNET hUrl = InternetOpenUrlW(
            hInet, url.c_str(),
            nullptr, 0,
            INTERNET_FLAG_RELOAD | INTERNET_FLAG_KEEP_CONNECTION,
            0
        );
        if (!hUrl) {
            AppendLog(L"[FEHLER] Konnte URL nicht öffnen: " + url);
            continue;
        }

        // in Datei schreiben
        std::ofstream ofs(zipPath, std::ios::binary);
        constexpr DWORD bufSize = 8192;
        std::vector<char> buffer(bufSize);
        DWORD bytesRead = 0;
        while (InternetReadFile(hUrl, buffer.data(), bufSize, &bytesRead) && bytesRead) {
            ofs.write(buffer.data(), bytesRead);
            downloaded += bytesRead;
            SendMessageW(hProgress, PBM_SETPOS,
                static_cast<WPARAM>(downloaded), 0);
        }
        ofs.close();
        InternetCloseHandle(hUrl);

        // Log: Download fertig
        AppendLog(L"[INFO] Download abgeschlossen: " + fn);

        // Entpacken
        AppendLog(L"[INFO] Entpacke: " + fn);
        std::wstring cmd = L"powershell.exe -WindowStyle Hidden -Command \""
            L"Expand-Archive -Path '" + zipPath.wstring() +
            L"' -DestinationPath '" + extractPath.wstring() +
            L"' -Force\"";
        STARTUPINFOW si{}; si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        CreateProcessW(nullptr, cmd.data(),
            nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, nullptr,
            &si, &pi);
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        // Log: Entpacken fertig
        AppendLog(L"[INFO] Entpacken abgeschlossen: " + fn);

        // Zip löschen (ohne Log)
        fs::remove(zipPath, ec);

        AppendLog(L"");
    }

    InternetCloseHandle(hInet);
    AppendLog(L"[INFO] Alle Vorgänge abgeschlossen.");
    EnableWindow(hButton, TRUE);
}

// Fenster-Prozedur (Dark-Mode)
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        hBrushBG = CreateSolidBrush(RGB(30, 30, 30));
        hFont = CreateFontW(
            -MulDiv(10, GetDeviceCaps(GetDC(nullptr), LOGPIXELSY), 72),
            0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI"
        );

        // Start-Button
        hButton = CreateWindowExW(
            0, L"BUTTON", L"Starten",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            10, 10, 80, 28,
            hWnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BUTTON)),
            nullptr, nullptr
        );
        SendMessageW(hButton, WM_SETFONT, (WPARAM)hFont, TRUE);

        // ProgressBar
        INITCOMMONCONTROLSEX ic{ sizeof(ic), ICC_PROGRESS_CLASS };
        InitCommonControlsEx(&ic);
        hProgress = CreateWindowExW(
            0, PROGRESS_CLASS, nullptr,
            WS_CHILD | WS_VISIBLE,
            100, 15, 330, 16,
            hWnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PROGRESS)),
            nullptr, nullptr
        );

        // Log-Feld
        hLog = CreateWindowExW(
            0, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
            10, 50, 420, 210,
            hWnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_LOG)),
            nullptr, nullptr
        );
        SendMessageW(hLog, WM_SETFONT, (WPARAM)hFont, TRUE);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wp);
        SetTextColor(dc, colText);
        SetBkMode(dc, OPAQUE);
        SetBkColor(dc, RGB(30, 30, 30));
        return reinterpret_cast<INT_PTR>(hBrushBG);
    }
    case WM_ERASEBKGND: {
        HDC dc = reinterpret_cast<HDC>(wp);
        RECT r; GetClientRect(hWnd, &r);
        FillRect(dc, &r, hBrushBG);
        return 1;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == ID_BUTTON) {
            EnableWindow(hButton, FALSE);
            AppendLog(L"[INFO] Starte alle Downloads");
            std::thread(DownloadAndExtract).detach();
        }
        break;
    case WM_DESTROY:
        DeleteObject(hFont);
        DeleteObject(hBrushBG);
        PostQuitMessage(0);
        break;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

// Unicode-Entry-Point
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"DownloaderGUI";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    HWND hWnd = CreateWindowExW(
        0, L"DownloaderGUI", L"Ryujinx Installer",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_SIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 450, 300,
        nullptr, nullptr, hInst, nullptr
    );
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
