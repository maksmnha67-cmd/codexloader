#include <windows.h>
#include <wininet.h>
#include <shlobj.h>
#include <shellapi.h>
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
#include <cwctype>
#include <cctype>
#include <wrl.h>
#include <wil/com.h>
#include "WebView2.h"
#include <map>
#include <set>
#include <nlohmann/json.hpp>
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "shell32.lib")

using namespace Microsoft::WRL;
namespace fs = std::filesystem;

#define WM_WEBVIEW_UPDATE (WM_USER + 101)


std::wstring CHEAT_NAME = L"Dune Visuals";

// ==== ВЕРСИЯ МОДА ====
// Лаунчер сам спрашивает у GitHub, какой файл dune-.NNN.jar самый новый в релизе "visuals",
// и качает именно его. Тебе больше не нужно ничего трогать в коде — просто залей новый
// dune-.00X.jar в тот же релиз, и все лаунчеры подхватят его при следующем запуске.
// Это значение — просто запасной вариант на случай, если GitHub недоступен.
std::wstring MOD_VERSION = L"003";

const std::wstring JRE_URL = L"https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.10%2B7/OpenJDK21U-jre_x64_windows_hotspot_21.0.10_7.zip"; // Это JRE если надо замени
const std::wstring MOD_RELEASE_API_URL = L"https://api.github.com/repos/maksmnha67-cmd/dune-visuals/releases/tags/visuals";
std::wstring GetModUrl() { return L"https://github.com/maksmnha67-cmd/dune-visuals/releases/download/visuals/dune-." + MOD_VERSION + L".jar"; }
const std::wstring MOD_FILENAME = L"dune.jar"; // имя файла на диске всегда одно и то же, чтобы апдейт просто перезаписывал его
const std::wstring ADD_MOD_URL = L"https://www.dropbox.com/scl/fi/jmb2gzykcd467bj8ncknw/fabric-api-0.119.4-1.21.4.jar?rlkey=4bx8x9v9uq95i0nv7w3pmeiiw&st=05orip2n&dl=1"; // Это Fabric API
const std::wstring GAME_URL = L"https://github.com/maksmnha67-cmd/dune-visuals/releases/download/visuals/game.zip"; // versions/Fabric 1.21.4 + libraries
const std::wstring ASSETS_URL = L"https://github.com/maksmnha67-cmd/dune-visuals/releases/download/visuals/assets.zip"; // assets

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

const int SIDEBAR_WIDTH = 60;
const int MAIN_WIDTH = 382 + SIDEBAR_WIDTH;
const int MAIN_HEIGHT = 532;
const int EXTRA_WIDTH = 220;

struct RegState { bool isInstalled; int ram; bool darkTheme; bool langRu; bool hasPrefs; std::wstring nickname; std::wstring modVersion; };

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

void SaveRegistry(bool installed, int ram, bool darkTheme, bool langRu, const std::wstring& nickname, const std::wstring& modVersion = MOD_VERSION) {
    HKEY hKey; std::wstring regPath = L"SOFTWARE\\" + CHEAT_NAME;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, regPath.c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        std::string nickStr = SanitizeNicknameForStorage(nickname);
        std::string modVerStr = WideToUtf8(modVersion);
        std::string p;
        p += "RAM=" + std::to_string(ram) + ";";
        p += "STATUS=" + std::string(installed ? "INSTALLED_OK" : "NOT_INSTALLED") + ";";
        p += "THEME=" + std::string(darkTheme ? "DARK" : "LIGHT") + ";";
        p += "LANG=" + std::string(langRu ? "RU" : "EN") + ";";
        p += "NICK=" + nickStr + ";";
        p += "PREFS=YES;";
        p += "MODVER=" + modVerStr + ";";
        p += "CHECKSUM=" + std::to_string(ram * 31337 + (installed ? 99991 : 13579)) + ";";
        std::vector<BYTE> pv(p.begin(), p.end()); std::vector<BYTE> ev;
        if (AESEncrypt(pv, ev)) RegSetValueExW(hKey, L"Data", 0, REG_BINARY, ev.data(), (DWORD)ev.size());
        RegCloseKey(hKey);
    }
}

RegState LoadRegistry() {
    RegState state = { false, 4028, true, true, false, L"Player", L"" };
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
                    std::string rs = ex("RAM"), ss = ex("STATUS"), ts = ex("THEME"), ls = ex("LANG"), ns = ex("NICK"), ps = ex("PREFS"), cs = ex("CHECKSUM"), mvs = ex("MODVER");
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
                    if (!mvs.empty()) state.modVersion = Utf8ToWide(mvs);
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

// Удаляет старые версии главного мода из папки mods (например оставшийся с прошлых билдов
// "dune-.001.jar", а также хвосты со старого бренда "codex.jar"/"codex-.00X.jar" после
// ребрендинга), чтобы в игре не оказалось сразу двух версий одного и того же мода.
void CleanupOldModJars(const std::wstring& modsDir) {
    if (!fs::exists(modsDir)) return;
    std::error_code ec;
    for (auto& entry : fs::directory_iterator(modsDir, ec)) {
        if (!entry.is_regular_file()) continue;
        std::wstring fname = entry.path().filename().wstring();
        std::wstring lower = fname; std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
        bool looksLikeModJar = (lower.rfind(L"dune", 0) == 0 || lower.rfind(L"codex", 0) == 0)
            && lower.size() >= 4 && lower.substr(lower.size() - 4) == L".jar";
        if (looksLikeModJar && fname != MOD_FILENAME) {
            fs::remove(entry.path(), ec);
        }
    }
}

std::string FetchUrlToString(const std::wstring& url) {
    HINTERNET hI = InternetOpenW(L"Launcher/1.0", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hI) return "";
    DWORD timeout = 5000;
    InternetSetOptionW(hI, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionW(hI, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionW(hI, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    HINTERNET hU = InternetOpenUrlW(hI, url.c_str(), NULL, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hU) { InternetCloseHandle(hI); return ""; }
    std::string result; char buf[8192]; DWORD br;
    while (InternetReadFile(hU, buf, sizeof(buf), &br) && br > 0) result.append(buf, br);
    InternetCloseHandle(hU); InternetCloseHandle(hI);
    return result;
}

// Спрашивает у GitHub какие ассеты есть в релизе "visuals" и находит файл dune-.NNN.jar
// с самым большим номером. Если сеть/API недоступны — тихо оставляет запасную версию MOD_VERSION.
void RefreshLatestModVersion() {
    std::string body = FetchUrlToString(MOD_RELEASE_API_URL);
    if (body.empty()) return;
    try {
        auto j = nlohmann::json::parse(body);
        if (!j.contains("assets") || !j["assets"].is_array()) return;
        int best = -1; std::string bestStr;
        for (auto& a : j["assets"]) {
            std::string name = a.value("name", std::string());
            size_t p1 = name.find("dune-.");
            if (p1 == std::string::npos) continue;
            size_t start = p1 + 6;
            size_t p2 = name.find(".jar", start);
            if (p2 == std::string::npos || p2 <= start) continue;
            std::string numPart = name.substr(start, p2 - start);
            if (numPart.empty() || !std::all_of(numPart.begin(), numPart.end(), [](unsigned char c) { return std::isdigit(c); })) continue;
            int val = std::stoi(numPart);
            if (val > best) { best = val; bestStr = numPart; }
        }
        if (best >= 0) MOD_VERSION = std::wstring(bestStr.begin(), bestStr.end());
    }
    catch (...) { /* оставляем запасную версию как есть */ }
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
    of.close(); InternetCloseHandle(hU); InternetCloseHandle(hI);
    if (ts > 0 && tr != ts) {
        SendError(L"Download incomplete: " + std::to_wstring(tr) + L"/" + std::to_wstring(ts) + L" bytes");
        fs::remove(destPath);
        return false;
    }
    return true;
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
    CleanupOldModJars(modd);
    if (ok) ok = DownloadFile(GetModUrl(), modd + MOD_FILENAME, g_LangRu ? L"\u0421\u043A\u0430\u0447\u0438\u0432\u0430\u043D\u0438\u0435 \u043C\u043E\u0434\u0430..." : L"Downloading mod...");
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

// Лёгкий апдейт: перекачивает ТОЛЬКО мод (без JRE/Minecraft/assets) и потом запускает игру.
void UpdateModThread() {
    g_CancelDownload = false;
    std::wstring modd = GetModsDir();
    fs::create_directories(modd);
    CleanupOldModJars(modd);
    bool ok = DownloadFile(GetModUrl(), modd + MOD_FILENAME, g_LangRu ? L"\u041E\u0431\u043D\u043E\u0432\u043B\u0435\u043D\u0438\u0435 \u043C\u043E\u0434\u0430..." : L"Updating mod...");
    if (g_CancelDownload) return;
    if (ok) {
        RegState st = LoadRegistry();
        SaveRegistry(true, st.ram, st.darkTheme, st.langRu, st.nickname, MOD_VERSION);
        SafePostJson(L"{ \"type\": \"finish_install\" }");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        LaunchGame();
    }
    else {
        SafePostJson(L"{ \"type\": \"progress\", \"status\": \"Error!\" }");
    }
}

void StartProcessLogic() {
    if (g_GamePID != 0) { TerminateGame(); return; }
    RegState st = LoadRegistry();
    bool fe = fs::exists(GetVersionDir() + L"Fabric 1.21.4.jar") && (fs::exists(GetBaseDir() + L"jre"));
    bool modUpToDate = (st.modVersion == MOD_VERSION) && fs::exists(GetModsDir() + MOD_FILENAME);
    if (st.isInstalled && fe && modUpToDate) {
        SafePostJson(L"{ \"type\": \"launch_success\" }"); LaunchGame();
    }
    else if (st.isInstalled && fe && !modUpToDate) {
        // Minecraft/JRE уже стоят, но вышла новая версия мода — качаем только её.
        SafePostJson(L"{ \"type\": \"start_load\" }"); std::thread(UpdateModThread).detach();
    }
    else {
        SafePostJson(L"{ \"type\": \"start_load\" }"); std::thread(InstallAndLaunchThread).detach();
    }
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
    std::thread(RefreshLatestModVersion).detach(); // тихо спрашивает у GitHub актуальную версию мода, пока показывается окно

    std::wstring themeColor = L"#FFFFFF";
    std::wstring themeColorRgb = L"255, 255, 255";
    std::wstring cheatNameUpper = CHEAT_NAME;
    std::transform(cheatNameUpper.begin(), cheatNameUpper.end(), cheatNameUpper.begin(), ::towupper);

    std::wstring css1 = LR"CSS(
<style>
:root{--green:)CSS" + themeColor + LR"CSS(;--bg:#121214;--bg-dark:#08080a;--bg-light:#F5F5F5;--bg-light-card:#FFF;--border:#242428;--border-light:#E0E0E0;--text-white:#FFF;--text-dark:#111;--orange:#FF9D00;--red:#FF6200;--btn-red:#D93025;--theme-rgb:)CSS" + themeColorRgb + LR"CSS(;--main-w:382px;--extra-w:220px;--sidebar-w:60px;}
*{box-sizing:border-box;}
body{margin:0;padding:0;display:flex;justify-content:flex-start;align-items:center;height:100vh;font-family:'Montserrat',sans-serif;overflow:hidden;user-select:none;transition:background-color .4s;}
body.dark{background-color:var(--bg-dark);color:var(--text-white);}
body.light{background-color:var(--bg-light);color:var(--text-dark);}
.outer-container{display:flex;height:532px;position:relative;}
.wrapper{position:relative;width:calc(var(--main-w) + var(--sidebar-w));min-width:calc(var(--main-w) + var(--sidebar-w));height:532px;overflow:hidden;transition:background .4s,border-color .4s,box-shadow .4s;flex-shrink:0;}
.content-area{position:relative;margin-left:var(--sidebar-w);width:var(--main-w);height:100%;}
.sidebar{position:absolute;top:0;left:0;width:var(--sidebar-w);height:100%;display:flex;flex-direction:column;align-items:center;padding-top:22px;gap:10px;z-index:50;}
.sidebar-icon{width:36px;height:36px;border-radius:11px;display:flex;align-items:center;justify-content:center;cursor:pointer;transition:.2s;flex-shrink:0;}
.sidebar-icon svg{width:17px;height:17px;transition:.2s;}
body.dark .sidebar-icon{color:rgba(255,255,255,.32);}body.light .sidebar-icon{color:rgba(0,0,0,.32);}
.sidebar-icon:hover{color:var(--green);}
.sidebar-icon.active{background:var(--green);}
body.dark .sidebar-icon.active{color:var(--bg);}body.light .sidebar-icon.active{color:#fff;}
.sidebar-divider{width:24px;height:1px;flex-shrink:0;margin:2px 0;}
body.dark .sidebar-divider{background:var(--border);}body.light .sidebar-divider{background:var(--border-light);}
.wrapper.pre-welcome .sidebar{opacity:0;pointer-events:none;}
.icon-btn{width:30px;height:30px;border-radius:9px;display:flex;align-items:center;justify-content:center;cursor:pointer;transition:.2s;}
.icon-btn svg{width:14px;height:14px;}
body.dark .icon-btn{background:#1c1c20;border:1.4px solid var(--border);color:rgba(255,255,255,.5);}
body.light .icon-btn{background:#F0F0F0;border:1.4px solid var(--border-light);color:rgba(0,0,0,.5);}
.icon-btn:hover{color:var(--green);border-color:var(--green);}
.top-icons-row{position:absolute;top:16px;right:52px;display:flex;gap:8px;z-index:60;}
.status-pill{position:absolute;top:12px;right:12px;padding:5px 10px;border-radius:999px;font-size:11px;font-weight:600;letter-spacing:.3px;text-transform:uppercase;backdrop-filter:blur(6px);background:rgba(0,0,0,.45);border:1px solid rgba(255,255,255,.15);color:#fff;}
.btn-play-circle{position:absolute;bottom:12px;right:12px;width:38px;height:38px;border-radius:50%;display:flex;align-items:center;justify-content:center;cursor:pointer;transition:.2s cubic-bezier(.25,.8,.25,1);border:none;background:var(--green);}
body.dark .btn-play-circle{color:var(--bg);}body.light .btn-play-circle{color:#fff;}
.btn-play-circle svg{width:15px;height:15px;margin-left:2px;}
.btn-play-circle:hover{transform:scale(1.08);box-shadow:0 5px 15px rgba(var(--theme-rgb),.35);}
.btn-play-circle:active{transform:scale(.95);}
.btn-play-circle.btn-quit-mode{background:var(--btn-red);color:#fff;}
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
body.dark .win-btn{background:#1c1c20;border:1.4px solid var(--border);color:#fff;}
body.light .win-btn{background:#F0F0F0;border:1.4px solid var(--border-light);color:#111;}
.win-btn:hover{opacity:.85;}
.win-close:hover{background:var(--btn-red);border-color:var(--btn-red);color:#fff;}
.header-title{position:absolute;top:56px;left:30px;display:flex;align-items:center;gap:16px;font-size:26px;line-height:32px;}
.logo-icon{width:52px;height:52px;object-fit:contain;filter:drop-shadow(0 0 5px rgba(var(--theme-rgb),.3));}
.version-row{position:absolute;top:123px;left:30px;font-size:22px;white-space:nowrap;}
)CSS";

    std::wstring css2 = LR"CSS(
.image-frame{position:absolute;top:156px;left:30px;width:322px;height:140px;border-radius:16px;background-color:#333;background-image:url('https://raw.githubusercontent.com/maksmnha67-cmd/duneloader/main/1647606525_50-amiel-club-p-kartinki-mainkraft-postroiki-53.png');background-size:cover;background-position:center;overflow:visible;}
.description{position:absolute;top:335px;left:30px;width:322px;font-size:14px;line-height:18px;}
.btn-small{position:absolute;height:50px;border-radius:14px;display:flex;align-items:center;justify-content:center;font-size:20px;cursor:pointer;transition:.2s;user-select:none;}
body.dark .btn-small{background:#1c1c20;border:1.4px solid var(--border);}body.light .btn-small{background:#F0F0F0;border:1.4px solid var(--border-light);}
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
body.dark .nick-input{background:#1c1c20;border:1.4px solid var(--border);color:var(--green);}
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
body.dark .slider{background:#1c1c20;border:1.4px solid var(--border);}body.light .slider{background:#E0E0E0;border:1.4px solid var(--border-light);}
.slider::-webkit-slider-thumb{-webkit-appearance:none;width:36px;height:20px;background:var(--green);border-radius:10px;cursor:grab;box-shadow:0 0 10px rgba(var(--theme-rgb),.4);transition:transform .1s;}
body.dark .slider::-webkit-slider-thumb{border:2px solid var(--bg);}body.light .slider::-webkit-slider-thumb{border:2px solid #fff;}
.slider::-webkit-slider-thumb:hover{transform:scale(1.1);}
.btn-extra-settings{position:absolute;top:296px;left:30px;width:322px;height:44px;border-radius:12px;display:flex;align-items:center;justify-content:center;gap:8px;font-size:15px;font-family:'Montserrat',sans-serif;font-weight:600;cursor:pointer;transition:.2s;user-select:none;}
body.dark .btn-extra-settings{background:#18181b;border:1.4px solid var(--border);color:var(--green);}
body.light .btn-extra-settings{background:#f0f0f0;border:1.4px solid var(--border-light);color:var(--green);}
.btn-mods-pos{top:240px;}
.btn-extra-settings:hover{border-color:var(--green);}
.btn-extra-settings:active{transform:scale(.97);}
.btn-extra-settings svg{width:16px;height:16px;transition:transform .3s;}
.btn-extra-settings.open svg{transform:rotate(180deg);}
)CSS";

    std::wstring css3 = LR"CSS(
.btn-back{position:absolute;bottom:30px;left:30px;width:322px;height:50px;border-radius:14px;display:flex;align-items:center;justify-content:center;font-size:20px;cursor:pointer;transition:.2s;user-select:none;font-family:'Montserrat',sans-serif;font-weight:600;}
body.dark .btn-back{background:#1c1c20;border:1.4px solid var(--border);}body.light .btn-back{background:#F0F0F0;border:1.4px solid var(--border-light);}
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
body.dark .loader-bar-bg-large{background:#1c1c20;border:1px solid rgba(36,36,40,.81);}body.light .loader-bar-bg-large{background:#E0E0E0;border:1px solid var(--border-light);}
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
    std::wstring logoDataUri = std::wstring(L"data:image/png;base64,") + L"iVBORw0KGgoAAAANSUhEUgAAAgAAAAIACAYAAAD0eNT6AACK8klEQVR4nO3dd5wcR5k//qdC94SNyjkHS7JsS5ZzwplgYzDpfORwB/f9cUe844hHOIONwRzhyHccOdvYOIDBgJMcZVmyJVnBkqyctXFSd1fV74+enu2Z7Umbw+fNa/Fqpqenp6e366mqp6qIAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACAiIgN9wHA+MIYIyEEs21bxuJxWwrBGReMMWJaKZPL5ZxsNucq5WmttTHGDPchAwCMSQgAYFDFYjHW1NQk5y9Y0HrOOecsXLx4ydzWlgnTpk6dunDGjBkLLUs0K00WGcMZZ253V9e+I0cOv9DW3n4kle7qOnzoyOGNGzfs2rJ58+G29g7XyeVULpczWuvh/mgAAKMaAgAYcLZts2nTpjVcfsWVp/z9m9/y3kULF57CpZyYiCdmJhKJRsuyuDGGtNYkhCAioqCmr7XW2mjOSREZQ67rplOp7mOp7tRhy7LV0eNHDvzu9ju+9/BDDz+/fdu2o11dXZ7neWgmAACoEwIAGDCMMVq8dOnkd7ztHVddcPFFr1y4aPEliXiiSUjZyDnnQgjSSmttDOeMEeOMGGOktSatNTHGiDNOxIgYGU3aI2KcM0aklNFEWnuu7nbdrJPOZNs3P/fs7zZsfHbTH/9477rNmzbvbG9vd4f7HAAAjBYIAGBANDc3W696zWsv+PC/fvizUyZPW93Q2NhojCHOBCfyg4NAUNuPeowMkSGdDw6I/Ic1EfHCfw1pMpo8rT2utHHSXZ3bX9yz//mvf+0rn3/88cd2nDxxIpfNZNAqAABQAQIA6BcpJZ11znmnfvBDH3r7JS+59O3SslpjsRh3XJdsYfGo19SS2McYkWGGqMymfsuB0Z7nEmOMG2Mcpbz2LVs2/+GJxx//y5/uu2/jc89ufP7Y0aNoFQAAiIAAAOrGGCNjDDU1NVlvefvbX/6+933wMzNmzlyezeVkPB7nuVyONzY2knJVr9caYwoBQFRLQM+bEBlTnOgX3k4pRVJK4pyT67paKUXGGM0YI22019nZsX3nju0Pfu973/vJpo0bX9i1e3e76zgmvC/jH8QAnBEAgNEHAQDUJSj8J0yYYH3q059+75ve8taPJhIN04hIM86547gUj8col8uR5DJyH+VaAMKBAWMUeXUGQYBSihhjheMJfic/j5CINM9kMllLWt7xk8ef/e43v3XTE088tmnXrl2Hjh057mijioMBBAIAMM4gAICacc5Ja01TpkyxP/WpT7/r7/7+hk/Hk4lJShE3xvBYPE65XI601hSLxUi5qlBgR9by88KFb+F31rtADu8jSBoUQlAwoiA4RsYYcc61UoqUUp4xShutqbOjY/P6Z9avu+fuu+95+IEH/7Z7z4spFRpBEA4oEBAAwFiHAABqUmj2b2ySN3/plvdf/+rX/EeisbFZa61tO5bP1NcUj8concn6ffSerhgAlBayRUmBZKhsAgARWZZFWmvyPK/QAiCEKBTinueRJS1SWhERadd1tGVZknOmc9lseteOnY/8+Cc//vbaRx5Zd+jQgZNHjhzJRc06FJmoOMqUC74G+vOUvg+6WABGNgQAUBMhBAkh+Qc/+IHr/r9/ed9XJ0ycOItzybPZHI/FbJKSkeN4ROT3rbuuW9QFUEsA4G9oiAzLX5kmcrtwk32+tl8o+AstAUyQUoo458Q5o0w2o7XWOhaLkTFGu56nLSG8A3v3P7Vhw/rnH3rogT9u377tmUOHDrXtfvHFjJPL9YpAgvcqbSEYysCg13lkjBj1DlRG0kRJPectH9gZg+AAYARAAABVCeEXpi+/5tozvva1r/90xqyZK5XSxBgnKSVprSmbzVFDQwM5bo60NmRZViEJMFxghwvL6Nq1IWOCUQCFJyOPKyhYtNaklP9eXAi/W0AbYsTIU35QYtuSjCHyXJcYZ6SN0drzqCHZwF3X0UZ53YYxemb90w/f/+f7f797z+7de/fu23XwwP4jbW1tTntbm1Pr+QrlI/T6zJVeE95+AIMKRoyxWCwmYrGYiMfjwrZjPBazRSyesBLxeEMy2dAQi8ViliUtKaUQUsQkl1JYUnLGOHHGOTGmtVZKa08rpZRSnuu5TjabzeacXDbdnenMZNLZdCrtdHd3qY7ODsf1I8KqHyQqqAKAwYcAACoKCth58+Y13PKVr3z2yquuem/MjtnK05wYJyJTVPtmjBEFBX6oElpvjdkwU1TLL3ou9HhpF0MwqZAQsvC4ISKmel5jmMnPMWB0PmOQuDbccV1HCouYJGmYSZ84dnzb89u2bdm5ffveTZs2PXTo4MEXUqnu7sOHD6ePHj3idLR3aq1VT+QyuEr/VlksFmfJhiRvbW2VEydOtJPJpEgkkpZl2YlYLJa0Y3ZzU1Pz9JbW1hktrS0zmpuaJ7a0tk5saW5ubGhsiDckGhONLc3NE1pbJiWTDQkpRVxKIaVl2ZIz4iVvqkMfUhOR63jZdLq7O5PJpNraO453d3anTp440X3kyJH2/Qf27zh85PCWk8eO7ejq6mpPpdOpE8ePpQ8dPJRra2tTxujIPp7SVhYEBQCDBwEAlBUUqlwI/tkbP//Od7zzXV9rSCbjUgrueYoYExTVvWzyVXjt6aJ9VbuZFwUJEUmA4e2Cwj+qpm2MIc5F8CAVX+aGmGFkmC56vdFa59uoyfFcTYwoHo9JxhhppcnTOqtcN93Z3X1wx/Ztz+3YsWPzsSNHj7iuYxzX8bq7U6n29rYTJ46fOHz0yJHj6VQql8llVS6TU46b067rGuUpY8hvmQjem3POpJTMsiwupeR2LCbisZhoaW1tnDh50qTmpubmpqbG5kQsmbRjsYQdj8UtKZhl2TzRkGhsbWmZPG3a9HkzZ89eOqG1dWJzU2uLtK24FNIW+dkX/fo7+cWtH5+R6yryPI+UUv70y1rp/OgLTfkzFoRw4eCKMcbD555zzhljMt9FRJz3DN9gROS4rs6kM93t7W1te/ft2b596/b1e/fv2+3kstlUd7pz6/Obn92xY9vhI0eO5rRSXul3XfQdISAAGFAIAKCsoNC+/Mqrln7xllv/b9ny5RcwIp1Kp3k8ESceUaiH/91TOaay25R/cyKdnwcgqgUgqvZfPIwwcg6insEFLNy6wAqBTBAu+EsSGK39QpETFZqqvZhtS8Y5JyKKWzwoLKkzlXWOnzix58D+Ay92tLW1d6e73Ew6k3JyTncul005juO6SmvSxnBLMEHEiTEpbTuRiMUTyWSiMZFM2MlEMjZp4uSZU2dMn9XS3DyhsbGx0RY9f6yG/JJZGyKlDHmuS66nNGP+OTfGBMMkdbjw9vMhOGltSGtVSJzknHNDpmjeBZ7/fOFzn3+ocI6D1hNGjAwZ7ThOUWtQ/nvi+VYkLaWQ8XiCiDGKW5w4EW17YfeGTZufe27b5i3bXti544nOjo7DR44eSW3atOlQe1t7tvT7Kx31AQB9hwAAIrF88/7MWbNin7/5ln+95tpXfjIWi9n5BC5eWvhHFerlAoBKryk8x0yhGuofT3EtsLT2X7rfigFAvhYc7CfYvzHGT6jLN0GrQkIhI6WUDm9HjGnO/FwCz/W00oqEEDIei0vLkqSNX9MXgpMQ/kTGyhApRcQ4Ec8fg9L5RgpjSApGnibSfq2cPM8LJjnygmPlnOt8LbxQQOeTIDkRUVCGlyRJ8tJuE53/rOHFmAzpXm3yhfNDPcFR0MWjQ+eHGCPlqcLv5J8/Taa4i0hrrbkQxDnTREQxO2ZLmxPTRDknm81ms87ePXtOrHv66bt2vvDC33Zs375p165dR06cOOHt27s3G1wRQeInggGAvkMAAJGCvv9/ePd7zvr3T3zq15MnTZojhJBBZr3RPU3o5QIBU+d9uSggyLckhwvpyO3KKBcAEJleWfP+PqmnOyM0mkAESYX5WjWRP8KBMUZSyqKJibTRmhEjrfzaeH5nwUIG+a4JTkL6SYr+5/CL1XCfd7hQz7+Gh8918Dr/lf6ICb9LPb87Kg5qes5Jz6RJoRp6T2FKRMQMBd0UwWuC/3qe56/RQMUtLoVgTOcfC7L9iQrvwzkvDNMMzq1lSdKadC6X1YwbElJI7o9q8CzbloaIDh8+tGXPzl1P7zuw/8TT69b9bfu2besfXbv2YHt7uw5/rvB+AaA2CACglyAJq7V1Av/K177xkTfccMNNnk8Gz3HO/S4AokKWfmnBrHXxXP71FOSGmaBpuddra+sHLndp5wu9IDmQgr7toDhlREIUHvcDg3wuBC9ECPnnNBGxQr93IQGR855CMjgBxvi17aCgytdcC4Uo50VRCMsP7yv0WOQLepbvrgjXxoP9G+oJusJN//kH/P0FAYAxpDyvEMhwzsmQyXe7mMLAi95dLT3PBV0GflBCJDgvBDZBd0Hw/qEWALIsi6Tk5Hn+v6WwyDBNnnK14IIYZ+S6LhmtybJtzpkfoDhO7uShAwd2PPDQA7/dsnnL2g3r12/dvOm57o6OjsJ6D1JKyk8LXeniAAAiip6rFca1oJa5es2aaatWrb5Sa61d1+WWZRVnaFfZR+kG9SQBEvWMBIhqZag0s2CZvYffqLAfU3jOL8yJCdKeR8R4oRXE7z9nZExQEAb94Tz/b5Yvu/PBBfGewMX0BA3GEJlCLZX5iYrGz3Vg1DN6whhDLN9HUJQLaYLkyFChbPwgqef0sKJRk0FLCMu3MvB8cGKMP1LCL1w5KeVRvlOh9/kKH4IJ1/zDrSzaPz7T+zyHEgb9h7Xxg8P8+3jKJS44Mca5MoYkcRJCkmZaG0Pa00ozzjkTcuLc+QvPfduixed2tLcd3fvii49s2PjMw48/+tiDj659ZPsLO3akPM8LvScnrXuvRwEAPrQAQJGgsG1pbRUf+8Sn3viuf/zH/7Ysu9nka7BBIRLMxV+2+T/fB15IzIsoUFjo8vMniKGibUsL+dJkv9L3LW4lCDXpR31OCtWuQ9v5NVW/kO1JejOFV5R7rzDBeSGBMbxfo03hL67sZw8NWujpRWCFmn9RM3dJ/zyZ4nMUBDhBzZ/InychaC0IWhkYLz624haAnrfrSbLs+UzhQIERJ2OCwRSmZywAo0JuBfPzKQotJVyIQkuIMbqwXbD/cEuGUkoLIYgR09poads2aa29traThzY/9+wD+/bue3LtI4+sve2232xJdXfniHpyBYLuGwDogQAAigST/ixfvqLh1q9+/daXXHbZexzH0UIIXq7gjfp3ac09qvZfrlm/dNvSbP9y20c9X1NLQdCmXibwqFe4EI7aV6UWjHKNJEEYVQgcWCiE8PsCigc7hnMFCgVydEAVdC0QI2LGFPYXvHOlhpuiHAMu8i0/xXkH+b0UvX908Nj7vESdy/zrdJAfIaUkWwryPEOHDh3a+dRTj/72oQcfvu+hBx94ftNzzx4mol5dEgCAAABKiHyN7IYbblh005e+/PuW1onLOOeRGXXlAoCowr5SAFAtsIhS7rVRj5crhKNeV277cttWUikwKb+f/gcf1UZnlFd52+p5GNGFt9bFcy5EvnOZACDYT7nvyx+KyIgzQdlsWicSSRmLW3Ty+ImjL+7a/fyDDz3wo1/+8ud3bXzmmRNEZBAIAPRADgCE9GSHz1+4aOmMmTNXdHWlHM65XbplXwrtavsoHEXEDb/W15cbNVCp8Km3xl+9EK//fJh8CwQrqn1X32/UZ63W2tLX46y2bbnvrb8tKpUCykJwyogaGhu5nyzoUUtr69Rzzztr6vKVp154/oXn37f2wYd+dvvttz/49NPrDhH1DCVEIADjGQIAKODcDwAaGhrljBkzVxlDxAXvdY1UzN6vsfZfTbVCo95jqHf/taqnqyEqaKo12Km0z/B7RxVo4ffoTyBUSwAQ3i44tnKtNSV7r7jvasfEWU9Lg1KKPE9rpSVJKeXq1WuuWbVq9Uuuue6VGx78699++fOf//yOJ554/JDWWtd2bABjEwIA6GXe/PmTTlm67MLSYXwjVS2FW60jB+otkCsVILUU8PU215frVqj1s1eu8fbMiFirWgK+gQi2qn3HyvPysxoK4hYnxogrrUl5nj/kk1Hj0iWnXDRv1twzL7n0kretfWTtj77x9a//btu2bYco3zWA1gAYbxAAQEFwk508ZUrj7Hmz5+VvtjqYkKba62p9j/40uVd6rp6CsFwzfq2FcOl+KjWx11JY13oKqzXl15oDEf1c5YS/qPcLC89rULptrd0w1c5/2fMtBJHg5BrlD+PMz0dg2zZJIchxiXKuSw0tTckVLaedPX/h4lPPOff8635z22/+/J1vfuv/ujo7TgT7RWsAjBflpkuDcSh/42OzZs2aM3XKtCmu41C5BMB+7L/ov+X6rKOeq7R9vcdQ7r1q3XfUsQ2XqGMPmt7LFbrh50ub6cv9VNMznK/4vAzFxDzaaHJclxwnR8QYxWIxklKS53mUzeXIGH+SoFQ6TdlsjizLSq48/bSr3/++93/6j3/+849e87rXv5SIrL4EqACjFQIAKMUnTZq8uKEp2WKofFJZIPx8uUI7UFrYVCukSkVtX1qIRRVapQVcMLlPX4OJcoVmsN/SmnAthShjQa2Wld1fLQVz+DVRP6XHVO6Yo1Q7nnLfZ7AIUblj9r+36ByJcoFN6ffAmD9vg8yvbeAojzSFcxAMKW2ImCBDnDxlyPE8amptbjxj1eqX33TLrd/+6S9+/YXTV5252hiDCADGBQQAQERFTaos0dDQIoSwTb2T+ZeICgwGohY/EKLmFuiLqM9RSzJd+XNSX3fKYJ3TcgV1pZ9q+6v2XuUCt9IulIotSDXkZPQ8QETEKJVO89mzZy246uqXfuCeP/7xd5//4i2faJ0wYT5hmDSMcQgAoBRLJhKSM8aV6smS7qt6b/yDERyU2+dwNfVW+tzlPnvFQq9km5GqUktNtS6V0m16tT5oTdwY4oYKP8E0yuX3y8hoRnYsRpozamhskPF4Yt67/uHdH/vL3x75xVve/o6XNzQ09BoCCzBWIACAXuyYbQVFIytZVq/eQqavhexgtxJUSqar5X0H+/j6UsAP9NDGWt6z9HXVgph6zlu1YKiWx8s9Fg4kpLSos6uLMpkMJZNJam5uTp6y/JTzPnPjjT94dsuWJ9769refx3tWggIYMxAAQBHOObOkHNDRIeX67YdCPYVZ6baD1UJRqRk9qi+83Gv7YjhbXSq9V08OROX9lPvsLP9DxkT/3mvLYNEkRp5S1JhsIGHblHVdSueypDnRrBkzpzU1T1j5nzfe+L+f+/znr582fXq8rpMAMMIhAIAihoiUMlV7omspMEpv1H0puAaiYCpXGJUaqMCkXJN2fwracn3yA22guxZqCTKC5Md6X1ftPXs/VvYFlMlkSAhBsViMtNbkui51dnVTLBaTEyZOWvEP73739++5995fX3rZ5bNisRjumzAm4EKGYsaQUp7nz7De312Vr1HX+/rBMBj7L1fwl/67rzXxway1V9PfVpFy8y3Us6+6318zMpqImfzSytoQy+cHMOMvguS6LtmxGOVch7z8hEJSSGpubiRjDGkiamxqmjh77rwrvvP97995y5e+9I7Gpkar1vMGMFIhAIAixhjjum7WXxGXkQ4NBehLgVPu5lxLQTiY3QThPuCB3mfwe7l/VyvEyh1TpSb1gQgIqhW8/Xl9ue0rHX8tSYP9/9z+MtfaaD8gyL+X67qUTmXzwwsZ5XI5amhoTE6fPn3129729lt+8L8//MKcOXMn9eONAYYdAgAoZdJdXV3acdNSSk5lCqXwjGnlC2pGjHHy8whZXT/BOu7VkvXKFQTV+sqr7b8vonIdoo6nliF0UZ+rtCCMZvrx01+GjNHkx4zF+/b7+Cv9sF7Xicn30QfXUPin9HrxK/em8FN4Z26IOJFhpvCjw//Lx7fMGJKMkXZdkoyTZJw498+39gwlYklyHJfi8SRXxFtf9brXfOjOu+7++fkXXrh8AOfKAhhSuHKhlE5lMicc100brct2A5SrjdZjKJMBh8NI6eoYCeqtwVcL/mp5v/4ca5hSirTWhZkFLcviHe1dtHTZ8iu/9/0ffOfNb33rZa0TJiT6/IYAwwQBABBR0U3PdHd2HnacbMqYnmTA0pnvBlJxbXhAdz0o6inMxkPhHqWv3QHV9lVum6iAodYuAmYMMV0+oBVCkOd5RETkOA5ZlkXJZJJ3dXXRnLlzL7j5llvu+OrXv37zjJkzC10CYzmwhbEDAQAU5G9apq3t5OF0Ot0mpCRjjB6sIiz6Jlk8HG7g+3x9g7lviFZL3kdpnkSt0zYPVg6EMYYY5+R6HkkpSUpJ6XSaOOfU2NjEc7kcTySS8ete9eq3/vDHP/7G6avOmBu8Dl0DMNLhCoWCoEA+eeJkx8m2tmMyP3/7YL/nSKstDXTCWX+T6+p9r5ES3NT7npHj+2u8PqolEtaq0rlzHIeklMQYo2w2S0JwkpbFubBsbVjz+Rdd8vc/+vFPf/CSyy5fxhhjWusRd20DhCEAgILg5nno8MH03hf3POsPBGCDOgXaeKl1l37O8fK5iWr7rKUJkfWsM1D6HuVq8n0NilzXpUQiQdoYymazRSMFBPeTEpXWXCvtLVi05OJvffu7v3jnP/zjlc3NzQItATCS4cqEXvbt25fesnnLk1pr4pyTNkaHb5aVbsiVbsLltu39ONHAZKWPPMNd8NfaDD8crQdRqxf29/372mIT/uGck9aahBD5ibKUv/KglGRZFrlKUTyZIMa4VErJGbNnr/r8zV/85b9/7BMfmDhxYiy8CiPASDKgU77C6McYI9d1TUdXR7tSSjPGCkMBS7eLurkO5uJBI0V/Vr6DaKXnrGx/fJlzH5UAGDxWrSm+2vdlWRZlMplCDoDWmsgYyuVypIwhLiVxxsnTLgkhuee52rbtxn98z3s+3tzSor/ypVu+t3v3rtRgJdEC9BVaAKBIcHNKpbrT2Wz2pK6j+tKXJWL7W8usNK5+sGqu1Y673Ix3I109cxUMdAtBX+dHKH3vqGug0uuMMUTGFM0qUMp1XbJtmzjn5Hme/zoiYpyTJSUxxslxeoIMKS1OxG0urea3vPVtX7j1q1/95ulnrJoevB/yAmCkQAAABeH+yl07d+0+cujwZskFNyWzAVYr9EaC0VTwwsAJdxvU0/ReGhCEhSdgigqESGvirPg1QgiyhJCO4/CXvuzlb/n6N77+n2eedda04DUj7e8FxicEAFAkuDHt27e37ciRQ1sZL27qH4wZ9AD6KyrJMurxSoqSEKu8V/i//uBZ3StrhXFOlm3LdDqjV5919ju//OX/+toZq1bNr+lgAIYAAgAoEtzUMpm0bu/oatclzavBf2uZpnckQvAyNvXrmiwzhXPpOgTBfsL/zf+jaKGh0MMkhORcSJ7Nuvqc886//qabvnTzgoULp4/Uvw8YXxAAQJHgxnTs2DFv794Xd7m5XLcQghORDp6vt3l1uNUznAwG1lCOIggbiO+6NCegXGJiUPXPZx2EnySlFMXsGJdS8lQmzS+57PLXfvm/vnHzwkWLZhERhgjCsMLVB0WCm1xnR4e3edNzG9KZ9DEuRK/m1VqGBAIMlWoJidWGp0Y9GgxFLLevQqM/M8SC987vTWtNKr+Noz2ybJsLLnh3dxdd8/JXvO2mm75085QpU5uCobYAwwFXHvQS3JB2vvDCrgMHD2xRqmdMNkCthvN6Ca7XoBCvOAxQazIRcxAUni8TUIRDA5Nv+zfakNb+ioTKaGL+PBrkei7Zdoxbls27u1PeS1/2itd84eZbPjxp0qSGcPItwFDCVQdlPfvsxrbNz256WHBGlmUVmkOlECTzrQKVhmL5S8MGzxX/+JnQPcvBlj5fTbUm/aFu9q90LuoRfS56zlXpuSxeTrf6T/i46p1tL+r11bavdXhf1Geu76fyexe9U+l3Fb4QQz/BnktfH+yTGyrU+jVRYbnh4HsRxuQXGvIDDCJN0hJcWEJqo+PXv+61H/z4pz79gYaGRolpg2E4IACAXoKbUdvJk2rf3r3bmCGt8uOfpRDEGCtMhhI1SVCP3gVUAPe6vil33hgzxFil7yK87fg5+YMZBFbca36oX7CkthSCtFZkjCEhBTHBOGMs+cY3vfkDH/jwv70lkUjK4HgBhgoCAIgU3Ii2bNn83KGD+x/lnPOg4FdK1Xijil7cBSqLqnGW05OU6f9US7ob6POP77O88HfgdykUz0zIGOPxRGLiu//pPZ9981vfdm2Qa4NzCkMFAQBUtGXLpoPbd2zbZFkWMcZ0sC66yLcEVJqtLcpA9QtXmtmtlp+RplITfaXtKhnoz1kpqBjNhVZ/r5tarqlgOGGYEJK7nuINjU3T/uPTn/nyy1/xygvZaD6RMOogAIBIwc1qx44duS2bn38mm81ktdZcCKGFEOS6btWbXrkb6EgthIdLX+75tfbBD6XRHgjUotJ1W0vgW9QqYDQlbJtIGdnY1LToxpu+cMtpp69ahL8NGCoIACBSkJmcy2b11ue3PJ7u7t4XPKaUIqLiCVPCr4v6vdJjEK2WBLpaWw7KbT/Yxz0W1Rr4lntdzwyCLOhO48YYmjdv/un/+4MffGvWrFkTB/6oAXpDAABlBTfw3bt3Hz969OhuIQQppbjWmqSUoRs8i/yJKgDGaqHQV5UK6lLhuRdKC9nas+0rvy/01tegNSoQKG0JsyybpLT8UQWMJZeecsoVn/jUp9+dTDZgpVYYdAgAoKzgRrVp06aOTZu3PKe11owxklKS53k1zwZYrpYKxfoaMFUq/Ac7CBjrtf3AYLRcMcbIUx5ppUgKSUTEtSF61fXX//M73/muyzjnY/ukwrBDAABlaa1JCEFHjxxOvbh716Na66zneWTbstfQvqgCJ3i+1iSpsV6IBCqNT+ec95oUpjDuvMzj9bxf6U/wfuFFnur5KT2+Si0TlY6pv8pdYwOx/9L9hq/p/rYOGDLEJSOdnzNDCMEbG5umvfu9773lrW97+2nj5W8ChgcCAKjJH+69Z+3mTZsficVilEqlSQiJ2cv6oJa++Wp5FKX7Kc3B6Gsme19F7Xuocz2iAiei8tP59kVUIl/w73pHCRReE5qpMAgshBBy9qzZK9/9//75ixddfMncATl4gAi4g0NFwc3zySceP/7sxg1/NcZoIaQ/1ao//VnEq3rXVMe7aol5gVoSJyt1FZSr3YcLyKjCqPS19eQThPczFkd41HP9Vgq6os5NaXCiDZHSRJZty1OWnXL5Zz73uU/Onj0n2f9PAdAbAgCoKJiYRCmlntv07EPt7W0H4vEYKaV09drp2Cr4+9KcPNBdG9X69Ottuh+MYxwO5Wr65VoG+qpcgFbLyICo34vPezCpkwq63+TiJadc95GPfux1bLR/QTAiIQCAqoIb6Obnnjtw/OjRg1qbfKHhP99TkAgKX1Jj5Z5VbwHZn77tWhMBy9XOqzX/96WPv9bPVSm3YSy1ClQa5ldtnoBSwbnyA5jwzIGGjCE+bfq0aZdeccX7zj3/ggUDcOgARRAAQFU6f+N6duPGE3v37nnaGE2cc16pht/fwr+/zdEjVVShGrVNpWS6Sq8p957VEvjK7b9aMFDus5T7vZz+ft9DkQNQKqprploLTfi/we/h4CyckGmMoVzOoRkzZi7/9Gc+8+F4MtE4KB8Exi0EAFCVya9Z3tHRntqwYcNf0ql02r+J1bZqX/i/o81AHXe1ArPc47UU7OF/R/X7B49X+iy15hlUCiDKFYjB+49W1a6B/rT2FM6ZYYV8muAxzjkJIYmIJc9cveYN//y+972h7jcBqGD0/lXCkApucI+ufWTjvv37nhOCE/Vafa60pjV2mn0DA9esH52QVyhYqbhgDRfuQgiypCQhOEkhyLKswr+DfZY29wf7DgcGQoie3zknzsMFWc+iNOX6rCODAQoHAj25IIPdBTCUOQC1fpbScxP13/C2wf611oX3UEqRlJISDY2T3/NP//LhRUuWYFQADJjRWS2DIRcURDHbtm6/885vXnHV1f+Yy+WIiBPlb3TBSoFEfkGjteq1n6jCJGp4VfgGOdT9x/5nLd/CEVXbLX6+9/b53wr/ZsyfBlaRIWlZZMgQKUOWEKRczy+MuX8ePc8jzjg5rpN2nFw6m811eblMrjuV6sqkUm1Z12nLprNt6Wy6LZvNpRxPKW6Mx6RgRETckOBSSCGELZiI2TGrJdnQNKkhkZgQS8QbE4l4gyVlIpaIJ+PJRINt2Y1SSh68t1aGpLTI8zwSQhQKWyEsIjI9w9iM/9ly2RzZMZsMo8LKkcGcEsF50/4nLuDEiBlTNmSsfg305xrpb3dVLdvU1opTKvgO8ufd+cM993zz7W9503+4rtPd1+MFCGC6SahJcAPOOY63cePGteeef8HrE4lkq1IeGSqe7CeoaRqje72+XK0yqLGOBP6hVU/mqmd4Xs9rNTHmF4RSCJKckzaaGOektEueVsSZICEFecqjVEdX58mTbRte3PPiCwf279t0YP++nYcOHtrf0dGebmtvU0ePHsucPHki1d7Wlkml0q7WqlKHN5NSskSywZo4cUJywsRJydaW1lhzS4tobW2xJk6a2Dp9xowFc2bPXjl1+vT506ZMnTVtxrRliWTjZEZE8XiMHNct1Eo5Z5TLOUREZNs2eY6/QFRDYwMppcjxHBJCRnYRBF1IRaeQMaIhDvaGSl+v71QqRRMntlAm45DruvbFl1xyww1vfNPDP/nR//2uWiAKUM3IuOPCqODX6jWde+55M2/60pe+dfbZZ79Ka02mMP9/UCgGvxeXReVmVIt6rtLrBl9tY94jX8lYUY0w6qavlCIhBAXzKQStAVIIElLoXDZLbe1tB/bv3ffndevXPfTw3x54cPOm547u37c3l3Oc3s0qJe9f7zGX7iKRTIoZM2bYK087bfI55567ZubMmafMmj172dy5c9dMnT59oRR2MpfLacuySErJlVKac86NMpTNZimZTJLjOH6QY1mUc3LEmD+FdK85CJifZMqJFc563yYrGtktAP52tY3wCLMsv+WFiEhKSUop2rp124PXvvyq1x4/evQEggDoDwQAULOe/kvOvvr1b/zjm97y5m9KKaQ2jDgXRBT0kQY1Hio8FlbLZDe1Pjc4avuzKN83TpGPE/lBlJSSXNclrQ1ZliTPU8SY/5zrOPrIkcMb77jt9pvvvfeu+5984ok2z/NMeH/hfuR6Z+CrlpQZ1cdt2zF2xqozmlasXDl71apVF51z7nnXL1iw8JzmlpaJSinyXI8MGRJcEiMipfw+bNu2SBtTGEUS+b33pBwUnfX6r4fhCwDC+RJV36nGRNDw85xzcl2P4vEYaa214zjp737nW5/9xL9/5FYai4k2MGTQBQA1M8YECwGZ7Tu2P9vV1bVn8uTJi5jpXRCFRwlENf+WK7hGSjdAlNLui/I388q5A9lslizLonjcpq6uLj8BT0qdzWY7N25Yf9fdd975rV//6pfrjhw54jHGCn3nAzHTXi0tLqXJfY6TM089+WTnU08+ueWX8fi2888///eXXnb5minTps5fsXzFhStWLL+8qbllWjbrUDyR0J2dnSSlJCEFd7NZMowRL2kZKZzDntCm5s8wEmu9fb1+q3UNCCHy3QCt1NWVIiklj8fjjVdd9dKr7rj99jueeuLxF4KWOYB6jdy7LYxIwc1q5qxZDbf97ne3r1p95tWu5yf/Bc3ZREGfr39TKtcCUGsAMJwtALUcT+9x3Try8aJ9aE2UbxZnRLqjs+Pk/X/64/e+/a1vfeXJxx8/QUT5PIrhn0SnJ6fD9Cpozjr77AkXXXzxWatWr37lqtVnXbRkyeLVmUyGjCHinPmtHLYd+TmK/13f9977ueFtASjaWw21+lqfE0KQ67pk2xYRMXJdl4QQREbrv/7lL99/y9/f8C+pdModiUERjHwIAKAuoVow/+nPf/7Zl73imn+1Y/E4ESsUDoWhzaZ3raSW5urSm9lIDwDKbRvVFcAYI8Y5ac8jy7Iom8lmO7o7O++79+7/+tIXb/7Wi7tf7BwpBX+U8JwCSqngGNnEiRMT519wwZxLLrnkhqtf9opXLV6y+JRcLhv3uzlixBjjOjQ8sVSlj1pbTsDoCQAqbVP6uOu61NjYSKlUimzbpqBFiDOt9+07uPOzn/rEDT//+c/WoxUA+gIBANQtKKBe97rXn/HJz3z2l4uXLFkW9Ps7jlNIbiu9MdZyI4+qyYyUACAovEpvtOXHdpfbxhROTSrVfejXv/rV175w4+e+fvzYsYwQIj+UcuQLuglKzod12ZVXLnzlta982TXXXvdPc+bMWpbJ5rQxhgshSVqSHMctCiLyeysb+IWDhuD38H/zWxeOqf7rZeACgNIRLZWa+MuNfundouR3veVyOb/FiDHylKZ4zNZ/vf/P33v/v/zzv+3etbMbQQDUCxMBQd2CiUr+eN8fX9i/b+9j4ULLGBO5LnyUcs3C4ceHuxZcqd89/BmLavgVPrfRmsgw8ofrKefhhx747Ve+dMu3R1vhT9QzaQ0RFSYWIiL3b/ffv+2Tn/j4tz/58Y++5e677/6O57ntWivH8xydy+V00IoQfNbwjIXlMuXryYGodt0NtGrDQaOOuTSwLA18SoMfL99iFLBti5RSfM1ZZ17/rn/4h2sJlTnoAwQA0CeMMerq7Mzs37fvyXQ643meR0opfzy45/VpyFO92w0WxqIT/Cr16VfYW69HXNfRnBFt3bp1/VduvfVr+/fv6xxthX+pYBKooHBPp1LOb379q3Uf+sAHPvKfn/vsWzdv3nSnZVmO6zgeZ6Q9zyUiP1gMmkpK80AqTWkczO5XmphZ2po0FMFApcK83DHUEsiUBgHhf2uliIyh1taJ0y69/PIbTjv99NZgsiWAWiEAgD5jjOl77rnnsUMHD26PxWKFloGorP8+7HsgD3VAVUzui0hsK7QOEBFjpG1bUCaTOfKzH//kv9Y9+eRuot7rwo9WQatA8Jn379vb9d9f+9q9//ahD37wZz/9yU2MzBHyZ47QnHESghNjVPQaovr60fsWmA2+akMt6xkeGxksMEau6+klS5ae/7a3v+MqImLD3WIGowsCAOiT4Ebzp/v++PyJkye3BjWP0ilfKxlJN+swY3q6OaJ+qr6e9YxvL/2M/oQ/Fn/88cf/dscdv/urMUYHORVjSfB58jV1s+6pdQc++fGP3/ypT37iHdu2bXvSsiwnl3McrZQ2Wke2X1c656XN5OXm+x+uZMpwa8ZAXee9WheISErJGxoap65es+bvVq0+cyJaAaAeCACgzxhjlM1knN27dj7T1dWVDZpmK9VmqzWtjwSVamJENfTzU/mcdCEkd13He+Lxx+7bv2/fCaKxU/uPEq7Zt5086Xz/u9/967/8f//0xt///o7vxGKW57meZsSIi+Im/XqFuwdGgtK8hVpVu/bC/zYUBAWaTj311HOvuOrKU8vtAyAKAgDot1/94hd3nTh2fG+4FWA060/+QvXXGL158+bnHn/iiaeJyIzmZXJrVdoa8OQTT+7+yIf/9T++9d///e+pdGq36zppx3GcXl0mQStKaF+lyZbh9xju2n6glnkA+hv4Ft7TMPJcRU2NzTMuuPCSNyxbsbwBrQBQq7F/94FBExT0jzz00L6jx48+b4wJLRRTfGn1Z3KU4VApw7+vtNY6Hk/wLVs2P/rkY4/tIhpftTWdn/yIiOjA/n1dX/j8f373c5/59L8dP3b8WDKRtJXyCpGjf67zyX+hRMDgueLtyr/nULcwVc4J6a0/37/JDyflXJA2hl900UWvf/WrX3sp0egPwmFoIACAfmGMUVdXR8f6p9fdx5lxyCh/2lciYoYRM8wf9q4NkSYiTf5a98QKzwfbhP89VLW5cv38Uf3+tQ4H5MSIdDAbYL45WGttWRY/ceLYgWc3bPh9JpPJ9G3M+igX6htPdXe7P/zB/971rne87apHHnzg/7SXS7uO4xFRIctdK02CCzK69HvQ/k8o8bSvNWtmKv9UfX2ZlohK323R8ZrefwvBT+ljRf9WRK72SFr+IkvJZGLqy1/+8n9YtfrMqcYYtAJAVQgAoN+MMerBv96/5fjx44ds2yLXyRXXyky+0A9uePn/Fe+k5PchKhdrHb9f1z6p5/Mak18rkTEupaTdu3ZtX/fUU5uIqGzi2ngQFNpKKf3o2kd2fOjDH/jkxg3P/Ikzw8l4WghORIY4Y+TmVxYMT5pT+K6YJlZLKT3CFD6PibjeQgFAT1M/9fobCc5BznGIc6611rRoyeLzL7744lOI0AoA1Y3fOxAMiOAGdf/99z95+NDBzUSMdMnsZn0ZAz2Ueo4vCAIqb1/u2IMbclD78ofEGQoy/XNOznt+2/bHNm3a1EaEG3S4QN+yadPBT3z84+9/+OEHv5dJp7s9z9PGmMLMd4z7U+Ay1vPfoJuAsZF9G6t3FEk9+w0l3XIi0i2tLVMuuOji66ZMnRozpvZVCmF8Gtl/OTAqMMaou6s7s+m5TX/t7Ohoj8Vi4XniC9uEjaQAoEftQxfLJX6Fnw/PiSCEIOVpb9/uF9d1d3dnx2Xzf4RwIfXkE0/s/8THPvaJp59e94tcLptmjOlcLkdCyMjRI/5/awsARtJok7qHlZbZxhhDRpvC8tKcC66VofPPP+/t11//mjOICN0AUBECABgwt9122x+6U+ljRDRqZrUrvRn74/9rf325EQP+/oLZ7BjXWlMmk+o4fuLYcRon2f+1ChdwWzZvPvmB973vI3+4997PZtPpk5YttaccYsaU7Y+vJ5Aa6iCg1hn/av0Mpdspo/Jzb0jy/KBbz5k1c/LSZcvOJKLC7IwAUXAXgn4Lbkrrn153NJdJH3EcRwdNt+W2HU1Ka/cV5wDIN1v7zbPBvAj+Zz529Nj+XTt3ncjvZAiOfHTa+cILnf/2oQ98/dHH1v4qler2kslEr1r+aLmO6ml5qPSZIucHID/XRAhBOh9wSmlxTxOdc87Z15519tmT0A0AlSAAgAFz/NixrnXr1z+glPKCftpAr1nMRuBNqd5EwEpBQDAxTf7fWkpJe/ft3bj+6XV7iPKLAkEkxhi1tbU57/3//t/nNz373J0dHR2ONoqIGWKU/2GcCumWdeYA9Preqg4DqPbTt8/oJzH26eU9+8kHmY7jkhCctFHU0dlFZ6xaffGVV111EWOMlU6zDBBAAAADRmude2zt2ke053V6rkdEI7OgDysdPhZV2y/3mrBwcBPOfwgS/bQ2dOzo0S3Hjh1Nl24PxYIA6vDBg4fe8553f2Tvi3seJiKPqO/z/o+U67BagFlPToAfPzDK5XJkWZKIGLmu4nYs5lmW3XzppZddt3z5ijjR+B5xAuXhqoAB9fTT67YdPHJ4p7RkryVeC+PkI5Z/LaoRMb9mU65wHiyVWilqncgoyFIPlrtljMi2bd528vjOLZuee5zy/f8IACoLzs/unTtefN/73vv+3bt3Ps6YIdd1SUpJpP2cAMkFac/PN4m6Xsol2w1XQNBrjgnqWTvCMCLirOjfUT+FbcgPMIPCPcgF0Fpzz1O0ZNnyi5YtXz59WD4ojAoIAGBAbd3y/P6O9vYdrusVtXHXMlmKMWbIxv8PJs/z8r8xUp7SlpTU2dV9YMeOF3YRjZza6Gjx+KOPbv70pz75zy/s2PF0Q0MDOY5DUkqyLIs8z+u1BkDUug1RRuv30OtvpuQxzjl3XdebP2fW0jPPOvsKImJIBoQoCABgQGWzGZVJZ455npvu6yIooz0Q6GnpYCQtixsi6mhvO9redjI33Mc2Wt11553P3nXn779+4sSxI4wxymQy+UDALhpKWK+RVijW2zKkGZHu9cfCiHHBPUN0/vkX3LB8xamTiUbeZ4XhhwAABpRSyjzw17/+IZNJt4Ufr9aE36u5fRTfq4LJWYLx/9lsVu/evXvz/v37M0To/+8jc9NNX/j12kce/bXWOh2Px4kLQa7rkmVZFEziVGokjf8fTMEnDF9b2axLp566YsXK007DzIAQCQEADLj7//KnZ9pOntxtjNFE5fvSoxLvCj+jOAIIujY8z9Naa/JcN3vwwP5t+w/szwXPQ/1y2Uz2kx//6Jd37971lFKKyPgJl0pXn3NitAcB5WbT9INlRjqfFxA8R0SUy2Z164SJM04//YxLGWN21H5gfEMAAAPuhe072tY/88ztWmuv1klOxtKNyRhDtm2TlJIrpcjz3K729pMHPdfVSADsnx3bt+37+te++qWjR47uyOUyXmHeBepZZKhcrX+0XGOl+TF9OW5DxIUQWnmaVq9Zc+HSU05p7uu+YOxCAAADrqurS+198cUdyvOyxhgdzLAX/EQZS4UiY4zCa7IfPXJs6ws7du0NnoN+MT/+4f/94fbbfnOL6zonGxqSlElniIfmAqg0hHO0qf4Z/LkIiv9+GDESJKTFM5mMd8H551999jnnrSRCNwAUQwAAA84YTbt37TrQ1tG2XwjBtVa9hz8ZXXYkgD88avQGBMb4w9WMMWRJQfv279/xxBOPHwieg/5hRPrWW2/5xbqnn76nre2kJ4TQ/nntWYfBb2nxVx/2C9GRGRT093iiW9gMMaZJK8W1Ubq1qYEWL15yoZBSDsR7wtiBAAAGnDGG1q59ZPeBffu2CCFIaU8TM8QFIy4YMUF+Ac+p8GOYIcNCj5XZ7+gQFESKDBF1dp48fOjgQdd/bLR8hpHLGEMnTxxP/fynP/mhIX3CcXOF8fCcC1JKkzGMGPN/90cK5JdoNj2LUfuzCvo14oGd56+ez6LLvjNjlA+Uo7cxRpMgToI4cWOItL+dIUOaG2KSERdCZjzDzz7vnNcuWLhwAhECAOiBAAAGxZHDh1MdnR3b29ra0oz3rNhmjCHOOLFRNDNZ/TdMU1gDIJNOZbu6uo44Tg4LAA0grTXdecftjz7x+KO/54y6GbHCAlRSSvI8TVJKiscT5DguMcbJlEssHQUFYs2BY2E7Rkb7XW/EDJ1++mnT582dN7GufcGYhzsSDIpUqlutXfvInzzldNiWzaUUhZnZxgPO/dkAU5n0kQMH9u8MHh8vn38o5LJZ7z8/+9kvtrW177JjdqHrhXNOsZhNuVwuP1EQI6J87ZgREQtWFAiYUTlcsOrxciLbtimTyeipkydPWrJs6QoiYrgGIYAAAAaFUor+ev9fNimtu13XdYLCf7TVgvs23zzzm2eFoLaT7Yc2PrNha737gtps3fr8np//5Mff6erqTDNGWkpB/uI3fiuBUqqQjFmL0RgIhBmifNeBIUaMpJBca6MNY/aaNWdd2NraiuGAUDC67sYwquzbu6fzxd27Hs6Ph9dBAMD8O9RwH14F9dUIo8ZoBxMBHT1yZM+O7Tvag8dh4BhjyMnlvNt+d9sdTi53QnmeI4TURESu61AiYZMQFgWr4fV8T37/OjE/9yTKUK0/UU1frpngJUGLiG1LyrmuPuvss89etmxZywAfIoxiCABg0HR2dngb1m+435+preemOtKHIvX3ps85kRCClFLe7t27nti3f1+KCAHAYDmwf//x9euf+rXruk46nSZLSn+5ZcOIMV3IDSgnPEpgbPCzHYI5J4whrpXyFi1adNbc+fMXEuFaBB8CABg0juOYnTtf2JtNp08EN+GgNsY5D/XBsjI/oxUjKSU5jtO5e/eujV2dnQ4mABo87W1t6r++fOt/d3Z17RCccc91PcEEKc8jYiafAxAomYkyfwv0J9Qb/hp/lD61ApA/6oFzTmSIG2N0PB5Pzpo5ezEbiR8ShgUCABg0Silav37djj0v7nmKiLjW2iPqGaedH5k9nIc4KIIJj7q6uo53d3Z2EaHPdbA9u3HDgbUPPfgzrXW367mc5RPgglFzZc//KPpaosb8l5tpM5hLw5Ahz/OIMSaJDC1dtvSU1tZWQYRrEhAAwCB7Yfv2k8dPHt8phOzVBeD/mwq/16PcOu8jBWOMOjraD+zZ8+IRIjS5DrbOzk73ttt+c5fjOB2MMa2N1p5yiRkqJJ7611hJAUqlE1QN7/dUbn0Mot7LaJf7yW9cyEUJghylNJ255qzLZ82Zkwz2DeMbAgAYVJ7nGdd1M66T84iI+yvk8VAeQOlNKGj+773O+UhJzKqGMUZCCOru6j664ZlnEAAMAa01bXjmmX0v7tr1BJFxjDGUzebIsix/rYBgXn3ye8gN84cBGjPau5t6C2Y/DLrdEvEEGWN4NpulBfPmL54yeUqzvx2uyfEOAQAMqlQqpZ59dsOTnnKzwepljHHSoalbyxmtNyjOOSmlqLur6/jBAwdy6P8fGsePHcv99Kc//qZWKquU8hKJhL8qo+v2zEHBmH/XY0REvGQ+gNGrV9cAEREjf42E/J+a6zheQ2ND68yZM5cErxnpwTQMLgQAMKgcx6GnnnryOSIiY3RhOGC0cG0surY/EpppKwmO2fM88jwvNRrnPhitHMehB/76l6d3vbDzL0JIWynlecojonABGZ4PcOReR7Wq9LfAyQ88Hc8hzhjnXGghpb18+YpL4omENYSHCSMU7kww6I4cPtLteW6KMUOUn3udR9Y8Rn+LQNDv6mSz7cdPHN0/3MczXgSB4qFDh7qffPLxB7Ux3Vorv8Iviq8rRiw/G+DYEPxd+JP/hB4n4891YIh0vravtaYVy5ctnTxpkh1+LYxPCABg0HV1dXnZnNumtfGIGDfG+IPl6zSacgBSmcyxZ9atX0eEm+xQCM5xV1eX+stf//JILps6Ztu2JGKe56merHhGZNhY6/XvjRERGX8+CiEEGW2IGJOZbFYvX3HqimnTpk8a7mOE4YcAAAZdJpNxuzraDhitHb9WxkgwXqUGNjKysvuCMUaZVKp98+bNe4gQAAyVoIa7bevWnYcPHdzU3d3laK14+PSP1W+i3FBA/xcixjhJKUl5yluwaMGymbNmzx3iQ4QRCAEADLruri5n965dj5BhnBHXSvkFOzPk/xAjToyY0cTyY5eJoidmCYKC6q0B5SYXqvUnYo/59ysdmVCKc0apdLrz6JHDGf+YazpNMED27d2buveee34lpeR+JrzRwdz43DBiOn/dGVa4/ipdCz3XWbnH+84/hp6fcmsS9yxjXLx9MKlWYYptxguf009G1fmlkAWR4ZyIEeMy3jpxYmu/DhzGBAQAMOjSqZS3dcvWzYILWzDh35FMON2vZ/ESMwLqaDWNsy6DMUbkKd127Oieo8eOZPJ7HIKjhuC7SaVSZt26dZvj8TgZQ44xuvd9Ln/9ReeilCoXFA7CEMIyAUDw98Ei3i8/3S/5i1CFrtPQmgBERIxz4sIfEjlhwoSGYEbAkd6lBoMHAQAMOk8pc+jwoQ7GmMc4Iy5E+UK1EBiM/JtSZO2fMdJK6QMHD2w6eeKkS4QugOGwb9++YwcPHNia/4o0jfgFqKorO+NfKAkwLDzxVqi1ghMxmjtn7vRkQ8PI/yODQYUAAAad53l0+PDhTsfJdRARJ2MqDAUcfuHuhaifKi8m1/OyBw4e3Km1VhgCOLSC72fvnj3HH3jgwfssy4oTYzpf1R3WYxss5a7J8N9YsDol55ynMlnv9FWrzpkyZYqs9HoY+3B3gsFnDB05cuRkNps5YYzhI7nwHwiuk8u2nThxYriPYzwKrq0Tx487jzz84N9Sqe6jjEiSNrq0J2Y0lntRtfyo38PbBhNRaa2Jc06e6+rly5edP3nylGTUPmH8QAAAQ+LE8eNtXd3dh4w25E8HNHKXBO5XDgARZbO5VHt7uzs0RwtRjDFm+9at69pOtO3Kt9xoP0mueLvwehSjRdR1GFWLD7YTQhSSV3k+aXDCxMlzGhobEQCMcwgAYEh0dXelU6nUcWL+pCRBk+RYufmEb8DpTLrjcH4EAAy94Lto72jvPnz00B4yRhMxzoiIazZmcjIr9f8T9ZyHQhJgKB9ASi5bWlomD93RwkiEAACGhPI85eYcv1Ac4QV/tRyAyn2mjFKpVNvRo8e6huyAIdKJEyey27dtf5oLSfmhgHokjDIZaJVaqkqHzXpKca0155xo+rTpi6nQJjLKmkFgQCAAgCHhep5WWikpJGmtSUoZsZXJJ2v7/w2aLcM3tFoL48IcA1V+eH4OgvBPuQAl6uYafo6ISBtN6XT68J4Xdx8r3QaG1vFjx9TGDRueiMdj3BijC+PlqWf+CUb5IXT5pvGoay5Q7rob6iS68DEEnyk49qjjCz6L1rrQHeA6mhYsXLiyoaGB+/sc0o8AIwQCABgSWmnKZrKuob6tQDZYN9myBXQ/3i+dTnd2dLRnKu4fBk34nHd2tne5rlf1NcFcFESjKyu+Wm5K8Fl6BSyM0cSJE5pty0YZMI7hy4chobU2mUzGq7gEaWG2td5Nl/7TxX2aA6XX/ljUdCvhp8sMuyJ/uFUul+t0XdeMpoJkrMqmMzk3l0srpT3GWK/7nTHlv8/R9v3Vk1NjyNCECZPilh0bXR8SBhQCABgS2mjK5XKKKH9jLVvbCgKEkhXcRuDNuFetyhhyHCebyWY6iQgBwAjQnUql0qnU/tLMv+g59ULPj+LvrlogYIzRRESJeJykFEN2XDDyIACAIWG0IaWU5owTUX1LsQ7FzbjWmlPUsYQf056XzmVzSAAcZj3LAx88uWvXzkeSsVjS1Dj2tPQ7Hg0rUEapOIzVEAkpNeej73PBwEEAAEPCGG1czzP+YKzeTftRN92e11Yf8zwwx2hKK4pF71kt8YsxRp7xvPb2tpODcoBQt7YTJ7L79u/bLaQs1HyJ8l9zfg6AqMupWqA3klUdrWL8ripLSosxMTo+FAwKBAAwJIwh0kpprXVhDoD6Xj80CVqGygcBgUqBgOuq3NEjR49GPQdDz3Fdk8tkTPmabu+8k7ChCj4HQrlAutcxM/86t23btizBiZCsOl4hAIAhYkgpZVzl9RqLXSjcKVyw+svqRhmOYVe1bud5Xvbgwf0HiXBTHQlc1zOZXNbRxEhpPSJWmxxINa9RUbwtJyKSUtqCowVgPIsajA0wKDyjtdKaGOdkGJEm49e/WH4ZYBYsa2rImJ4114nCS54GomvhhTH6rG83en9XvedULzfPOpE/13rAzWUze/bsOdqnN4cBp5VHuZzjadKktUfMBG3+PUvoBvNP9AjPO0EU6jkoMhABXj3XaXAdBkFMOJk2LOrvIZjfwH9Mk1aabNtutG3b6s/xw+iGFgAYMkwIksJvctTGkGFEZgzUP8IFgadUrr2tLVv6OAwPZTR5nle0GmDvCXNGx0U4kJMPGWMonky0xOJxu7/HBaMXAgAYEoxzsm2bU34otvEHYEf0WQa/jc7C03WcdCaXVcN9HOAzxpDSShdak0ZHWV8waMmIjIgLLqQUGAc4jqELAIYEZ4xsIRmRKTSZB0V8UDsbzTXm4Ng918vlsrmRu9TheGOIyJAZwbl7VYW7wUpbAfryN8PyE10Zz/DKsyHAWIcAAIYEZ5zZsZjw1yb3H1P5VoDRptyIBENErnJznueN3khmDGKMCWP8nvN80UdE4cJz/Hxd4eWPGWOcMUwEMJ4hAIChwRnFYjERNTd5Kb9mM1QH1j/h2pk2Rruum/Y8Fy0AIwTnnKQlORfcX4maDDHio+b6Iuq96FR/p8Q2xp+Ii0nGq0yICGMccgBgSHDOmWVZgsifhKSohCwKCkbP/aj0Rmy0Jtfxsp5So6h4Gds452RJSwb1fs54KPlv5ItacbKeOf/Dr+0Jvv3zYrRxtPYQrI5jCABgSAjOmWVL2+SXYSVjKl581W7QQ5EvUCkBq8xznvK8rEYAMGJwIUhKyYmIeGExoOilpYPHSp8babkpfV1Ns3RKYKW8nOdpJKyOY+gCgCHBpWSWtG1/6lVGxDgZ5s8DEO6CDY/DN0WPl78RD9QNOlxDKjfPQPjfpasV5h8zI63AGM8sS7JkMhnL/1NX+m6qFayR801UWt1ygPV1GGC5OSw8z3Nc16u+VjKMWWgBgCGRiCdiDQ2NE40x5CmPhIheEMiYfH+6rlyIVuoHHagbcvgmX/pY1L+NMZwFKdYwIiQSSdHU0tyglfb7oSK+HpOfG7/c4jm9unpGaYAXbtXgnJOTc9Ku67jDfVwwfNACAEOiubm5qbG5cZrf9Kh40U3U5Ofgr5CNHTUne+kNeSBrYpXu8aW1vtD7c2lZSSEwvepIMWXKlOSCBQuW53I5jxfS33n+Uuv5kmtt6h9NawNUOFbNGCPHcbtdz0EXwDiGFgAYEhMnTpzYkGyYZIi0ZVmktSmdcHeYjqw3U1I4+I9VPz7OOY/F7QZLWPi7GmbB9zV5ytTWuXPnn+V6nscZ8ys8Ed8vUe/+/1r2Pxr06rIiPxnSc52ccj0T3gbGF9yoYEhMnDSpKRaLTSBjtBSyrhto1Lbhx4Yjq7tc14MUdsyyLNxNR4h4Ip5IxGPTGPMHnvjzAYyPxPdwEiPnvCcRkIiIETmu43hKjY+TAZEQAMCgk1LStOnTGqVltXqeIkOm7OwjQQ5AraIK/uGqnTHGKBazGi0LLQAjRcyyJBeCE3GutfYLO0NFffxBa0C5YXaVFoIa6SKDY2O40Zq6u7odTFo1vuFGBYNOSsGmT53awjnnwc1VSknMBD3//sxstYq6QY+UJkxhybjIDzuD4Sdt219/ihEPFp3u1flkigv/nsfrH28/KjBGWmvK5DJKKaQAjGdIAoRBF48n+eKly5a4Ts4RUtpkNOWrYcRKb7CMyDBGLJQTWJqgVa6wH7gx24YKQ8br4GdXCx5PxBEADKPwddDaOqElv+g0+dP/UuF3Kko87RkCagrXnf/fntcEAULxnAHllguu8WjrfoU/g3bwuupJieE5AHq6BbQ+cexE2nGcMRjhQK1wo4JBl2xssBctWnyuENITXHDBORld+02z1kJ94GprfQ8kbGklWlsnJIiQWDVcgvM+cdIkceqKU8/MZHPacC6JcX8uoIpfCyv5vfzGo6V1oKQlQ+cb3Ly29pOdCADGNwQAMOgakg1W64SJi6SU0hhTqHtFYUR+W22dt6XBuBn3ZbpVK2Y3LFy8eOaAHwzULPjeps+cGT/9jFXnekpxQ0b7F5Umlv8v0Rht4s8LAqEg9aEoEdAYOnr06IFsNltIjoTxBwEADLqWlhY7Ebcnu64rGWNaGUO8ZArWQGl/7HDfmOodrRCzrOSSxUsWEKEFYLgE31lLU3PjpElT5lpS9prYoS/dRdVGo4xEQaFfWvgbrfX+/fu3YdrK8Q0BAAy6WbNntwjLjhcmzOG8wjLA9c+2Ntz3sHAgY0krNm3atOkj4bjGo+B74IKzFStXntna2rJAKUWsL0kdo5w/o6Yuuj611kSMyPO8dNvJtmPDfIgwzMbdHwUMrXg8ThdffMn5jIjHYjFujOGccwpnH/e1pjxULQT1vAcXQjY1Nk8axMOBGrS2tMo1Z511TrIxOdV1XY9zXrjXjYSWpaEQTvwr+d3r7Oo6msmkskRoqRrPEADAoIrFYnzevHlLY7F4ozJa63ytRNeRBFiqths468dP9HvWQlpWsqGpcUJNG8OAC76nGbNmNZ919tkXZzNZT0jJC4n8o3w+/1LlPkfUCofBhEBERIcPHXyxq6s7E34exh8EADCoYrG4SCQbJiYScXJyDkkpSWtNoQpZXYbzxl3Le1uWxePxRCMRsbFSyIxGM2bOmDZv/vwLtTGebVncL/vD8074ai38Rtt3GdT2S/v+iUhzzuM7tu9Y19HR4QbbwviEAAAG1bTp0xviicREx3XJsiweFP5KqUIfpdLaHxboZwBWXImndA33qDXd+yrcslBuX1WnJSZGZLQdj8el8Qds9+uYoC8YzZ+/aI5lxeJSSs4Y00T+/Pf+XPgsVCuu3hI1EgvI8LVablrqwt+XnwNBUkpyHEc3NDTQrp07t7a1tWEa4HEOAQAMGs45rVq9esm8BQvOUUoVNfsHNZNCo3vJQizDXWz2+abPiBqbmidPnzmzNf9PGCJBoT5/wYLk37/pjf9Pea4Tj8el67o8nPXf8/voy+onopoD3qAFgMj/XJ7naWMM7+zsdPbu3XvIyeXKLIsE4wUCABg0Ukp2yrJlc5qbGmdorTVjrHAjLty8GPOHBNLY6Is0xlAyGWudO2fuJKKx8ZlGi0L//8wZk5YuWbrcsmO2k3OIc9FTyJmg1j86i71yM/1FiRoFIITQyvPa2traukMbDs7BwoiHAAAGTVNzs7z4kkuuZ4zLcG0kaP4nIqIqC68Mt76MFbfteOOMmTMSg3RIECG4bmKxGH/JSy67PB5PzHQdRwshe2r8FS6t0TIyILyIUVEgTZUDgeAcCCFkLpvZf/LkSQwBBAQAMHgWLlzUumDBosvyzf1cCEFEVKiVUMkQpcJN2JiiRK3hUK2PtRIrZieamprxtzWEgu9o8uQpsUsvu+ycWDzeyDnXUkquta7YFTOSC/7iVQvrU+hm68kH0Jxzvmfvni3Hjh5BAAAIAGBwCCHoFde+8kJiZHHOZTD0LxiKFAQApTe2IFN7OHvP+1sgxGy7qaW1BS0AQ4xzTuedf97yc8455w2pdNqRUnKlPDK6/Pc5kgv/QK39/aW/lwax/n5Ib9u+48W2kyed0tfB+IPVAGFQNDY3y8uuuPLVVizeHF4GOMCIiDiPnKK1pwVg5N2carlh2nYsOXP6jFnkDwUc+SXMKBfUcCdOnChveOMb35xznKRlx6TrutySFhEzPYv/ke65DoMlJ83IXfCndCx/7+crvz44fqWU5pxzz1Pdz214ZtPJEye8AT1QGJXQAgCDYuWpp05tbmmZa1t2PN/0SEQ9N2ulddFirPknC/8d/nEAlVSuUdq2HZ8zb+6pDY2NVmk/LQy8oH9/0ZKlLeeee/41lmVJaVn+cFOjybKk3wqQv+aCS8vvbiIq930Od+FP1HtCn1qHvRb+lDgPjwTgrpPL7Ny561nP87AIEKAFAAYeY4zOO/+iZZMnTTrVz+7n3B8Sz/NdAIK0NqHaS77Jsqiptq83poG7oRXfYIvzAfyGit6Fe/5zyunTZ502ZfKUWKq72xmwA4KybMtmr7jmmgulZU8mxiVnzF/vTysyihELCv88/3vs/f32PDdUKr9X8bFUGgFQ3FLAmCCtFREzxIiTIEFaa93Z3nms4+SJY8G2CADGN7QAwICzbZvNnDlrZsKONxORLi0kB2LSnihDdTOrdOzGGOLEafLkqTPmzZvXUG176D/GGK1atXryy192zTsSyWSr9iec4EIIKlTyazQ2CkSWD0QZ2dImz3PJcR2dTMT5M+uffmjv3r3dRLguAQEADILp02ckzj7r7FcwIYhHzPk7GAHASLpxe8qjRDI+/cKLL15JhBvtYEsmk/x9H/jQ+xadsvRKItJSShlMPFXLuY/KtB+o2SUHSn3HYYj89g9yPZcMI7JsoaUl6On1Tz926PAhh2hk/c3A8EAAAAPuqqteunz2vLnnCSFsov4NZRqNDBHF48kZp6487TwiBACDbdHixZMuv/KKtzi5nGSMSSmln2eSnwK33utuLH1fSimypCQuJGUyGX344IGDSik/82Gc/D1CecgBgAHFOafXveHv3pqMJ1oYY7yvY+nrMeJuZJxRc0uT3diYnE5E/Vr5ECqzLEu+4e/+7o1S2pOEFHZQ6HPOCzkaQUvAiLtOBllo8i3teR4/cfTI9oMHDuwe7uOCkQMtADCgmpqa7ClTpiy27FirocEt/Ie7VSFcUwwHOlopkpJTc9OEabNmz4mHl2GFgTV33rylr7/hje+0bDtp23ah5k/Uk+TWl9kcR5L+tEgIIchxHC04lxs2bnj4hZ0vHO3vPmHswF0JBtSyFafOa2xsmm1Zkge3GJ4fijSQN53hvkmX+yycc5JSUjqdoznz5628/IorFiEAGBycc/Ev7//Ae6ZMnbzUGE2e53ErP/wvaHXRWpOfDFj7CpNEo6PbqvjYolYE5OT5uRCcM+asX/f0wwcOHMj0fi2MV7grwUBi5513/prJU6Ys9P/VO4mqdMayqBnM/P8MXw2l1uSvyFkM88PLGGPU0tw8e9GixfOJqFArhQHCGL3kssvPfvNb3vp2N+dyxhiP6mqpNpHOSFT7WP/S58IzazAyRhGR8aSUPJNJn3jhhR0bXNdFfxQUIACAAcOFTF5x5VWv4JzHg2l/a8kB6B0EjPybdbnPk192lYQQJC0ruWjx4nObW1rimBBo4DDGqCHZ0PTRj3/y342hZDLZYDPGSAhRmG46vG09td3RXDPufXkxYsZwpTzv8OEjR44cPXqciNAaBQW4EmDAtLS0tKw8/bQLtNYyqn+8nPDzPUHDyL0RV/s8jBG5rkNCCH7KKcsuOvXUU1uJcOMdKMYY/q8f/fhbV69efUEsHpdefnXJSkHZaC7YK2ImP6UxUU/g3DMM0BCjeCzGH3ts7QObNz3XNjwHCSMV7kjQb0G28WWXX3Ga4LzRtu2y246GG3F/j9GvjUpyHIdmzZ659Kqrrz4LhX//5YNK9trXv+Fl//iP//AxIeXkqMK9NKAsNRquwUqKuwd6B8tB8K2UIs6ZFlzqp5544g/tbW1ZIoxKgR64K8GA0FrTDW9609/FYvFJQghSVWplgXLPD3dzebVjr9i1wZl/WzaGWlonzFq8eOmaYDVE6JugKX/2nLnTPvbxT30inmiYYdsx7nmKwgFntcJ/tKv17yKfeOoxxvhzW559eseOHZvqeT2MDwgAoF+CG/PCxYvmrli+7NympmbpecULjZXLqK7elD78N6u+DCHTxlDOdUgKSUSMps2YtmDx4iVJInQD9I1/jbVOmNj4xVtv/fS8BfNX2LEYdxyHhBCUy/nLLdRS+I/moKD830PZZEGeSMT442sfe3T9+qdPVt4HjEe4G0G/5G+o7I1vfNOVLc0tc7VW5LguWbZVUw262mNRN6yRdhOPCnCM1sSFPxxt+fLlF1z10quXIRGwbxgj4kLwGz9/49tf9rJXvN2yrFZGRFKKwjb1XmtjQc+1lM8DYJoKC2sZo4mIp9PZ9MaNG59Jp1IOEZr/oRgCAOiz4AaUbGxsvfKKK1/e0jqh0RhD/v98A33zHY6beb3vKYhRzLILr0smGxacdfY5l02bPt0KZqqD2uRbmNiHPvSvL7nu1a/+gBDCZoyRUoocxy3MMVFOX7ugRrrCNWTC11LxebBtm7Zt27Z907MbH2eM9VqUCwABAPRZkIh01tnnzGpunXRGLG6T0oZisRi5rlthnDwVJgYqXe+8dNvSn5G0QEu5xWO0VsTIECMi13EoFovLFaeuuGDJkqXNROgGqFUwne+73/NPZ//De95zU3PzhEWcOA+iSz/XpHioaWlrTOnkPqXbRf17aLHCD2OcGONl/22Mv9x04XcyZDgnZRiRZqQ8TdKSZDSR8pSWQtDjj6z97YZn1u8arYEODC7ciaBfjDH05re85dqp06fNyOU84pxTLpcjKWXZwjqqYA8er+X9RqLwcfF8DbXncxFNmzZ96bJTTpldui1EC8b0v+71b1j+T+/951umT59xdiFwyp++8Bj/qFNaLieg1ol2RjpjGGmjSXBBFKx/oA0xRlpwwVPd6ez27Vv/4jiOqjYbIoxPCACgT4JFVlpaWxOnn7H6ggkTJjQGS7Datl20FGs9QcBoE1XIhKc9llKS4zh60qQpy86/8IKXWZbFkQtQWTCK5KUve/miD3z4w59dsGDhxVHLSpdTz7U0UoKAPh2DMSSIkSUEGaML542MoXg8znfu2PHQpk3P7Rj4o4WxAgEA9Elww7r+Na9b09zUtMx13UKTrW1bhaVY+3JjC88GONrGcYe7KgKMMS6EkKvXnPXqSy+/YnF+iNYwHuXIxTknpRStWrV60kc+9tFPn3rqylcZY3il1qJKE0fVkgMwnNcTY+UL/2pJsZxzkkJSeIgp55yUMVwppTdsfOaB5557tpOISI/gvxkYPrgLQZ/kbzjioosvPnfGzJkLgsellNTZ2UWWZRW2rZQLQDR2mmQDnlLE8sFQ/jxpxojmz59/+jnnnrOGiBjWBugtWMTn6pe+dPGXv/LVr55xxpl/b1m2XXQtsZ68N/8aKs5qLxcwVuqOGsmqzZPhX2OGOPdzT4wxWgpBBw8fePYv9//ptva2NtfvGkD2P/SGAADqFtRely1fMfnUlStfGovFJJF/UwpaAqJquKVdAmOlwI8KcILPZowhIQRXSlEiHk9e+pLLXrNw8eJJREgGDAu6lC69/PK5H//kJ7+w8vTTb7BjMZlOpwvPEw1cjX24a/59EfW3EuSaSGkFAae2YzZtfm7TX9aufeTFoT9KGE1wB4K6BTfO173h7y6cN3fe6alUqtAE6XkeNTY2kOu6hW1Ls7Ir7bdn29F1c+41D0B+QqB8Gy9pQ5TO5PQFF1746je/5S2vJaLI1evGoyBQevX1r13x+Ztu/vay5Stf39TUJF3HoWQySUopUtqQ7sclMRbyTQK9A+h8wElERitSnkfPPLP+kf379jnhpZEBSiEAgLrlb6Dioosvuba5pWWaZVkkhCDP8ygej1M2m+vTDHrBf+sZFTDSBP37UcPQGGOcGOcXXnzxm1eceupcIj/hbbwzxtBlV1y59P0f+uDNK1asvDIejxet6je2a/x97/pijJGUfg6AP+8/Jyml3L1r5+PPPL1uAxFamaAyXB1Ql+CGctHFl8yZPmPa6URBP6QuNOMG25UWgrW0BPRsO+gfpS6Vao9ROQzh5ZAL6yIQadfz+IUXXHzeq69/7dWMMTHecwG4EMm3vO3tN3z1G9/46aozz7qGcW4zzkkbQ9KyyPUUGWK9avBRy02PzAK+dlF/E6XXVrj2r7WmXM4h244R48EcFDp99913fftP9923l8jvIgAoBwEA9Mmrr7/+klkzZy8fiObF0X7jrkW+ZYBrrbXjOPIV17zivZdcetnq4T6u4dTY1NT6H5/53L/919e/9s0FCxauJm040XBPzDPUKndNlAtygjwTKSVls1lSntKMc9q3f99zG59Z/5jrOjochANEQQAANQv6E0897fSFF77kknclG5JJ9C/WPg99/ndOjOkLzjv39Le9453vSzY0NhGNr6ZazjlbdeaaVT/+2S9+8f4PfugDWrNW11WSBCfNGOlCC1D0PBJFI0jy0+CPRuHhrmFRAUHU70YbIm0oGU8QEXEyhtY/9cS9f/zjH/cM8qHDGDF+7jrQb/kbD3vHO99x2fy5885086uwRW0XlRk/VvTns+RrbvxYW6e+5pprX//+D37oFVJKFp44aSybMmVK/O3vfNdV//ejn3zn4ksuvZyImuPxOGeCF5ZQrtT3X21IaZSRdu1V6/OPGhIb9bvJtx5wzsmSknbv3rXpzjvvvK2jvd0JZlIEqEQO9wHA6BD04be0ttorVqx4SWtra2OuTABQi6AJc6TdnGtVOtlP+N86/zA3Pc8R9dzYlVKUSCTIaB1/05ve/G8H9+/f+dOf/OhppZQZzeekkoaGRn7l1Ved8i/v/+C/LTll6aXJRMMsKbjUWnMiIi4EqXyBZYwhRUTEiHiVYKBSyDQSz2O9QV5UwR/8lzNOjIjS6TRJyfUjDz38wztuv31rOBcHoBIEAFCTYIa26669bvGs2XNXZ7NZYqx3BnuttbaxrFCDJVZUQAVBQjweJyfncCJD02fMOPUDH/7Xb2Uy6X/8za9/tXG0B0alOOd0yimntL7rXf/4d6+8/tXvamhoPLWpuSXpui5pYyiZTJCTc8l1PWIiaJD0h7ZR8P81XFP1Pj/SVAsMIqfVZkSMGCUSCdq+7fmnfve7395BRAoBANQKAQDUJD/HP7/woouuWrRo4TJ/3v/yc9qX1pCrqWWegJGmUisAEZHOj8/mJWWR4+TIU4rsmE3CCDljxozV//6xj3/N9bz33nnH7zbr/DTKo6kQK11xj3POFixc0Hj5ZVesete73vUv8xcsvEzGYq35Wf10MpngrutRJpMr9IVrzYgzRowY+W0AhjQZMoxIhE7FaDovYdXmwKi0bVHeQ3CutSZliJRSzqOPPXrbQw88uBeFP9QDAQBUFSwysvL002esPP206xgjqZQmziuMYS+arjVa6U2tZwKg0RMElDLGFA6fEfWaz0gpRVJKSiZsyjoOGa15PJ6ghYuWnPufN37+R2T02353++2bR0tLQOnwTiKiyVOm2Ndd96pz/98///PHFy5YeAkZI2UsJskw0sZwMpqy2RwREQnhrxqpjb+ITXDSGJmeU2cqXBMsnwwXenqkn7Ow0r8BFvE455y0Uv5sf9QzBFJKQUSGdu/cvfUP997ze8fJuYUFgQBqgAAAqgpuRuece96smXPmLRRCktaOJmZ48Zh9/xe/D5xR6dRttXQF+A8NxEyA/Qki+vfe3JS8d9ECLoKMIco5LnHGiQTnKj+Ry/SZs077yEc/fmPctj9x5113PZ9OpfxhAyNsNrcglyE8WQ8RsYWLFiVWnnb6kte+4fWvfenVr3i7kGKaq5S2LEsScdLGv17CTfzB52Jk/NMUniuC/H/7W/eeWKmAExlT/vwMfyAVtI70HE+gKAvbhJr68w9JKclzPWImWGZak+SCXO2RMZo4Gb11y3N//uO99+wMuukAaoUAACoKCp+m5hb7oosufkNjQ8O8TCZHtm1zVaZQiip6R1OtbKBFdYeUJncJIWzGiBbMX3jtl778X0vnzl/wsdt++9u/vvDCju5CIVnSzD5UwgV+8P7BMUyYONGeOWtmw7nnnnf+G274+3edf/7Fr+GckeN6pJSnE4kk54yT67k0EGvS15JjUu/zQymqab+0Gym8nTGGlFYUj8cpl8uRlH52fzKRIM9x6cWdOzb+7Gc/+ZFSyhtPQ0lhYCAAgJq88lXXnXbWWWe/Mh5PaGN0xXnsg3HZI+e2O/L50wQTxWIxTUbPf9/73vfVyy697G+/+MXP/ve+P9237vChw06of52IoseG9/MYin4P1/LDNf2Gxga+cuXpE+bPnzf34pdc+to1Z5+1YvGSpZcz4rYhnU1nHBmL2VzKmD+xD5nCVLWVWoHq/QwjqWDvMxad/xI8lsvlyLZtchyH4vE4CcGouztNwhG6u7ur8w9/+MNX777r7s0jrZUIRgcEAFBW0HTa2NQUu/zKq2+YPWfOUiLSjqOIC0HMmLKFfKWx3GNJVHJWpe2imqN7mn0ZERcynkxwKRoXrF5z1qzVZ5758otf8pJv/f7Ou+5/7rlnN+/etbNb6+rL4kRmjVPl76VcYWzbMTFz5szkwkWLZi5ZsnT5oiWLF1/9spfdsGL5stVKGe16rnQd5WljuNaGYrEYD4Y7Evk5JLZtFxVQtXUH1Xbt+GPhexeeI03k9WF6Px7+7qSU5HkeSSkpl/MTJhsbG8lxcvz48aNb/+///vcuItKjKXkWRg4EAFBWUAM844xVs1ecsvw8pZT2PI8T535BRtRriNZA10pHk1pGMlQbHaG1R1JK7iqjhWVJrc2EV77qNR976cuuefvDDz9034MP/O3enS/seG737t0n9u/bl8nlcuGTHMqbqzsAKxxUY2OjmDplqjV77tzmiRMntqxcufKyM1evuWT12WedPm369GWMuMxm005HZ0ozxrgx5EkpuCUEdxyXGGPkeR7Ztk1ERK7rVlz0qK/DRINgym8RGcnXGqNyXznjnJjxW0mKAsE8rXWwyA+5rkuxWIzSqTRlMqnOhx984Mdbt25tR98/9BUCACgrqLGdddbZi+YvmLeMiLg2Rscti3ueV/dQP4gWLvAsyyLHdYlzzi3bJq1U3HVd3TphwoLLr7zqH6644orXtbe3H37++S3rnt+69Zm2kyc7OGfkuq7X2d7Zlc6k29PpVKqrs7Orq7s77eRyrlJKG0NGCMHjsZgdTyZijQ2NyUQymUwkEo2JeKLJtmUiHkvath23J0xonTxnzpzFK09buWbq1Olz7FisWQhuG8aoq7PLkdJyLCml4+bIsgSX0uKe5wa5DIW8kfACUVHXyUAEiyP/+itf+BPlwxYW+hyGyDA/KZLy505wTo6To3g8Rp7nkrAE7X9h3x8+//nP/4yIzHgLtGHgIACASEF/7YKFi5ovu/KKG2LxxGTP83Q8HudBv3Cv5uVQLax858DobxmollVea0tA6TaMMco5DgkhCrVorTXFEwnuKaWFEJILe+qUadMnzpg1e+VVL33FOw1psi1BnIhyrvYy6VR3Jpvu6uroONHZ1d2RzWUzWinFiDEhOLdj8UQimWxsbGxsTsbjjbF4vCUWs5NSWsSJkfYMqfz3q7UmRkSKiHKO6xlteDLeYGutyXP9pZ+DAp5zSbmcQ5Zlkda68F+tNUkpCzkApZ+/VuHug1qm0h0915jJT4Hs/8v/XP4oGEamkPRnSJOnPOKCU0dH+9Ef//hHPzp44EAn+v6hPxAAQKTgxn7tK69bc8EFF73KEDlCCKmNIc/zChndJj9G2xgTTNvm78AQMV4+a73aTbpcxnutEwZVmkhlsEX1v1ea6CX8WaWUhd8ZY4XatFKK5wtSzTjnjuMQkaM5F5TuTmultbYsSYzxxsaGpuaW5glzOO8pKHl+iV3KF+xKKVJKaa2NzmZzHpHrf2eGiHHOg2blwlzzliWV1qSMJn/0on+c4W2CZv7waAEiKrlWqqtlu97bjLwC37/GWc3Xevh3f0Sk/11JISnn5EgI0lopZ9Om5/7wkx/98C9E5H+nAH2EAAB6CQr/hoYGfvqqVZc0NTdP7O7u9oiIm1CNrjBZTTBGm0I1mSr7H836E0xUmz0wKlAIAgNjDAlhcSJDQnAy+cHvdiwm/ddqf/YFP0gLqoX50rjQz8yD/foTOTHOmD8cXWvS+XmYuDaGeL5AZ5RvxufBpD+Fgy0cY1Doe57Xq8DzPK/suajVaLpmisb5VxiZFz5P4aAx/BjnnIj5QZQQgu8/uGfvN7/xje91dXY6qP1DfyEAgEjGGDr/ggtmrzn7rKtTaT/ZK5jFTghRdFNnZe5BfU3uGgjVmugHs0Apl4BXOp4+eKz43z2Pl+7P/73oZOcL7qCPnRGR4RRqIjfG+EUQI+LRMzT0BG2c8XDoZnq28P8bPs7CwYYCggrHHfXvsaIv1xLr+aILybThwj84U0GgbdsWnThx4uj3v/e9G+/+/e+fQOEPAwEBABQJbkKJZIJfcullF8yaOXulEJJzxnTQjJtOp8m27ZqbdcttU2lim2qvqddwFD6VanfhbaIT5IomEIw4/jJrMJTdvnojeT0TOAXdCf1Va5fOaFTTZ2KMDDPEdPG1EPzXsizK5XKUTndnN2545v7vfOtbvyQyqoaRoABVIQCAIkHf75IlSxNnnbXm0lgs1kjEtOs63LIs4pyT4zg9BUM+ByDQlwSs4ZrhbrAE56C0b7fWlge/TqiJTDjZrfR1hawx/z/B11EoQPL/LjmlFTPS8/PwF72GmZ59F+0r6ruqnOlfy+NjUaX8DyK/iyWYPbr0DCrlkue52s3lXvz0Jz/1hVQq5Y6uJEcYyRAAQKTTTj991qpVZ15N5I/jDmr7+VUBiai4STis9AY1Egr4Sk3Tg/l+tTaD9yR/hc+pCQUN0SV3hVz4Mu9VeWRC6egNY0yv1QzLvbae73kkXBOBcq0Q/T3GnteV+e7y+TMmPFSwMDGQ/9qc45Blye7//elPf7R+/bqtKPxhICEAgIJg9rZkQ4M44/TTX9rS2rIglc15jJiMxWKFYV08mAiIsbLNwOHkwPBj1d5/oG5ulfY1lM3NwXFEBURhPc/7tf9woe8XrkFgwCu8jsiULkREdRRgLDpY0SW1Uxaxu75+d2Oh6b9aDT/quaJt8v09jLF8609PDonROrtrz4t//cpXbv0mY0yh8IeBhAAACoKby1lr1iy5+qUvf4fWhhhnPGbFSHseuZ5HiUSCUqkU2bYdWtMtWrUa1GDXZoa61l/6XkVJXaanNl9boVemj78P+RS1vJ6ICpPSVHy+8iZl3zP43OOhABuovAattdfZ1fXizZ//wn8eP3asayCODSAMAQAQUVFhJc8+97yXL1+x4oxUNqO5YaTzmchB9n8wJC3/Qr8Zs89N/oM1njucOT/UhU7vUQCMhc9L8ep+xUmCnIh477NSON3lRgeUL3SqzaUQHnte/Mb1zaVQ+tY97+PvuNrIjOFSrqAuvaZLR3f0BHP1F/TGGGLESGlFQkp/4q1g4SVtiEvSXV0d7bf/5tdfuvOO322o+w0AaoAAAIio5wY3adLkxBmrV53BBOOe63mWZUsTNFES9ZrWtVxttxbjoTZYj/6MqKhnH0PR/47vtrfSQEMbTbFYjFyliDEi1yhKWDHSnqJUuju9a+cL93z1K1/5heu6GO8HgwIBABBRz+xt551//sILL3nJpalU1pPSKro+qiW09beJF4VG71pnrc8Hv5fOMVDL+9R63mtp0h7L32E9Izlq2RdnnDzPJS4EOdksNSSTpJSm9o52J93d8dRH//XfPnro0KHMABw6QCQEAFDU/L/itJVXzpw2fV4mm/Oi1m+vVbV+0KGajGckC85B+bkAak8uq2UuhdIArd6+6v6e09HwnVQTNXtfra/r/RpDWhuyLU5MWqSU1kop7nneCzfeeONHnnrqycMDeewApRAAQMHK006bfNVVV78+v9gPDy/gUq6AGYhCfCwUDP1Va/N/fxIbo2aOi2pFqPT+9RqL323v+R2qbxf9OkPJeJyy2SzFYjZ1dXZr4jr929/88ou3//a36wfl4AFCKsxUDeNF/ibNTlm27JRVq1Yt8JSiwjzkFV5TeoOrpRYafqx0eNxgC5K2RtLQs6hulb6cl0qjDMrtr57Cv15D/d0OlVrPUy1DATnnlM1mKR6PUzab05Yl048+vPY7t95yyy+ymQz6/WHQoQVgnAtqIg0NDdaZa86+LB5PTnY9TwthEeflF3IpNdD5AONJufNUa5BFVDmxbyj77sfyd146pXO58xr1eNSIDyIiS1qklattS/Bt27c/+rlP/8eXjx096g7G8QOUQgAwzhXG/p9zzqLXvva1f+fkchSLJ0hpzZXSkQV5aeZ/1PAoGBzVkgSj1JJfUO4xiA7E+nKNB38v4W41zplOp9P65PHjT3/83z/y/mef3XhkQA4aoAYIAICIiJ2ydOny+QsXLkt1px2tja2Nzk9LWly7jCrwCzupcJMcCYVLtbkKRmIAMxBj50fCuR9torpHynelRHeVhbtjlFJkWRYxxsh1XX/4n+tqIsaPHD7y9Bdu/Nw//e0vf9k+CB8FoCzkAIxjQU1k9uzZ8cuvuPo6J+dobph0nBwxYsUT/owxUTfzkZYfAMOjWv99PddIkHgZj8epu7ubHMehWCxGmUxGe57Hj584uvMrX/7SR3/9q18/o/K5NwBDBS0A41hQAC4/deW0Sy+99MpsJqcT8Tg3ipMxPTPWhbcdK6Jq+0MxQQ6MDVEZ/eHHg/9KKcl1XXJdjyZNmkCO45LjuMQY01KI7Fdv/cotP/7xjx4wRhvkzcBQQwAwTgU3GyEEP/XUUy9paGqc4joe95QiIQQprUkpPwCod4a/0aDWfnEYXyol8IXVMkcD55xc1y20pGUyOeKck6dcsqXlfOc737z1h//3Pz8z+WYCXH8w1BAAjHPz5s1ruuLKK1/GGNm2bXnpVE5KmxMXnLRWhe1wc4KxrN6m91qG+RljyLZtcl23MKw2k8l4ruuc/PXvfvHzr3z5S7d0dnSmUPOH4YIAYJwKbjjzFyxccOaq1Wdlso5nS5tblk1CCvKUJsqv9zeWWwBGehLgWNKf8zqY1169EyDVmwMQ2t4TQji//MVvvvSZ//jkt9tPnkxxziMnaAIYCkgCHN/YvLlz50yYMHGRVkorrbhlSSLivZLk+nMTHIm01r1uvEgChFqU/m1E5ZGEryUhBDHGKJfL6bt+f9dPbrn5C99B4Q8jAVoAxqGgybF1wgS+es2ZKx3X1UJKzjkjV7lkKBghwEbZDap4nvXatg3fvPOvLLy03lpnsK+x01Iy1tU6SVLxdqWtR/nnDRHn+amBg9Ykzsl1c5oznl335OO33fz5z3724P793UIIUkoRwHBCADAOBQHA3Llz41de9bI3p7M5J5mIx4kzYoaR0ZqIVNEc56VN5gOxdG2Vo6y6v8rzDlTeO+eV+nBr20fv12EUQSWVz0u1grja831b0bCW/vfScf49++iZ0pdzTuQZMlr71w/zj8nN5XRjY4KvfXTtbR/51w9/ePu2rccYYyj8YURAADAOBTe8lpbWKZMmTZ5q2ZbNCjX+oIAf+QVZ1OI40auu9U2tBTq6DUaveq9x1lO697zeGBKcU07nyLL9YX+WZWmlFGUy3Ucee2Ltb2/6zxtv3rzpuWNo9oeRBAHAOGSMISklX7nytEtiMZsbIu7fyIgMhfs2668FD7VyK+QNZKGMAn68Kve9Fz+uPUVuYd4MQ5xzrZTSUkq59pG13/vQB9//hYMHDjgo/GGkQRLgOBMUZs3NzfKM1atWaa2TSinKLwHca7uwempLw916MBwJfUgiHOuKa/9ERJwLkpZFnDNqbGykrq6UllJyx3E6f3/HHV/44k1f+OrBAwccKSUKfxhx0AIwTsXjcTZj2oyEEDKuXMdhjNmcc1JExEzvDPlaDXXBPxCLtMD4UP7aqPGaCecCMCJGjIj8vxMuGGVzWd3Y2EDHjx/fc+ftt930la98+Ud79+zJCiFqXlUTYCghABhnggI6Fk/ICRMnNLJ8ApN/czT5m1rQtN77dbXsu8fgFcZBbbu0379akmItXQQlY7d77bvca3uer9ywVvlcjvA+l2FSfax+La+P2ogVHo6YA7BnE0MU5I36v/vZ/ow0acPI8RzNOecnTrZt+cH3vvuR//ned+87fvyYxzlHwh+MWAgAxqnW1uamGdOnLVbKIyKShZX+ihKciMrdd8v1vQ+1cuusV5rDoN5gpjRTvNzr0fow0kV/PxXGg/j/0fnv1viRAOcs/xiRMUZr7ZHg3Nu3d9/aW2/90ofuuO23z3Z2dmoM9YORDgHAODWhdUJLQ2PjPKWULjf/uV/ODfZwv9r2Wa2pv1rtvJaugmAfnPOKSwdXK+hZxOkoSq4sySIvmr1giGOIankf4ef6+z1XW4CpdPf1BJi15qxEXVfGz36N2Ef+ehA9tXghJOWcHMVjcVLK00YbiifitGH904/deOON7/vjvfds8rdD4Q8jHwKAcaqlpbXRErLBGKNZRJt1+SbTgVOuUCkXkNSS7V/L/soVdMHyyOFtqhWQNQ8VzJfshoq7VkrPAfM7l2va50CotWAfiGOpVEgPRwNSodWIiq/0nuMMjk0Tz/cUMEYUj8XIyWU9bQwxTp2//tWvfvlfX/nSTc9tfPYAkX8dofCH0QABwDjV1NSUtOyYdLTWbJBGg/SnSbyWPnei2muwtbxfrY+Xq8mG8w96va7QnTzyuglqDXaG8ljqFXXOo4Kb0rwR/0FO0fNC5Sf6EZw8z6VkMkmp7hRJyyI7HpOe52b/69Yv3/L9733n20cOH+4M9o9sfxgtEACMQ4wxsmxLKqU5C2o2g/A+9Q4bLG4ONjXVrgeqoKq3X798t4npFRhUe4+i48g3R5c7xsEomMs1lQ92EFD8vkMXhJR+P1FdNmGe55JlWZRKpTQx0tlMOnvixMlNn/vsf9x49+/vvC+dThdS/Id7+CtAPRAAjEeMkdFGK+1pJq1hnQuir6vxDeTwv77NBlf+uahnawpiQhMxDXZBMtyzG/YlEbMvqn1XxQ9EH1PcjpHjetoYRZZlOYcPHt7wrne+6y1PPvH4XiLSA5EfATAcEACMQ0ZrUkorYswjY+yyqf79fZ8+FjLGmKL++HpfX69ak/wqNZWXK6yiRiCUPe782HIzBEMBR1Kz/3Ao7gJgwcLXvbYzRNTZ1e0Rkae1c+K++/74nS/fcsv3Nz7zzJFgPyj8YbRCADBOOY6jOON6MG9dfanJ17rfSkuy9mV/pfuopem+Ug5A6eP1Ht9QNH9XO65aEizrUTlBc+iCj15dAPmCv7TtRmuliQwJTk53d9f27333u5/93//5/j3Hjx1zg21Q+MNohgBgnEqlulJaK49Ja9iOodbkvtLX1Dq8q55jYIwVWh2q3dT7ktsQbl2o+HoWnShYup++CI+kiCr8h6Lfv/R4en4nqhQE9KWbpuYWqFC3TZAPo40mrZX2XLd94zMbbrvp5ptueuKxx/Zlsxlk+MGYgQBgnEql0lnlqYwl7YlCCDLkD8WSQlDOdYg4J8EZGaWDu3MvfakhViqow4UkY72b5P3/sl4396j31VqRZVmktSGlPJJS5rf1n9PaUCxmkzGGPM8rvKe/LoIhIQQJIQpZ3UqpoiCh9HjDj0khC2srBIVuMDQs+L3neX8+eSIirTQZMr1aN6SUxLmgXC4XmrWx+vmtJVCq9fnSYZjV1JvbUeucE2W7jRj3J7Fi/iyOwe6YocK5llIQ54y09of2FVqSiJHxFHHGyZKCcrmcNmTSx48de/bu3//+f7/xja/9au/evamaPzzAKIEAYJzq7OzoTKe7DzfZsTmeIk8IwY3RlMspshMxcj2PPOWRZDx6AtUax79Hva6e11YahldpG8Y45XI5MsaQZVnEOSfHdUh5irjwC/FsNkuccxIiXwBrnS/4/YIhNJZbU76wcBwnWP+dl8v4V0pp7isED0RUeB+lVL5AksQ5J9d1SSlFQgiyLZu06XmN1ppc1yUpiWzb7rVoUw+eP8zyNfzB0NfvsB61jMQwFBFI+k+QkJIEF6SUl78m/HPJOSfHcYhpQ/FYjFKplHZdrl3XObLnxb1PffrTH//Qww8/vCeTQa0fxiYEAOPUiRMnu44ePf7CzDnzzu7s7CRjDNm2TY52yWjjT1hT6B2NVq2WV25mt0rNs+X64UsnZwm9ote2RERaKYrlb/Jaa/Jclxgxitk2ScsqPMY5JyklKaUol8s5ZEzaU57jKW0E51oIzl3Pc1zH84gMMS6kbYkY88eOB80BnDHOGRdSSiGlZcWNMaS1ImJEQvKivAVpCQqeV55HQgqybL/VIJfLkioEIoKCVeRKApIIOnRudZ/yOsuV5bUEYeXUOwyy0vtWDCRYcRnNg7di/rWgPI+IDNl2jCzLItdxKJvJUCKRIEGF76bzxImTL95+26+//J3vfOsO1PphrEMAMM4EBVE6nfL2Hdh/8vQzz/SbtfNN0/FYnFLZNPF84WM8z+8jrVBo13tjH8haY7lCQghRaN4P3k/mH8tls/kCWHd3dXcf7O7qPO7knM5MJn28u7OrLZPLnuzq6Dh24OCBfYcOHTrc2dmVyWWznjHGSMsSDQ3JRHNzS8O06dOmT5k8ZerEiZNmNDY2TrYsq0GTIc5YPJFsmJBIxBubmppaG5INU+KJRKtSShMRD3cFBE36wbFyzsmOxYgxRtlsloiILMsixnihq6LyOa2cK9DfORWGYk6GSp+x5mPMT9sXbO+39PitQp7rkm3bFI/HSRut06mMl+5OH9i1a8cDt9xyy3/97W/3b3ZdF7V+GPMQAIxTJ0+edJ984vGnL7/8sjTjPC6l1NlslkspyRKC/KqTKdv/3xe1JmWVr+333ja8jf+Y/2+tFGljKB6PkxCC0umM093d1ZbqSp/o7Oo4cvjwofV79+x59vktm7c8+eQTOw/sP5BKpbqV67rGdV2jtTZBzbtclwPjnHi+IGeMMSEkE4KTHYvxRDzBp06dGlt95plzFy5auGjO7LkrFi5ZfNGUyVPmxeKxGOcsGY/HW2w7lvQ8jyulKJlMkusp8pQiS4pCEOMHKx5JKUJdAOVqx72PdyBV6r6JCjxqbykgqmUkQLnvIqjF5x8N7SrIswgC3BiZ/NgXrbXu7u5qP7R//1+///3v3vqrX/7yqc7OTszhC+PG+Bn4C71cedVVC35z++1PptPZxkSywTbG8KBvWqmeJLWwajXLSl0AUaL776MDgNpbBYikkKQ8j7pT3SePHTu28+Txkzu2b9/610ceeujhw0cOpZ9//vkTx44ezXqeZwZ7KBdjjBobm+T8BfNbZs+e0zRx4gRrxakrT1115uqXTps6dcXUadMXNDc3T2GM4kSCmBDkOg6ZfACjtaZsNltIZMzvlYiivo/KEwkNRg0+2GdUgmT498r7rzwcsdpxl88ToKC1pyfT3/+3097e8eKOHdvX/udnPv25xx9b+2KFgwMYkxAAjEPBDXXq1GnWLbd+5ZZrX3ntu7UmOxaPSc9Thcz3an2/1bPxo/vzw9tX7+ctGQlQsl3hPfNDuZRytZPLdecymcPbtm9/7P4//ek3Dz744DObt2w+ks1ktFJqRAzcZoKzmBVjs2bPbDr7nHNOPe200xeuWLHijPkLF75k6tSp8xobmyZqrWWQOOi6waiG3kmARf+uUljWGgBUGrNfS/5G/UFVpUCw+oRF4YeKckfy/9bakDGKjNFOd3f3vkceeujX3/nWt364du0jL2hM3g/jFAKA8Y296vrrL/7u977/YzuWnKW0x4WQnDFGSqlCclxY1I04agKc/BZF25W+plzNsHjcfPHreIWM8I6O9kN79+19ZtvW5x/46Q9/dN/69ev3d3S0t1Et48uGH4sn4okFCxdNu/TSy8645tprrpk7d97KhsbGORNaW2dIO8YZMVJa+VMGlwu8KhTC9c4jUMtoi5rH2ld9r8qvK3fspa1Fpa0BjPxrJpvNOtls5tiGjc/84Rtf+/oP7v/zn57WSjk1HRzAGIUAYJybOXNWw41fvPlTr7ruug8z4py45MbowpC4QFSts3qNsPd0vrUWKlLKfNKbPw7ezWfs25ZFnudRsIar1h51d3fv2/H8tgcee/zRe3/y4x8/sG3b1iM0Ogr9SvikyZMnXPXSl65+ySWXrlmxfMU5M2fPXNPc2jIpEU80MubnA6h80zZjjDyliBEVAjdjeqZUDp/nchXeWk9Yue+w2ndbbt4E/5hMYUhmad5F8fVQPKdCsD0raSkyhkgITp7nOEcOHtyyZfPm+3/+i1/cecfvbn/S8zyn3mAIYCxCAADsNa973blf/drXf9nQ2DTHECPXdXkymSQiKtx0+9KsXBoA1Dv0TwhBnucnvxWG0OUfIyJKd3d3Hjhw4PE///m+n/3gf/7n3p07XzhB+XJstN7gyxSmfOqUqU0XXfqSBdde88orly5fdtnUyVNPaWltnRWLxeJkDHlKEZEhKS0iMuS6/jmyLEnGUKElJzyZUXhoIueMtKleow+UK8yrTVIU/bwhramoQA/vPxghkUgkguGaZFlWITA0xlA8ZofnaCDleU5nV9eRp5584q4f/M//fO8P9969mYi88D4BxjsEAONYcCOcO39+8sbPf/7Gq65+2XsaGhrjSinuOA7FE4lCrZKocuEQ3Txb29S6vbP/qVDjKxT80p+Vz8nlNBHpvS/u3rJh4zP/8+3//u871q1bd4Dyq7LV8n6jBmPEWa/15dmECRNbr37p1avPPuvseXPmzVs4Y/qMs2bNnr162tSpkzQjGXThhIdCcs7JyreeBMFAMBGRMf4ER8Q4MSaoUltApSCuP+fdmJ7gJPjOg+b8IIkvHo+T4ziklCLbtsnzPNJaUywWIyeXpXjcpu6urvb9+/c/vXH9M3/73e9+98Tdd//+ESLKElFhTggA8CEAGOeCG+wVV121/Bvf+OYPJk2ZsioWi8eJ+c2yYbUGAOFm2FrePxCumQaFVMyfoY1iMZsE53TixMnjW7ds/sO99/z+h9/97ncfymWz46JWFy4Mi881t1avXj39iiuvXHP22Wdd0tTaOn3WrFkLpk2bvsKyrOagkNdaFwKBYO4B13UL+xZCkNKmpu+sVvUkC/qfz//+g0I6vDZDkJgaHnEQTJaktaIjhw7v3rl7x9NPP/nU/T/96U/+8PzmzQeISAXnDQU/QG8IAMa54GacbGiU/++9/+/6f3nfB25taGicRcSIOOdRSXelzbPlFOemVR7mFf7dGH8ufpPv45bS0t3dHendu3Y99PRTT9/2rW9+/Y5tW59vIyIzHmt14Zpy6LMzIpITJ02yVp955pRrr33lxTNnzz5j4cIFF0+aPGViMh6fEIvHJ0shSPutApoxxvOFrDZGkyHO+3NLqDYPQPVroLgFJ19w63wOAHddtzA7Yi6b0zkne+TggYM7nlm//sEnHlt73+/vuH3T8RMnuig/LeJ4vDYA6oEAAHq6AubOTfznTTd/7LrrXvV+z1ONXHAelchXqnwQUPnyKjcEMKidMsbIsizqaG/b98jDD//oRz/6wf/dd++9u4nIjLnm/j5jxHnvlgEiIimlff4FF8xZtGhxfPbcufOXnrL0gqVLT7lw/vz5a1pbWxu11uR6nr/6ndZkDMvPqR/dV1+pj7/e76E0WAgHANr/LFoKUVhPIWgNyGaz2ee3bHl45ws7Htm6dfNDj659bNejax8+qJXygn2h4AeoDQIAIKKem+YZq1fNuOmLt3xxzZo1r+RcNrL8yjk8PMuLP7tK0eujM8B7bvKc9RQuwfOlhUAwHlsppWOxGLeESW/ZvH3DXXfd+Y3vf+87dx88sD+NJt3Kwkl+pUM4m5tb5DnnnDP1gosuOmfevPlLJkyasHD69BlLJk+ePG3ihAnT7Xh8ImOC+yMKPFKep8mfZJ8H+yZGnFGwWiNpbXQhZaD4GmC9JpHS2hDjrDCUMxy0GONfI0IKzhgvfIZ0KnWyvb398JHDh/e+8ML2dUeOHHnuD/fc/cBjjz920s3lCoV+eOQDANQGAQD48hPpGGNo9ZlnzvzEf3z6/RddeNG7Y4lkKxGR0VozzrnR/hBBHhoiWJiCN59FroOhXcaQZdnEGBUy90V+6VujtT+Vrj+kSxMZ4pxzwQXF4pKef37bM3/585+/de/ddz10/5//tIPGaXN/f5Qm1ZUWjlOnTG04deWpE2dMn5mcPnPGzPkLFpw+fcb0RVOnz5gzbdrUOa1NLdOZYAlpxZoTiZg0hpE2HpHhZEgRGU6MGyJTkuxZMlNx0L/PiEhpTVr7S+8KKYgzTq7nkptzHCeXPdbR3XX42NGje9rb2k4c3H9ox44Xtm04eODA3h07dnStX//0ER2KaoLPV36FRACoBAEAFAlq5aedfnrz29/5zmsuvvjSty5evORSIUVcK6UpNEkQEfFwxnbJnkgZTUwKv4nZaGKe9meqI9L+GP5Coh8nIvIcp3v79u0Pbt2y+bF77rn7jtt+85uthowiQrPuQAgKzCDJsrTQ5JyLyVMm27Nnz0mccsopk2fPmT3LsuympsbGlkmTJ0+eNGnSzNYJLdOamlunJOOxRiGthLREQkgrZlt23JKWxfySnpPxF+Qzxmgy2lNKuUrpbNbJ5dxsNpXJOe3dXV0njx07fvDo0UN7Oto6Dne0tx8/ePDgwZ07dx45fPhQbv++/VnHdYqaMaSUhdwHFPoA/YMAAHoJClspJbvy6qtnXXPtda887/zzr583d95L4omEzTkLxl/r/A8PmuYZ8/MGGTHiUlAmlyMioy3LIlJaO65Lggtp2xZ5SpHRmrpTqT3PrH/mL+vXPfWnv/31L399+KEHj1O+YTlcg4WBFV6JsNo5tm2bzZgxw5oyZUp8wqSJycaGxphlW1JwwYUUVjweT8QTyYQU0hKcSS6ENMZoz1Oe4zi5TCaVymZyGUNaKU+7XV2dmWPHjmcOHTyQPXbsmOu6btk3D44ThT7AwEIAAJGEEIU+5ImTJslzzj136po1a8684qqr3rhixamXxOzYNMaZ9Cfp8Zv7lecFAYE/U5vgxBknLjiXUnIuBAnOyc3mdKq769juF/esXffkE/ds3Ljx6ccefWznli2bU5Qv+NGnO/R6grjiBMs+trz0JHz08b0R+AEMLgQAUFZUpv255503YdXq1acsWbJ01YKFC5e2tLa0Tp0yZVFjc/OMZDw+yY7FGi3LsoWQZMiQVkqnUqn2tvb23Z0dHS90tLd37dq5c+eGDRse2rRp87bHH1t70nO9whv447pR0xtJwqMzyi3QU62wLi3go1oe8J0DDC0EAFBVsO59OKs8kUyI2bNmWw0NDXLq9GnNE1pbm5pbWic2NDRMsCzLFlJYyvWUp7yu9ra2k4cPHT7W1t7e2dXZ6R45esQ9fux4YSEWNPGOLUVBQj45FAAARrFgxriBIoSoebU4AAAYWLj7Qp9ENQdXmvq1tKBHRj8AwPBCAAADqtwSsAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAw9v3/YGyo/c/881IAAAAASUVORK5CYII=";
    std::wstring svgChevron = LR"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><polyline points="9 18 15 12 9 6"></polyline></svg>)";
    std::wstring svgHome = LR"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M3 9l9-7 9 7v11a2 2 0 01-2 2H5a2 2 0 01-2-2z"/><polyline points="9 22 9 12 15 12 15 22"/></svg>)";
    std::wstring svgGear = LR"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 00.33 1.82l.06.06a2 2 0 11-2.83 2.83l-.06-.06a1.65 1.65 0 00-1.82-.33 1.65 1.65 0 00-1 1.51V21a2 2 0 01-4 0v-.09A1.65 1.65 0 009 19.4a1.65 1.65 0 00-1.82.33l-.06.06a2 2 0 11-2.83-2.83l.06-.06a1.65 1.65 0 00.33-1.82 1.65 1.65 0 00-1.51-1H3a2 2 0 010-4h.09A1.65 1.65 0 004.6 9a1.65 1.65 0 00-.33-1.82l-.06-.06a2 2 0 112.83-2.83l.06.06a1.65 1.65 0 001.82.33H9a1.65 1.65 0 001-1.51V3a2 2 0 014 0v.09a1.65 1.65 0 001 1.51 1.65 1.65 0 001.82-.33l.06-.06a2 2 0 112.83 2.83l-.06.06a1.65 1.65 0 00-.33 1.82V9a1.65 1.65 0 001.51 1H21a2 2 0 010 4h-.09a1.65 1.65 0 00-1.51 1z"/></svg>)";
    std::wstring svgGlobe = LR"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"/><line x1="2" y1="12" x2="22" y2="12"/><path d="M12 2a15.3 15.3 0 010 20 15.3 15.3 0 010-20z"/></svg>)";
    std::wstring svgPlay = LR"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M8 5v14l11-7z"/></svg>)";

    std::wstring htmlBody = LR"HTML(
<div class="outer-container">
    <div class="wrapper pre-welcome" id="mainWrapper">
        <div class="sidebar" id="sidebarNav">
            <div class="sidebar-icon active" id="sidebarHome" onclick="sidebarGoHome()">)HTML" + svgHome + LR"HTML(</div>
            <div class="sidebar-divider"></div>
            <div class="sidebar-icon" id="sidebarSettings" onclick="sidebarGoSettings()">)HTML" + svgGear + LR"HTML(</div>
        </div>
        <div class="content-area">
        <div class="title-drag-area" onmousedown="window.chrome.webview.postMessage('drag_window')"></div>
        <div class="top-icons-row">
            <div class="icon-btn" id="btnSiteIcon" onclick="window.open('https://dunevisualss.web.app')">)HTML" + svgGlobe + LR"HTML(</div>
        </div>
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
            <div class="image-frame">
                <div class="status-pill" id="cardStatusPill">FABRIC 1.21.4</div>
                <button class="btn-play-circle" id="cardPlayBtn" onclick="handleMainButton()">)HTML" + svgPlay + LR"HTML(</button>
            </div>
            <div class="description font-medium text-main" id="mainDesc">desc</div>
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
            <div class="btn-extra-settings btn-mods-pos" id="btnModsFolder" onclick="openModsFolder()">
                <span id="modsLabel">Mods</span>
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
const L={ru:{welcome:'Добро пожаловать',choosePrefs:'Выберите настройки',language:'Язык',theme:'Тема',continue_:'Продолжить',settings:'Настройки',ram:'Оперативная память',saveExit:'Сохранить и выйти',site:'Сайт',launch:'Запустить',terminate:'Завершить',cancel:'Отменить',loading:'Загрузка',done:'Готово',desc:'Максимальная оптимизация, скорость и комфорт.',settingsSaved:'Конфигурация сохранена',launchedCache:'Запущен из кеша',gameTerminated:'Игра завершена',clientLaunched:'Клиент запущен',process:'Процесс',nickname:'Никнейм',save:'Сохранить',nickSaved:'Никнейм сохранён',nickEmpty:'Введите никнейм',extraSettings:'Доп. Настройки',mods:'Моды'},
en:{welcome:'Welcome',choosePrefs:'Choose your preferences',language:'Language',theme:'Theme',continue_:'Continue',settings:'Settings',ram:'RAM',saveExit:'Save & Exit',site:'Site',launch:'Launch',terminate:'Terminate',cancel:'Cancel',loading:'Loading',done:'Done',desc:'Maximum optimization, speed and comfort.',settingsSaved:'Configuration saved',launchedCache:'Launched from cache',gameTerminated:'Game terminated',clientLaunched:'Client launched',process:'Process',nickname:'Nickname',save:'Save',nickSaved:'Nickname saved',nickEmpty:'Enter a nickname',extraSettings:'Advanced Settings',mods:'Mods'}};
function t(k){return L[currentLang][k]||k;}
function refreshSlider(){const s=document.getElementById('ramSlider');updateSliderBackground(s.value,s.min,s.max);}
function applyLang(){
document.getElementById('welcomeTitle').innerText=t('welcome');document.getElementById('welcomeSubtitle').innerText=t('choosePrefs');
document.getElementById('langLabel').innerText=t('language');document.getElementById('themeLabel').innerText=t('theme');
document.getElementById('welcomeContinueBtn').innerText=t('continue_');document.getElementById('settingsTitle').innerText=t('settings');
document.getElementById('ramLabel').innerText=t('ram');document.getElementById('btnSaveExit').innerText=t('saveExit');
document.getElementById('loadingTitle').innerText=t('loading');document.getElementById('btnCancelText').innerText=t('cancel');
document.getElementById('mainDesc').innerText=t('desc');
document.getElementById('nickLabel').innerText=t('nickname');
document.getElementById('btnNickSave').innerText=t('save');
document.getElementById('extraSettLabel').innerText=t('extraSettings');
document.getElementById('modsLabel').innerText=t('mods');
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
function finishWelcome(){welcomeScreen.classList.add('hidden');mainScreen.classList.remove('inactive-right');mainScreen.classList.add('active');document.getElementById('mainWrapper').classList.remove('pre-welcome');window.chrome.webview.postMessage("welcome_done:"+currentLang+":"+currentTheme);}
function goToSettings(){mainScreen.classList.remove('active');mainScreen.classList.add('inactive-left');settingsScreen.classList.remove('inactive-right');settingsScreen.classList.add('active');document.getElementById('nicknameInput').value=currentNickname;document.getElementById('sidebarHome').classList.remove('active');document.getElementById('sidebarSettings').classList.add('active');}
function sidebarGoHome(){if(settingsScreen.classList.contains('active')){saveAndExitSettings();}}
function sidebarGoSettings(){if(!settingsScreen.classList.contains('active')){goToSettings();}}
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
function saveAndExitSettings(){closeExtraPanel();settingsScreen.classList.remove('active');settingsScreen.classList.add('inactive-right');mainScreen.classList.remove('inactive-left');mainScreen.classList.add('active');document.getElementById('sidebarSettings').classList.remove('active');document.getElementById('sidebarHome').classList.add('active');let ram=document.getElementById('ramSlider').value;window.chrome.webview.postMessage("save_ram:"+ram);showToast(t('settings'),t('settingsSaved'));}
function showToast(title,desc){const toast=document.getElementById('toast');document.getElementById('toast-title').innerText=title;document.getElementById('toast-desc').innerText=desc;toast.classList.add('show');setTimeout(()=>{toast.classList.remove('show');},3000);}
const slider=document.getElementById('ramSlider'),output=document.getElementById('ramValue');
function updateSliderBackground(v,mn,mx){const p=((v-mn)/(mx-mn))*100;const bg=currentTheme==='dark'?'#1c1c20':'#E0E0E0';slider.style.background='linear-gradient(to right, var(--green) '+p+'%, '+bg+' '+p+'%)';}
slider.addEventListener('input',function(){output.innerHTML=this.value+"MB";updateSliderBackground(this.value,this.min,this.max);});
function handleMainButton(){window.chrome.webview.postMessage("action_button");}
function openModsFolder(){window.chrome.webview.postMessage("open_mods");}
function cancelInstall(){window.chrome.webview.postMessage("cancel_install");loadingScreen.classList.remove('active');loadingScreen.classList.add('inactive-right');mainScreen.classList.remove('inactive-left');mainScreen.classList.add('active');}
function setRunningState(r){isGameRunning=r;const btn=document.getElementById('mainLaunchBtn');if(r){btn.innerText=t('terminate');btn.classList.add('btn-quit-mode');}else{btn.innerText=t('launch');btn.classList.remove('btn-quit-mode');}}
)JS";

    std::wstring js2 = LR"JS(
function startLoadingUI(){closeExtraPanel();mainScreen.classList.remove('active');mainScreen.classList.add('inactive-left');loadingScreen.classList.remove('inactive-right');loadingScreen.classList.add('active');document.getElementById('successCheck').style.display='none';document.getElementById('loaderFill').style.width='0%';document.getElementById('errorLog').style.display='none';document.getElementById('errorLog').innerText='';}
function updateProgress(p,c,tot,s){document.getElementById('loaderFill').style.width=p+'%';document.getElementById('currentMb').innerText=c;document.getElementById('totalMb').innerText=tot;document.getElementById('loaderStatus').innerText=s;}
function showError(m){const el=document.getElementById('errorLog');el.style.display='block';el.innerText+=m+'\n';el.scrollTop=el.scrollHeight;}
function finishLoading(){document.getElementById('loaderStatus').innerText=t('done');document.getElementById('loaderStatus').style.color='var(--green)';document.getElementById('successCheck').style.display='flex';setTimeout(()=>{loadingScreen.classList.remove('active');loadingScreen.classList.add('inactive-right');mainScreen.classList.remove('inactive-left');mainScreen.classList.add('active');setRunningState(true);showToast(t('done'),t('clientLaunched'));},2500);}
function skipWelcome(lang,theme,nick){currentLang=lang;currentNickname=nick||'Player';applyTheme(theme);applySettingsLang(lang);applyLang();welcomeScreen.classList.add('hidden');mainScreen.classList.remove('inactive-right');mainScreen.classList.add('active');document.getElementById('mainWrapper').classList.remove('pre-welcome');}
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
                                else if (msg == L"open_mods") {
                                    std::wstring modsDir = GetModsDir();
                                    fs::create_directories(modsDir);
                                    ShellExecuteW(nullptr, L"open", modsDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                                }
                                else if (msg.find(L"save_nick:") == 0) {
                                    std::wstring nick = msg.substr(10); std::wstring safe;
                                    for (wchar_t c : nick) {
                                        if ((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L'_') safe += c;
                                    }
                                    if (safe.empty()) safe = L"Player"; if (safe.length() > 16) safe = safe.substr(0, 16);
                                    g_Nickname = safe;
                                    SaveRegistry(LoadRegistry().isInstalled, g_RamAmount, g_DarkTheme, g_LangRu, g_Nickname, LoadRegistry().modVersion);
                                    std::string nickUtf8 = SanitizeNicknameForJson(safe);
                                    std::wstring js = L"{ \"type\": \"set_nickname\", \"value\": \"" + Utf8ToWide(nickUtf8) + L"\" }";
                                    g_webview->PostWebMessageAsJson(js.c_str());
                                }
                                else if (msg.find(L"save_ram:") == 0) {
                                    int requested = std::stoi(msg.substr(9));
                                    int safe = GetSafeRamAmount(requested);
                                    g_RamAmount = safe;
                                    SaveRegistry(LoadRegistry().isInstalled, g_RamAmount, g_DarkTheme, g_LangRu, g_Nickname, LoadRegistry().modVersion);
                                    if (safe != requested) {
                                        std::wstring js = L"{ \"type\": \"set_ram\", \"value\": " + std::to_wstring(safe) + L" }";
                                        g_webview->PostWebMessageAsJson(js.c_str());
                                    }
                                }
                                else if (msg.find(L"set_theme:") == 0) { g_DarkTheme = (msg.substr(10) == L"dark"); SaveRegistry(LoadRegistry().isInstalled, g_RamAmount, g_DarkTheme, g_LangRu, g_Nickname, LoadRegistry().modVersion); }
                                else if (msg.find(L"set_lang:") == 0) { g_LangRu = (msg.substr(9) == L"ru"); SaveRegistry(LoadRegistry().isInstalled, g_RamAmount, g_DarkTheme, g_LangRu, g_Nickname, LoadRegistry().modVersion); }
                                else if (msg.find(L"welcome_done:") == 0) {
                                    std::wstring p = msg.substr(13); size_t s = p.find(L':');
                                    if (s != std::wstring::npos) { g_LangRu = (p.substr(0, s) == L"ru"); g_DarkTheme = (p.substr(s + 1) == L"dark"); }
                                    SaveRegistry(LoadRegistry().isInstalled, g_RamAmount, g_DarkTheme, g_LangRu, g_Nickname, LoadRegistry().modVersion);
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