#include <SkyrimScripting/Plugin.h>
#include <collections.h>

// Do not import SimpleIni until after CommonLib/SkyrimScripting (anything including Windows.h)
#include <SimpleIni.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>

using namespace std::literals;
using SpellIndex = std::uint32_t;

struct SpellInfo {
    RE::SpellItem* spell{nullptr};
    SpellIndex     spellIndex{0};
    std::string    originalSpellName;
    std::string    grantingBookName;
};

constexpr auto INI_FILENAME                 = "Data/SKSE/Plugins/ForgottenMagic_UpdateSpellsWithProgress.ini"sv;
constexpr auto MCM_QUEST_EDITOR_ID          = "vMCM"sv;
constexpr auto MCM_SCRIPT                   = "vMCMscript"sv;
constexpr auto PAPYRUS_XP_TRACKER_ARRAY     = "fSPXP"sv;
constexpr auto PAPYRUS_XP_REQUIREMENT_ARRAY = "fXPreq"sv;

std::string        forgottenMagicFilename = "ForgottenMagic_Redone.esp";
const RE::TESFile* forgottenMagicFile     = nullptr;
const RE::TESForm* mcmQuest               = nullptr;

collections_map<SpellIndex, SpellInfo>      spellInfosByIndex;
collections_map<RE::FormID, RE::SpellItem*> spellEffectsBySpellEffectFormID;
collections_map<RE::SpellItem*, SpellInfo>  spellInfosBySpellItem;

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

bool LookupForgottenMagicPluginAndMCM() {
    forgottenMagicFile = RE::TESDataHandler::GetSingleton()->LookupModByName(forgottenMagicFilename);
    if (forgottenMagicFile) {
        Log("Found Forgotten Magic plugin: {}", forgottenMagicFile->GetFilename());
        mcmQuest = RE::TESForm::LookupByEditorID(MCM_QUEST_EDITOR_ID);
        if (mcmQuest) {
            Log("Found MCM quest: {}", mcmQuest->GetName());
            return true;
        } else {
            SKSE::log::error("Could not find MCM quest with editor ID {}", MCM_QUEST_EDITOR_ID);
            return false;
        }
    } else {
        SKSE::log::error("Could not find {}", forgottenMagicFilename);
        return false;
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
                            spellInfosBySpellItem.emplace(spell, spellInfo);
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
private:
    // Mutex for protecting access to the spell queue and last use times
    // Ensures thread-safe operations on the collection of spells pending processing
    std::mutex queue_mutex;

    // Mutex for ensuring only one batch of spells can be processed at a time
    // Controls access to the is_processing flag
    std::mutex processing_mutex;

    // Condition variable for signaling when new spells are added to the queue
    // Used to wake up the background thread when there's work to do
    std::condition_variable cv;

    // Condition variable for signaling when spell processing is complete
    // Used to coordinate waiting for processing to finish before starting new work
    std::condition_variable processing_cv;

    // Queue of spells waiting to be processed (not actively used in current implementation)
    // Kept for potential future use if immediate processing is needed
    std::queue<RE::SpellItem*> spell_queue;

    // Maps spells to the timestamp of their last use
    // Used to track when spells haven't been used for the required time (1 second)
    std::unordered_map<RE::SpellItem*, std::chrono::steady_clock::time_point> last_use_times;

    // Background thread that monitors for spells to process and handles processing
    // Runs continuously until the plugin is unloaded
    std::thread background_thread;

    // Flag indicating whether the background thread should continue running
    // Set to false when the plugin is being unloaded to gracefully terminate the thread
    std::atomic<bool> running{true};

    // Flag indicating whether spell processing is currently in progress
    // Used to prevent multiple batches of spells from being processed simultaneously
    bool is_processing{false};

public:
    /**
     * Processes a batch of spells after they've been used and sufficient time has passed
     *
     * This function is called by the background thread with a batch of spells that
     * haven't been used for at least 1 second. It's responsible for updating spell
     * progress, XP, and any other necessary modifications.
     *
     * Only one instance of this function can run at a time due to the processing_mutex
     * and is_processing flag.
     *
     * @param spells A vector of spells to be processed in this batch
     */
    void UpdateSpellsXP(const std::vector<RE::SpellItem*>& spells) {
        Log("Processing batch of {} spells to update for XP", spells.size());

        // This is where we'd find the MCM script instance and call functions to update spell XP
        auto* vm                 = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        auto* objectHandlePolicy = vm->GetObjectHandlePolicy();
        auto  mcmScriptHandle    = objectHandlePolicy->GetHandleForObject(mcmQuest->GetFormType(), mcmQuest);
        auto  mcmAttachedScripts = vm->attachedScripts.find(mcmScriptHandle);
        if (mcmAttachedScripts != vm->attachedScripts.end()) {
            for (const auto& script : mcmAttachedScripts->second) {
                //

                auto* xpBySpellIdProperty = script->GetProperty(PAPYRUS_XP_TRACKER_ARRAY);
                if (!xpBySpellIdProperty) {
                    Log("fSPXP property not found!");
                    break;
                }
                auto* xpReqProperty = script->GetProperty(PAPYRUS_XP_REQUIREMENT_ARRAY);
                if (!xpReqProperty) {
                    Log("fXPreq property not found!");
                    break;
                }

                // And each property should be an array
                if (!xpBySpellIdProperty->IsArray()) {
                    Log("fSPXP property is not an array!");
                    break;
                }
                if (!xpReqProperty->IsArray()) {
                    Log("fXPreq property is not an array!");
                    break;
                }

                // Get the arrays
                auto xpBySpellIdArray     = xpBySpellIdProperty->GetArray();
                auto xpReqsBySpellIdArray = xpReqProperty->GetArray();

                for (const auto& spell : spells) {
                    auto&      spellInfo  = spellInfosBySpellItem[spell];
                    const auto spellIndex = spellInfo.spellIndex;

                    // Make sure the spell index is valid in both arrays
                    if (xpBySpellIdArray->size() <= spellIndex) {
                        Log("XP array size is smaller than spell index!");
                        break;
                    }
                    if (xpReqsBySpellIdArray->size() <= spellIndex) {
                        Log("XP req array size is smaller than spell index!");
                        break;
                    }

                    // Get the variable at the right spell index
                    auto currentXpVariable = (*xpBySpellIdArray)[spellIndex];
                    auto xpReqVariable     = (*xpReqsBySpellIdArray)[spellIndex];

                    // Both need to be floats
                    if (!currentXpVariable.IsFloat()) {
                        Log("XP variable is not a float!");
                        break;
                    }
                    if (!xpReqVariable.IsFloat()) {
                        Log("XP req variable is not a float!");
                        break;
                    }

                    // Get the float values
                    auto currentXp = currentXpVariable.GetFloat();
                    auto xpReq     = xpReqVariable.GetFloat();
                    if (currentXp < 0) {
                        Log("XP is negative!");
                        break;
                    }
                    if (xpReq <= 0) {
                        Log("XP req is negative or zero!");
                        break;
                    }

                    // Sweet! Now we can calculate the progress:
                    auto progress = (currentXp / xpReq) * 100;
                    Log("Progress: {} ({} / {})", progress, currentXp, xpReq);

                    // Get it as a percentage, and clamp it to 0-100
                    auto progressInt = static_cast<std::int32_t>(progress);
                    if (progressInt < 0) progressInt = 0;
                    if (progressInt > 100) progressInt = 100;

                    // Get the original name of this form:
                    auto originalName = spellInfo.originalSpellName;
                    if (originalName.empty()) break;

                    // If it's zero, then set the name back to the original name
                    if (progressInt == 0) {
                        spell->SetFullName(originalName.c_str());
                        Log("Setting name back to original for spell: {}", originalName);
                    } else {
                        // Otherwise, set the name to the original name + progress
                        // Make a new name which is the original name + progress, like
                        // "<original name> (<progress>%)"
                        std::string newName = originalName + " (" + std::to_string(progressInt) + "%)";
                        spell->SetFullName(newName.c_str());
                        Log("Setting new name for spell: {}", newName);
                    }
                }
            }
        }

        // Signal that processing is complete by setting is_processing to false
        // and notifying any waiting threads that they can now process new batches
        {
            std::lock_guard<std::mutex> lock(processing_mutex);
            is_processing = false;       // Mark processing as complete
            processing_cv.notify_all();  // Notify any waiting threads
        }

        Log("Batch processing complete");
    }

    /**
     * Main function for the background thread that monitors spells and processes them
     *
     * This function runs in a continuous loop until the plugin is unloaded.
     * It periodically checks for spells that haven't been used for at least 1 second,
     * collects them into a batch, and processes them.
     *
     * The function has several synchronization points:
     * 1. Waiting for new spells or checking existing ones (with timeout)
     * 2. Collecting spells that haven't been used for the required time
     * 3. Waiting for any current processing to complete before starting new processing
     */
    void BackgroundThreadFunction() {
        while (running) {  // Main loop continues until plugin unload
            // Collection of spells that are ready to be processed
            std::vector<RE::SpellItem*> spells_to_process;

            // SECTION 1: Collect spells that haven't been used for the required time
            {
                // Lock the queue to safely access last_use_times
                std::unique_lock<std::mutex> lock(queue_mutex);

                // Wait with timeout - either wakes up when a new spell is added
                // or after 100ms to check existing spells
                cv.wait_for(lock, std::chrono::milliseconds(100));

                // Get current time to compare against last use times
                auto now = std::chrono::steady_clock::now();

                // Track spells that will be removed from the monitoring map
                std::vector<RE::SpellItem*> spells_to_remove;

                // Check each spell's last use time
                for (auto& [spell, last_time] : last_use_times) {
                    // Calculate time since last use
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time).count();

                    // If at least 1 second has passed since last use
                    if (elapsed >= 1000) {
                        spells_to_process.push_back(spell);  // Add to processing batch
                        spells_to_remove.push_back(spell);   // Mark for removal from monitoring
                    }
                }

                // Remove processed spells from the monitoring map
                for (auto* spell : spells_to_remove) {
                    last_use_times.erase(spell);
                }
            }  // queue_mutex is released here

            // SECTION 2: Process collected spells if any were found
            if (!spells_to_process.empty()) {
                // Lock the processing mutex to check/update processing status
                std::unique_lock<std::mutex> lock(processing_mutex);

                // Wait until any current processing is complete
                // This ensures only one batch of spells is processed at a time
                while (is_processing && running) {
                    processing_cv.wait(lock);
                }

                // Exit if we're shutting down
                if (!running) break;

                // Mark as processing and release lock before actual processing
                is_processing = true;
                lock.unlock();

                // Process the batch of spells
                UpdateSpellsXP(spells_to_process);
                // Note: RunThisAfterTheMagicEffectIsDoneBeingUsed will reset is_processing when done
            }
        }
    }

    /**
     * Adds a spell to be monitored for processing
     *
     * When a spell is used, this function is called to update its last use time.
     * The spell will be processed after it hasn't been used for 1 second.
     *
     * @param spell The spell that was just used
     */
    void QueueSpell(RE::SpellItem* spell) {
        // Lock the queue to safely update last_use_times
        std::lock_guard<std::mutex> lock(queue_mutex);

        // Update the spell's last use time to now
        last_use_times[spell] = std::chrono::steady_clock::now();

        // Notify the background thread that there's a new/updated spell
        cv.notify_one();
    }

    /**
     * Constructor - starts the background thread
     */
    MagicEffectApplyEventSink() {
        // Start the background thread that will monitor and process spells
        background_thread = std::thread(&MagicEffectApplyEventSink::BackgroundThreadFunction, this);
    }

    /**
     * Destructor - ensures the background thread is properly terminated
     */
    ~MagicEffectApplyEventSink() {
        // Signal the background thread to stop
        running = false;

        // Wake up all waiting threads so they can check the running flag
        cv.notify_all();

        // Wait for the background thread to finish
        if (background_thread.joinable()) {
            background_thread.join();
        }
    }

    /**
     * Singleton accessor - ensures only one instance exists
     *
     * @return Pointer to the singleton instance
     */
    static MagicEffectApplyEventSink* instance() {
        static MagicEffectApplyEventSink singleton;
        return &singleton;
    }

    /**
     * Event handler for magic effect application events
     *
     * Called whenever a magic effect is applied in the game.
     * Filters for Forgotten Magic effects and queues the corresponding spell for processing.
     *
     * @param event The magic effect application event
     * @param eventSource The source of the event
     * @return Control flag to continue processing other event handlers
     */
    RE::BSEventNotifyControl ProcessEvent(const RE::TESMagicEffectApplyEvent* event, RE::BSTEventSource<RE::TESMagicEffectApplyEvent>* eventSource) override {
        // Get the form for the magic effect
        auto* form = RE::TESForm::LookupByID(event->magicEffect);
        if (!form) return RE::BSEventNotifyControl::kContinue;

        // Check if Forgotten Magic plugin is loaded
        if (!forgottenMagicFile) return RE::BSEventNotifyControl::kContinue;

        // Check if the magic effect belongs to the Forgotten Magic plugin
        if (!forgottenMagicFile->IsFormInMod(event->magicEffect)) return RE::BSEventNotifyControl::kContinue;

        // Look up the spell associated with this magic effect
        auto it = spellEffectsBySpellEffectFormID.find(event->magicEffect);
        if (it != spellEffectsBySpellEffectFormID.end()) {
            auto* spell = it->second;
            Log("Found a Forgotten Magic spell was used: {}", spell->GetName());

            // Queue the spell for monitoring and eventually processing
            QueueSpell(spell);
        }

        return RE::BSEventNotifyControl::kContinue;
    }
};

void ResetAllSpellsToTheirOriginalNames() {
    for (const auto& [spell, spellInfo] : spellInfosBySpellItem) {
        if (spell && spellInfo.spell) {
            Log("Resetting spell name to original: {}", spellInfo.originalSpellName);
            spell->SetFullName(spellInfo.originalSpellName.c_str());
        }
    }
}

void UpdateXPofAllSpells() {
    Log("Updating XP for all of the player's Forgotten Magic spells...");
    std::vector<RE::SpellItem*> allSpells;
    const auto*                 player = RE::PlayerCharacter::GetSingleton();
    for (const auto& [spell, spellInfo] : spellInfosBySpellItem) {
        if (player->HasSpell(spell)) {
            if (spell && spellInfo.spell) {
                allSpells.push_back(spell);
            }
        }
    }
    MagicEffectApplyEventSink::instance()->UpdateSpellsXP(allSpells);
    Log("Updated XP for all of the player's Forgotten Magic spells");
}

SKSEPlugin_Entrypoint { ParseIni(); }

SKSEPlugin_OnDataLoaded {
    const auto now = std::chrono::steady_clock::now();
    if (LookupForgottenMagicPluginAndMCM()) {
        if (forgottenMagicFile) LoadForgottenMagicSpellsData();
        RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink(MagicEffectApplyEventSink::instance());
    }
    const auto durationInMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - now).count();
    Log("Data loaded in {}ms", durationInMs);
}

SKSEPlugin_OnPostLoadGame {
    ResetAllSpellsToTheirOriginalNames();
    UpdateXPofAllSpells();
}
SKSEPlugin_OnNewGame { ResetAllSpellsToTheirOriginalNames(); }
