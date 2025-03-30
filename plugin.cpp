#include <SkyrimScripting/Plugin.h>
// Do not import SimpleIni until after CommonLib/SkyrimScripting
#include <SimpleIni.h>
#include <collections.h>

#include <atomic>

using namespace std::literals;

constexpr auto INI_FILENAME = "Data/SKSE/Plugins/ForgottenMagic_UpdateSpellsWithProgress.ini"sv;

std::string        forgottenMagicFilename = "ForgottenMagic_Redone.esp";
const RE::TESFile* forgottenMagicFile     = nullptr;

// ....
// constexpr auto                        INI_SECTION           = "General"sv;
// constexpr auto                        INI_KEY               = "wheeler_toggle_keycode"sv;
// constexpr auto                        MAGIC_EFFECT_SCRIPT   = "FMR_MagicScript"sv;
// constexpr auto                        MCM_EDITOR_ID         = "vMCM"sv;
// constexpr auto                        MCM_SCRIPT            = "vMCMscript"sv;
// constexpr auto                        WHEELER_TOGGLE_TIME   = 500ms;
// std::atomic<bool>                     _spellsLoaded         = false;
// int                                   wheelerToggleKey      = 0x3A;  // Default: CAPS LOCK
// std::chrono::steady_clock::time_point lastWheelerToggleTime = std::chrono::steady_clock::now();

using SpellIndex = std::uint32_t;

struct SpellInfo {
    RE::SpellItem* spell{nullptr};
    SpellIndex     spellIndex{0};
    std::string    originalSpellName;
    std::string    grantingBookName;
};

collections_map<SpellIndex, SpellInfo> spellInfosByIndex;

void ParseIni() {
    CSimpleIniA ini;
    ini.SetUnicode();

    if (ini.LoadFile(INI_FILENAME.data()) == SI_OK) {
        forgottenMagicFilename = ini.GetValue("ForgottenMagic", "plugin_filename", forgottenMagicFilename.c_str());
        Log("[INI] Forgotten Magic plugin filename: {}", forgottenMagicFilename);

        CSimpleIniA::TNamesDepend spellIndexBookNameKeys;
        if (ini.GetAllKeys("SpellIndexes", spellIndexBookNameKeys)) {
            for (auto& sectionEntry : spellIndexBookNameKeys) {
                const auto* bookName       = sectionEntry.pItem;
                const auto  spellIndex     = ini.GetLongValue("SpellIndexes", bookName, 0);
                auto&       spellInfo      = spellInfosByIndex.emplace(spellIndex, SpellInfo{}).first->second;
                spellInfo.grantingBookName = bookName;
                spellInfo.spellIndex       = spellIndex;
                Log("[INI] Spell Book '{}' has spell index {}", bookName, spellIndex);
            }
        }
    }
}

void LookupForgottenMagicPlugin() {
    forgottenMagicFile = RE::TESDataHandler::GetSingleton()->LookupModByName(forgottenMagicFilename);
    if (forgottenMagicFile) {
        Log("Found Forgotten Magic plugin: {}", forgottenMagicFile->GetFilename());
    } else {
        SKSE::log::error("Could not find {}", forgottenMagicFilename);
    }
}

void LoadForgottenMagicSpellsData() {
    auto found                 = 0;
    auto allBooksInTheGameData = RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESObjectBOOK>();
    for (const auto& book : allBooksInTheGameData) {
        if (forgottenMagicFile->IsFormInMod(book->GetFormID())) {
            if (book->TeachesSpell()) {
                auto* spell = book->GetSpell();
                Log("Found Forgotten Magic spell book '{}' grants spell '{}'", book->GetName(), spell->GetName());
                for (auto& [spellIndex, spellInfo] : spellInfosByIndex) {
                    if (book->fullName == spellInfo.grantingBookName.c_str()) {
                        Log("Matched this spell book with spell index {}", spellIndex);
                        spellInfo.spell             = spell;
                        spellInfo.originalSpellName = spell->GetName();
                        found++;
                        break;
                    }
                }
            }
        }
    }
    Log("Found {} out of {} Forgotten Magic spell books", found, spellInfosByIndex.size());
}

SKSEPlugin_Entrypoint { ParseIni(); }

SKSEPlugin_OnDataLoaded {
    if (spellInfosByIndex.size() > 0) {
        LookupForgottenMagicPlugin();
        if (forgottenMagicFile) LoadForgottenMagicSpellsData();
    }
}
