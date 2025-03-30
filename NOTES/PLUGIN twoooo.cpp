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

std::atomic<bool>                            _spellsLoaded         = false;
int                                          wheelerToggleKey      = 0x3A;  // Default: CAPS LOCK
const RE::TESFile*                           forgottenMagicFile    = nullptr;
std::chrono::steady_clock::time_point        lastWheelerToggleTime = std::chrono::steady_clock::now();
collections_map<RE::SpellItem*, std::string> spellNames;

void UpdateAllPlayerForgottenMagicSpellNamesWithXP() {
    Log("Updating all player Forgotten Magic spell names with XP...");

    auto* player = RE::PlayerCharacter::GetSingleton();
    for (auto& spell : player->addedSpells) {
        if (forgottenMagicFile->IsFormInMod(spell->GetFormID())) {
            // Get the original name of the spell
            std::string spellName;
            if (spellNames.find(spell) != spellNames.end()) {
                spellName = spellNames[spell];
            } else {
                spellName         = spell->fullName;
                spellNames[spell] = spell->fullName;
            }

            // ...
            // spell->effects[0]->baseEffect.has
            // const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            // vm.script
        }
    }
}

void FindAllForgottenMagicSpells() {
    Log("Searching for Forgotten Magic spells...");

    auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
    Log("VM: {}", vm != nullptr ? "true" : "false");

    auto* objectHandlePolicy = vm->GetObjectHandlePolicy();
    Log("ObjectHandlePolicy: {}", objectHandlePolicy != nullptr ? "true" : "false");

    // Go through every spell in the game, looking for the ones from Forgotten Magic...
    auto spells = RE::TESDataHandler::GetSingleton()->GetFormArray<RE::SpellItem>();
    for (const auto& spell : spells) {
        if (!forgottenMagicFile->IsFormInMod(spell->GetFormID())) continue;
        Log("Found a Forgotten Magic spell: {}", spell->GetName());

        for (const auto& effect : spell->effects) {
            if (auto* baseEffect = effect->baseEffect) {
                if (baseEffect->HasVMAD()) {
                    Log("Found a VMAD effect: {}", baseEffect->GetName());
                    auto       effectHandle            = objectHandlePolicy->GetHandleForObject(baseEffect->GetFormType(), baseEffect);
                    const auto scriptsAttachedToEffect = vm->attachedScripts.find(effectHandle);
                    if (scriptsAttachedToEffect != vm->attachedScripts.end()) {
                        for (const auto& script : scriptsAttachedToEffect->second) {
                            if (script->type->name == MAGIC_EFFECT_SCRIPT) {
                                Log("Found a script on a Forgotten Magic spell: {}", spell->GetName());
                                const auto* sIndexProperty = script->GetProperty("sIndex");
                                if (sIndexProperty) {
                                    Log("sIndex property found!");
                                    const auto sIndex = sIndexProperty->GetUInt();
                                    Log("sIndex: {}", sIndex);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

struct InputEventSink : public RE::BSTEventSink<RE::InputEvent*> {
    RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* eventPtr, RE::BSTEventSource<RE::InputEvent*>*) {
        if (!eventPtr) return RE::BSEventNotifyControl::kContinue;
        auto* event = *eventPtr;
        if (!event) return RE::BSEventNotifyControl::kContinue;
        if (event->GetEventType() == RE::INPUT_EVENT_TYPE::kButton) {
            auto* buttonEvent = event->AsButtonEvent();
            auto  dxScanCode  = buttonEvent->GetIDCode();
            if (dxScanCode == wheelerToggleKey) {
                auto now     = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastWheelerToggleTime).count();
                if (elapsed < WHEELER_TOGGLE_TIME.count()) return RE::BSEventNotifyControl::kContinue;
                else lastWheelerToggleTime = now;
                // std::thread([]() { UpdateAllPlayerForgottenMagicSpellNamesWithXP(); }).detach();
                // std::thread([]() { FindAllForgottenMagicSpells(); }).detach();
            }
        }
        return RE::BSEventNotifyControl::kContinue;
    }
};

struct OnActorLocationChangeEventSink : public RE::BSTEventSink<RE::TESActorLocationChangeEvent> {
    RE::BSEventNotifyControl ProcessEvent(const RE::TESActorLocationChangeEvent* event, RE::BSTEventSource<RE::TESActorLocationChangeEvent>*) override {
        // if (!_spellsLoaded.exchange(true)) std::thread([]() { FindAllForgottenMagicSpells(); }).detach();
        return RE::BSEventNotifyControl::kContinue;
    }
};

struct MagicEffectApplyEventSink : public RE::BSTEventSink<RE::TESMagicEffectApplyEvent> {
    RE::BSEventNotifyControl ProcessEvent(const RE::TESMagicEffectApplyEvent* event, RE::BSTEventSource<RE::TESMagicEffectApplyEvent>* eventSource) override {
        //
        auto* form = RE::TESForm::LookupByID(event->magicEffect);
        if (!form) return RE::BSEventNotifyControl::kContinue;

        if (!forgottenMagicFile) return RE::BSEventNotifyControl::kContinue;
        if (!forgottenMagicFile->IsFormInMod(event->magicEffect)) return RE::BSEventNotifyControl::kContinue;

        // Ignore "Area"

        Log("ID: {}", form->GetFormID());
        Log("TYPE: {}", form->GetFormType());
        Log("Magic effect: {}", form->GetName());

        auto* asSpellEffect = form->As<RE::ActiveEffect>();
        if (asSpellEffect) {
            Log("YES, got an ActiveEffect!");
            // Log("AsSpellEffect: {}", asSpellEffect->spell
        } else {
            Log("Not an ActiveEffect");
        }

        return RE::BSEventNotifyControl::kContinue;
    }
};

struct TESSpellCastEventSink : public RE::BSTEventSink<RE::TESSpellCastEvent> {
    RE::BSEventNotifyControl ProcessEvent(const RE::TESSpellCastEvent* event, RE::BSTEventSource<RE::TESSpellCastEvent>* eventSource) override {
        // if (!_spellsLoaded.exchange(true)) std::thread([]() { FindAllForgottenMagicSpells(); }).detach();

        if (event->object) {
            // event->object->GetName()
            Log("Spell cast event object: {}", event->object->GetName());
        }

        return RE::BSEventNotifyControl::kContinue;
    }
};

std::unique_ptr<InputEventSink>                 inputEventSink               = nullptr;
std::unique_ptr<OnActorLocationChangeEventSink> actorLocationChangeEventSink = nullptr;
std::unique_ptr<MagicEffectApplyEventSink>      magicEffectApplyEvent        = nullptr;
std::unique_ptr<TESSpellCastEventSink>          spellCastEventSink           = nullptr;

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

SKSEPlugin_OnInputLoaded {
    inputEventSink = std::make_unique<InputEventSink>();
    RE::BSInputDeviceManager::GetSingleton()->AddEventSink(inputEventSink.get());

    // actorLocationChangeEventSink = std::make_unique<OnActorLocationChangeEventSink>();
    // RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink(actorLocationChangeEventSink.get());

    magicEffectApplyEvent = std::make_unique<MagicEffectApplyEventSink>();
    RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink(magicEffectApplyEvent.get());

    spellCastEventSink = std::make_unique<TESSpellCastEventSink>();
    RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink(spellCastEventSink.get());
}
