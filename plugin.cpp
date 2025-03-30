#include <SkyrimScripting/Plugin.h>
#include <collections.h>

// Do not import SimpleIni until after CommonLib/SkyrimScripting (anything including Windows.h)
#include <SimpleIni.h>

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

using namespace std::literals;

constexpr auto INI_FILENAME = "Data/SKSE/Plugins/ForgottenMagic_UpdateSpellsWithProgress.ini"sv;

std::string        forgottenMagicFilename = "ForgottenMagic_Redone.esp";
const RE::TESFile* forgottenMagicFile     = nullptr;

using SpellIndex = std::uint32_t;

struct SpellInfo {
    RE::SpellItem* spell{nullptr};
    SpellIndex     spellIndex{0};
    std::string    originalSpellName;
    std::string    grantingBookName;
};

collections_map<SpellIndex, SpellInfo>      spellInfosByIndex;
collections_map<RE::FormID, RE::SpellItem*> spellEffectsBySpellEffectFormID;

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
                        for (auto& effect : spell->effects) {
                            spellEffectsBySpellEffectFormID.emplace(effect->baseEffect->GetFormID(), spell);
                            Log("Saving spell effect {} for spell {}", effect->baseEffect->GetName(), spell->GetName());
                        }
                        found++;
                        break;
                    }
                }
            }
        }
    }
    Log("Found {} out of {} Forgotten Magic spell books", found, spellInfosByIndex.size());
}

class MagicEffectApplyEventSink : public RE::BSTEventSink<RE::TESMagicEffectApplyEvent> {
    void RunThisAfterTheMagicEffectIsDoneBeingUsed() {
        // ...
        // THIS IS WHERE I WANNA DO THE THINGS
    }

public:
    static MagicEffectApplyEventSink* instance() {
        static MagicEffectApplyEventSink singleton;
        return &singleton;
    }

    RE::BSEventNotifyControl ProcessEvent(const RE::TESMagicEffectApplyEvent* event, RE::BSTEventSource<RE::TESMagicEffectApplyEvent>* eventSource) override {
        auto* form = RE::TESForm::LookupByID(event->magicEffect);
        if (!form) return RE::BSEventNotifyControl::kContinue;
        if (!forgottenMagicFile) return RE::BSEventNotifyControl::kContinue;
        if (!forgottenMagicFile->IsFormInMod(event->magicEffect)) return RE::BSEventNotifyControl::kContinue;

        auto it = spellEffectsBySpellEffectFormID.find(event->magicEffect);
        if (it != spellEffectsBySpellEffectFormID.end()) {
            auto* spell = it->second;
            Log("!!!!!!!!!!!!!!!!!!!!! Found a Forgotten Magic spell effect: {} (spell: {})", form->GetName(), spell->GetName());
            // HERE! I want to queue up the spell to be updated with progress!
        }

        return RE::BSEventNotifyControl::kContinue;
    }
};

SKSEPlugin_Entrypoint { ParseIni(); }

SKSEPlugin_OnDataLoaded {
    if (spellInfosByIndex.size() > 0) {
        LookupForgottenMagicPlugin();
        if (forgottenMagicFile) LoadForgottenMagicSpellsData();
        RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink(MagicEffectApplyEventSink::instance());
    }
}
