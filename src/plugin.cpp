#include <SkyrimScripting/Plugin.h>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>

using namespace std::literals;
using SpellIndex = std::uint32_t;

constexpr auto     FORGOTTEN_MAGIC_ESP = "ForgottenMagic_Redone.esp"sv;
const RE::TESFile* forgottenMagicFile  = nullptr;

std::unordered_map<std::string, std::uint32_t> spellNameToSpellIndex = {
    {"fireblast",         0 },
    {"conflagrate",       1 },
    {"meteor shower",     2 },
    {"doppelganger",      3 },
    {"blight curse",      4 },
    {"necrosis",          5 },
    {"void bolt",         6 },
    {"deathguard",        7 },
    {"ice lance",         8 },
    {"frost bomb",        9 },
    {"ancient lich",      10},
    {"frost armor",       11},
    {"healing touch",     12},
    {"seed of life",      13},
    {"earthbound weapon", 14},
    {"wild mushroom",     15},
    {"divine light",      16},
    {"holy bolt",         17},
    {"divine armor",      18},
    {"hammer of justice", 19},
    {"stormstrike",       20},
    {"lightning strike",  21},
    {"storm armor",       22},
    {"skyfall",           23},
    {"arcane weapon",     24},
    {"cursed rune",       25},
    {"discord",           26},
    {"phantom shroud",    27},
    {"veil of nature",    28},
    {"wolf pack",         29},
    {"salamander touch",  30},
    {"deathly pall",      31},
    {"nether rift",       32},
    {"phoenix strike",    33},
    {"winter woe",        34},
    {"glacial fortress",  35},
    {"spectral missiles", 36},
    {"phantom armor",     37},
    {"electric charge",   38},
    {"stormgate",         39},
    {"blessed weapon",    40}
};

// Helper function to convert a string to lowercase
static std::string toLower(const std::string& str) {
    std::string lowerStr = str;
    std::transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(), ::tolower);
    return lowerStr;
}

// Case-insensitive lookup function
std::optional<std::uint32_t> getSpellIndexFromSpellName(const std::string& spellName) {
    std::string lowerSpellName = toLower(spellName);
    auto        it             = spellNameToSpellIndex.find(lowerSpellName);
    if (it != spellNameToSpellIndex.end()) {
        return it->second;
    }
    // throw std::runtime_error("Spell not found: " + spellName);
    return std::nullopt;
}

std::unordered_map<std::uint32_t, RE::SpellItem*> spellIndexToSpellForm;
std::unordered_map<std::uint32_t, std::string>    spellIndexToOriginalSpellName;

class EventSink : public RE::BSTEventSink<RE::TESMagicEffectApplyEvent> {
public:
    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESMagicEffectApplyEvent*               event,
        RE::BSTEventSource<RE::TESMagicEffectApplyEvent>* eventSource
    ) override {
        if (!event->caster) return RE::BSEventNotifyControl::kContinue;
        if (!event->caster->IsPlayerRef()) return RE::BSEventNotifyControl::kContinue;
        if (!event->magicEffect) return RE::BSEventNotifyControl::kContinue;

        auto* magicEffectForm = RE::TESForm::LookupByID(event->magicEffect);
        if (!magicEffectForm) return RE::BSEventNotifyControl::kContinue;

        Log("Magic Effect name: {}", magicEffectForm->GetName());
        Log("Magic Effect ID: {}", magicEffectForm->GetFormID());
        Log("Magic Effect type: {}", magicEffectForm->GetFormType());

        if (!forgottenMagicFile) return RE::BSEventNotifyControl::kContinue;
        if (!forgottenMagicFile->IsFormInMod(event->magicEffect))
            return RE::BSEventNotifyControl::kContinue;

        // auto* magicEffectForm = RE::TESForm::LookupByID(event->magicEffect);
        // if (!magicEffectForm) return RE::BSEventNotifyControl::kContinue;

        Log("Magic Effect from Forgotten Magic: {}", magicEffectForm->GetName());

        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        if (!vm) return RE::BSEventNotifyControl::kContinue;
        Log("Got VM!");

        auto* handlePolicy = vm->GetObjectHandlePolicy();
        if (!handlePolicy) return RE::BSEventNotifyControl::kContinue;
        Log("Got handle policy!");

        auto spellIndex = getSpellIndexFromSpellName(magicEffectForm->GetName());
        if (!spellIndex) {
            Log("Spell index not found!");
            return RE::BSEventNotifyControl::kContinue;
        } else {
            Log("Spell index found: {}", *spellIndex);
        }

        // Let's right here, right now, try to get the MCM.
        auto* mcmForm = RE::TESQuest::LookupByEditorID("vMCM");
        if (!mcmForm) return RE::BSEventNotifyControl::kContinue;
        Log("Got MCM form!");

        auto mcmFormHandle = handlePolicy->GetHandleForObject(mcmForm->GetFormType(), mcmForm);
        if (!mcmFormHandle) return RE::BSEventNotifyControl::kContinue;
        Log("Got MCM form handle!");

        auto mcmFormScripts = vm->attachedScripts.find(mcmFormHandle);
        if (mcmFormScripts == vm->attachedScripts.end()) return RE::BSEventNotifyControl::kContinue;
        if (mcmFormScripts != vm->attachedScripts.end()) {
            for (auto& script : mcmFormScripts->second) {
                if (script->type->name == "vMCMscript") {
                    Log("Found vMCM script!");
                    // We found the vMCM script! Let's call the function to set the spell index.
                    auto* xpBySpellIdProperty = script->GetProperty("fSPXP");
                    if (xpBySpellIdProperty) {
                        auto xpBySpellIdArray = xpBySpellIdProperty->GetArray();
                        Log("Got fSPXP property!");

                        auto item      = (*xpBySpellIdArray)[*spellIndex];
                        auto currentXp = item.GetFloat();

                        auto* xpReqProperty = script->GetProperty("fXPreq");
                        if (xpReqProperty) {
                            Log("Got fXPreq property!");
                            auto xpReqsBySpellIdArray = xpReqProperty->GetArray();

                            auto xpReq = (*xpReqsBySpellIdArray)[*spellIndex].GetFloat();
                            Log("XP required: {}", xpReq);

                            auto progress = (currentXp / xpReq) * 100;
                            Log("Progress: {}", progress);

                            // Get the form!
                            auto* spellForm = spellIndexToSpellForm[*spellIndex];
                            if (!spellForm) return RE::BSEventNotifyControl::kContinue;
                            Log("Spell form: {}", spellForm->GetName());

                            // Get the original name of this form:
                            auto originalName = spellIndexToOriginalSpellName[*spellIndex];
                            if (originalName.empty()) return RE::BSEventNotifyControl::kContinue;
                            Log("Original name: {}", originalName);

                            // Make a new name which is the original name + progress, like
                            // "<original name> (<progress>%)"
                            std::string newName = originalName + " (" +
                                                  std::to_string(static_cast<int>(progress)) + "%)";

                            Log("New name: {}", newName);

                            // Update the form name:
                            spellForm->SetFullName(newName.c_str());
                            Log("Set new name!");

                        } else {
                            Log("fXPreq property not found!");
                        }
                    }
                }
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }
};

std::unique_ptr<EventSink> eventSink = nullptr;

SKSEPlugin_OnDataLoaded {
    forgottenMagicFile = RE::TESDataHandler::GetSingleton()->LookupModByName(FORGOTTEN_MAGIC_ESP);

    if (!forgottenMagicFile) {
        SKSE::log::error("Could not find {}", FORGOTTEN_MAGIC_ESP);
        return;
    }

    // Find all of the spells for the mod which go to the spell index...
    // For now we look at literally EVERY SPELL FORM ... oof ...
    Log("Searching for Forgotten Magic spells...");
    auto allSpells = RE::TESDataHandler::GetSingleton()->GetFormArray<RE::SpellItem>();
    for (auto* item : allSpells) {
        if (forgottenMagicFile->IsFormInMod(item->GetFormID())) {
            auto spellName = item->fullName;
            if (!spellName.empty()) {
                Log("Found Forgotten Magic spell: {}", spellName.c_str());
                auto spellIndex = getSpellIndexFromSpellName(spellName.c_str());
                if (spellIndex) {
                    Log("Spell index: {}", *spellIndex);
                    spellIndexToSpellForm[*spellIndex]         = item;
                    spellIndexToOriginalSpellName[*spellIndex] = spellName;
                } else {
                    Log("Spell index not found for: {}", spellName.c_str());
                }
            }
        }
    }
    Log("Found {} Forgotten Magic spells.", spellIndexToSpellForm.size());

    eventSink = std::make_unique<EventSink>();
    RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink(eventSink.get());
}
