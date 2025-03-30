add_rules("mode.debug", "mode.release")

set_languages("c++23")

option("commonlib")
    set_default("skyrim-commonlib-ng")
option_end()

if not has_config("commonlib") then
    return
end

add_repositories("SkyrimScripting     https://github.com/SkyrimScripting/Packages.git")
add_repositories("SkyrimScriptingBeta https://github.com/SkyrimScriptingBeta/Packages.git")
add_repositories("MrowrLib            https://github.com/MrowrLib/Packages.git")

includes("xmake/*.lua")

add_requires(
    "unordered_dense",
    "collections",
    "simpleini",
    "nlohmann_json",
    "inih"
)
add_requires(get_config("commonlib"))
add_requires("SkyrimScripting.Plugin", { configs = { commonlib = get_config("commonlib") } })
    
skse_plugin({
    name = "Forgotten Magic - Update Spell Names with Progress",
    version = "0.0.1",
    author = "Mrowr Purr",
    email = "mrowr.purr@gmail.com",
    packages = {"SkyrimScripting.Plugin", "unordered_dense", "collections", "simpleini", "nlohmann_json", "inih"},
    mod_files = {"SKSE"}
})
