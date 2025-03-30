#include <SkyrimScripting/Plugin.h>

// Do not import SimpleIni until after CommonLib/SkyrimScripting
#include <SimpleIni.h>
#include <collections.h>

#include <atomic>

using namespace std::literals;

constexpr auto INI_FILENAME        = "Data/SKSE/Plugins/HazForgottenWheelerXP.ini"sv;
constexpr auto INI_SECTION         = "General"sv;
constexpr auto INI_KEY             = "wheeler_toggle_keycode"sv;
constexpr auto FORGOTTEN_MAGIC_ESP = "ForgottenMagic_Redone.esp"sv;
constexpr auto MAGIC_EFFECT_SCRIPT = "FMR_MagicScript"sv;
constexpr auto MCM_EDITOR_ID       = "vMCM"sv;
constexpr auto MCM_SCRIPT          = "vMCMscript"sv;
constexpr auto WHEELER_TOGGLE_TIME = 500ms;

std::atomic<bool>                     _spellsLoaded         = false;
int                                   wheelerToggleKey      = 0x3A;  // Default: CAPS LOCK
const RE::TESFile*                    forgottenMagicFile    = nullptr;
std::chrono::steady_clock::time_point lastWheelerToggleTime = std::chrono::steady_clock::now();
// collections_map<RE::SpellItem*, std::string> spellNames;

SKSEPlugin_Entrypoint {
    CSimpleIniA ini;
    ini.SetUnicode();
    if (ini.LoadFile(INI_FILENAME.data()) == SI_OK) {
        wheelerToggleKey = ini.GetLongValue(INI_SECTION.data(), INI_KEY.data(), wheelerToggleKey);
        Log("Wheeler toggle key: {}", wheelerToggleKey);
    }
}

SKSEPlugin_OnDataLoaded {
    forgottenMagicFile = RE::TESDataHandler::GetSingleton()->LookupModByName(FORGOTTEN_MAGIC_ESP);

    if (!forgottenMagicFile) {
        SKSE::log::error("Could not find {}", FORGOTTEN_MAGIC_ESP);
        return;
    }
}

SKSEPlugin_OnInputLoaded {}
