local name = "SekiroFix"

set_project(name)
add_rules("mode.debug", "mode.release")
set_languages("cxxlatest", "clatest")
set_optimize("smallest")

target(name)
    set_kind("shared")
    set_prefixname("")
    set_extension(".asi")
    add_files("src/*.cpp", "external/safetyhook/safetyhook.cpp", "external/safetyhook/Zydis.c")
    add_syslinks("user32")
    add_includedirs("external/spdlog/include", "external/inipp", "external/safetyhook")

    if is_plat("windows") then
        set_toolchains("msvc")
        set_runtimes("MT")
        add_cxflags("/utf-8", "/GL")
        add_ldflags("/LTCG", "/OPT:REF", "/OPT:ICF")
    end