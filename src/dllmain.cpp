
#include "stdafx.h"
#include "helper.hpp"

#ifdef _DEBUG
    #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#else
    #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#endif

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <inipp/inipp.h>
#include <safetyhook.hpp>
#include <atomic>
#include <cwctype>
#include <mutex>
#include <unordered_set>

#define spdlog_confparse(var) spdlog::info("Config Parse: {}: {}", #var, var)

HMODULE exeModule = GetModuleHandle(NULL);
HMODULE thisModule;

// Fix details
std::string sFixName = "SekiroFix";
std::string sFixVersion = "0.0.4-resource-log-test3";
std::filesystem::path sFixPath;

// Ini
inipp::Ini<char> ini;
std::string sConfigFile = sFixName + ".ini";

// Logger
std::shared_ptr<spdlog::logger> logger;
std::string sLogFile = sFixName + ".log";
std::filesystem::path sExePath;
std::string sExeName;

// Aspect ratio / FOV / HUD
std::pair DesktopDimensions = { 0,0 };
const float fPi = 3.1415926535f;
const float fNativeAspect = 16.00f / 9.00f;
float fAspectRatio;
float fAspectMultiplier;
float fHUDWidth;
float fHUDWidthOffset;
float fHUDHeight;
float fHUDHeightOffset;

// Ini variables
bool bBorderlessWindowed;
bool bUnlockResolutions;
bool bUnlockFPS;
bool bFixAspect;
bool bFixHUD;
bool bDisableCameraReset;
bool bAutoLoot;
bool bHideAwarenessMarkers;
bool bHideVignettes;
bool bLogResourcePaths;
bool bPreventDragonrot;
bool bDisableDeathPenalties;
bool bLogStats;
bool bSpiritEmblemUpgrade;
float fGameplayFOVMulti;

// Variables
int iCurrentResX;
int iCurrentResY;
float fCurrentFramerate;
std::uint8_t* pPlayerDeaths;
std::uint8_t* pTotalKills;

struct DLString
{
    wchar_t* string;
    void* unk;
    std::uint64_t length;
    std::uint64_t capacity;
};

struct SekiroString
{
    void* unk;
    DLString string;
};

using SekiroArchivePathFn = void* (*)(SekiroString*, std::uint64_t, std::uint64_t, DLString*, std::uint64_t, std::uint64_t);

SafetyHookInline ResourcePathHook{};
std::vector<SafetyHookMid> ResourcePathCandidateHooks{};
std::atomic_uint64_t ResourcePathCallCount = 0;
std::atomic_uint64_t ResourcePathCandidateCallCount = 0;
std::mutex ResourcePathLogMutex;
std::unordered_set<std::wstring> LoggedResourcePaths;

bool IsReadableAddress(std::uint8_t* address, size_t size)
{
    if (!address || reinterpret_cast<uintptr_t>(address) < 0x10000 || reinterpret_cast<uintptr_t>(address) >= 0x000F000000000000)
        return false;

    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(address, &mbi, sizeof(mbi)))
        return false;

    if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) || (mbi.Protect & PAGE_NOACCESS))
        return false;

    return reinterpret_cast<uintptr_t>(address) + size <= reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
}

template<typename T>
bool ReadMemory(std::uint8_t* address, T& value)
{
    if (!IsReadableAddress(address, sizeof(T)))
        return false;

    __try
    {
        value = *reinterpret_cast<T*>(address);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

std::string ReadAsciiPreview(std::uint8_t* address, size_t maxLength = 96)
{
    if (!IsReadableAddress(address, 1))
        return "<unreadable>";

    std::string preview;
    for (size_t i = 0; i < maxLength; i++) {
        if (!IsReadableAddress(address + i, 1))
            break;

        char c = *reinterpret_cast<char*>(address + i);
        if (c == '\0')
            break;

        preview += (c >= 32 && c <= 126) ? c : '.';
    }

    return preview.empty() ? "<no ascii>" : preview;
}

bool IsWritableProtect(DWORD protect)
{
    protect &= 0xFF;
    return protect == PAGE_READWRITE || protect == PAGE_WRITECOPY || protect == PAGE_EXECUTE_READWRITE || protect == PAGE_EXECUTE_WRITECOPY;
}

bool IsScannableProtect(DWORD protect)
{
    if (protect & PAGE_GUARD)
        return false;

    protect &= 0xFF;
    return protect == PAGE_READONLY || protect == PAGE_READWRITE || protect == PAGE_WRITECOPY ||
        protect == PAGE_EXECUTE_READ || protect == PAGE_EXECUTE_READWRITE || protect == PAGE_EXECUTE_WRITECOPY;
}

std::string HexValue(uintptr_t value)
{
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << value;
    return stream.str();
}

void PatchLayoutDimension(std::uint8_t* address, size_t digitCount)
{
    std::string zeroes(digitCount, '0');
    Memory::PatchBytes(address, zeroes.c_str(), static_cast<unsigned int>(zeroes.size()));
}

std::string WideToUtf8(std::wstring_view value)
{
    if (value.empty())
        return {};

    const int requiredSize = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (requiredSize <= 0)
        return {};

    std::string result(requiredSize, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), requiredSize, nullptr, nullptr);
    return result;
}

std::wstring ReadDLString(const DLString* value)
{
    if (!value || !IsReadableAddress(reinterpret_cast<std::uint8_t*>(const_cast<DLString*>(value)), sizeof(DLString)) ||
        !value->string || value->length == 0 || value->length > 1024 || value->capacity < value->length)
        return {};

    if (!IsReadableAddress(reinterpret_cast<std::uint8_t*>(value->string), static_cast<size_t>(value->length) * sizeof(wchar_t)))
        return {};

    size_t length = static_cast<size_t>(value->length);
    if (length > 0 && value->string[length - 1] == L'\0')
        length--;

    return std::wstring(value->string, length);
}

std::string DescribeDLString(const DLString* value)
{
    std::ostringstream stream;
    stream << "struct=" << HexValue(reinterpret_cast<uintptr_t>(value));

    if (!value || !IsReadableAddress(reinterpret_cast<std::uint8_t*>(const_cast<DLString*>(value)), sizeof(DLString))) {
        stream << " <unreadable>";
        return stream.str();
    }

    stream << " string=" << HexValue(reinterpret_cast<uintptr_t>(value->string));
    stream << " length=" << value->length;
    stream << " capacity=" << value->capacity;

    const std::wstring text = ReadDLString(value);
    if (!text.empty())
        stream << " text=\"" << WideToUtf8(text) << "\"";

    return stream.str();
}

bool IsInterestingResourcePath(std::wstring path)
{
    std::transform(path.begin(), path.end(), path.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });

    return path.find(L"menu") != std::wstring::npos ||
        path.find(L"01_common") != std::wstring::npos ||
        path.find(L"sb_fe") != std::wstring::npos ||
        path.find(L"sb_dying") != std::wstring::npos ||
        path.find(L".gfx") != std::wstring::npos ||
        path.find(L".layout") != std::wstring::npos ||
        path.find(L".dcx") != std::wstring::npos ||
        path.find(L".bnd") != std::wstring::npos;
}

void LogResourcePath(std::string_view label, const DLString* path)
{
    const std::wstring value = ReadDLString(path);
    if (value.empty() || !IsInterestingResourcePath(value))
        return;

    std::scoped_lock lock(ResourcePathLogMutex);
    const auto [_, inserted] = LoggedResourcePaths.insert(value);
    if (!inserted)
        return;

    spdlog::info("Resource Path Log: {:s}: {:s}", std::string(label), WideToUtf8(value));
}

void* SekiroArchivePathDetour(SekiroString* path, std::uint64_t p2, std::uint64_t p3, DLString* p4, std::uint64_t p5, std::uint64_t p6)
{
    const auto callCount = ++ResourcePathCallCount;
    if (callCount <= 64 || callCount % 1000 == 0) {
        spdlog::info("Resource Path Log: call #{:d}: path {:s}; p4 {:s}; p2={:s}; p3={:s}; p5={:s}; p6={:s}",
            callCount,
            DescribeDLString(path ? &path->string : nullptr),
            DescribeDLString(p4),
            HexValue(static_cast<uintptr_t>(p2)),
            HexValue(static_cast<uintptr_t>(p3)),
            HexValue(static_cast<uintptr_t>(p5)),
            HexValue(static_cast<uintptr_t>(p6)));
    }

    LogResourcePath("input", path ? &path->string : nullptr);

    void* result = ResourcePathHook.call<void*>(path, p2, p3, p4, p5, p6);

    if (callCount <= 64 || callCount % 1000 == 0) {
        spdlog::info("Resource Path Log: call #{:d}: result {:s}",
            callCount,
            DescribeDLString(result ? &reinterpret_cast<SekiroString*>(result)->string : nullptr));
    }

    if (result)
        LogResourcePath("output", &reinterpret_cast<SekiroString*>(result)->string);

    return result;
}

void LogResourcePathCandidateCall(size_t candidateIndex, std::uint8_t* candidateAddress, SafetyHookContext& ctx)
{
    const auto callCount = ++ResourcePathCandidateCallCount;
    const auto* path = reinterpret_cast<SekiroString*>(ctx.rcx);
    const auto* p4 = reinterpret_cast<DLString*>(ctx.r9);

    const std::wstring text = ReadDLString(path ? &path->string : nullptr);
    const bool interesting = !text.empty() && IsInterestingResourcePath(text);

    if (interesting) {
        std::ostringstream label;
        label << "candidate #" << candidateIndex << " input";
        LogResourcePath(label.str(), path ? &path->string : nullptr);
    }

    if (interesting || callCount <= 128 || callCount % 1000 == 0) {
        spdlog::info("Resource Path Candidate: call #{:d}: candidate #{} at {:s}+{:x}: path {:s}; p4 {:s}; rdx={:s}; r8={:s}",
            callCount,
            candidateIndex,
            sExeName.c_str(),
            candidateAddress - reinterpret_cast<std::uint8_t*>(exeModule),
            DescribeDLString(path ? &path->string : nullptr),
            DescribeDLString(p4),
            HexValue(static_cast<uintptr_t>(ctx.rdx)),
            HexValue(static_cast<uintptr_t>(ctx.r8)));
    }
}

bool PatchLayoutMarkerEntry(std::uint8_t* chunkStart, std::uint8_t* bufferStart, std::uint8_t* bufferEnd, std::string_view markerName)
{
    bool bPatched = false;
    auto searchStart = bufferStart;

    while (searchStart + markerName.size() <= bufferEnd) {
        auto match = std::search(searchStart, bufferEnd, markerName.begin(), markerName.end());
        if (match == bufferEnd)
            break;

        auto lineEnd = match;
        while (lineEnd < bufferEnd && lineEnd - match < 256 && *lineEnd != '\n')
            lineEnd++;

        constexpr std::string_view widthPrefix = "width=\"";
        constexpr std::string_view heightPrefix = "height=\"";

        auto widthMatch = std::search(match, lineEnd, widthPrefix.begin(), widthPrefix.end());
        auto heightMatch = std::search(match, lineEnd, heightPrefix.begin(), heightPrefix.end());

        if (widthMatch != lineEnd) {
            auto digitStart = widthMatch + widthPrefix.size();
            auto digitEnd = digitStart;
            while (digitEnd < lineEnd && *digitEnd >= '0' && *digitEnd <= '9')
                digitEnd++;

            if (digitEnd > digitStart) {
                PatchLayoutDimension(chunkStart + (digitStart - bufferStart), digitEnd - digitStart);
                bPatched = true;
            }
        }

        if (heightMatch != lineEnd) {
            auto digitStart = heightMatch + heightPrefix.size();
            auto digitEnd = digitStart;
            while (digitEnd < lineEnd && *digitEnd >= '0' && *digitEnd <= '9')
                digitEnd++;

            if (digitEnd > digitStart) {
                PatchLayoutDimension(chunkStart + (digitStart - bufferStart), digitEnd - digitStart);
                bPatched = true;
            }
        }

        if (bPatched)
            spdlog::info("HUD: Layout: Test10 zeroed marker atlas entry '{:s}' at {:s}.", std::string(markerName), HexValue(reinterpret_cast<uintptr_t>(chunkStart + (match - bufferStart))));

        searchStart = match + markerName.size();
    }

    return bPatched;
}

void PatchLayoutMarkerEntries()
{
    static bool bPatched = false;
    if (bPatched)
        return;

    constexpr size_t iChunkSize = 4 * 1024 * 1024;
    std::vector<std::uint8_t> buffer(iChunkSize);
    bool found[] = { false, false, false };
    const std::string_view names[] = { "MENU_Find_01.png", "MENU_Find_02.png", "MENU_Radar.png" };

    spdlog::info("HUD: Layout: Test10 marker atlas scan starting.");
    std::uint8_t* address = reinterpret_cast<std::uint8_t*>(0x10000);
    MEMORY_BASIC_INFORMATION mbi{};

    while (VirtualQuery(address, &mbi, sizeof(mbi))) {
        auto regionStart = reinterpret_cast<std::uint8_t*>(mbi.BaseAddress);
        auto regionEnd = regionStart + mbi.RegionSize;

        if (mbi.State == MEM_COMMIT && mbi.Type != MEM_IMAGE && IsScannableProtect(mbi.Protect)) {
            for (std::uint8_t* chunkStart = regionStart; chunkStart < regionEnd;) {
                size_t bytesToRead = static_cast<size_t>(std::min<uintptr_t>(iChunkSize, regionEnd - chunkStart));
                SIZE_T bytesRead = 0;

                if (ReadProcessMemory(GetCurrentProcess(), chunkStart, buffer.data(), bytesToRead, &bytesRead)) {
                    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
                        if (!found[i] && PatchLayoutMarkerEntry(chunkStart, buffer.data(), buffer.data() + bytesRead, names[i]))
                            found[i] = true;
                    }

                    if (found[0] && found[1] && found[2])
                        break;
                }

                auto nextChunk = chunkStart + bytesToRead;
                if (nextChunk <= chunkStart)
                    break;
                chunkStart = nextChunk;
            }
        }

        if (regionEnd <= address)
            break;
        if (found[0] && found[1] && found[2])
            break;
        address = regionEnd;
    }

    for (size_t i = 0; i < sizeof(found) / sizeof(found[0]); i++) {
        if (!found[i])
            spdlog::warn("HUD: Layout: Test10 did not find '{:s}'.", std::string(names[i]));
    }

    bPatched = true;
    spdlog::info("HUD: Layout: Test10 marker atlas scan finished.");
}

DWORD WINAPI DelayedPatchScaleformMarkerStringBlock(LPVOID)
{
    Sleep(500);
    spdlog::info("HUD: Layout: Test10 delayed marker atlas patch starting.");
    PatchLayoutMarkerEntries();
    spdlog::info("HUD: Layout: Test10 delayed marker atlas patch finished.");
    return 0;
}

void CalculateAspectRatio(bool bLog)
{
    if (iCurrentResX <= 0 || iCurrentResY <= 0)
        return;

    // Calculate aspect ratio
    fAspectRatio = (float)iCurrentResX / (float)iCurrentResY;
    fAspectMultiplier = fAspectRatio / fNativeAspect;

    // HUD 
    fHUDWidth = (float)iCurrentResY * fNativeAspect;
    fHUDHeight = (float)iCurrentResY;
    fHUDWidthOffset = (float)(iCurrentResX - fHUDWidth) / 2.00f;
    fHUDHeightOffset = 0.00f;
    if (fAspectRatio < fNativeAspect) {
        fHUDWidth = (float)iCurrentResX;
        fHUDHeight = (float)iCurrentResX / fNativeAspect;
        fHUDWidthOffset = 0.00f;
        fHUDHeightOffset = (float)(iCurrentResY - fHUDHeight) / 2.00f;
    }

    // Log details about current resolution
    if (bLog) {
        spdlog::info("----------");
        spdlog::info("Current Resolution: Resolution: {:d}x{:d}", iCurrentResX, iCurrentResY);
        spdlog::info("Current Resolution: fAspectRatio: {}", fAspectRatio);
        spdlog::info("Current Resolution: fAspectMultiplier: {}", fAspectMultiplier);
        spdlog::info("Current Resolution: fHUDWidth: {}", fHUDWidth);
        spdlog::info("Current Resolution: fHUDHeight: {}", fHUDHeight);
        spdlog::info("Current Resolution: fHUDWidthOffset: {}", fHUDWidthOffset);
        spdlog::info("Current Resolution: fHUDHeightOffset: {}", fHUDHeightOffset);
        spdlog::info("----------");
    }
}

void Logging()
{
    // Get path to DLL
    WCHAR dllPath[_MAX_PATH] = {0};
    GetModuleFileNameW(thisModule, dllPath, MAX_PATH);
    sFixPath = dllPath;
    sFixPath = sFixPath.remove_filename();

    // Get game name and exe path
    WCHAR exePath[_MAX_PATH] = {0};
    GetModuleFileNameW(exeModule, exePath, MAX_PATH);
    sExePath = exePath;
    sExeName = sExePath.filename().string();
    sExePath = sExePath.remove_filename();

    // Spdlog initialisation
    try
    {
        // Truncate existing log file
        std::ofstream file(sExePath.string() + sLogFile, std::ios::trunc);
        if (file.is_open()) file.close();

        // Create single log file that's size-limited to 10MB
        logger = std::make_shared<spdlog::logger>(sFixName, std::make_shared<spdlog::sinks::rotating_file_sink_st>(sExePath.string() + sLogFile, 10 * 1024 * 1024, 1));
        spdlog::set_default_logger(logger);
        spdlog::flush_on(spdlog::level::debug);

        #ifdef _DEBUG
        spdlog::set_level(spdlog::level::debug); 
        #endif 

        spdlog::info("----------");
        spdlog::info("{:s} v{:s} loaded.", sFixName, sFixVersion);
        spdlog::info("----------");
        spdlog::info("Log file: {}", sFixPath.string() + sLogFile);
        spdlog::info("----------");
        spdlog::info("Module Name: {:s}", sExeName);
        spdlog::info("Module Path: {:s}", sExePath.string());
        spdlog::info("Module Address: 0x{:x}", (uintptr_t)exeModule);
        spdlog::info("Module Timestamp: {:d}", Memory::ModuleTimestamp(exeModule));
        spdlog::info("----------");
    }
    catch (const spdlog::spdlog_ex &ex)
    {
        AllocConsole();
        FILE *dummy;
        freopen_s(&dummy, "CONOUT$", "w", stdout);
        std::cout << "Log initialisation failed: " << ex.what() << std::endl;
        FreeLibraryAndExitThread(thisModule, 1);
    }
}

void Configuration()
{
    // Inipp initialisation
    std::ifstream iniFile(sFixPath / sConfigFile);
    if (!iniFile)
    {
        AllocConsole();
        FILE *dummy;
        freopen_s(&dummy, "CONOUT$", "w", stdout);
        std::cout << "" << sFixName.c_str() << " v" << sFixVersion.c_str() << " loaded." << std::endl;
        std::cout << "ERROR: Could not locate config file." << std::endl;
        std::cout << "ERROR: Make sure " << sConfigFile.c_str() << " is located in " << sFixPath.string().c_str() << std::endl;
        spdlog::error("ERROR: Could not locate config file {}", sConfigFile);
        spdlog::shutdown();
        FreeLibraryAndExitThread(thisModule, 1);
    }
    else
    {
        spdlog::info("Config file: {}", sFixPath.string() + sConfigFile);
        ini.parse(iniFile);
    }

    // Parse config
    ini.strip_trailing_comments();
    spdlog::info("----------");

    // Load settings from ini
    inipp::get_value(ini.sections["Borderless Windowed"], "Enabled", bBorderlessWindowed);
    inipp::get_value(ini.sections["Gameplay FOV"], "Multiplier", fGameplayFOVMulti);
    inipp::get_value(ini.sections["Disable Camera Reset"], "Enabled", bDisableCameraReset);
    inipp::get_value(ini.sections["Auto Loot"], "Enabled", bAutoLoot);
    inipp::get_value(ini.sections["Hide Awareness Markers"], "Enabled", bHideAwarenessMarkers);
    inipp::get_value(ini.sections["Hide Vignettes"], "Enabled", bHideVignettes);
    inipp::get_value(ini.sections["Debug Resource Path Log"], "Enabled", bLogResourcePaths);
    inipp::get_value(ini.sections["Prevent Dragonrot"], "Enabled", bPreventDragonrot);
    inipp::get_value(ini.sections["Disable Death Penalties"], "Enabled", bDisableDeathPenalties);
    inipp::get_value(ini.sections["Log Stats"], "Enabled", bLogStats);
    inipp::get_value(ini.sections["Spirit Emblem Upgrade"], "Enabled", bSpiritEmblemUpgrade);
    inipp::get_value(ini.sections["Unlock Framerate"], "Enabled", bUnlockFPS);
    inipp::get_value(ini.sections["Unlock Resolutions"], "Enabled", bUnlockResolutions);
    inipp::get_value(ini.sections["Fix Aspect Ratio"], "Enabled", bFixAspect);
    inipp::get_value(ini.sections["Fix HUD"], "Enabled", bFixHUD);

    // Clamp settings
    fGameplayFOVMulti = std::clamp(fGameplayFOVMulti, 0.01f, 4.00f);

    // Log ini parse
    spdlog_confparse(bBorderlessWindowed);
    spdlog_confparse(fGameplayFOVMulti);
    spdlog_confparse(bDisableCameraReset);
    spdlog_confparse(bAutoLoot);
    spdlog_confparse(bHideAwarenessMarkers);
    spdlog_confparse(bHideVignettes);
    spdlog_confparse(bLogResourcePaths);
    spdlog_confparse(bPreventDragonrot);
    spdlog_confparse(bDisableDeathPenalties);
    spdlog_confparse(bLogStats);
    spdlog_confparse(bSpiritEmblemUpgrade);
    spdlog_confparse(bUnlockFPS);
    spdlog_confparse(bUnlockResolutions);
    spdlog_confparse(bFixAspect);
    spdlog_confparse(bFixHUD);

    spdlog::info("----------");
}

void ResourcePathLogging()
{
    if (!bLogResourcePaths)
        return;

    // ModEngine uses a Sekiro archive resolver as the point where virtual resource paths become loadable paths.
    // Test3 logs all resolver-looking candidates because ModEngine may already have patched the first/expected one.
    std::vector<std::uint8_t*> candidates = Memory::PatternScanAll(exeModule, "40 55 56 41 54 41 55 48 83 EC 28 4D 8B E0");
    spdlog::info("Resource Path Candidate: found {} archive resolver candidate(s).", candidates.size());

    ResourcePathCandidateHooks.reserve(candidates.size());
    for (size_t i = 0; i < candidates.size(); i++) {
        std::uint8_t* candidate = candidates[i];
        spdlog::info("Resource Path Candidate: candidate #{} address is {:s}+{:x}", i, sExeName.c_str(), candidate - reinterpret_cast<std::uint8_t*>(exeModule));

        auto hook = safetyhook::create_mid(candidate, [i, candidate](SafetyHookContext& ctx) {
            LogResourcePathCandidateCall(i, candidate, ctx);
        });

        if (hook) {
            spdlog::info("Resource Path Candidate: candidate #{} hook installed.", i);
            ResourcePathCandidateHooks.emplace_back(std::move(hook));
        }
        else {
            spdlog::error("Resource Path Candidate: candidate #{} hook failed to install.", i);
        }
    }

    if (ResourcePathCandidateHooks.empty()) {
        spdlog::error("Resource Path Candidate: no candidate hooks installed.");
    }
}

void Resolution() 
{
    // Current Resolution
    std::uint8_t* CurrentResolutionScanResult = Memory::PatternScan(exeModule, "85 ?? 74 ?? ?? 8B ?? ?? ?? ?? ?? ?? 45 ?? ?? 74 ?? 41 ?? ?? 0F ?? ??");
    if (CurrentResolutionScanResult) {
        spdlog::info("Current Resolution: Address is {:s}+{:x}", sExeName.c_str(), CurrentResolutionScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        static SafetyHookMid CurrentResolutionMidHook{};
        CurrentResolutionMidHook = safetyhook::create_mid(CurrentResolutionScanResult + 0x2,
            [](SafetyHookContext& ctx) {
                if (bFixAspect) {
                    // Jump over scaling to 16:9
                    ctx.rflags |= (1ULL << 6);
                }

                // Get current resolution
                int iResX = ctx.rax;
                int iResY = ctx.r9;
  
                // Log current resolution
                if (iCurrentResX != iResX || iCurrentResY != iResY) {
                    iCurrentResX = iResX;
                    iCurrentResY = iResY;
                    CalculateAspectRatio(true);
                }
            });
    }
    else {
        spdlog::error("Current Resolution: Pattern scan failed.");
    } 
    
    if (bUnlockResolutions) 
    {
        // Resolution List
        std::uint8_t* ResolutionListScanResult = Memory::PatternScan(exeModule, "0F 84 ?? ?? ?? ?? 48 8B ?? ?? ?? ?? ?? 48 85 ?? 0F 84 ?? ?? ?? ?? 0F ?? ?? ?? 48 8D ?? ?? E8 ?? ?? ?? ??");
        if (ResolutionListScanResult) {
            spdlog::info("Resolution List: Address is {:s}+{:x}", sExeName.c_str(), ResolutionListScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            // Force the use of the (unrestricted) fullscreen resolution list at all times
            Memory::PatchBytes(ResolutionListScanResult, "\x90\x90\x90\x90\x90\x90", 6);
        }
        else {
            spdlog::error("Resolution List: Pattern scan failed.");
        } 
    }

    if (bBorderlessWindowed)
    {
        // Force borderless style in windowed mode
        std::uint8_t* WindowedModeStyleScanResult = Memory::PatternScan(exeModule, "74 ?? 84 ?? B8 ?? ?? ?? ?? B9 ?? ?? ?? ?? 0F ?? ?? 48 83 ?? ?? C3");
        if (WindowedModeStyleScanResult) {
            spdlog::info("Windowed Mode Style: Address is {:s}+{:x}", sExeName.c_str(), WindowedModeStyleScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            Memory::PatchBytes(WindowedModeStyleScanResult, "\x90\x90", 2);
        }
        else {
            spdlog::error("Windowed Mode Style: Pattern scan failed.");
        } 
    } 
}

void AspectRatio()
{
    if (bFixAspect) 
    {
        // Fix decimated animations at wider aspect ratios
        std::uint8_t* AnimationCullingAspectScanResult = Memory::PatternScan(exeModule, "F3 0F ?? ?? ?? BA 01 00 00 00 48 ?? ?? E8 ?? ?? ?? ?? 48 ?? ?? ?? 48 ?? ?? E8 ?? ?? ?? ??");
        if (AnimationCullingAspectScanResult) {
            spdlog::info("Animation Culling Aspect Ratio: Address is {:s}+{:x}", sExeName.c_str(), AnimationCullingAspectScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            static SafetyHookMid AnimationCullingAspectMidHook{};
            AnimationCullingAspectMidHook = safetyhook::create_mid(AnimationCullingAspectScanResult,
                [](SafetyHookContext& ctx) {
                    if (fAspectRatio > fNativeAspect)
                        ctx.xmm3.f32[0] = fAspectRatio;
                });
        }
        else {
            spdlog::error("Animation Culling Aspect Ratio: Pattern scan failed.");
        }
    }

    if (bFixAspect || bHideAwarenessMarkers)
    {
        // Awareness Markers 
        std::uint8_t* AwarenessMarkersTransitionScanResult = Memory::PatternScan(exeModule, "F3 0F ?? ?? ?? ?? 0F ?? ?? 72 ?? 0F ?? ?? F3 0F ?? ?? F3 0F ?? ?? 0F ?? ?? 72 ??");
        std::uint8_t* AwarenessMarkersCullingScanResult = Memory::PatternScan(exeModule, "0F ?? ?? 0F ?? ?? ?? 72 ?? F3 0F ?? ?? ?? ?? ?? ?? 0F 57 ?? 0F ?? ?? 72 ??");
        if (AwarenessMarkersTransitionScanResult && AwarenessMarkersCullingScanResult) {
            spdlog::info("Awareness Markers: Transition: Address is {:s}+{:x}", sExeName.c_str(), AwarenessMarkersTransitionScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            static SafetyHookMid AwarenessMarkersTransitionHorMidHook{};
            AwarenessMarkersTransitionHorMidHook = safetyhook::create_mid(AwarenessMarkersTransitionScanResult,
                [](SafetyHookContext& ctx) {
                    if (bHideAwarenessMarkers) {
                        // Test12: collapse marker bounds to a tiny onscreen point instead of sending them offscreen.
                        ctx.xmm0.f32[0] = 960.00f;
                        ctx.xmm1.f32[0] = 960.01f;
                    }
                    else if (fAspectRatio > fNativeAspect) {
                        ctx.xmm0.f32[0] = -((1080.00f * fAspectRatio) - 1920.00f) / 2.00f;
                        ctx.xmm1.f32[0] = 1080.00f * fAspectRatio;
                    }
                });

            static SafetyHookMid AwarenessMarkersTransitionVertMidHook{};
            AwarenessMarkersTransitionVertMidHook = safetyhook::create_mid(AwarenessMarkersTransitionScanResult + 0x2A,
                [](SafetyHookContext& ctx) {
                    if (bHideAwarenessMarkers) {
                        // Test12: collapse marker bounds to a tiny onscreen point instead of sending them offscreen.
                        ctx.xmm0.f32[0] = 540.00f;
                        ctx.xmm4.f32[0] = 540.01f;
                    }
                    else if (fAspectRatio < fNativeAspect) {
                        ctx.xmm0.f32[0] = -((1920.00f / fAspectRatio) - 1080.00f) / 2.00f;
                        ctx.xmm4.f32[0] = 1920.00f / fAspectRatio;
                    }
                });

            spdlog::info("Awareness Markers: Culling: Address is {:s}+{:x}", sExeName.c_str(), AwarenessMarkersCullingScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            static SafetyHookMid AwarenessMarkersCullingMidHook{};
            AwarenessMarkersCullingMidHook = safetyhook::create_mid(AwarenessMarkersCullingScanResult,
                [](SafetyHookContext& ctx) {
                    if (!bHideAwarenessMarkers && fAspectRatio > fNativeAspect) {
                        ctx.xmm2.f32[0] += ((1080.00f * fAspectRatio) - 1920.00f) / 2.00f;
                    }
                    else if (!bHideAwarenessMarkers && fAspectRatio < fNativeAspect) {
                        ctx.xmm3.f32[0] += ((1920.00f / fAspectRatio) - 1080.00f) / 2.00f;
                    }
                });
        }
        else {
            spdlog::error("Awareness Markers: Pattern scan(s) failed.");
        }
    }
}

void FOV() 
{
    if (fGameplayFOVMulti != 1.00f) {
        // Gameplay FOV
        std::uint8_t* GameplayFOVScanResult = Memory::PatternScan(exeModule, "48 0F ?? ?? F3 0F ?? ?? F3 0F ?? ?? ?? ?? ?? ?? F3 0F ?? ?? ?? F3 41 ?? ?? ?? ?? ?? ?? ?? F3 0F ?? ??");
        if (GameplayFOVScanResult) {
            spdlog::info("Gameplay FOV: Address is {:s}+{:x}", sExeName.c_str(), GameplayFOVScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            static SafetyHookMid GameplayFOVMidHook{};
            GameplayFOVMidHook = safetyhook::create_mid(GameplayFOVScanResult + 0x8,
                [](SafetyHookContext& ctx) {
                    // Default FOV = 43
                    ctx.xmm1.f32[0] *= fGameplayFOVMulti;
                });
        }
        else {
            spdlog::error("Gameplay FOV: Pattern scan failed.");
        } 
    }
}

void HUD()
{
    if (true)
    {
        // Scaleform GFX
        std::uint8_t* LoadScaleformGFXScanResult = Memory::PatternScan(exeModule, "8B ?? 0F ?? ?? F3 0F ?? ?? ?? F3 0F ?? ?? ?? F3 0F ?? ?? F3 0F ?? ?? ?? F3 0F ?? ?? ?? F3 0F ?? ?? 85 ??");
        if (LoadScaleformGFXScanResult) {
            spdlog::info("HUD: Scaleform GFX: Address is {:s}+{:x}", sExeName.c_str(), LoadScaleformGFXScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            static SafetyHookMid LoadScaleformGFXMidHook{};
            LoadScaleformGFXMidHook = safetyhook::create_mid(LoadScaleformGFXScanResult,
                [](SafetyHookContext& ctx) {
                    if (!ctx.rax) return;

                    if (ctx.rax + 0x48) {
                        // Get loaded .gfx file name
                        auto pGFXName = *reinterpret_cast<std::uint8_t**>(ctx.rax + 0x48);
                        std::string sGFXName = (char*)(pGFXName + 0xB);
                        spdlog::info("HUD: Scaleform GFX: Loaded {:s}", sGFXName);

                        if (bHideVignettes && (sGFXName.contains("01_201_stealtheffect.gfx") || sGFXName.contains("01_200_dyingeffect.gfx"))) {
                            // Hide low-health/dying and stealth vignettes without touching fades/black screens.
                            ctx.xmm5.f32[0] = 0.00f;
                            ctx.xmm7.f32[0] = 0.00f;
                            return;
                        }

                        // Stretch screen vignetting effects/fades
                        if (bFixHUD && (sGFXName.contains("01_201_stealtheffect.gfx") || sGFXName.contains("01_200_dyingeffect.gfx") || sGFXName.contains("01_910_fade.gfx") || sGFXName.contains("01_900_black.gfx"))) {
                            if (fAspectRatio > fNativeAspect)
                                ctx.xmm5.f32[0] = static_cast<float>(iCurrentResY * fNativeAspect) * 20.00f;
                            else if (fAspectRatio < fNativeAspect)
                                ctx.xmm7.f32[0] = static_cast<float>(iCurrentResX / fNativeAspect) * 20.00f;
                        }
                    }
                });
        }
        else {
            spdlog::error("HUD: Scaleform GFX: Pattern scan failed.");
        } 
    } 
}

void Gameplay()
{
    if (bDisableCameraReset)
    {
        // Disable camera centering/reset when lock-on is pressed with no valid target.
        std::uint8_t* CameraResetScanResult = Memory::PatternScan(exeModule, "C6 86 ?? ?? 00 00 ?? F3 0F 10 8E ?? ?? 00 00");
        if (CameraResetScanResult) {
            spdlog::info("Gameplay: Disable Camera Reset: Address is {:s}+{:x}", sExeName.c_str(), CameraResetScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            Memory::PatchBytes(CameraResetScanResult + 0x6, "\x00", 1);
        }
        else {
            spdlog::error("Gameplay: Disable Camera Reset: Pattern scan failed.");
        }
    }

    if (bAutoLoot)
    {
        // Force the loot pickup check to behave as if pickup is active.
        std::uint8_t* AutoLootScanResult = Memory::PatternScan(exeModule, "C6 85 ?? ?? ?? ?? ?? B0 01 EB ?? C6 85 ?? ?? ?? ?? ?? 32 C0");
        if (AutoLootScanResult) {
            spdlog::info("Gameplay: Auto Loot: Address is {:s}+{:x}", sExeName.c_str(), AutoLootScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            Memory::PatchBytes(AutoLootScanResult + 0x12, "\xB0\x01", 2);
        }
        else {
            spdlog::error("Gameplay: Auto Loot: Pattern scan failed.");
        }
    }

    if (bPreventDragonrot)
    {
        // Skip the Dragonrot increase path after death.
        std::uint8_t* DragonrotScanResult = Memory::PatternScan(exeModule, "45 ?? ?? BA ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 0F 85 ?? ?? ?? ?? 48 8B 0D ?? ?? ?? ?? 48 85 C9 75 ?? 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 4C ?? ?? 4C ?? ?? ?? ?? ?? ?? BA ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B 0D ?? ?? ?? ?? 45 ?? ?? BA ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 0F 84 ?? ?? ?? ?? 48 8D");
        if (DragonrotScanResult) {
            spdlog::info("Gameplay: Prevent Dragonrot: Address is {:s}+{:x}", sExeName.c_str(), DragonrotScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            Memory::PatchBytes(DragonrotScanResult + 0x0D, "\x90\x90\x90\xE9", 4);
        }
        else {
            spdlog::error("Gameplay: Prevent Dragonrot: Pattern scan failed.");
        }
    }

    if (bDisableDeathPenalties)
    {
        // Disable modern Sen/XP loss regions when dying.
        std::uint8_t* DeathPenaltySenScanResult = Memory::PatternScan(exeModule, "F3 ?? 0F 2C ?? 41 ?? ?? 48 ?? ?? E8 ?? ?? ?? ?? 8B");
        if (DeathPenaltySenScanResult) {
            spdlog::info("Gameplay: Disable Death Penalties: Sen Address is {:s}+{:x}", sExeName.c_str(), DeathPenaltySenScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            Memory::PatchBytes(DeathPenaltySenScanResult + 0x0B, "\x90\x90\x90\x90\x90", 5);
        }
        else {
            spdlog::error("Gameplay: Disable Death Penalties: Sen pattern scan failed.");
        }

        std::uint8_t* DeathPenaltyXpScanResult = Memory::PatternScan(exeModule, "E8 ?? ?? ?? ?? 45 ?? ?? 44 89 ?? 24 ?? ?? 00 00 8B ?? 24 ?? ?? 00 00 2B ?? 89 ?? 24 ?? ?? 00 00 E8 ?? ?? ?? ?? 48 ?? ?? 24 ?? ?? 00 00 48 ?? ?? 48");
        if (DeathPenaltyXpScanResult) {
            spdlog::info("Gameplay: Disable Death Penalties: XP Address is {:s}+{:x}", sExeName.c_str(), DeathPenaltyXpScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            Memory::PatchBytes(DeathPenaltyXpScanResult, "\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90", 32);
            Memory::PatchBytes(DeathPenaltyXpScanResult + 0x2D, "\x90\x90\x90", 3);
        }
        else {
            spdlog::error("Gameplay: Disable Death Penalties: XP pattern scan failed.");
        }
    }

    if (bSpiritEmblemUpgrade)
    {
        // Treat prosthetic upgrade skill effects as a +1 Spirit Emblem capacity upgrade.
        std::uint8_t* SpiritEmblemUpgradeScanResult = Memory::PatternScan(exeModule, "48 85 C0 74 ?? 0F B6 50 37 85 D2 74 ?? 48 8B 0D");
        if (SpiritEmblemUpgradeScanResult) {
            spdlog::info("Gameplay: Spirit Emblem Upgrade: Address is {:s}+{:x}", sExeName.c_str(), SpiritEmblemUpgradeScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            static SafetyHookMid SpiritEmblemUpgradeMidHook{};
            SpiritEmblemUpgradeMidHook = safetyhook::create_mid(SpiritEmblemUpgradeScanResult + 0x09,
                [](SafetyHookContext& ctx) {
                    std::uint32_t skillFamily = 0;
                    if (ReadMemory(reinterpret_cast<std::uint8_t*>(ctx.rax) + 0x30, skillFamily) && skillFamily == 0x2932E0)
                        ctx.rdx = (ctx.rdx & 0xFFFFFFFF00000000ULL) | 1;
                });
        }
        else {
            spdlog::error("Gameplay: Spirit Emblem Upgrade: Pattern scan failed.");
        }
    }
}

DWORD __stdcall StatsThread(void*)
{
    while (true)
    {
        int deaths = 0;
        int totalKills = 0;

        if (ReadMemory(pPlayerDeaths, deaths) && ReadMemory(pTotalKills, totalKills))
        {
            int kills = std::max(totalKills - deaths, 0);

            std::ofstream deathsFile(sExePath / "DeathCounter.txt", std::ios::trunc);
            if (deathsFile.is_open())
                deathsFile << deaths;

            std::ofstream killsFile(sExePath / "TotalKillsCounter.txt", std::ios::trunc);
            if (killsFile.is_open())
                killsFile << kills;
        }
        else {
            spdlog::error("Stats: Failed to read counter pointer(s).");
        }

        Sleep(2000);
    }
}

void Stats()
{
    if (!bLogStats)
        return;

    std::uint8_t* PlayerDeathsScanResult = Memory::PatternScan(exeModule, "0F B6 48 ?? 88 8B ?? ?? 00 00 48 8B 05 ?? ?? ?? ?? 8B 88 ?? ?? 00 00 89 8B ?? ?? 00 00 48 8B 05 ?? ?? ?? ?? 8B 88 ?? ?? 00 00");
    if (PlayerDeathsScanResult) {
        std::uint8_t* playerStatsRelatedRef = Memory::GetAbsolute(PlayerDeathsScanResult + 0x20);
        std::uint8_t* playerStatsRelated = nullptr;
        std::int32_t deathOffset = 0;

        if (ReadMemory(playerStatsRelatedRef, playerStatsRelated) && ReadMemory(PlayerDeathsScanResult + 0x26, deathOffset)) {
            pPlayerDeaths = playerStatsRelated + deathOffset;
            spdlog::info("Stats: Player Deaths: Address is {:s}+{:x}", sExeName.c_str(), PlayerDeathsScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        }
        else {
            spdlog::error("Stats: Player Deaths: Failed to resolve pointer.");
        }
    }
    else {
        spdlog::error("Stats: Player Deaths: Pattern scan failed.");
    }

    std::uint8_t* TotalKillsScanResult = Memory::PatternScan(exeModule, "48 ?? D8 ?? ?? ?? ?? 48 8B 05 ?? ?? ?? ?? 48 ?? ?? 48 89 ?? ?? ?? 48 8B ?? 08");
    if (TotalKillsScanResult) {
        std::uint8_t* totalKillsRef = Memory::GetAbsolute(TotalKillsScanResult + 0x0A);
        std::uint8_t* totalKillsFirst = nullptr;
        std::uint8_t* totalKillsSecond = nullptr;

        if (ReadMemory(totalKillsRef, totalKillsFirst) && totalKillsFirst && ReadMemory(totalKillsFirst + 0x08, totalKillsSecond)) {
            pTotalKills = totalKillsSecond + 0x00DC;
            spdlog::info("Stats: Total Kills: Address is {:s}+{:x}", sExeName.c_str(), TotalKillsScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
        }
        else {
            spdlog::error("Stats: Total Kills: Failed to resolve pointer.");
        }
    }
    else {
        spdlog::error("Stats: Total Kills: Pattern scan failed.");
    }

    if (!pPlayerDeaths || !pTotalKills)
        return;

    HANDLE statsHandle = CreateThread(NULL, 0, StatsThread, 0, NULL, 0);
    if (statsHandle)
        CloseHandle(statsHandle);
}

void Framerate()
{
    if (bUnlockFPS) 
    {
        // Fullscreen Refresh Rate
        std::uint8_t* FullscreenRefreshRateScanResult = Memory::PatternScan(exeModule, "C7 ?? ?? 3C 00 00 00 48 ?? ?? ?? 01 00 00 00 4C ?? ?? ??");
        std::uint8_t* FullscreenRefreshRateStartupScanResult = Memory::PatternScan(exeModule, "C7 45 ?? 3C 00 00 00 C7 45 ?? 01 00 00 00 48 8D ?? ??");
        if (FullscreenRefreshRateScanResult && FullscreenRefreshRateStartupScanResult) {
            spdlog::info("Framerate: FS Refresh Rate: Address is {:s}+{:x}", sExeName.c_str(), FullscreenRefreshRateScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            Memory::PatchBytes(FullscreenRefreshRateScanResult + 0x3, "\x00", 1);
            spdlog::info("Framerate: FS Refresh Rate (Startup): Address is {:s}+{:x}", sExeName.c_str(), FullscreenRefreshRateStartupScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            Memory::PatchBytes(FullscreenRefreshRateStartupScanResult + 0x3, "\x00", 1);
        }
        else {
            spdlog::error("Framerate: FS Refresh Rate: Pattern scan(s) failed.");
        } 

        // Framerate Cap
        std::uint8_t* FramerateCapScanResult = Memory::PatternScan(exeModule, "F3 0F ?? ?? ?? 0F ?? ?? 48 8B ?? ?? ?? ?? ?? 0F 57 ?? F2 ?? ?? ?? ??");
        if (FramerateCapScanResult) {
            spdlog::info("Framerate: Cap: Address is {:s}+{:x}", sExeName.c_str(), FramerateCapScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            Memory::PatchBytes(FramerateCapScanResult, "\x0F\x57\xC0\x90\x90", 5); // movss xmm0,[rbx+18] -> xorps xmm0,xmm0
        }
        else {
            spdlog::error("Framerate: Cap: Pattern scan failed.");
        } 

        // Current Framerate
        std::uint8_t* CurrentFramerateScanResult = Memory::PatternScan(exeModule, "F3 0F ?? ?? ?? 4C ?? ?? ?? ?? 48 ?? ?? ?? ?? 0F ?? ?? F3 ?? ?? ?? ??");
        if (CurrentFramerateScanResult) {
            spdlog::info("Framerate: Current Framerate: Address is {:s}+{:x}", sExeName.c_str(), CurrentFramerateScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            static SafetyHookMid CurrentFramerateMidHook{};
            CurrentFramerateMidHook = safetyhook::create_mid(CurrentFramerateScanResult ,
                [](SafetyHookContext& ctx) {
                    fCurrentFramerate = 1.00f / ctx.xmm3.f32[0];
                });
        }
        else {
            spdlog::error("Framerate: Current Framerate: Pattern scan failed.");
        } 

        // Sprint Speed
        std::uint8_t* PlayerCtrlSprintSpeedScanResult = Memory::PatternScan(exeModule, "76 ?? F3 0F ?? ?? ?? ?? ?? ?? 0F ?? ?? ?? ?? ?? ?? 76 ?? F3 0F ?? ?? ?? ?? ?? ??");
        if (PlayerCtrlSprintSpeedScanResult) {
            spdlog::info("Framerate: Sprint Speed: Address is {:s}+{:x}", sExeName.c_str(), PlayerCtrlSprintSpeedScanResult - reinterpret_cast<std::uint8_t*>(exeModule));
            static SafetyHookMid PlayerCtrlSpeedMidHook{};
            PlayerCtrlSpeedMidHook = safetyhook::create_mid(PlayerCtrlSprintSpeedScanResult,
                [](SafetyHookContext& ctx) {
                    // Not exactly sure what this code section is doing. It looks like it's comparing some sort of movement delta and that delta becomes so miniscule
                    // at high framerates that it causes the game to slow down your sprint speed.
                    // Whatever it is, let's just skip over it at >60fps.
                    if (fCurrentFramerate > 60.00f)
                        ctx.rflags |= (1ULL << 6);
                });
        }
        else {
            spdlog::error("Framerate: Sprint Speed: Pattern scan failed.");
        } 
    }
}

DWORD __stdcall Main(void*)
{
    Logging();
    Configuration();
    Resolution();
    AspectRatio();
    FOV();
    ResourcePathLogging();
    HUD();
    Gameplay();
    Stats();
    Framerate();
    return true;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    {
        thisModule = hModule;

        DisableThreadLibraryCalls(hModule);
        HANDLE mainHandle = CreateThread(NULL, 0, Main, 0, NULL, 0);
        if (mainHandle)
        {
            SetThreadPriority(mainHandle, THREAD_PRIORITY_HIGHEST);
            CloseHandle(mainHandle);
        }
        break;
    }
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
