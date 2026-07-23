#include <windows.h>
#include <wininet.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <bcrypt.h>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <thread>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <atomic>
#include <wrl.h>
#include <wil/com.h>
#include "WebView2.h"
#include <map>
#include <set>
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "bcrypt.lib")

using namespace Microsoft::WRL;
namespace fs = std::filesystem;

#define WM_WEBVIEW_UPDATE (WM_USER + 101)


std::wstring CHEAT_NAME = L"Codex Visuals";
const std::wstring JRE_URL = L"https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.10%2B7/OpenJDK21U-jre_x64_windows_hotspot_21.0.10_7.zip"; // Это JRE если надо замени
const std::wstring MOD_URL = L"https://github.com/maksmnha67-cmd/codex-visuals/releases/download/visuals/codex-.001.jar"; // Это твой главный мод клиента. Fabric 1.21.4
const std::wstring ADD_MOD_URL = L"https://www.dropbox.com/scl/fi/jmb2gzykcd467bj8ncknw/fabric-api-0.119.4-1.21.4.jar?rlkey=4bx8x9v9uq95i0nv7w3pmeiiw&st=05orip2n&dl=1"; // Это Fabric API
const std::wstring GAME_URL = L"https://github.com/maksmnha67-cmd/codex-visuals/releases/download/visuals/game.zip"; // versions/Fabric 1.21.4 + libraries
const std::wstring ASSETS_URL = L"https://github.com/maksmnha67-cmd/codex-visuals/releases/download/visuals/assets.zip"; // assets

HWND g_hWnd = nullptr;
wil::com_ptr<ICoreWebView2Controller> g_webviewController;
wil::com_ptr<ICoreWebView2> g_webview;
int g_RamAmount = 4028;
std::atomic<DWORD> g_GamePID{ 0 };
std::atomic<bool> g_CancelDownload{ false };

bool g_DarkTheme = true;
bool g_LangRu = true;
bool g_HasSavedPrefs = false;
std::wstring g_Nickname = L"Player";
bool g_ExtraPanelOpen = false;

const int MAIN_WIDTH = 382;
const int MAIN_HEIGHT = 532;
const int EXTRA_WIDTH = 220;

struct RegState { bool isInstalled; int ram; bool darkTheme; bool langRu; bool hasPrefs; std::wstring nickname; };

static const BYTE AES_KEY[32] = {
    0x4F,0x2B,0x7E,0x15,0x16,0xA8,0x09,0xCF,0xAA,0xDF,0x2C,0x9B,0x7D,0x51,0x3A,0xE8,
    0xC1,0x03,0x5E,0xF7,0xD4,0x62,0xB9,0x80,0x1C,0xA6,0x3F,0x58,0xE9,0x74,0x0D,0xBB
};
static const BYTE AES_IV[16] = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F
};

std::wstring GetBaseDir() { return L"C:\\" + CHEAT_NAME + L"\\"; }
std::wstring GetMinecraftDir() { return GetBaseDir() + L".minecraft\\"; }
std::wstring GetModsDir() { return GetMinecraftDir() + L"mods\\"; }
std::wstring GetVersionDir() { return GetMinecraftDir() + L"versions\\Fabric 1.21.4\\"; }

bool AESEncrypt(const std::vector<BYTE>& plaintext, std::vector<BYTE>& ciphertext) {
    BCRYPT_ALG_HANDLE hAlg = NULL; BCRYPT_KEY_HANDLE hKey = NULL; NTSTATUS status;
    status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, NULL, 0);
    if (!BCRYPT_SUCCESS(status)) return false;
    status = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PBYTE)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
    if (!BCRYPT_SUCCESS(status)) { BCryptCloseAlgorithmProvider(hAlg, 0); return false; }
    status = BCryptGenerateSymmetricKey(hAlg, &hKey, NULL, 0, (PBYTE)AES_KEY, sizeof(AES_KEY), 0);
    if (!BCRYPT_SUCCESS(status)) { BCryptCloseAlgorithmProvider(hAlg, 0); return false; }
    DWORD cb = 0; BYTE iv[16]; memcpy(iv, AES_IV, 16);
    status = BCryptEncrypt(hKey, (PBYTE)plaintext.data(), (ULONG)plaintext.size(), NULL, iv, 16, NULL, 0, &cb, BCRYPT_BLOCK_PADDING);
    if (!BCRYPT_SUCCESS(status)) { BCryptDestroyKey(hKey); BCryptCloseAlgorithmProvider(hAlg, 0); return false; }
    ciphertext.resize(cb); memcpy(iv, AES_IV, 16);
    status = BCryptEncrypt(hKey, (PBYTE)plaintext.data(), (ULONG)plaintext.size(), NULL, iv, 16, ciphertext.data(), cb, &cb, BCRYPT_BLOCK_PADDING);
    if (!BCRYPT_SUCCESS(status)) { BCryptDestroyKey(hKey); BCryptCloseAlgorithmProvider(hAlg, 0); return false; }
    ciphertext.resize(cb); BCryptDestroyKey(hKey); BCryptCloseAlgorithmProvider(hAlg, 0); return true;
}

bool AESDecrypt(const std::vector<BYTE>& ciphertext, std::vector<BYTE>& plaintext) {
    BCRYPT_ALG_HANDLE hAlg = NULL; BCRYPT_KEY_HANDLE hKey = NULL; NTSTATUS status;
    status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, NULL, 0);
    if (!BCRYPT_SUCCESS(status)) return false;
    status = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PBYTE)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
    if (!BCRYPT_SUCCESS(status)) { BCryptCloseAlgorithmProvider(hAlg, 0); return false; }
    status = BCryptGenerateSymmetricKey(hAlg, &hKey, NULL, 0, (PBYTE)AES_KEY, sizeof(AES_KEY), 0);
    if (!BCRYPT_SUCCESS(status)) { BCryptCloseAlgorithmProvider(hAlg, 0); return false; }
    DWORD cb = 0; BYTE iv[16]; memcpy(iv, AES_IV, 16);
    status = BCryptDecrypt(hKey, (PBYTE)ciphertext.data(), (ULONG)ciphertext.size(), NULL, iv, 16, NULL, 0, &cb, BCRYPT_BLOCK_PADDING);
    if (!BCRYPT_SUCCESS(status)) { BCryptDestroyKey(hKey); BCryptCloseAlgorithmProvider(hAlg, 0); return false; }
    plaintext.resize(cb); memcpy(iv, AES_IV, 16);
    status = BCryptDecrypt(hKey, (PBYTE)ciphertext.data(), (ULONG)ciphertext.size(), NULL, iv, 16, plaintext.data(), cb, &cb, BCRYPT_BLOCK_PADDING);
    if (!BCRYPT_SUCCESS(status)) { BCryptDestroyKey(hKey); BCryptCloseAlgorithmProvider(hAlg, 0); return false; }
    plaintext.resize(cb); BCryptDestroyKey(hKey); BCryptCloseAlgorithmProvider(hAlg, 0); return true;
}

void SafePostJson(const std::wstring& j) {
    if (g_hWnd) { std::wstring* m = new std::wstring(j); PostMessage(g_hWnd, WM_WEBVIEW_UPDATE, 0, (LPARAM)m); }
}

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return ""; int s = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), 0, 0, 0, 0);
    std::string r(s, 0); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &r[0], s, 0, 0); return r;
}

std::wstring Utf8ToWide(const std::string& u) {
    if (u.empty()) return L""; int s = MultiByteToWideChar(CP_UTF8, 0, u.c_str(), (int)u.size(), 0, 0);
    std::wstring r(s, 0); MultiByteToWideChar(CP_UTF8, 0, u.c_str(), (int)u.size(), &r[0], s); return r;
}

std::string SanitizeNicknameForJson(const std::wstring& nick) {
    std::string u = WideToUtf8(nick); std::string result;
    for (char c : u) {
        if (c == '"') result += "\\\""; else if (c == '\\') result += "\\\\";
        else if (c == '\n' || c == '\r') continue; else result += c;
    }
    return result;
}

std::string SanitizeNicknameForStorage(const std::wstring& nick) {
    std::string u = WideToUtf8(nick); std::string result;
    for (char c : u) { if (c == ';' || c == '=' || c == '\n' || c == '\r') continue; result += c; }
    if (result.empty()) result = "Player"; if (result.length() > 16) result = result.substr(0, 16);
    return result;
}

void SaveRegistry(bool installed, int ram, bool darkTheme, bool langRu, const std::wstring& nickname) {
    HKEY hKey; std::wstring regPath = L"SOFTWARE\\" + CHEAT_NAME;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, regPath.c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        std::string nickStr = SanitizeNicknameForStorage(nickname);
        std::string p;
        p += "RAM=" + std::to_string(ram) + ";";
        p += "STATUS=" + std::string(installed ? "INSTALLED_OK" : "NOT_INSTALLED") + ";";
        p += "THEME=" + std::string(darkTheme ? "DARK" : "LIGHT") + ";";
        p += "LANG=" + std::string(langRu ? "RU" : "EN") + ";";
        p += "NICK=" + nickStr + ";";
        p += "PREFS=YES;";
        p += "CHECKSUM=" + std::to_string(ram * 31337 + (installed ? 99991 : 13579)) + ";";
        std::vector<BYTE> pv(p.begin(), p.end()); std::vector<BYTE> ev;
        if (AESEncrypt(pv, ev)) RegSetValueExW(hKey, L"Data", 0, REG_BINARY, ev.data(), (DWORD)ev.size());
        RegCloseKey(hKey);
    }
}

RegState LoadRegistry() {
    RegState state = { false, 4028, true, true, false, L"Player" };
    HKEY hKey; std::wstring regPath = L"SOFTWARE\\" + CHEAT_NAME;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, regPath.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD dataSize = 0;
        if (RegQueryValueExW(hKey, L"Data", NULL, NULL, NULL, &dataSize) == ERROR_SUCCESS && dataSize > 0) {
            std::vector<BYTE> ev(dataSize);
            if (RegQueryValueExW(hKey, L"Data", NULL, NULL, ev.data(), &dataSize) == ERROR_SUCCESS) {
                std::vector<BYTE> pv;
                if (AESDecrypt(ev, pv)) {
                    std::string pd(pv.begin(), pv.end());
                    auto ex = [&](const std::string& k) -> std::string {
                        size_t p2 = pd.find(k + "="); if (p2 == std::string::npos) return "";
                        p2 += k.length() + 1; size_t e = pd.find(";", p2); if (e == std::string::npos) return "";
                        return pd.substr(p2, e - p2);
                        };
                    std::string rs = ex("RAM"), ss = ex("STATUS"), ts = ex("THEME"), ls = ex("LANG"), ns = ex("NICK"), ps = ex("PREFS"), cs = ex("CHECKSUM");
                    if (!rs.empty()) {
                        try {
                            int rv = std::stoi(rs); bool iv = (ss == "INSTALLED_OK");
                            int ec = rv * 31337 + (iv ? 99991 : 13579);
                            if (!cs.empty() && std::stoi(cs) == ec) { state.ram = rv; state.isInstalled = iv; }
                        }
                        catch (...) {}
                    }
                    state.darkTheme = (ts != "LIGHT"); state.langRu = (ls != "EN"); state.hasPrefs = (ps == "YES");
                    if (!ns.empty()) state.nickname = Utf8ToWide(ns);
                }
            }
        }
        RegCloseKey(hKey);
    }
    return state;
}

void SendProgress(double percent, double currentMb, double totalMb, const std::wstring& status) {
    std::wstringstream js; js.imbue(std::locale::classic());
    js << L"{ \"type\": \"progress\", \"percent\": " << percent
        << L", \"current\": \"" << std::fixed << std::setprecision(1) << currentMb << L"MB\""
        << L", \"total\": \"" << std::fixed << std::setprecision(1) << totalMb << L"MB\""
        << L", \"status\": \"" << status << L"\" }";
    SafePostJson(js.str());
}

void SendError(const std::wstring& errorMsg) {
    std::wstringstream js; js << L"{ \"type\": \"error\", \"message\": \"" << errorMsg << L"\" }"; SafePostJson(js.str());
}

bool DownloadFile(const std::wstring& url, const std::wstring& destPath, const std::wstring& statusText) {
    HINTERNET hI = InternetOpenW(L"Launcher/1.0", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hI) { SendError(L"InternetOpen failed"); return false; }
    HINTERNET hU = InternetOpenUrlW(hI, url.c_str(), NULL, 0, INTERNET_FLAG_RELOAD, 0);
    if (!hU) { DWORD e = GetLastError(); SendError(L"InternetOpenUrl failed: " + std::to_wstring(e)); InternetCloseHandle(hI); return false; }
    DWORD ts = 0, ls = sizeof(ts); HttpQueryInfoW(hU, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &ts, &ls, NULL);
    fs::create_directories(fs::path(destPath).parent_path());
    std::ofstream of(destPath, std::ios::binary);
    if (!of) { SendError(L"Cannot create: " + destPath); InternetCloseHandle(hU); InternetCloseHandle(hI); return false; }
    char buf[8192]; DWORD br, tr = 0; double tm = ts / 1024.0 / 1024.0;
    while (InternetReadFile(hU, buf, sizeof(buf), &br) && br > 0) {
        if (g_CancelDownload) { of.close(); InternetCloseHandle(hU); InternetCloseHandle(hI); fs::remove(destPath); return false; }
        of.write(buf, br); tr += br;
        double cm = tr / 1024.0 / 1024.0, pc = (ts > 0) ? (double)tr / ts * 100.0 : 0.0;
        SendProgress(pc, cm, tm, statusText);
    }
    of.close(); InternetCloseHandle(hU); InternetCloseHandle(hI); return true;
}

bool UnzipWithPowerShell(const std::wstring& zipPath, const std::wstring& targetDir) {
    if (g_CancelDownload) return false;
    fs::create_directories(targetDir);
    std::wstring cmd = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \"Expand-Archive -LiteralPath '" + zipPath + L"' -DestinationPath '" + targetDir + L"' -Force\"";
    STARTUPINFOW si = { sizeof(si) }; si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi; std::vector<wchar_t> cb(cmd.begin(), cmd.end()); cb.push_back(0);
    if (CreateProcessW(NULL, cb.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE); DWORD ec = 0; GetExitCodeProcess(pi.hProcess, &ec);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        if (ec != 0) { SendError(L"Unzip failed, code: " + std::to_wstring(ec)); return false; }
        return true;
    }
    SendError(L"Failed to start PowerShell"); return false;
}

std::wstring FindJavaExe(bool useJavaw) {
    std::wstring base = GetBaseDir();
    std::wstring exeName = useJavaw ? L"javaw.exe" : L"java.exe";
    std::wstring je = base + L"jre\\bin\\" + exeName;
    if (fs::exists(je)) return je;
    if (fs::exists(base + L"jre")) {
        for (auto const& d : fs::directory_iterator(base + L"jre")) {
            if (d.is_directory()) {
                std::wstring sp = d.path().wstring() + L"\\bin\\" + exeName;
                if (fs::exists(sp)) return sp;
            }
        }
    }
    je = base + L"jre\\bin\\" + (useJavaw ? L"java.exe" : L"javaw.exe");
    if (fs::exists(je)) return je;
    if (fs::exists(base + L"jre")) {
        for (auto const& d : fs::directory_iterator(base + L"jre")) {
            if (d.is_directory()) {
                std::wstring sp = d.path().wstring() + L"\\bin\\" + (useJavaw ? L"java.exe" : L"javaw.exe");
                if (fs::exists(sp)) return sp;
            }
        }
    }
    return L"";
}

std::string ReadFileToString(const std::wstring& path) {
    std::ifstream f(path); if (!f) return "";
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::string ExtractJsonString(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    size_t pos = json.find(searchKey); if (pos == std::string::npos) return "";
    size_t colon = json.find(':', pos + searchKey.length()); if (colon == std::string::npos) return "";
    size_t qs = json.find('"', colon + 1); if (qs == std::string::npos) return "";
    size_t qe = json.find('"', qs + 1); if (qe == std::string::npos) return "";
    return json.substr(qs + 1, qe - qs - 1);
}

std::string BuildClasspath() {
    std::wstring vd = GetVersionDir();
    std::wstring vj = vd + L"Fabric 1.21.4.jar";
    std::wstring libDir = GetMinecraftDir() + L"libraries";
    std::string cp = WideToUtf8(vj);

    auto parseVersion = [](const std::string& ver) -> std::vector<int> {
        std::vector<int> nums; std::string current;
        for (size_t i = 0; i < ver.size(); i++) {
            char c = ver[i];
            if (c == '.' || c == '-' || c == '_' || c == '+') {
                if (!current.empty()) { try { nums.push_back(std::stoi(current)); } catch (...) { nums.push_back(0); } current.clear(); }
            }
            else if (c >= '0' && c <= '9') { current += c; }
            else { if (!current.empty()) { try { nums.push_back(std::stoi(current)); } catch (...) { nums.push_back(0); } current.clear(); } }
        }
        if (!current.empty()) { try { nums.push_back(std::stoi(current)); } catch (...) { nums.push_back(0); } }
        while (nums.size() < 4) nums.push_back(0); return nums;
        };

    auto isVersionGreater = [&parseVersion](const std::string& a, const std::string& b) -> bool {
        auto va = parseVersion(a), vb = parseVersion(b);
        size_t len = va.size() < vb.size() ? va.size() : vb.size();
        for (size_t i = 0; i < len; i++) { if (va[i] > vb[i]) return true; if (va[i] < vb[i]) return false; }
        return false;
        };

    struct LibEntry { std::string key; std::string version; std::wstring fullPath; };
    std::map<std::string, LibEntry> bestLibs;

    if (fs::exists(libDir)) {
        for (auto const& entry : fs::recursive_directory_iterator(libDir)) {
            if (!entry.is_regular_file()) continue;
            std::wstring ext = entry.path().extension().wstring();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
            if (ext != L".jar") continue;
            std::wstring fullPath = entry.path().wstring();
            std::string fileName = WideToUtf8(entry.path().filename().wstring());
            if (fileName.find("natives-linux") != std::string::npos) continue;
            if (fileName.find("natives-macos") != std::string::npos) continue;
            if (fileName.find("natives-osx") != std::string::npos) continue;
            if (fileName.find("linux-aarch") != std::string::npos) continue;
            if (fileName.find("linux-x86_64") != std::string::npos) continue;
            std::wstring relPath = fullPath.substr(libDir.length());
            if (!relPath.empty() && (relPath[0] == L'\\' || relPath[0] == L'/')) relPath = relPath.substr(1);
            std::string relUtf8 = WideToUtf8(relPath);
            std::replace(relUtf8.begin(), relUtf8.end(), '\\', '/');
            std::vector<std::string> pathParts; std::istringstream ss(relUtf8); std::string tok;
            while (std::getline(ss, tok, '/')) { if (!tok.empty()) pathParts.push_back(tok); }
            std::string key, version;
            if (pathParts.size() >= 4) {
                version = pathParts[pathParts.size() - 2];
                std::string artifact = pathParts[pathParts.size() - 3]; std::string group;
                for (size_t i = 0; i < pathParts.size() - 3; i++) { if (!group.empty()) group += "."; group += pathParts[i]; }
                key = group + ":" + artifact;
                if (fileName.find("natives-windows") != std::string::npos) {
                    key += ":natives-windows";
                    if (fileName.find("arm64") != std::string::npos) key += "-arm64";
                    else if (fileName.find("x86") != std::string::npos && fileName.find("x86_64") == std::string::npos) key += "-x86";
                }
            }
            else { key = WideToUtf8(fullPath); version = "0"; }
            auto it = bestLibs.find(key);
            if (it == bestLibs.end()) { LibEntry le; le.key = key; le.version = version; le.fullPath = fullPath; bestLibs[key] = le; }
            else { if (isVersionGreater(version, it->second.version)) { it->second.version = version; it->second.fullPath = fullPath; } }
        }
    }
    for (auto it = bestLibs.begin(); it != bestLibs.end(); ++it) { cp += ";" + WideToUtf8(it->second.fullPath); }
    return cp;
}

std::string GetMainClass() {
    std::wstring jp = GetVersionDir() + L"Fabric 1.21.4.json";
    if (fs::exists(jp)) { std::string jc = ReadFileToString(jp); std::string mc = ExtractJsonString(jc, "mainClass"); if (!mc.empty()) return mc; }
    return "net.fabricmc.loader.impl.launch.knot.KnotClient";
}

std::string GetAssetIndex() {
    std::wstring jp = GetVersionDir() + L"Fabric 1.21.4.json";
    if (fs::exists(jp)) {
        std::string jc = ReadFileToString(jp);
        size_t aiPos = jc.find("\"assetIndex\"");
        if (aiPos != std::string::npos) {
            size_t braceStart = jc.find('{', aiPos);
            if (braceStart != std::string::npos) {
                size_t braceEnd = jc.find('}', braceStart);
                if (braceEnd != std::string::npos) {
                    std::string aiBlock = jc.substr(braceStart, braceEnd - braceStart + 1);
                    std::string id = ExtractJsonString(aiBlock, "id"); if (!id.empty()) return id;
                }
            }
        }
        std::string assets = ExtractJsonString(jc, "assets"); if (!assets.empty()) return assets;
    }
    return "21";
}

void MonitorProcessThread(DWORD pid) {
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid); if (h) { WaitForSingleObject(h, INFINITE); CloseHandle(h); }
    g_GamePID = 0; SafePostJson(L"{ \"type\": \"process_stopped\" }");
}

void LogCommandLine(const std::wstring& cmd) {
    std::wstring logPath = GetBaseDir() + L"launch_cmd.log";
    std::ofstream f(logPath, std::ios::trunc); if (f) { f << WideToUtf8(cmd); f.close(); }
}

int GetSafeRamAmount(int requested) {
    MEMORYSTATUSEX memInfo; memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        int totalMb = (int)(memInfo.ullTotalPhys / 1024 / 1024);
        int maxAllowed = totalMb - 1024; if (maxAllowed < 1024) maxAllowed = 1024;
        maxAllowed = (maxAllowed / 128) * 128;
        if (requested > maxAllowed) {
            SendError(L"RAM " + std::to_wstring(requested) + L"MB exceeds available (" + std::to_wstring(totalMb) + L"MB total), clamped to " + std::to_wstring(maxAllowed) + L"MB");
            return maxAllowed;
        }
    }
    return requested;
}

std::wstring GetSafeNickname() {
    std::wstring nick = g_Nickname; std::wstring safe;
    for (wchar_t c : nick) {
        if ((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L'_') safe += c;
    }
    if (safe.empty()) safe = L"Player"; if (safe.length() > 16) safe = safe.substr(0, 16);
    return safe;
}

void ResizeWindow(bool expanded) {
    if (!g_hWnd) return;
    RECT rc; GetWindowRect(g_hWnd, &rc);
    int newW = expanded ? (MAIN_WIDTH + EXTRA_WIDTH) : MAIN_WIDTH;
    SetWindowPos(g_hWnd, NULL, rc.left, rc.top, newW, MAIN_HEIGHT, SWP_NOZORDER | SWP_NOACTIVATE);
    if (g_webviewController) {
        RECT b; GetClientRect(g_hWnd, &b);
        g_webviewController->put_Bounds(b);
    }
    g_ExtraPanelOpen = expanded;
}

void LaunchGame() {
    std::wstring javawExe = FindJavaExe(true);
    if (javawExe.empty()) javawExe = FindJavaExe(false);
    if (javawExe.empty()) { SendError(L"No Java executable found"); return; }

    int safeRam = GetSafeRamAmount(g_RamAmount);
    std::string cpStr = BuildClasspath();
    std::string mc = GetMainClass();
    std::string assetIndex = GetAssetIndex();
    std::wstring md = GetMinecraftDir();
    std::wstring nd = GetVersionDir() + L"natives";
    if (!fs::exists(nd)) fs::create_directories(nd);
    if (!md.empty() && md.back() == L'\\') md.pop_back();
    if (!nd.empty() && nd.back() == L'\\') nd.pop_back();
    std::wstring assetsDir = GetMinecraftDir() + L"assets";
    if (!assetsDir.empty() && assetsDir.back() == L'\\') assetsDir.pop_back();

    std::wstring safeNick = GetSafeNickname();

    std::vector<std::wstring> jvmArgs;
    jvmArgs.push_back(L"-Xmx" + std::to_wstring(safeRam) + L"M");
    jvmArgs.push_back(L"-Xms512M"); jvmArgs.push_back(L"-Xss1M");
    jvmArgs.push_back(L"-XX:+UseG1GC"); jvmArgs.push_back(L"-XX:+UnlockExperimentalVMOptions");
    jvmArgs.push_back(L"-XX:G1NewSizePercent=20"); jvmArgs.push_back(L"-XX:G1ReservePercent=20");
    jvmArgs.push_back(L"-XX:MaxGCPauseMillis=50"); jvmArgs.push_back(L"-XX:G1HeapRegionSize=32M");
    jvmArgs.push_back(L"-XX:HeapDumpPath=MojangTricksIntelDriversForPerformance_javaw.exe_minecraft.exe.heapdump");
    jvmArgs.push_back(L"-Djava.library.path=" + nd);
    jvmArgs.push_back(L"-Djna.tmpdir=" + nd);
    jvmArgs.push_back(L"-Dorg.lwjgl.system.SharedLibraryExtractPath=" + nd);
    jvmArgs.push_back(L"-Dio.netty.native.workdir=" + nd);
    jvmArgs.push_back(L"-Dminecraft.launcher.brand=custom-launcher");
    jvmArgs.push_back(L"-Dminecraft.launcher.version=1.0");
    jvmArgs.push_back(L"-DFabricMcEmu= net.minecraft.client.main.Main ");
    jvmArgs.push_back(L"-cp"); jvmArgs.push_back(Utf8ToWide(cpStr));

    std::vector<std::wstring> gameArgs;
    gameArgs.push_back(L"--username"); gameArgs.push_back(safeNick);
    gameArgs.push_back(L"--version"); gameArgs.push_back(L"Fabric 1.21.4");
    gameArgs.push_back(L"--gameDir"); gameArgs.push_back(md);
    gameArgs.push_back(L"--assetsDir"); gameArgs.push_back(assetsDir);
    gameArgs.push_back(L"--assetIndex"); gameArgs.push_back(Utf8ToWide(assetIndex));
    gameArgs.push_back(L"--uuid"); gameArgs.push_back(L"00000000-0000-0000-0000-000000000000");
    gameArgs.push_back(L"--accessToken"); gameArgs.push_back(L"0");
    gameArgs.push_back(L"--userType"); gameArgs.push_back(L"legacy");
    gameArgs.push_back(L"--versionType"); gameArgs.push_back(L"release");

    auto quoteIfNeeded = [](const std::wstring& s) -> std::wstring {
        if (s.find(L' ') != std::wstring::npos || s.find(L'\t') != std::wstring::npos) return L"\"" + s + L"\"";
        return s;
        };

    std::wstring cmd = quoteIfNeeded(javawExe);
    for (auto& a : jvmArgs) cmd += L" " + quoteIfNeeded(a);
    cmd += L" " + Utf8ToWide(mc);
    for (auto& a : gameArgs) cmd += L" " + quoteIfNeeded(a);
    LogCommandLine(cmd);

    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end()); cmdBuf.push_back(0);
    std::wstring elp = GetBaseDir() + L"launch_error.log";
    SECURITY_ATTRIBUTES sa; sa.nLength = sizeof(sa); sa.lpSecurityDescriptor = NULL; sa.bInheritHandle = TRUE;
    HANDLE hLog = CreateFileW(elp.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    STARTUPINFOW si = { sizeof(si) };
    if (hLog != INVALID_HANDLE_VALUE) { si.dwFlags |= STARTF_USESTDHANDLES; si.hStdError = hLog; si.hStdOutput = hLog; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE); }
    PROCESS_INFORMATION pi;
    if (CreateProcessW(NULL, cmdBuf.data(), NULL, NULL, TRUE, 0, NULL, md.c_str(), &si, &pi)) {
        g_GamePID = pi.dwProcessId;
        HANDLE hPC = pi.hProcess, hLC = hLog; std::wstring elpCopy = elp;
        std::thread([hPC, hLC, elpCopy]() {
            WaitForSingleObject(hPC, 30000); DWORD exitCode = 0; GetExitCodeProcess(hPC, &exitCode);
            if (exitCode != STILL_ACTIVE && exitCode != 0) {
                CloseHandle(hPC); if (hLC != INVALID_HANDLE_VALUE) CloseHandle(hLC);
                std::wstring errMsg = L"Java exited code " + std::to_wstring(exitCode);
                if (fs::exists(elpCopy) && fs::file_size(elpCopy) > 0) {
                    std::string content = ReadFileToString(elpCopy);
                    std::istringstream iss(content); std::vector<std::string> lines; std::string line;
                    while (std::getline(iss, line)) lines.push_back(line);
                    int start = (int)lines.size() > 10 ? (int)lines.size() - 10 : 0; std::string last;
                    for (int i = start; i < (int)lines.size(); i++) {
                        std::string c = lines[i]; size_t p;
                        while ((p = c.find('"')) != std::string::npos) c.replace(p, 1, "'");
                        while ((p = c.find('\\')) != std::string::npos) c.replace(p, 1, "/");
                        last += c + " | ";
                    }
                    if (!last.empty()) errMsg += L" :: " + Utf8ToWide(last);
                }
                SendError(errMsg);
            }
            else { CloseHandle(hPC); if (hLC != INVALID_HANDLE_VALUE) CloseHandle(hLC); }
            }).detach();
        std::thread(MonitorProcessThread, pi.dwProcessId).detach();
        CloseHandle(pi.hThread);
    }
    else { DWORD e = GetLastError(); if (hLog != INVALID_HANDLE_VALUE) CloseHandle(hLog); SendError(L"CreateProcess failed: " + std::to_wstring(e)); }
}

void TerminateGame() {
    DWORD pid = g_GamePID; if (pid != 0) {
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid); if (h) { TerminateProcess(h, 0); CloseHandle(h); } g_GamePID = 0;
    }
}

void InstallAndLaunchThread() {
    g_CancelDownload = false;
    std::wstring base = GetBaseDir(), md = GetMinecraftDir(), modd = GetModsDir();
    fs::create_directories(base); fs::create_directories(md); fs::create_directories(modd);
    bool ok = true;
    if (ok) ok = DownloadFile(JRE_URL, base + L"jre.zip", g_LangRu ? L"\u0421\u043A\u0430\u0447\u0438\u0432\u0430\u043D\u0438\u0435 Java..." : L"Downloading Java...");
    if (ok) {
        SendProgress(100, 0, 0, g_LangRu ? L"\u0420\u0430\u0441\u043F\u0430\u043A\u043E\u0432\u043A\u0430 Java..." : L"Extracting Java...");
        if (fs::exists(base + L"jre")) fs::remove_all(base + L"jre");
        ok = UnzipWithPowerShell(base + L"jre.zip", base + L"jre"); fs::remove(base + L"jre.zip");
    }
    if (ok) ok = DownloadFile(MOD_URL, modd + L"codex-.001.jar", g_LangRu ? L"\u0421\u043A\u0430\u0447\u0438\u0432\u0430\u043D\u0438\u0435 \u043C\u043E\u0434\u0430..." : L"Downloading mod...");
    if (ok) ok = DownloadFile(ADD_MOD_URL, modd + L"fabric-api-0.119.4-1.21.4.jar", g_LangRu ? L"\u0421\u043A\u0430\u0447\u0438\u0432\u0430\u043D\u0438\u0435 Fabric API..." : L"Downloading Fabric API...");
    if (ok) ok = DownloadFile(GAME_URL, base + L"game.zip", g_LangRu ? L"\u0421\u043A\u0430\u0447\u0438\u0432\u0430\u043D\u0438\u0435 Minecraft..." : L"Downloading Minecraft...");
    if (ok) {
        SendProgress(100, 0, 0, g_LangRu ? L"\u0420\u0430\u0441\u043F\u0430\u043A\u043E\u0432\u043A\u0430 Minecraft..." : L"Extracting Minecraft...");
        ok = UnzipWithPowerShell(base + L"game.zip", md); fs::remove(base + L"game.zip");
    }
    if (ok) ok = DownloadFile(ASSETS_URL, base + L"assets.zip", g_LangRu ? L"\u0421\u043A\u0430\u0447\u0438\u0432\u0430\u043D\u0438\u0435 \u0430\u0441\u0441\u0435\u0442\u043E\u0432..." : L"Downloading assets...");
    if (ok) {
        SendProgress(100, 0, 0, g_LangRu ? L"\u0420\u0430\u0441\u043F\u0430\u043A\u043E\u0432\u043A\u0430 \u0430\u0441\u0441\u0435\u0442\u043E\u0432..." : L"Extracting assets...");
        ok = UnzipWithPowerShell(base + L"assets.zip", md); fs::remove(base + L"assets.zip");
    }
    if (g_CancelDownload) return;
    if (ok) { std::wstring je = FindJavaExe(true); if (je.empty()) je = FindJavaExe(false); if (je.empty()) { SendError(L"Java not found after install"); ok = false; } }
    if (ok) {
        std::wstring vjson = GetVersionDir() + L"Fabric 1.21.4.json";
        std::wstring vjar = GetVersionDir() + L"Fabric 1.21.4.jar";
        if (!fs::exists(vjson)) SendError(L"Version JSON missing: " + vjson);
        if (!fs::exists(vjar)) SendError(L"Version JAR missing: " + vjar);
    }
    if (ok) {
        SaveRegistry(true, g_RamAmount, g_DarkTheme, g_LangRu, g_Nickname);
        SafePostJson(L"{ \"type\": \"finish_install\" }");
        std::this_thread::sleep_for(std::chrono::milliseconds(2500));
        LaunchGame();
    }
    else {
        SafePostJson(L"{ \"type\": \"progress\", \"status\": \"Error!\" }");
        SaveRegistry(false, g_RamAmount, g_DarkTheme, g_LangRu, g_Nickname);
    }
}

void StartProcessLogic() {
    if (g_GamePID != 0) { TerminateGame(); return; }
    RegState st = LoadRegistry();
    bool fe = fs::exists(GetVersionDir() + L"Fabric 1.21.4.jar") && (fs::exists(GetBaseDir() + L"jre"));
    if (st.isInstalled && fe) { SafePostJson(L"{ \"type\": \"launch_success\" }"); LaunchGame(); }
    else { SafePostJson(L"{ \"type\": \"start_load\" }"); std::thread(InstallAndLaunchThread).detach(); }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_WEBVIEW_UPDATE: { std::wstring* j = (std::wstring*)lParam; if (g_webview) g_webview->PostWebMessageAsJson(j->c_str()); delete j; return 0; }
    case WM_SIZE: if (g_webviewController) { RECT b; GetClientRect(hWnd, &b); g_webviewController->put_Bounds(b); } break;
    case WM_NCHITTEST: { POINT p = { LOWORD(lParam), HIWORD(lParam) }; ScreenToClient(hWnd, &p); if (p.y < 45 && p.x < 302) return HTCAPTION; return DefWindowProc(hWnd, message, wParam, lParam); }
    case WM_DESTROY: TerminateGame(); PostQuitMessage(0); break;
    default: return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    RegState saved = LoadRegistry();
    g_DarkTheme = saved.darkTheme; g_LangRu = saved.langRu; g_RamAmount = saved.ram;
    g_HasSavedPrefs = saved.hasPrefs; g_Nickname = saved.nickname;

    std::wstring themeColor = L"#FFFFFF";
    std::wstring themeColorRgb = L"255, 255, 255";
    std::wstring cheatNameUpper = CHEAT_NAME;
    std::transform(cheatNameUpper.begin(), cheatNameUpper.end(), cheatNameUpper.begin(), ::towupper);

    std::wstring css1 = LR"CSS(
<style>
:root{--green:)CSS" + themeColor + LR"CSS(;--bg:#111;--bg-dark:#050505;--bg-light:#F5F5F5;--bg-light-card:#FFF;--border:#2C2B2B;--border-light:#E0E0E0;--text-white:#FFF;--text-dark:#111;--orange:#FF9D00;--red:#FF6200;--btn-red:#D93025;--theme-rgb:)CSS" + themeColorRgb + LR"CSS(;--main-w:382px;--extra-w:220px;}
*{box-sizing:border-box;}
body{margin:0;padding:0;display:flex;justify-content:flex-start;align-items:center;height:100vh;font-family:'Montserrat',sans-serif;overflow:hidden;user-select:none;transition:background-color .4s;}
body.dark{background-color:var(--bg-dark);color:var(--text-white);}
body.light{background-color:var(--bg-light);color:var(--text-dark);}
.outer-container{display:flex;height:532px;position:relative;}
.wrapper{position:relative;width:var(--main-w);min-width:var(--main-w);height:532px;overflow:hidden;transition:background .4s,border-color .4s,box-shadow .4s;flex-shrink:0;}
body.dark .wrapper{background:var(--bg);border:1px solid var(--border);box-shadow:0 20px 60px rgba(0,0,0,.8);}
body.light .wrapper{background:var(--bg-light-card);border:1px solid var(--border-light);box-shadow:0 20px 60px rgba(0,0,0,.15);}
body.light{--green:#111111;--theme-rgb:17,17,17;}
.title-drag-area{position:absolute;top:0;left:0;width:100%;height:40px;z-index:999;cursor:default;}
.screen{position:absolute;top:0;left:0;width:100%;height:100%;transition:transform .6s cubic-bezier(.22,1,.36,1),opacity .4s,background .4s;display:flex;flex-direction:column;}
body.dark .screen{background:var(--bg);}body.light .screen{background:var(--bg-light-card);}
.screen.active{transform:translateX(0);opacity:1;z-index:2;pointer-events:all;}
.screen.inactive-left{transform:translateX(-100px) scale(.95);opacity:0;z-index:1;pointer-events:none;filter:blur(5px);}
.screen.inactive-right{transform:translateX(100%);opacity:0;z-index:1;pointer-events:none;}
.text-green{color:var(--green);}
body.dark .text-main{color:var(--text-white);}body.light .text-main{color:var(--text-dark);}
body.dark .text-faint{color:rgba(255,255,255,.2);}body.light .text-faint{color:rgba(0,0,0,.3);}
.font-unbounded{font-family:'Unbounded',sans-serif;font-weight:500;}
.font-medium{font-weight:500;}.font-semibold{font-weight:600;}
.window-controls{position:absolute;top:14px;right:14px;display:flex;gap:6px;z-index:1000;cursor:pointer;}
.win-btn{width:22px;height:22px;border-radius:7px;display:flex;align-items:center;justify-content:center;font-size:14px;line-height:1;font-weight:700;font-family:'Montserrat',sans-serif;transition:.2s;}
body.dark .win-btn{background:#212121;border:1.4px solid var(--border);color:#fff;}
body.light .win-btn{background:#F0F0F0;border:1.4px solid var(--border-light);color:#111;}
.win-btn:hover{opacity:.85;}
.win-close:hover{background:var(--btn-red);border-color:var(--btn-red);color:#fff;}
.header-title{position:absolute;top:40px;left:30px;display:flex;align-items:center;gap:12px;font-size:26px;line-height:32px;}
.logo-icon{width:32px;height:32px;object-fit:contain;filter:drop-shadow(0 0 5px rgba(var(--theme-rgb),.3));}
.version-row{position:absolute;top:90px;left:30px;font-size:22px;white-space:nowrap;}
)CSS";

    std::wstring css2 = LR"CSS(
.image-frame{position:absolute;top:125px;left:30px;width:322px;height:140px;border-radius:16px;background-color:#333;background-image:url('https://s4.iimage.su/s/08/geyw6HWxcE3UZ5b8Mfpu7SXbSsfCqNn4PWw8q8h6.jpg');background-size:cover;background-position:center;}
.description{position:absolute;top:300px;left:30px;width:322px;font-size:14px;line-height:18px;}
.btn-small{position:absolute;height:50px;border-radius:14px;display:flex;align-items:center;justify-content:center;font-size:20px;cursor:pointer;transition:.2s;user-select:none;}
body.dark .btn-small{background:#212121;border:1.4px solid var(--border);}body.light .btn-small{background:#F0F0F0;border:1.4px solid var(--border-light);}
.btn-small:hover{border-color:var(--green);}.btn-small:active{transform:scale(.96);}
.btn-site{width:88px;bottom:90px;left:30px;}.btn-settings{width:222px;bottom:90px;left:130px;}
.btn-launch{position:absolute;bottom:30px;left:30px;width:322px;height:50px;background:var(--green);border-radius:14px;display:flex;align-items:center;justify-content:center;font-size:24px;font-family:'Montserrat',sans-serif;font-weight:600;border:none;cursor:pointer;transition:.3s cubic-bezier(.25,.8,.25,1);}
body.dark .btn-launch{color:var(--bg);}body.light .btn-launch{color:#fff;}
.btn-launch:hover{opacity:.9;transform:translateY(-2px);box-shadow:0 5px 15px rgba(var(--theme-rgb),.3);}
.btn-launch:active{transform:scale(.98);}
.btn-launch.btn-quit-mode{background:var(--btn-red);color:#FFF;box-shadow:0 5px 15px rgba(217,48,37,.3);}
.screen-title{position:absolute;top:40px;left:30px;font-size:26px;font-family:'Unbounded',sans-serif;color:var(--green);}
.nick-group{position:absolute;top:90px;left:30px;width:322px;}
.nick-label{font-size:18px;margin-bottom:8px;display:block;color:var(--green);font-weight:600;}
.nick-row{display:flex;align-items:center;gap:8px;}
.nick-input{width:0;flex:1;height:38px;border-radius:10px;font-size:16px;font-family:'Montserrat',sans-serif;font-weight:600;padding:0 12px;outline:none;transition:border-color .2s;min-width:0;}
body.dark .nick-input{background:#212121;border:1.4px solid var(--border);color:var(--green);}
body.light .nick-input{background:#f0f0f0;border:1.4px solid var(--border-light);color:var(--text-dark);}
.nick-input:focus{border-color:var(--green);}
.nick-input::placeholder{color:#444;}body.light .nick-input::placeholder{color:#aaa;}
.btn-nick-save{flex-shrink:0;width:100px;height:38px;background:var(--green);border-radius:10px;display:flex;align-items:center;justify-content:center;font-size:15px;font-family:'Montserrat',sans-serif;font-weight:600;cursor:pointer;border:none;transition:.2s;}
body.dark .btn-nick-save{color:var(--bg);}body.light .btn-nick-save{color:#fff;}
.btn-nick-save:hover{opacity:.9;box-shadow:0 2px 10px rgba(var(--theme-rgb),.2);}
.btn-nick-save:active{transform:scale(.95);}
.ram-group{position:absolute;top:170px;left:30px;width:322px;}
.ram-header{display:flex;justify-content:space-between;align-items:flex-end;margin-bottom:8px;font-size:18px;}
.slider{-webkit-appearance:none;width:100%;height:12px;border-radius:10px;outline:none;margin:0;cursor:pointer;}
body.dark .slider{background:#212121;border:1.4px solid var(--border);}body.light .slider{background:#E0E0E0;border:1.4px solid var(--border-light);}
.slider::-webkit-slider-thumb{-webkit-appearance:none;width:36px;height:20px;background:var(--green);border-radius:10px;cursor:grab;box-shadow:0 0 10px rgba(var(--theme-rgb),.4);transition:transform .1s;}
body.dark .slider::-webkit-slider-thumb{border:2px solid var(--bg);}body.light .slider::-webkit-slider-thumb{border:2px solid #fff;}
.slider::-webkit-slider-thumb:hover{transform:scale(1.1);}
.btn-extra-settings{position:absolute;top:240px;left:30px;width:322px;height:44px;border-radius:12px;display:flex;align-items:center;justify-content:center;gap:8px;font-size:15px;font-family:'Montserrat',sans-serif;font-weight:600;cursor:pointer;transition:.2s;user-select:none;}
body.dark .btn-extra-settings{background:#1a1a1a;border:1.4px solid var(--border);color:var(--green);}
body.light .btn-extra-settings{background:#f0f0f0;border:1.4px solid var(--border-light);color:var(--green);}
.btn-extra-settings:hover{border-color:var(--green);}
.btn-extra-settings:active{transform:scale(.97);}
.btn-extra-settings svg{width:16px;height:16px;transition:transform .3s;}
.btn-extra-settings.open svg{transform:rotate(180deg);}
)CSS";

    std::wstring css3 = LR"CSS(
.btn-back{position:absolute;bottom:30px;left:30px;width:322px;height:50px;border-radius:14px;display:flex;align-items:center;justify-content:center;font-size:20px;cursor:pointer;transition:.2s;user-select:none;font-family:'Montserrat',sans-serif;font-weight:600;}
body.dark .btn-back{background:#212121;border:1.4px solid var(--border);}body.light .btn-back{background:#F0F0F0;border:1.4px solid var(--border-light);}
.btn-back:hover{border-color:var(--green);}.btn-back:active{transform:scale(.98);}
.btn-cancel{position:absolute;bottom:30px;left:30px;width:322px;height:40px;background:transparent;border-radius:12px;display:flex;align-items:center;justify-content:center;font-size:16px;color:#888;cursor:pointer;transition:.2s;user-select:none;font-family:'Montserrat',sans-serif;font-weight:600;}
body.dark .btn-cancel{border:1px solid var(--border);}body.light .btn-cancel{border:1px solid var(--border-light);}
.btn-cancel:hover{border-color:var(--btn-red);color:var(--btn-red);background:rgba(217,48,37,.05);}
.toast{position:absolute;bottom:40px;left:191px;transform:translateX(-50%) translateY(120px);backdrop-filter:blur(16px);border:1px solid rgba(255,255,255,.05);padding:14px 24px;border-radius:18px;font-size:14px;font-family:'Montserrat',sans-serif;font-weight:600;opacity:0;pointer-events:none;transition:all .6s cubic-bezier(.22,1,.36,1);z-index:100;display:flex;align-items:center;gap:14px;min-width:200px;white-space:nowrap;}
body.dark .toast{background:rgba(18,18,18,.9);color:#fff;box-shadow:0 0 0 1px rgba(0,0,0,1),0 20px 50px rgba(0,0,0,.8);}
body.light .toast{background:rgba(255,255,255,.95);color:#111;box-shadow:0 0 0 1px rgba(0,0,0,.05),0 20px 50px rgba(0,0,0,.15);}
.toast-icon{flex-shrink:0;width:32px;height:32px;background:linear-gradient(135deg,rgba(var(--theme-rgb),.2),rgba(var(--theme-rgb),.05));border:1px solid rgba(var(--theme-rgb),.3);border-radius:10px;display:flex;align-items:center;justify-content:center;color:var(--green);}
.toast-icon svg{width:16px;height:16px;stroke-width:2.5;}
.toast-content{display:flex;flex-direction:column;gap:2px;}
.toast.show{opacity:1;transform:translateX(-50%) translateY(0);}
.loader-subtitle{position:absolute;top:75px;left:30px;font-size:16px;color:rgba(var(--theme-rgb),.5);font-family:'Unbounded',sans-serif;}
.loader-image-large{position:absolute;top:120px;left:30px;width:322px;height:220px;border-radius:16px;background-image:url('https://i.pinimg.com/1200x/bd/d1/93/bdd193dd24d9d5cadd72494fd22aa8a3.jpg');background-size:cover;background-position:center;border:1px solid rgba(44,43,43,.81);display:flex;align-items:center;justify-content:center;}
.loader-stats-row{position:absolute;top:360px;left:30px;width:322px;display:flex;justify-content:space-between;font-family:'Unbounded',sans-serif;font-size:12px;color:var(--green);}
.loader-bar-bg-large{position:absolute;top:385px;left:30px;width:322px;height:16px;border-radius:8px;overflow:hidden;}
body.dark .loader-bar-bg-large{background:#212121;border:1px solid rgba(44,43,43,.81);}body.light .loader-bar-bg-large{background:#E0E0E0;border:1px solid var(--border-light);}
.loader-bar-fill-large{height:100%;width:0%;background:var(--green);border-radius:8px;transition:width .1s linear;}
)CSS";

    std::wstring css4 = LR"CSS(
.checkmark-container{display:none;width:100%;height:100%;background:rgba(0,0,0,.7);backdrop-filter:blur(4px);align-items:center;justify-content:center;border-radius:16px;}
.checkmark-svg{width:56px;height:56px;border-radius:50%;display:block;stroke-width:2;stroke:var(--green);stroke-miterlimit:10;animation:fill .4s ease-in-out .4s forwards,scale .3s ease-in-out .9s both;}
.checkmark-circle{stroke-dasharray:166;stroke-dashoffset:166;stroke-width:2;stroke-miterlimit:10;stroke:var(--green);fill:none;animation:stroke .6s cubic-bezier(.65,0,.45,1) forwards;}
.checkmark-check{transform-origin:50% 50%;stroke-dasharray:48;stroke-dashoffset:48;animation:stroke .3s cubic-bezier(.65,0,.45,1) .6s forwards;}
@keyframes stroke{100%{stroke-dashoffset:0;}}
@keyframes scale{0%,100%{transform:none;}50%{transform:scale3d(1.1,1.1,1);}}
@keyframes fill{100%{box-shadow:inset 0 0 0 30px transparent;}}
.error-log{position:absolute;bottom:80px;left:30px;width:300px;max-height:100px;overflow-y:auto;font-size:11px;color:#ff6666;font-family:monospace;background:rgba(255,0,0,.05);border:1px solid rgba(255,0,0,.2);border-radius:8px;padding:8px;display:none;word-break:break-all;box-sizing:border-box;}
.welcome-screen{position:absolute;top:0;left:0;width:var(--main-w);height:100%;display:flex;flex-direction:column;align-items:center;justify-content:center;z-index:50;transition:opacity .5s,transform .5s;}
body.dark .welcome-screen{background:var(--bg);}body.light .welcome-screen{background:var(--bg-light-card);}
.welcome-screen.hidden{opacity:0;pointer-events:none;transform:scale(.95);}
.welcome-title{font-family:'Unbounded',sans-serif;font-size:28px;color:var(--green);margin-bottom:30px;}
.welcome-subtitle{font-size:14px;margin-bottom:24px;opacity:.6;}
.welcome-options{display:flex;flex-direction:column;gap:14px;width:280px;}
.welcome-row{display:flex;justify-content:space-between;align-items:center;padding:12px 16px;border-radius:12px;transition:background .3s,border-color .3s;}
body.dark .welcome-row{background:#1a1a1a;border:1px solid var(--border);}body.light .welcome-row{background:#f0f0f0;border:1px solid var(--border-light);}
.welcome-row-label{font-size:16px;font-weight:600;}
.welcome-toggle{display:flex;gap:6px;}
.toggle-btn{padding:6px 14px;border-radius:8px;cursor:pointer;font-size:13px;font-weight:600;transition:.2s;border:1px solid transparent;display:flex;align-items:center;gap:4px;}
.toggle-btn.active{background:var(--green);border-color:var(--green);}
body.dark .toggle-btn.active{color:var(--bg);}
body.light .toggle-btn.active{color:#fff;}
body.dark .toggle-btn:not(.active){background:#2a2a2a;color:#aaa;border-color:var(--border);}
body.light .toggle-btn:not(.active){background:#e0e0e0;color:#666;border-color:var(--border-light);}
.toggle-btn:hover:not(.active){border-color:var(--green);}
.toggle-btn svg{width:14px;height:14px;}
.welcome-continue{margin-top:20px;width:280px;height:48px;background:var(--green);border:none;border-radius:14px;font-size:18px;font-weight:600;cursor:pointer;transition:.3s;font-family:'Montserrat',sans-serif;}
body.dark .welcome-continue{color:var(--bg);}body.light .welcome-continue{color:#fff;}
.welcome-continue:hover{opacity:.9;transform:translateY(-2px);box-shadow:0 5px 15px rgba(var(--theme-rgb),.3);}
.welcome-continue:active{transform:scale(.98);}
)CSS";

    std::wstring css5 = LR"CSS(
.extra-panel{width:0;overflow:hidden;height:532px;transition:width .4s cubic-bezier(.22,1,.36,1),opacity .3s;opacity:0;flex-shrink:0;position:relative;}
.extra-panel.open{width:var(--extra-w);opacity:1;}
body.dark .extra-panel{background:var(--bg);border-top:1px solid var(--border);border-right:1px solid var(--border);border-bottom:1px solid var(--border);}
body.light .extra-panel{background:var(--bg-light-card);border-top:1px solid var(--border-light);border-right:1px solid var(--border-light);border-bottom:1px solid var(--border-light);}
.extra-panel-inner{width:var(--extra-w);padding:30px 20px;display:flex;flex-direction:column;gap:20px;}
.extra-title{font-family:'Unbounded',sans-serif;font-weight:700;font-size:20px;color:var(--green);text-transform:uppercase;letter-spacing:1px;line-height:1.2;}
.extra-divider{width:100%;height:1px;margin:4px 0;}
body.dark .extra-divider{background:var(--border);}body.light .extra-divider{background:var(--border-light);}
.extra-section{display:flex;flex-direction:column;gap:10px;}
.extra-section-label{font-size:13px;font-weight:600;color:var(--green);opacity:.7;text-transform:uppercase;letter-spacing:.5px;}
.extra-toggle-row{display:flex;gap:6px;}
</style>
)CSS";

    std::wstring svgMoon = LR"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 12.79A9 9 0 1111.21 3a7 7 0 009.79 9.79z"/></svg>)";
    std::wstring svgSun = LR"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="5"/><line x1="12" y1="1" x2="12" y2="3"/><line x1="12" y1="21" x2="12" y2="23"/><line x1="4.22" y1="4.22" x2="5.64" y2="5.64"/><line x1="18.36" y1="18.36" x2="19.78" y2="19.78"/><line x1="1" y1="12" x2="3" y2="12"/><line x1="21" y1="12" x2="23" y2="12"/><line x1="4.22" y1="19.78" x2="5.64" y2="18.36"/><line x1="18.36" y1="5.64" x2="19.78" y2="4.22"/></svg>)";
    std::wstring logoDataUri = L"data:image/png;base64,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";
    std::wstring svgChevron = LR"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><polyline points="9 18 15 12 9 6"></polyline></svg>)";

    std::wstring htmlBody = LR"HTML(
<div class="outer-container">
    <div class="wrapper">
        <div class="title-drag-area" onmousedown="window.chrome.webview.postMessage('drag_window')"></div>
        <div class="window-controls">
            <div class="win-btn win-min" onclick="window.chrome.webview.postMessage('minimize')">&#8722;</div>
            <div class="win-btn win-close" onclick="window.chrome.webview.postMessage('close')">&times;</div>
        </div>
        <div id="toast" class="toast"><div class="toast-icon"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"></polyline></svg></div><div class="toast-content"><span id="toast-title">Title</span><span id="toast-desc">Desc</span></div></div>

        <div id="welcome-screen" class="welcome-screen">
            <div class="welcome-title" id="welcomeTitle">Welcome</div>
            <div class="welcome-subtitle text-main" id="welcomeSubtitle">Choose your preferences</div>
            <div class="welcome-options">
                <div class="welcome-row">
                    <span class="welcome-row-label text-green" id="langLabel">Language</span>
                    <div class="welcome-toggle">
                        <div class="toggle-btn active" id="btnRu" onclick="setWelcomeLang('ru')">RU</div>
                        <div class="toggle-btn" id="btnEn" onclick="setWelcomeLang('en')">EN</div>
                    </div>
                </div>
                <div class="welcome-row">
                    <span class="welcome-row-label text-green" id="themeLabel">Theme</span>
                    <div class="welcome-toggle">
                        <div class="toggle-btn active" id="btnDark" onclick="setWelcomeTheme('dark')">)HTML" + svgMoon + LR"HTML(</div>
                        <div class="toggle-btn" id="btnLight" onclick="setWelcomeTheme('light')">)HTML" + svgSun + LR"HTML(</div>
                    </div>
                </div>
            </div>
            <button class="welcome-continue" id="welcomeContinueBtn" onclick="finishWelcome()">Continue</button>
        </div>

        <div id="main-screen" class="screen inactive-right">
            <div class="header-title font-unbounded text-green">
                <img class="logo-icon" src=")HTML" + logoDataUri + LR"HTML(" alt="logo">
                <span id="cheatNameTitle">EXAMPLE</span>
            </div>
            <div class="version-row font-unbounded text-green">Minecraft 1.21.4</div>
            <div class="image-frame"></div>
            <div class="description font-medium text-main" id="mainDesc">desc</div>
            <div class="btn-small btn-site font-semibold text-green" id="btnSiteText" onclick="window.open('https://codexvisuals.netlify.app')">Site</div>
            <div class="btn-small btn-settings font-semibold text-green" id="btnSettingsText" onclick="goToSettings()">Settings</div>
            <button id="mainLaunchBtn" class="btn-launch font-semibold" onclick="handleMainButton()">Launch</button>
        </div>

        <div id="settings-screen" class="screen inactive-right">
            <div class="screen-title text-green" id="settingsTitle">Settings</div>
            <div class="nick-group">
                <label class="nick-label" id="nickLabel">Nickname</label>
                <div class="nick-row">
                    <input type="text" id="nicknameInput" class="nick-input" placeholder="Player" maxlength="16" spellcheck="false" autocomplete="off">
                    <button class="btn-nick-save" id="btnNickSave" onclick="saveNickname()">Save</button>
                </div>
            </div>
            <div class="ram-group">
                <div class="ram-header font-semibold text-green"><span id="ramLabel">RAM</span><span id="ramValue">4028MB</span></div>
                <input type="range" min="1024" max="16384" value="4028" step="128" class="slider" id="ramSlider">
            </div>
            <div class="btn-extra-settings" id="btnExtraSettings" onclick="toggleExtraPanel()">
                <span id="extraSettLabel">Advanced Settings</span>
                )HTML" + svgChevron + LR"HTML(
            </div>
            <div class="btn-back font-semibold text-green" id="btnSaveExit" onclick="saveAndExitSettings()">Save & Exit</div>
        </div>

        <div id="loading-screen" class="screen inactive-right">
            <div class="screen-title text-green" id="loadingTitle">Loading</div>
            <div class="loader-subtitle" id="loaderStatus">Downloading...</div>
            <div class="loader-image-large">
                <div class="checkmark-container" id="successCheck">
                    <svg class="checkmark-svg" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 52 52"><circle class="checkmark-circle" cx="26" cy="26" r="25" fill="none"/><path class="checkmark-check" fill="none" d="M14.1 27.2l7.1 7.2 16.7-16.8"/></svg>
                </div>
            </div>
            <div class="loader-stats-row"><span id="currentMb">0.0MB</span><span id="totalMb">...</span></div>
            <div class="loader-bar-bg-large"><div class="loader-bar-fill-large" id="loaderFill"></div></div>
            <div class="error-log" id="errorLog"></div>
            <div class="btn-cancel" id="btnCancelText" onclick="cancelInstall()">Cancel</div>
        </div>
    </div>

    <div class="extra-panel" id="extraPanel">
        <div class="extra-panel-inner">
            <div class="extra-title" id="extraPanelTitle">)HTML" + cheatNameUpper + LR"HTML(</div>
            <div class="extra-divider"></div>
            <div class="extra-section">
                <div class="extra-section-label" id="extraThemeLabel">Theme</div>
                <div class="extra-toggle-row">
                    <div class="toggle-btn" id="settBtnDark" onclick="toggleTheme('dark')">)HTML" + svgMoon + LR"HTML(</div>
                    <div class="toggle-btn" id="settBtnLight" onclick="toggleTheme('light')">)HTML" + svgSun + LR"HTML(</div>
                </div>
            </div>
            <div class="extra-section">
                <div class="extra-section-label" id="extraLangLabel">Language</div>
                <div class="extra-toggle-row">
                    <div class="toggle-btn" id="settBtnRu" onclick="toggleLang('ru')">RU</div>
                    <div class="toggle-btn" id="settBtnEn" onclick="toggleLang('en')">EN</div>
                </div>
            </div>
        </div>
    </div>
</div>
)HTML";

    std::wstring js1 = LR"JS(
<script>
const mainScreen=document.getElementById('main-screen'),settingsScreen=document.getElementById('settings-screen'),loadingScreen=document.getElementById('loading-screen'),welcomeScreen=document.getElementById('welcome-screen'),extraPanel=document.getElementById('extraPanel');
let isGameRunning=false,currentLang='ru',currentTheme='dark',currentNickname='Player',extraPanelOpen=false;
const L={ru:{welcome:'Добро пожаловать',choosePrefs:'Выберите настройки',language:'Язык',theme:'Тема',continue_:'Продолжить',settings:'Настройки',ram:'Оперативная память',saveExit:'Сохранить и выйти',site:'Сайт',launch:'Запустить',terminate:'Завершить',cancel:'Отменить',loading:'Загрузка',done:'Готово',desc:'Максимальная оптимизация, скорость и комфорт.',settingsSaved:'Конфигурация сохранена',launchedCache:'Запущен из кеша',gameTerminated:'Игра завершена',clientLaunched:'Клиент запущен',process:'Процесс',nickname:'Никнейм',save:'Сохранить',nickSaved:'Никнейм сохранён',nickEmpty:'Введите никнейм',extraSettings:'Доп. Настройки'},
en:{welcome:'Welcome',choosePrefs:'Choose your preferences',language:'Language',theme:'Theme',continue_:'Continue',settings:'Settings',ram:'RAM',saveExit:'Save & Exit',site:'Site',launch:'Launch',terminate:'Terminate',cancel:'Cancel',loading:'Loading',done:'Done',desc:'Maximum optimization, speed and comfort.',settingsSaved:'Configuration saved',launchedCache:'Launched from cache',gameTerminated:'Game terminated',clientLaunched:'Client launched',process:'Process',nickname:'Nickname',save:'Save',nickSaved:'Nickname saved',nickEmpty:'Enter a nickname',extraSettings:'Advanced Settings'}};
function t(k){return L[currentLang][k]||k;}
function refreshSlider(){const s=document.getElementById('ramSlider');updateSliderBackground(s.value,s.min,s.max);}
function applyLang(){
document.getElementById('welcomeTitle').innerText=t('welcome');document.getElementById('welcomeSubtitle').innerText=t('choosePrefs');
document.getElementById('langLabel').innerText=t('language');document.getElementById('themeLabel').innerText=t('theme');
document.getElementById('welcomeContinueBtn').innerText=t('continue_');document.getElementById('settingsTitle').innerText=t('settings');
document.getElementById('ramLabel').innerText=t('ram');document.getElementById('btnSaveExit').innerText=t('saveExit');
document.getElementById('btnSiteText').innerText=t('site');document.getElementById('btnSettingsText').innerText=t('settings');
document.getElementById('loadingTitle').innerText=t('loading');document.getElementById('btnCancelText').innerText=t('cancel');
document.getElementById('mainDesc').innerText=t('desc');
document.getElementById('nickLabel').innerText=t('nickname');
document.getElementById('btnNickSave').innerText=t('save');
document.getElementById('extraSettLabel').innerText=t('extraSettings');
document.getElementById('extraThemeLabel').innerText=t('theme');
document.getElementById('extraLangLabel').innerText=t('language');
const btn=document.getElementById('mainLaunchBtn');
if(!isGameRunning)btn.innerText=t('launch');else btn.innerText=t('terminate');}
function applyTheme(th){currentTheme=th;document.body.classList.remove('dark','light');document.body.classList.add(th);
document.getElementById('settBtnDark').classList.toggle('active',th==='dark');
document.getElementById('settBtnLight').classList.toggle('active',th==='light');refreshSlider();}
function applySettingsLang(lang){currentLang=lang;
document.getElementById('settBtnRu').classList.toggle('active',lang==='ru');
document.getElementById('settBtnEn').classList.toggle('active',lang==='en');applyLang();}
function setWelcomeLang(lang){currentLang=lang;document.getElementById('btnRu').classList.toggle('active',lang==='ru');document.getElementById('btnEn').classList.toggle('active',lang==='en');applyLang();}
function setWelcomeTheme(th){document.getElementById('btnDark').classList.toggle('active',th==='dark');document.getElementById('btnLight').classList.toggle('active',th==='light');applyTheme(th);}
function toggleTheme(th){applyTheme(th);window.chrome.webview.postMessage("set_theme:"+th);}
function toggleLang(lang){applySettingsLang(lang);window.chrome.webview.postMessage("set_lang:"+lang);}
function finishWelcome(){welcomeScreen.classList.add('hidden');mainScreen.classList.remove('inactive-right');mainScreen.classList.add('active');window.chrome.webview.postMessage("welcome_done:"+currentLang+":"+currentTheme);}
function goToSettings(){mainScreen.classList.remove('active');mainScreen.classList.add('inactive-left');settingsScreen.classList.remove('inactive-right');settingsScreen.classList.add('active');document.getElementById('nicknameInput').value=currentNickname;}
function toggleExtraPanel(){
    extraPanelOpen=!extraPanelOpen;
    const btn=document.getElementById('btnExtraSettings');
    if(extraPanelOpen){extraPanel.classList.add('open');btn.classList.add('open');window.chrome.webview.postMessage("extra_panel:open");}
    else{extraPanel.classList.remove('open');btn.classList.remove('open');window.chrome.webview.postMessage("extra_panel:close");}
}
function closeExtraPanel(){
    if(extraPanelOpen){extraPanelOpen=false;extraPanel.classList.remove('open');document.getElementById('btnExtraSettings').classList.remove('open');window.chrome.webview.postMessage("extra_panel:close");}
}
function saveNickname(){
    let nick=document.getElementById('nicknameInput').value.trim();
    nick=nick.replace(/[^A-Za-z0-9_]/g,'');
    if(nick.length===0){showToast(t('nickname'),t('nickEmpty'));return;}
    if(nick.length>16)nick=nick.substring(0,16);
    document.getElementById('nicknameInput').value=nick;
    currentNickname=nick;
    window.chrome.webview.postMessage("save_nick:"+nick);
    showToast(t('nickname'),t('nickSaved')+': '+nick);
}
function saveAndExitSettings(){closeExtraPanel();settingsScreen.classList.remove('active');settingsScreen.classList.add('inactive-right');mainScreen.classList.remove('inactive-left');mainScreen.classList.add('active');let ram=document.getElementById('ramSlider').value;window.chrome.webview.postMessage("save_ram:"+ram);showToast(t('settings'),t('settingsSaved'));}
function showToast(title,desc){const toast=document.getElementById('toast');document.getElementById('toast-title').innerText=title;document.getElementById('toast-desc').innerText=desc;toast.classList.add('show');setTimeout(()=>{toast.classList.remove('show');},3000);}
const slider=document.getElementById('ramSlider'),output=document.getElementById('ramValue');
function updateSliderBackground(v,mn,mx){const p=((v-mn)/(mx-mn))*100;const bg=currentTheme==='dark'?'#212121':'#E0E0E0';slider.style.background='linear-gradient(to right, var(--green) '+p+'%, '+bg+' '+p+'%)';}
slider.addEventListener('input',function(){output.innerHTML=this.value+"MB";updateSliderBackground(this.value,this.min,this.max);});
function handleMainButton(){window.chrome.webview.postMessage("action_button");}
function cancelInstall(){window.chrome.webview.postMessage("cancel_install");loadingScreen.classList.remove('active');loadingScreen.classList.add('inactive-right');mainScreen.classList.remove('inactive-left');mainScreen.classList.add('active');}
function setRunningState(r){isGameRunning=r;const btn=document.getElementById('mainLaunchBtn');if(r){btn.innerText=t('terminate');btn.classList.add('btn-quit-mode');}else{btn.innerText=t('launch');btn.classList.remove('btn-quit-mode');}}
)JS";

    std::wstring js2 = LR"JS(
function startLoadingUI(){closeExtraPanel();mainScreen.classList.remove('active');mainScreen.classList.add('inactive-left');loadingScreen.classList.remove('inactive-right');loadingScreen.classList.add('active');document.getElementById('successCheck').style.display='none';document.getElementById('loaderFill').style.width='0%';document.getElementById('errorLog').style.display='none';document.getElementById('errorLog').innerText='';}
function updateProgress(p,c,tot,s){document.getElementById('loaderFill').style.width=p+'%';document.getElementById('currentMb').innerText=c;document.getElementById('totalMb').innerText=tot;document.getElementById('loaderStatus').innerText=s;}
function showError(m){const el=document.getElementById('errorLog');el.style.display='block';el.innerText+=m+'\n';el.scrollTop=el.scrollHeight;}
function finishLoading(){document.getElementById('loaderStatus').innerText=t('done');document.getElementById('loaderStatus').style.color='var(--green)';document.getElementById('successCheck').style.display='flex';setTimeout(()=>{loadingScreen.classList.remove('active');loadingScreen.classList.add('inactive-right');mainScreen.classList.remove('inactive-left');mainScreen.classList.add('active');setRunningState(true);showToast(t('done'),t('clientLaunched'));},2500);}
function skipWelcome(lang,theme,nick){currentLang=lang;currentNickname=nick||'Player';applyTheme(theme);applySettingsLang(lang);applyLang();welcomeScreen.classList.add('hidden');mainScreen.classList.remove('inactive-right');mainScreen.classList.add('active');}
window.chrome.webview.addEventListener('message',event=>{const msg=event.data;
if(msg.type==='progress'){updateProgress(msg.percent,msg.current,msg.total,msg.status);}
else if(msg.type==='finish_install'){finishLoading();}
else if(msg.type==='set_ram'){slider.value=msg.value;output.innerText=msg.value+"MB";updateSliderBackground(slider.value,slider.min,slider.max);}
else if(msg.type==='launch_success'){setRunningState(true);showToast(t('process'),t('launchedCache'));}
else if(msg.type==='start_load'){startLoadingUI();}
else if(msg.type==='process_stopped'){setRunningState(false);showToast(t('process'),t('gameTerminated'));}
else if(msg.type==='error'){showError(msg.message);}
else if(msg.type==='init_settings'){skipWelcome(msg.lang,msg.theme,msg.nickname);}
else if(msg.type==='set_nickname'){currentNickname=msg.value;document.getElementById('nicknameInput').value=msg.value;}
});
applyLang();applyTheme('dark');
</script>
)JS";

    std::wstring html =
        L"<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\">"
        L"<link href=\"https://fonts.googleapis.com/css2?family=Montserrat:wght@500;600;700&family=Unbounded:wght@400;500;700&display=swap\" rel=\"stylesheet\">" +
        css1 + css2 + css3 + css4 + css5 + L"</head><body class=\"dark\">" + htmlBody + js1 + js2 + L"</body></html>";

    std::wstring ph = L"EXAMPLE";
    size_t pos = 0;
    while ((pos = html.find(ph, pos)) != std::wstring::npos) {
        html.replace(pos, ph.length(), cheatNameUpper);
        pos += cheatNameUpper.length();
    }

    WNDCLASSEXW wcex = { sizeof(WNDCLASSEX) }; wcex.style = CS_HREDRAW | CS_VREDRAW; wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance; wcex.hCursor = LoadCursor(nullptr, IDC_ARROW); wcex.lpszClassName = L"LauncherClass";
    RegisterClassExW(&wcex);

    int sW = GetSystemMetrics(SM_CXSCREEN), sH = GetSystemMetrics(SM_CYSCREEN);
    g_hWnd = CreateWindowExW(WS_EX_LAYERED, L"LauncherClass", CHEAT_NAME.c_str(), WS_POPUP | WS_VISIBLE,
        (sW - MAIN_WIDTH) / 2, (sH - MAIN_HEIGHT) / 2, MAIN_WIDTH, MAIN_HEIGHT, nullptr, nullptr, hInstance, nullptr);
    DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;
    DwmSetWindowAttribute(g_hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
    SetLayeredWindowAttributes(g_hWnd, 0, 255, LWA_ALPHA);

    CreateCoreWebView2EnvironmentWithOptions(nullptr, (GetBaseDir() + L"cache").c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [html, saved](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                env->CreateCoreWebView2Controller(g_hWnd, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [html, saved](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                        if (controller) { g_webviewController = controller; g_webviewController->get_CoreWebView2(&g_webview); }
                        wil::com_ptr<ICoreWebView2Settings> settings; g_webview->get_Settings(&settings);
                        settings->put_AreDefaultContextMenusEnabled(FALSE); settings->put_AreDevToolsEnabled(FALSE);
                        RECT b; GetClientRect(g_hWnd, &b); g_webviewController->put_Bounds(b);
                        g_webview->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                            [](ICoreWebView2* wv, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                LPWSTR pw; args->TryGetWebMessageAsString(&pw); std::wstring msg(pw); CoTaskMemFree(pw);
                                if (msg == L"close") DestroyWindow(g_hWnd);
                                else if (msg == L"minimize") ShowWindow(g_hWnd, SW_MINIMIZE);
                                else if (msg == L"drag_window") { ReleaseCapture(); SendMessage(g_hWnd, WM_NCLBUTTONDOWN, HTCAPTION, 0); }
                                else if (msg == L"action_button") StartProcessLogic();
                                else if (msg == L"cancel_install") g_CancelDownload = true;
                                else if (msg == L"extra_panel:open") ResizeWindow(true);
                                else if (msg == L"extra_panel:close") ResizeWindow(false);
                                else if (msg.find(L"save_nick:") == 0) {
                                    std::wstring nick = msg.substr(10); std::wstring safe;
                                    for (wchar_t c : nick) {
                                        if ((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L'_') safe += c;
                                    }
                                    if (safe.empty()) safe = L"Player"; if (safe.length() > 16) safe = safe.substr(0, 16);
                                    g_Nickname = safe;
                                    SaveRegistry(LoadRegistry().isInstalled, g_RamAmount, g_DarkTheme, g_LangRu, g_Nickname);
                                    std::string nickUtf8 = SanitizeNicknameForJson(safe);
                                    std::wstring js = L"{ \"type\": \"set_nickname\", \"value\": \"" + Utf8ToWide(nickUtf8) + L"\" }";
                                    g_webview->PostWebMessageAsJson(js.c_str());
                                }
                                else if (msg.find(L"save_ram:") == 0) {
                                    int requested = std::stoi(msg.substr(9));
                                    int safe = GetSafeRamAmount(requested);
                                    g_RamAmount = safe;
                                    SaveRegistry(LoadRegistry().isInstalled, g_RamAmount, g_DarkTheme, g_LangRu, g_Nickname);
                                    if (safe != requested) {
                                        std::wstring js = L"{ \"type\": \"set_ram\", \"value\": " + std::to_wstring(safe) + L" }";
                                        g_webview->PostWebMessageAsJson(js.c_str());
                                    }
                                }
                                else if (msg.find(L"set_theme:") == 0) { g_DarkTheme = (msg.substr(10) == L"dark"); SaveRegistry(LoadRegistry().isInstalled, g_RamAmount, g_DarkTheme, g_LangRu, g_Nickname); }
                                else if (msg.find(L"set_lang:") == 0) { g_LangRu = (msg.substr(9) == L"ru"); SaveRegistry(LoadRegistry().isInstalled, g_RamAmount, g_DarkTheme, g_LangRu, g_Nickname); }
                                else if (msg.find(L"welcome_done:") == 0) {
                                    std::wstring p = msg.substr(13); size_t s = p.find(L':');
                                    if (s != std::wstring::npos) { g_LangRu = (p.substr(0, s) == L"ru"); g_DarkTheme = (p.substr(s + 1) == L"dark"); }
                                    SaveRegistry(LoadRegistry().isInstalled, g_RamAmount, g_DarkTheme, g_LangRu, g_Nickname);
                                }
                                return S_OK;
                            }).Get(), nullptr);
                        g_webview->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
                            [saved](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                                std::wstring js = L"{ \"type\": \"set_ram\", \"value\": " + std::to_wstring(saved.ram) + L" }";
                                g_webview->PostWebMessageAsJson(js.c_str());
                                if (saved.hasPrefs) {
                                    std::wstring ls = saved.langRu ? L"ru" : L"en", ts = saved.darkTheme ? L"dark" : L"light";
                                    std::string nickJson = SanitizeNicknameForJson(saved.nickname);
                                    std::wstring initJs = L"{ \"type\": \"init_settings\", \"lang\": \"" + ls + L"\", \"theme\": \"" + ts + L"\", \"nickname\": \"" + Utf8ToWide(nickJson) + L"\" }";
                                    g_webview->PostWebMessageAsJson(initJs.c_str());
                                }
                                return S_OK;
                            }).Get(), nullptr);
                        g_webview->NavigateToString(html.c_str());
                        return S_OK;
                    }).Get());
                return S_OK;
            }).Get());

    MSG msg; while (GetMessage(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return (int)msg.wParam;
}