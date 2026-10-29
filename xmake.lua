-- Improved Wheel Menu for The Elder Scrolls IV: Oblivion Remastered (OBSE64 plugin).
-- The game's own Quick Keys radial is the wheel; this plugin manages what stands behind its eight slots
-- (two wheels - Magic and Equipment - five entries a slot, RB use / LB remove, D-pad wheels, triggers entries; the
-- owner's rulings 2026-09-26 and 2026-09-29), and a small ammo wheel of its own for a held bow (MinHook: the TES-thread
-- queue its equips run on, from Simple Loadout System).
-- rule 45: no build-machine paths in any compiled object - set BEFORE includes() so CommonLibOB64's own library
-- target gets it too (a std::source_location in an OBSE header reached through the PCH's absolute -FI path).
-- /d1trimfile strips the project folder from __FILE__ and std::source_location. The flag is wrapped in a TABLE so
-- xmake passes it as one quoted argument: a bare string is split on the path's spaces, and /d1trimfile:"<dir>"
-- reaches cl with the quote characters in the prefix, which then matches nothing (measured 2026-09-29). No trailing
-- separator. /PDBALTPATH:%_PDB% makes the debug directory record only the PDB's file name (it ships beside the DLL).
add_cxflags({"/d1trimfile:$(projectdir)"}, {force = true, expand = false})
add_shflags("/PDBALTPATH:%_PDB%", {force = true})

includes("lib/commonlibob64")

set_project("ImprovedWheelMenu")
set_version("2.4.1")   -- xmake only parses major.minor.patch (OBSE sees 2.4.1.0); a release number with a fourth part is IWM_RELEASE below
local IWM_RELEASE = "2.4.1"
set_license("GPL-3.0-or-later")
set_languages("c++23")
set_warnings("allextra")

add_rules("mode.debug", "mode.releasedbg")
add_requires("minhook")
add_rules("plugin.vsxmake.autoupdate")

target("ImprovedWheelMenu")
    add_rules("commonlibob64.plugin", {
        name = "ImprovedWheelMenu",
        author = "ApocryphaRealm (modified by cfa911)",
        description = "Improved Wheel Menu - the Quick Keys radial, augmented (Oblivion Remastered)"
    })
    add_syslinks("user32")
    add_packages("minhook")
    on_load(function (target)
        target:add("defines", "IWM_VERSION=\"" .. IWM_RELEASE .. "\"")
    end)
    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/core/pch.h")

-- Unit tests of the engine-free helpers (src/core/TextUtil.h and the other pure headers): xmake build Tests && xmake run Tests. Not part of the plugin and
-- not built by default.
target("Tests")
    set_kind("binary")
    set_default(false)
    set_languages("c++23")
    add_files("tests/*.cpp")
    add_includedirs("src")
