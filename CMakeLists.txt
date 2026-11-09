cmake_minimum_required(VERSION 3.21)

project(MMModern VERSION 0.18.0 LANGUAGES CXX)

configure_file(cmake/MMModernVersion.h.in generated/MMModernVersion.h @ONLY)
include_directories("${CMAKE_CURRENT_BINARY_DIR}/generated")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

set(SCUMMVM_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../scummvm-master"
	CACHE PATH "Path to the ScummVM source tree")
set(SCUMMVM_BUILD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../build-xeen-probe-sdl"
	CACHE PATH "Path to the configured minimal ScummVM build tree")
set(MMODERN_XEEN_DATA_DIR "" CACHE PATH "Required read-only World of Xeen CD source for original-data tests")
set(MMODERN_XEEN_UI_DATA "" CACHE FILEPATH "Optional installed uncompressed English XEEN.DAT for CD copies")
set(MMODERN_PROMUS_CAPTURES_DIR "" CACHE PATH "External DOSBox DRO v2 promus captures")
set(MMODERN_M94_CAPTURES_DIR "" CACHE PATH "External DOSBox M94 OPL and MIDI captures")
set(MMODERN_MT32_ROM_DIR "" CACHE PATH "External MT-32 control 1.07 and PCM ROMs for tests")
set(MMODERN_XEEN_AUDIO_DIR "${MMODERN_XEEN_DATA_DIR}" CACHE PATH "Required GOG cue sheets and MUSIC directory for CD audio validation")
set(MMODERN_RU_DATA_DIR "" CACHE PATH "Optional Russian World of Xeen translation folder for the M110 language layer tests")

find_package(SDL2 CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
include(cmake/MMModernBgfx.cmake)
add_library(mmodern_platform_libs INTERFACE)
if(WIN32)
 target_link_libraries(mmodern_platform_libs INTERFACE shell32 ole32 uuid bcrypt winmm gdi32 winspool sapi)
# Explicit static Vorbis boundary: no ScummVM backend or g_system dependency.
find_path(MMODERN_VORBIS_INCLUDE_DIR vorbis/vorbisfile.h REQUIRED)
find_library(MMODERN_VORBISFILE_LIBRARY NAMES libvorbisfile.a REQUIRED)
find_library(MMODERN_VORBIS_LIBRARY NAMES libvorbis.a REQUIRED)
find_library(MMODERN_OGG_LIBRARY NAMES libogg.a REQUIRED)

else()
 find_package(PkgConfig REQUIRED)
 pkg_check_modules(MMODERN_VORBIS REQUIRED IMPORTED_TARGET vorbisfile)
 set(MMODERN_VORBISFILE_LIBRARY PkgConfig::MMODERN_VORBIS)
endif()
include(cmake/MMModernScummVmDefines.cmake)
add_library(mmodern_platform_files STATIC src/platform/XeenUserFolders.cpp src/platform/XeenFileOperations.cpp)
target_include_directories(mmodern_platform_files PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(mmodern_platform_files PRIVATE mmodern_platform_libs)

add_library(mmodern_core INTERFACE)
target_include_directories(mmodern_core INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/ScummVmXeen.cmake")
add_mmodern_scummvm_xeen_bridge(mmodern_scummvm_xeen)
set(MMODERN_MT32_SOURCE "${SCUMMVM_SOURCE_DIR}/audio/softsynth/mt32")
set(MMODERN_MT32_FILES
 Analog BReverbModel Display File LA32FloatWaveGenerator LA32Ramp
 LA32WaveGenerator Part Partial PartialManager Poly ROMInfo Synth Tables TVA TVF TVP)
set(MMODERN_MT32_SOURCES src/compat/scummvm/XeenMt32.cpp "${MMODERN_MT32_SOURCE}/sha1/sha1.cpp")
foreach(name IN LISTS MMODERN_MT32_FILES)
 list(APPEND MMODERN_MT32_SOURCES "${MMODERN_MT32_SOURCE}/${name}.cpp")
endforeach()
add_library(mmodern_mt32 STATIC ${MMODERN_MT32_SOURCES})
target_include_directories(mmodern_mt32 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
 PRIVATE "${SCUMMVM_BUILD_DIR}" "${SCUMMVM_SOURCE_DIR}")
target_compile_definitions(mmodern_mt32 PRIVATE ${MMODERN_SCUMMVM_DEFINES})
target_compile_options(mmodern_mt32 PRIVATE -ffunction-sections -fdata-sections)
target_link_libraries(mmodern_mt32 PRIVATE mmodern_platform_files)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/MMModernItemCatalog.cmake")
add_mmodern_item_catalog_generation()

add_library(mmodern_xeen STATIC
	src/formats/xeen/XeenSoundSetup.cpp
	src/formats/xeen/XeenCdAudio.cpp
	src/games/xeen/PromusDriver.cpp
	src/games/xeen/MonoOplDriver.cpp
	src/games/xeen/RolmusDriver.cpp
	src/formats/xeen/XeenDosText.cpp
 src/formats/xeen/XeenCutsceneData.cpp
 src/formats/xeen/XeenCompressedFrame.cpp
 src/games/xeen/XeenCutscene.cpp
 src/games/xeen/XeenPresentationRandom.cpp
 src/games/xeen/XeenCloudsEnding.cpp
 src/games/xeen/XeenDarksideEnding.cpp
 src/games/xeen/XeenWorldEnding.cpp
   src/games/xeen/XeenDarksideIntro.cpp
	src/formats/xeen/XeenMonsterFormat.cpp
	src/formats/xeen/XeenGameplayContextFormat.cpp
	src/games/xeen/XeenActorApproach.cpp
    src/games/xeen/XeenCombat.cpp
    src/games/xeen/XeenCombatRandom.cpp
	src/games/xeen/XeenCombatRules.cpp
	src/games/xeen/XeenScriptedRules.cpp
	src/games/xeen/XeenEndingScore.cpp
	src/platform/XeenWonStore.cpp
	src/games/xeen/XeenRestRules.cpp
	src/games/xeen/XeenTerrainRules.cpp
	src/games/xeen/XeenMonsterTreasure.cpp
	src/games/xeen/XeenServiceEconomy.cpp
    src/games/xeen/XeenFoodBank.cpp
	src/games/xeen/XeenItemGeneration.cpp
	src/games/xeen/XeenMerchantGeneration.cpp
	src/games/xeen/XeenServiceDay.cpp
	src/games/xeen/XeenTraining.cpp
	src/games/xeen/XeenTempleHeal.cpp
	src/games/xeen/XeenEquipmentPurchase.cpp
	src/games/xeen/XeenItemServiceRules.cpp
	src/games/xeen/XeenJourneyRules.cpp
	src/games/xeen/XeenGameplayContext.cpp
	src/games/xeen/XeenRegionalRules.cpp
	src/games/xeen/XeenMonsterFixture.cpp
    src/games/xeen/XeenJumpRules.cpp
	src/formats/xeen/XeenAssetSource.cpp
	src/formats/xeen/XeenCloudsVisualMetadata.cpp
	src/formats/xeen/XeenMaterialNames.cpp
	src/games/xeen/XeenObjectVisual.cpp
	src/games/xeen/XeenServiceAnimation.cpp
    src/games/xeen/XeenEntranceScene.cpp
    src/games/xeen/XeenArenaRules.cpp
	src/formats/xeen/XeenCharacterFormat.cpp
	src/formats/xeen/XeenQuestItemFormat.cpp
	src/formats/xeen/XeenQuestFlagFormat.cpp
	src/formats/xeen/XeenSaveFormat.cpp
	src/games/xeen/XeenSaveState.cpp
	src/formats/xeen/XeenEventFormat.cpp
	src/formats/xeen/XeenEventTextFormat.cpp
	src/formats/xeen/XeenFontFormat.cpp
	src/formats/xeen/XeenGameFlagsFormat.cpp
	src/formats/xeen/XeenMapFormat.cpp
	src/games/xeen/XeenCharacter.cpp
	src/games/xeen/XeenCharacterRules.cpp
	src/games/xeen/XeenLearnedSpellRules.cpp
	src/games/xeen/XeenEventDecoder.cpp
	src/games/xeen/XeenEventValues.cpp
	src/games/xeen/XeenEventDiagnostics.cpp
	src/games/xeen/XeenEventInterpreter.cpp
	src/games/xeen/XeenItemRewards.cpp
	src/games/xeen/XeenItemCatalog.cpp
	src/games/xeen/XeenItemTransfer.cpp
	src/games/xeen/XeenAntidoteUse.cpp
	src/games/xeen/XeenEquipment.cpp
	src/games/xeen/XeenDialogView.cpp
	src/games/xeen/XeenCharacterCreation.cpp
	src/games/xeen/XeenJournalView.cpp
    src/games/xeen/XeenMapView.cpp
	src/games/xeen/XeenEventPresenter.cpp
	src/games/xeen/XeenEventLoader.cpp
	src/games/xeen/XeenEventTextLoader.cpp
	src/games/xeen/XeenEventScript.cpp
	src/games/xeen/XeenEventSystem.cpp
	src/games/xeen/XeenEventTrigger.cpp
	src/games/xeen/XeenTextRenderer.cpp
	src/games/xeen/XeenGameFlags.cpp
	src/games/xeen/XeenGameFlagsLoader.cpp
	src/games/xeen/XeenPartyVisualState.cpp
	src/games/xeen/XeenParty.cpp
    src/games/xeen/XeenWallClock.cpp
	src/games/xeen/XeenPartyLoader.cpp
	src/games/xeen/XeenMapLoader.cpp
	src/games/xeen/CloudsUiComposer.cpp
	src/games/xeen/CloudsUiLayout.cpp
	src/games/xeen/CloudsMapComposer.cpp
	src/games/xeen/XeenWorldPresentation.cpp
	src/games/xeen/XeenOutdoorScene.cpp
	src/games/xeen/XeenIndoorScene.cpp
	src/games/xeen/XeenIndoorRenderScene.cpp
	src/games/xeen/XeenMovement.cpp
	src/games/xeen/XeenWorld.cpp
	src/games/xeen/XeenVertigoWorld.cpp
	src/games/xeen/XeenInstallationDetector.cpp
)
target_include_directories(mmodern_xeen PRIVATE "${MMODERN_ITEM_CATALOG_GENERATED_DIR}")
target_include_directories(mmodern_xeen PRIVATE "${MMODERN_VORBIS_INCLUDE_DIR}")
target_link_libraries(mmodern_xeen PRIVATE "${MMODERN_VORBISFILE_LIBRARY}" "${MMODERN_VORBIS_LIBRARY}" "${MMODERN_OGG_LIBRARY}")
add_dependencies(mmodern_xeen mmodern_item_catalog_data mmodern_dialog_drawing_data)
target_link_libraries(mmodern_xeen PUBLIC mmodern_core mmodern_platform_files PRIVATE mmodern_scummvm_xeen ZLIB::ZLIB)

add_library(mmodern_platform_sdl STATIC
	src/platform/sdl/SdlAudioDevice.cpp
	src/platform/sdl/SdlAudioStream.cpp
	src/platform/sdl/SdlWindow.cpp
	src/platform/sdl/IndoorGpuRenderer.cpp
	src/platform/sdl/SdlWindowPlacement.cpp
)
target_link_libraries(mmodern_platform_sdl PUBLIC mmodern_core PRIVATE SDL2::SDL2 mmodern_platform_libs mmodern_platform_files)
target_link_libraries(mmodern_platform_sdl PRIVATE bgfx)
target_include_directories(mmodern_platform_sdl PRIVATE "${MMODERN_SHADER_OUTPUT}")
add_dependencies(mmodern_platform_sdl mmodern_bgfx_shaders)

add_library(mmodern_app STATIC
 src/app/XeenSetupScreen.cpp
 src/app/XeenPlayerSupport.cpp
	src/app/XeenAudio.cpp
	src/app/XeenFmSource.cpp
	src/app/XeenTitle.cpp
	src/app/XeenTitleFlow.cpp
 src/app/XeenCutsceneFlow.cpp
	src/platform/XeenSaveFile.cpp
 src/platform/XeenPlayerPlatform.cpp
 $<$<BOOL:${WIN32}>:src/platform/XeenSaveIoWindows.cpp>
 $<$<NOT:$<BOOL:${WIN32}>>:src/platform/XeenSaveIoPosix.cpp>
    src/platform/XeenSystemWallClock.cpp
	src/app/XeenGameplay.cpp
	src/app/XeenEncounterFlow.cpp
	src/app/XeenJourneyFlow.cpp
	src/app/XeenBarrierFlow.cpp
	src/app/XeenJourneyConsequences.cpp
	src/app/XeenRestFlow.cpp
	src/app/XeenCastingFlow.cpp
	src/app/XeenSmithFlow.cpp
	src/app/XeenInnFlow.cpp
	src/app/XeenEndingFlow.cpp
	src/app/XeenCreateFlow.cpp
	src/app/XeenTrainingFlow.cpp
	src/app/XeenCastingUi.cpp
	src/app/Application.cpp
	src/app/XeenEventFlow.cpp
	src/app/XeenInventoryFlow.cpp
	src/app/XeenCharacterDialogFlow.cpp
	src/app/XeenNavigationFlow.cpp
)
target_include_directories(mmodern_app PRIVATE "${SCUMMVM_BUILD_DIR}" "${SCUMMVM_SOURCE_DIR}")
set_source_files_properties(src/app/XeenSetupScreen.cpp PROPERTIES COMPILE_DEFINITIONS "${MMODERN_SCUMMVM_DEFINES}")
set_source_files_properties(src/app/XeenTitle.cpp src/app/Application.cpp PROPERTIES COMPILE_DEFINITIONS
    "MMODERN_REPOSITORY_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
target_link_libraries(mmodern_app PUBLIC mmodern_core PRIVATE
	mmodern_platform_libs
	mmodern_xeen
	mmodern_platform_sdl
	mmodern_mt32
	SDL2::SDL2
)

add_executable(mmodern src/main.cpp)
target_link_libraries(mmodern PRIVATE
	mmodern_platform_libs
	mmodern_app
	mmodern_xeen
	mmodern_scummvm_xeen
	mmodern_platform_sdl
	SDL2::SDL2
)

if(MINGW)
	target_compile_options(mmodern PRIVATE -Wall -Wextra -Wpedantic)
	target_link_options(mmodern PRIVATE -static-libgcc -static-libstdc++ -Wl,--gc-sections)
endif()
if(APPLE)
	target_link_options(mmodern PRIVATE "-Wl,-map,${CMAKE_CURRENT_BINARY_DIR}/mmodern-link-map.txt")
endif()

include(CTest)
include(cmake/MMModernPartATargets.cmake)
include(cmake/MMModernTestWraps.cmake)
option(MMODERN_ORIGINAL_DATA_TESTS
	"Register the tests that need material from outside the repository" ON)
# Registers a test with add_test() arguments when MMODERN_ORIGINAL_DATA_TESTS
# is ON; with it OFF, it only records the name. Such a test needs something
# outside the repository, the build tree, the pinned ScummVM checkout and
# temporary folders, none of which may be published:
# - original data: the World of Xeen CD source (MMODERN_XEEN_DATA_DIR);
# - captures: DOSBox audio captures (MMODERN_PROMUS_CAPTURES_DIR,
#   MMODERN_M94_CAPTURES_DIR);
# - ROMs: the MT-32 control and PCM ROMs (MMODERN_MT32_ROM_DIR);
# - CD audio: the GOG cue sheets and MUSIC tracks (MMODERN_XEEN_AUDIO_DIR).
# - translation: the Russian World of Xeen translation (MMODERN_RU_DATA_DIR),
#   registered only when that variable is set.
# A command that runs data-free and data cases together is a data test.
# Arguments are forwarded as bracket arguments: ${ARGV} would drop empty ones
# and split those holding semicolons.
function(mmodern_part_a_test)
 if(NOT WIN32)
  foreach(arg IN LISTS ARGV)
   if(arg IN_LIST MMODERN_WINDOWS_ONLY_TARGETS)
    return()
   endif()
   if(arg MATCHES "^\\$<TARGET_FILE:([^>]+)>$" AND CMAKE_MATCH_1 IN_LIST MMODERN_WINDOWS_ONLY_TARGETS)
    return()
   endif()
  endforeach()
 endif()
 set(call "add_test(")
 math(EXPR last "${ARGC} - 1")
 foreach(i RANGE ${last})
  string(APPEND call " [==[${ARGV${i}}]==]")
 endforeach()
 cmake_language(EVAL CODE "${call})")
endfunction()
function(mmodern_data_test)
	if(NOT ARGV0 STREQUAL "NAME")
		message(FATAL_ERROR "mmodern_data_test() needs NAME first")
	endif()
	set_property(GLOBAL APPEND PROPERTY MMODERN_DATA_TESTS "${ARGV1}")
	if(MMODERN_ORIGINAL_DATA_TESTS)
		set(call "mmodern_part_a_test(")
		math(EXPR last "${ARGC} - 1")
		foreach(i RANGE ${last})
			string(APPEND call " [==[${ARGV${i}}]==]")
		endforeach()
		cmake_language(EVAL CODE "${call})")
	endif()
endfunction()
# set_tests_properties() for a call that names a data test. With
# MMODERN_ORIGINAL_DATA_TESTS OFF it drops the data tests from the names and
# skips the call when no name is left.
function(mmodern_test_properties)
	get_property(data_tests GLOBAL PROPERTY MMODERN_DATA_TESTS)
 get_property(registered_tests DIRECTORY PROPERTY TESTS)
	set(call "set_tests_properties(")
	set(in_names TRUE)
	set(kept_name FALSE)
	math(EXPR last "${ARGC} - 1")
	foreach(i RANGE ${last})
		set(arg "${ARGV${i}}")
		if(arg STREQUAL "PROPERTIES")
			set(in_names FALSE)
		elseif(in_names AND NOT WIN32 AND NOT arg IN_LIST registered_tests)
   continue()
		elseif(in_names AND NOT MMODERN_ORIGINAL_DATA_TESTS AND arg IN_LIST data_tests)
			continue()
		elseif(in_names)
			set(kept_name TRUE)
		endif()
		string(APPEND call " [==[${arg}]==]")
	endforeach()
	if(kept_name)
		cmake_language(EVAL CODE "${call})")
	endif()
endfunction()
add_library(mmodern_coverage_support STATIC tools/XeenCoverage.cpp tools/XeenCoverageContext.cpp)
target_link_libraries(mmodern_coverage_support PUBLIC mmodern_app mmodern_xeen)
target_include_directories(mmodern_coverage_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/tools")
add_executable(mmodern_census tools/XeenCoverageMain.cpp)
target_link_libraries(mmodern_census PRIVATE mmodern_coverage_support)
if(BUILD_TESTING)
 add_executable(mmodern_child_process_tests tests/XeenChildProcessTests.cpp)
 target_link_libraries(mmodern_child_process_tests PRIVATE mmodern_platform_libs)
 add_test(NAME xeen_child_process COMMAND mmodern_child_process_tests)
 set_tests_properties(xeen_child_process PROPERTIES LABELS fast)
 add_executable(mmodern_sha256_tests tests/XeenSha256Tests.cpp)
 add_test(NAME xeen_sha256 COMMAND mmodern_sha256_tests)
 set_tests_properties(xeen_sha256 PROPERTIES LABELS fast)
 add_executable(mmodern_platform_tests tests/XeenPlatformTests.cpp)
 target_link_libraries(mmodern_platform_tests PRIVATE mmodern_app mmodern_xeen mmodern_mt32)
 if(WIN32)
  target_link_options(mmodern_platform_tests PRIVATE "LINKER:--wrap,SHGetKnownFolderPath")
 else()
  target_link_options(mmodern_platform_tests PRIVATE "LINKER:--wrap,fsync")
 endif()
 mmodern_part_a_test(NAME xeen_platform COMMAND mmodern_platform_tests "${CMAKE_CURRENT_BINARY_DIR}/platform-tests")
 if(NOT WIN32)
  add_executable(mmodern_posix_paths_tests tests/XeenPosixPathsTests.cpp)
  target_link_libraries(mmodern_posix_paths_tests PRIVATE mmodern_app mmodern_xeen)
  mmodern_data_test(NAME xeen_posix_paths_original COMMAND mmodern_posix_paths_tests
   "${MMODERN_XEEN_DATA_DIR}" "${MMODERN_RU_DATA_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/posix-paths-tests")
 endif()
endif()
if(BUILD_TESTING)
 add_executable(mmodern_scripted_rules_tests tests/XeenScriptedRulesTests.cpp)
 target_link_libraries(mmodern_scripted_rules_tests PRIVATE mmodern_xeen)
 mmodern_part_a_test(NAME xeen_scripted_rules COMMAND mmodern_scripted_rules_tests)

 add_executable(mmodern_ending_tests tests/XeenEndingTests.cpp)
 target_link_libraries(mmodern_ending_tests PRIVATE mmodern_test_save_support mmodern_dos_export_support mmodern_platform_libs)
 mmodern_data_test(NAME xeen_ending COMMAND mmodern_ending_tests "${MMODERN_XEEN_DATA_DIR}")
 mmodern_test_properties(xeen_scripted_rules PROPERTIES LABELS fast)
 if(MMODERN_ORIGINAL_DATA_TESTS AND (NOT IS_DIRECTORY "${MMODERN_XEEN_AUDIO_DIR}/MUSIC" OR NOT EXISTS "${MMODERN_XEEN_AUDIO_DIR}/GAME1.INS" OR NOT EXISTS "${MMODERN_XEEN_AUDIO_DIR}/GAME2.INS"))
  message(FATAL_ERROR "CD audio validation requires MMODERN_XEEN_AUDIO_DIR with both GAME*.INS and MUSIC/")
 endif()
    add_executable(mmodern_coverage_tests tests/XeenCoverageTests.cpp)
    target_link_libraries(mmodern_coverage_tests PRIVATE mmodern_coverage_support)
    mmodern_part_a_test(NAME xeen_coverage COMMAND mmodern_coverage_tests)
    mmodern_data_test(NAME xeen_coverage_fixture COMMAND mmodern_coverage_tests "${MMODERN_XEEN_DATA_DIR}")
    mmodern_data_test(NAME xeen_coverage_original_guard COMMAND mmodern_census "${MMODERN_XEEN_DATA_DIR}"
        --check "${CMAKE_CURRENT_SOURCE_DIR}/docs/coverage.md")
    add_executable(mmodern_coverage_calibration_tests tests/XeenCoverageCalibrationTests.cpp)
    mmodern_part_a_test(NAME xeen_coverage_calibration COMMAND mmodern_coverage_calibration_tests "${CMAKE_CURRENT_SOURCE_DIR}/docs/coverage.md")
    mmodern_test_properties(xeen_coverage_original_guard PROPERTIES FIXTURES_SETUP coverage_current)
    # The calibration checks docs/coverage.md after the original-data guard;
    # without data it checks the committed file alone.
    if(MMODERN_ORIGINAL_DATA_TESTS)
        mmodern_test_properties(xeen_coverage_calibration PROPERTIES FIXTURES_REQUIRED coverage_current)
    endif()
endif()
# Opt-in local play-test saves; neither target is linked into the game.
add_library(mmodern_test_save_support STATIC EXCLUDE_FROM_ALL tools/XeenTestSave.cpp)
target_include_directories(mmodern_test_save_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(mmodern_test_save_support PRIVATE MMODERN_TEST_SAVE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(mmodern_test_save_support PUBLIC mmodern_app mmodern_xeen)
# EXCLUDE_FROM_ALL: .github/workflows/ci.yml builds it by name; keep that list in step.

add_executable(mmodern_test_save EXCLUDE_FROM_ALL tools/GenerateTestSave.cpp)
target_link_libraries(mmodern_test_save PRIVATE mmodern_test_save_support mmodern_platform_libs)

add_library(mmodern_dos_export_support STATIC EXCLUDE_FROM_ALL tools/XeenDosArchive.cpp tools/XeenDosExport.cpp tools/XeenDosDiff.cpp)
target_include_directories(mmodern_dos_export_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
 PRIVATE "${SCUMMVM_BUILD_DIR}" "${SCUMMVM_SOURCE_DIR}" "${SCUMMVM_SOURCE_DIR}/engines")
target_compile_definitions(mmodern_dos_export_support PRIVATE ${MMODERN_SCUMMVM_DEFINES})
target_link_libraries(mmodern_dos_export_support PUBLIC mmodern_app mmodern_xeen mmodern_scummvm_xeen)
# EXCLUDE_FROM_ALL: .github/workflows/ci.yml builds it by name; keep that list in step.

add_executable(mmodern_dos_export EXCLUDE_FROM_ALL tools/ExportDosSave.cpp)
target_link_libraries(mmodern_dos_export PRIVATE mmodern_dos_export_support mmodern_platform_libs)
add_executable(mmodern_regional_original tests/XeenRegionalOriginal.cpp)
target_link_libraries(mmodern_regional_original PRIVATE mmodern_app mmodern_xeen)
add_executable(mmodern_vertigo_original tests/XeenVertigoOriginal.cpp)
target_link_libraries(mmodern_vertigo_original PRIVATE mmodern_app mmodern_xeen)
function(xeen_restore_replay target)
 set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/XeenRestoreReplayProbe.h")
 file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/tests/XeenRestoreReplayProbe.h" definitions REGEX "^#define XEEN_REPLAY_")
 foreach(definition IN LISTS definitions)
  string(REGEX MATCH "XEEN_WRAP_[A-Z_]+" name "${definition}")
  target_link_options(${target} PRIVATE "LINKER:--wrap,${${name}}")
 endforeach()
endfunction()
if(BUILD_TESTING)
 if(MMODERN_ORIGINAL_DATA_TESTS AND NOT IS_DIRECTORY "${MMODERN_XEEN_DATA_DIR}")
  message(FATAL_ERROR "Original-data validation requires MMODERN_XEEN_DATA_DIR pointing to a complete CD source")
 endif()
 if(MMODERN_XEEN_UI_DATA AND NOT EXISTS "${MMODERN_XEEN_UI_DATA}")
  message(FATAL_ERROR "MMODERN_XEEN_UI_DATA is missing")
 endif()
 add_compile_definitions(MMODERN_TEST_UI_DATA="${MMODERN_XEEN_UI_DATA}")

 add_executable(mmodern_player_log_tests tests/XeenPlayerLogTests.cpp)
 target_link_libraries(mmodern_player_log_tests PRIVATE mmodern_app mmodern_xeen)
 if(WIN32)
 target_link_options(mmodern_player_log_tests PRIVATE "LINKER:--wrap,SHGetKnownFolderPath")
 else()
 target_link_options(mmodern_player_log_tests PRIVATE "LINKER:--wrap,${XEEN_WRAP_USER_FOLDER}" "LINKER:--require-defined,${XEEN_WRAP_USER_FOLDER}")
 endif()
 mmodern_part_a_test(NAME xeen_player_log COMMAND mmodern_player_log_tests "${CMAKE_CURRENT_BINARY_DIR}/m100-player-log")

if(WIN32 OR NOT "mmodern_launcher_witness" IN_LIST MMODERN_WINDOWS_ONLY_TARGETS)
 add_executable(mmodern_launcher_witness src/main.cpp tests/XeenLauncherWitness.cpp)
 target_link_libraries(mmodern_launcher_witness PRIVATE mmodern_app mmodern_xeen mmodern_scummvm_xeen mmodern_platform_sdl SDL2::SDL2 mmodern_platform_libs)
 target_link_options(mmodern_launcher_witness PRIVATE "LINKER:--wrap,SDL_CreateRenderer" "LINKER:--wrap,${XEEN_WRAP_WINDOW_PLACEMENT}")
endif()
 add_executable(mmodern_sound_setup_tests tests/XeenSoundSetupTests.cpp)
 target_link_libraries(mmodern_sound_setup_tests PRIVATE mmodern_app mmodern_xeen)
 mmodern_data_test(NAME xeen_sound_setup COMMAND mmodern_sound_setup_tests "${MMODERN_XEEN_DATA_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/m100-sound-setup" "${MMODERN_MT32_ROM_DIR}")
 mmodern_data_test(NAME xeen_launcher COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
  -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/XeenLauncherTests.ps1"
  -Launcher "${CMAKE_CURRENT_SOURCE_DIR}/tools/release/MMModern-launcher.ps1"
  -Exe "$<TARGET_FILE:mmodern>" -GameDir "${MMODERN_XEEN_DATA_DIR}"
  -TestRoot "${CMAKE_CURRENT_BINARY_DIR}/m100-launcher-tests" -RomDir "${MMODERN_MT32_ROM_DIR}" -PlayerExe "$<TARGET_FILE:mmodern_launcher_witness>")
 add_executable(mmodern_cd_audio_tests tests/XeenCdAudioTests.cpp)
 target_link_libraries(mmodern_cd_audio_tests PRIVATE mmodern_app mmodern_xeen SDL2::SDL2)
 target_link_options(mmodern_cd_audio_tests PRIVATE "LINKER:--wrap,SDL_OpenAudioDevice" "LINKER:--wrap,SDL_AudioStreamGet")
 mmodern_data_test(NAME xeen_cd_audio COMMAND mmodern_cd_audio_tests "${MMODERN_XEEN_AUDIO_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/cd-audio-tests" "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/cd-silence.ogg")
 mmodern_test_properties(xeen_cd_audio PROPERTIES LABELS fast ENVIRONMENT "SDL_AUDIODRIVER=dummy")

	add_executable(mmodern_test_save_tests tests/XeenTestSaveTests.cpp)
	target_link_libraries(mmodern_test_save_tests PRIVATE mmodern_test_save_support mmodern_dos_export_support)
 add_executable(mmodern_scripted_original_tests tests/XeenScriptedOriginalTests.cpp)
 target_link_libraries(mmodern_scripted_original_tests PRIVATE mmodern_test_save_support)
 mmodern_data_test(NAME xeen_scripted_original COMMAND mmodern_scripted_original_tests "${MMODERN_XEEN_DATA_DIR}")
 mmodern_test_properties(xeen_scripted_original PROPERTIES LABELS fast)
 add_executable(mmodern_clouds_progression_tests tests/XeenCloudsProgressionTests.cpp)
 target_link_libraries(mmodern_clouds_progression_tests PRIVATE mmodern_test_save_support)
 mmodern_data_test(NAME xeen_clouds_progression COMMAND mmodern_clouds_progression_tests "${MMODERN_XEEN_DATA_DIR}")
 mmodern_test_properties(xeen_clouds_progression PROPERTIES LABELS fast)

 set(MMODERN_M92_TITLE_PRESET "${CMAKE_CURRENT_BINARY_DIR}/title-flow-preset/m92-sixth-mirror.mmsave")
 file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/title-flow-preset")
	foreach(preset m106-alamar m106-world m106-mirror m106-clouds-mirror m102-playtest m102-bank m102-smithy m102-guild m102-tavern m102-temple m102-training m102-inn m102-tower-refused m102-tower-keyed m102-dungeon-refused m102-dungeon-keyed m102-town-refused m102-town-keyed m92-lord-xeen m92-sixth-mirror broken-armor train-ready injured-dead poisoned monster-control monster-damage party-healing m76-spells combat-items m78-playtest m78-one-holder m79-playtest m80-playtest m80-arena m80-arena-maxed m81-drops m81-fullpacks m81-event m84-playtest-trap m84-playtest-label m84-playtest-save m85-playtest-label m85-playtest-run m85-playtest-trap m86-playtest-fall m86-playtest-moon-ray m86-playtest-border m87-inn m87-three m87-shangrila m88-roster m89-lord-xeen m89-clan-king m89-draco m89-dragons m89-trolls m90-newcastle m90-golem m90-illusion m90-high-magic m90-darzog m90-clouds m90-castle m90-winterkill m90-emerson-short m90-emerson-walls m90-taxman-walls m90-emerson-keep m90-taxman-keep m90-artemus m90-emerson-dungeon m90-taxman-dungeon m90-laboratory m90-sword m90-mirabeth m90-mirabeth-elixir m90-elixir m90-credit-yak m90-golem-terrors m90-credit-terrors m90-golem-stone m90-credit-golem m90-darzog-key m90-crodo m90-slick m90-slick-trade m90-generator m90-winterkill-gong m90-winterkill-wave2 m90-winterkill-wave3 m90-winterkill-final m95-witch-skull ellinger-speech paths)
  if(preset STREQUAL "m92-sixth-mirror")
   mmodern_data_test(NAME xeen_test_save_${preset} COMMAND mmodern_test_save_tests
    "${MMODERN_XEEN_DATA_DIR}" ${preset} "${CMAKE_CURRENT_SOURCE_DIR}" --export "${MMODERN_M92_TITLE_PRESET}")
   mmodern_test_properties(xeen_test_save_${preset} PROPERTIES FIXTURES_SETUP m92_title_preset)
  else()
   mmodern_data_test(NAME xeen_test_save_${preset} COMMAND mmodern_test_save_tests
    "${MMODERN_XEEN_DATA_DIR}" ${preset} "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()
	endforeach()
	add_executable(mmodern_equipment_purchase_rules_tests tests/XeenEquipmentPurchaseRulesTests.cpp)
	target_link_libraries(mmodern_equipment_purchase_rules_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_data_test(NAME xeen_equipment_purchase_rules COMMAND mmodern_equipment_purchase_rules_tests "${MMODERN_XEEN_DATA_DIR}")
	mmodern_data_test(NAME xeen_equipment_purchase_reference COMMAND mmodern_equipment_purchase_rules_tests "${MMODERN_XEEN_DATA_DIR}" --reference "${SCUMMVM_SOURCE_DIR}")

	add_executable(mmodern_purchase_flow_tests tests/XeenPurchaseFlowTests.cpp)
	target_link_libraries(mmodern_purchase_flow_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_data_test(NAME xeen_purchase_flow COMMAND mmodern_purchase_flow_tests "${MMODERN_XEEN_DATA_DIR}")
	add_executable(mmodern_purchase_allocation_tests tests/XeenPurchaseAllocationTests.cpp)
	target_link_libraries(mmodern_purchase_allocation_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_data_test(NAME xeen_purchase_allocation COMMAND mmodern_purchase_allocation_tests "${MMODERN_XEEN_DATA_DIR}")
	add_executable(mmodern_purchase_input_tests tests/XeenPurchaseInputTests.cpp)
	target_link_libraries(mmodern_purchase_input_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2)
	target_link_options(mmodern_purchase_input_tests PRIVATE "LINKER:--wrap,SDL_GetTicks")
	foreach(mode authority native controls)
		mmodern_data_test(NAME xeen_purchase_input_${mode} COMMAND mmodern_purchase_input_tests "${MMODERN_XEEN_DATA_DIR}" ${mode})
	endforeach()
	add_executable(mmodern_training_rules_tests tests/XeenTrainingRulesTests.cpp)
	add_executable(mmodern_temple_flow_tests tests/XeenTempleFlowTests.cpp)
	add_executable(mmodern_temple_rules_tests tests/XeenTempleRulesTests.cpp)
	target_link_libraries(mmodern_temple_rules_tests PRIVATE mmodern_xeen)
	mmodern_part_a_test(NAME xeen_temple_rules COMMAND mmodern_temple_rules_tests)
	target_link_libraries(mmodern_temple_flow_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_data_test(NAME xeen_temple_flow COMMAND mmodern_temple_flow_tests "${MMODERN_XEEN_DATA_DIR}")
	add_executable(mmodern_temple_original_tests tests/XeenTempleOriginalTests.cpp)
	target_link_libraries(mmodern_temple_original_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_data_test(NAME xeen_temple_original COMMAND mmodern_temple_original_tests "${MMODERN_XEEN_DATA_DIR}")

	add_executable(mmodern_m43_cli_witness src/main.cpp tests/XeenM43CliWitness.cpp)
	target_link_libraries(mmodern_m43_cli_witness PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2 mmodern_platform_libs)
	target_link_options(mmodern_m43_cli_witness PRIVATE
		"LINKER:--wrap,${XEEN_WRAP_PLAY_GAMEPLAY}"
		"LINKER:--wrap,${XEEN_WRAP_RANDOM_DRAW}")

	add_executable(mmodern_m43_process_tests tests/XeenM43ProcessTests.cpp)
	target_link_libraries(mmodern_m43_process_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl mmodern_platform_libs)
	add_dependencies(mmodern_m43_process_tests mmodern_m43_cli_witness)
	mmodern_data_test(NAME xeen_temple_process COMMAND mmodern_m43_process_tests $<TARGET_FILE:mmodern_m43_cli_witness> "${MMODERN_XEEN_DATA_DIR}")

	add_executable(mmodern_m43_depleted_process_tests tests/XeenM43DepletedProcessTests.cpp)
	target_link_libraries(mmodern_m43_depleted_process_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl mmodern_platform_libs)
	add_dependencies(mmodern_m43_depleted_process_tests mmodern_m42_cli_witness mmodern_m43_cli_witness)
	mmodern_data_test(NAME xeen_temple_depleted_process COMMAND mmodern_m43_depleted_process_tests
		$<TARGET_FILE:mmodern_m42_cli_witness> $<TARGET_FILE:mmodern_m43_cli_witness> "${MMODERN_XEEN_DATA_DIR}")
	target_link_libraries(mmodern_training_rules_tests PRIVATE mmodern_xeen)
	mmodern_part_a_test(NAME xeen_training_rules COMMAND mmodern_training_rules_tests)
	add_executable(mmodern_m62_part_c_tests tests/XeenM62PartCTests.cpp)
	target_link_libraries(mmodern_m62_part_c_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_data_test(NAME xeen_m62_part_c COMMAND mmodern_m62_part_c_tests "${MMODERN_XEEN_DATA_DIR}")
	mmodern_test_properties(xeen_m62_part_c PROPERTIES LABELS "fast")

	add_executable(mmodern_training_flow_tests tests/XeenTrainingFlowTests.cpp)
	target_link_libraries(mmodern_training_flow_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_data_test(NAME xeen_training_flow COMMAND mmodern_training_flow_tests "${MMODERN_XEEN_DATA_DIR}")
	add_executable(mmodern_training_allocation_tests tests/XeenTrainingAllocationTests.cpp)
	target_link_libraries(mmodern_training_allocation_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_data_test(NAME xeen_training_allocation COMMAND mmodern_training_allocation_tests "${MMODERN_XEEN_DATA_DIR}")
	add_executable(mmodern_training_input_tests tests/XeenTrainingInputTests.cpp)
	add_executable(mmodern_native_responsiveness_tests tests/XeenNativeResponsivenessTests.cpp)
	target_link_libraries(mmodern_native_responsiveness_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2)
	target_link_options(mmodern_native_responsiveness_tests PRIVATE "LINKER:--wrap,SDL_GetTicks" "LINKER:--wrap,SDL_PumpEvents" "LINKER:--wrap,SDL_WaitEventTimeout")
	mmodern_data_test(NAME xeen_native_responsiveness COMMAND mmodern_native_responsiveness_tests "${MMODERN_XEEN_DATA_DIR}")
	add_executable(mmodern_input_scheduling_tests tests/XeenInputSchedulingTests.cpp)
	target_link_libraries(mmodern_input_scheduling_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2)
	target_link_options(mmodern_input_scheduling_tests PRIVATE "LINKER:--wrap,SDL_UpdateTexture" "LINKER:--wrap,SDL_RenderCopy")
	mmodern_data_test(NAME xeen_input_scheduling COMMAND mmodern_input_scheduling_tests "${MMODERN_XEEN_DATA_DIR}")
	mmodern_data_test(NAME xeen_main_screen COMMAND mmodern_input_scheduling_tests "${MMODERN_XEEN_DATA_DIR}" mouse)
	target_link_libraries(mmodern_training_input_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2)
	target_link_options(mmodern_training_input_tests PRIVATE "LINKER:--wrap,SDL_GetTicks")
	foreach(mode authority native controls)
		mmodern_data_test(NAME xeen_training_input_${mode} COMMAND mmodern_training_input_tests "${MMODERN_XEEN_DATA_DIR}" ${mode})
	endforeach()
	add_executable(mmodern_training_original_tests tests/XeenTrainingOriginalTests.cpp)
	target_link_libraries(mmodern_training_original_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_data_test(NAME xeen_training_original COMMAND mmodern_training_original_tests "${MMODERN_XEEN_DATA_DIR}")

	add_executable(mmodern_m41_cli_witness src/main.cpp tests/XeenM41CliWitness.cpp tests/XeenRestoreReplayProbe.cpp)
	target_link_libraries(mmodern_m41_cli_witness PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2 mmodern_platform_libs)
	target_link_options(mmodern_m41_cli_witness PRIVATE "LINKER:--wrap,${XEEN_WRAP_PLAY_GAMEPLAY}"
		"LINKER:--wrap,${XEEN_WRAP_BANK_INTEREST}")
	xeen_restore_replay(mmodern_m41_cli_witness)

	target_link_options(mmodern_m41_cli_witness PRIVATE "LINKER:--wrap,SDL_UpdateTexture" "LINKER:--wrap,SDL_RenderCopy")

	add_executable(mmodern_training_process_tests tests/XeenTrainingProcessTests.cpp)
	target_link_libraries(mmodern_training_process_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_libs)
	add_dependencies(mmodern_training_process_tests mmodern_m41_cli_witness)
	mmodern_data_test(NAME xeen_training_process COMMAND mmodern_training_process_tests $<TARGET_FILE:mmodern_m41_cli_witness> "${MMODERN_XEEN_DATA_DIR}")

	add_executable(mmodern_m42_cli_witness src/main.cpp tests/XeenM42CliWitness.cpp tests/XeenRestoreReplayProbe.cpp)
	target_link_libraries(mmodern_m42_cli_witness PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2 mmodern_platform_libs)
	target_link_options(mmodern_m42_cli_witness PRIVATE "LINKER:--wrap,${XEEN_WRAP_PLAY_GAMEPLAY}"
		"LINKER:--wrap,${XEEN_WRAP_BANK_INTEREST}"
		"LINKER:--wrap,${XEEN_WRAP_ENEMY_CONSTRUCTOR}"
		"LINKER:--wrap,${XEEN_WRAP_PLAYER_CONSTRUCTOR}")
	xeen_restore_replay(mmodern_m42_cli_witness)

	target_link_options(mmodern_m42_cli_witness PRIVATE "LINKER:--wrap,SDL_UpdateTexture" "LINKER:--wrap,SDL_RenderCopy")

	add_executable(mmodern_purchase_process_tests tests/XeenPurchaseProcessTests.cpp)
	target_link_libraries(mmodern_purchase_process_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_libs)
	add_dependencies(mmodern_purchase_process_tests mmodern_m42_cli_witness)
	mmodern_data_test(NAME xeen_purchase_process COMMAND mmodern_purchase_process_tests $<TARGET_FILE:mmodern_m42_cli_witness> "${MMODERN_XEEN_DATA_DIR}")
	mmodern_data_test(NAME xeen_new_game_process COMMAND mmodern_purchase_process_tests $<TARGET_FILE:mmodern_m42_cli_witness> "${MMODERN_XEEN_DATA_DIR}" new-game)
    add_executable(mmodern_food_bank_rules_tests tests/XeenFoodBankRulesTests.cpp)
    target_link_libraries(mmodern_food_bank_rules_tests PRIVATE mmodern_xeen)
    mmodern_part_a_test(NAME xeen_food_bank_rules COMMAND mmodern_food_bank_rules_tests)
    mmodern_test_properties(xeen_food_bank_rules PROPERTIES LABELS fast)

	add_executable(mmodern_service_economy_tests tests/XeenServiceEconomyTests.cpp)
	target_link_libraries(mmodern_service_economy_tests PRIVATE mmodern_xeen mmodern_platform_libs)
	mmodern_part_a_test(NAME xeen_service_economy COMMAND mmodern_service_economy_tests)
	add_executable(mmodern_service_day_initialization_tests tests/XeenServiceDayInitializationTests.cpp)
	target_link_libraries(mmodern_service_day_initialization_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_data_test(NAME xeen_service_day_initialization COMMAND mmodern_service_day_initialization_tests "${MMODERN_XEEN_DATA_DIR}")

	add_executable(mmodern_m40_cli_witness src/main.cpp tests/XeenM40CliWitness.cpp tests/XeenRestoreReplayProbe.cpp)
	target_link_libraries(mmodern_m40_cli_witness PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2 mmodern_platform_libs)
	target_link_options(mmodern_m40_cli_witness PRIVATE "LINKER:--wrap,${XEEN_WRAP_PLAY_GAMEPLAY}"
		"LINKER:--wrap,${XEEN_WRAP_BANK_INTEREST}")
	xeen_restore_replay(mmodern_m40_cli_witness)

	target_link_options(mmodern_m40_cli_witness PRIVATE "LINKER:--wrap,SDL_UpdateTexture" "LINKER:--wrap,SDL_RenderCopy")

	add_executable(mmodern_service_day_process_tests tests/XeenServiceDayProcessTests.cpp)
	target_link_libraries(mmodern_service_day_process_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl mmodern_platform_libs)
	add_dependencies(mmodern_service_day_process_tests mmodern_m40_cli_witness)
	mmodern_data_test(NAME xeen_service_day_process COMMAND mmodern_service_day_process_tests $<TARGET_FILE:mmodern_m40_cli_witness> "${MMODERN_XEEN_DATA_DIR}")

	add_executable(mmodern_combat_clock_process_tests tests/XeenCombatClockProcessTests.cpp)
	target_link_libraries(mmodern_combat_clock_process_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl mmodern_platform_libs)
	add_dependencies(mmodern_combat_clock_process_tests mmodern_m40_cli_witness)
	mmodern_data_test(NAME xeen_combat_clock_process COMMAND mmodern_combat_clock_process_tests $<TARGET_FILE:mmodern_m40_cli_witness> "${MMODERN_XEEN_DATA_DIR}")
	mmodern_data_test(NAME xeen_vertigo_original COMMAND mmodern_vertigo_original "${MMODERN_XEEN_DATA_DIR}")
	add_executable(mmodern_journey_resource_tests tests/XeenJourneyResourceTests.cpp)
	target_link_libraries(mmodern_journey_resource_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_part_a_test(NAME xeen_journey_resources COMMAND mmodern_journey_resource_tests)
	add_executable(mmodern_regional_persistence_tests tests/XeenRegionalPersistenceTests.cpp)
	target_link_libraries(mmodern_regional_persistence_tests PRIVATE mmodern_xeen ZLIB::ZLIB)
	mmodern_part_a_test(NAME xeen_regional_persistence COMMAND mmodern_regional_persistence_tests)
	add_executable(mmodern_mutation_tests tests/XeenMutationTests.cpp)
	target_link_libraries(mmodern_mutation_tests PRIVATE mmodern_app mmodern_xeen mmodern_scummvm_xeen)
	mmodern_part_a_test(NAME xeen_mutation_boundary COMMAND mmodern_mutation_tests)
	add_executable(mmodern_mutation_value_tests tests/XeenMutationValueTests.cpp)
	target_link_libraries(mmodern_mutation_value_tests PRIVATE mmodern_core)
	mmodern_part_a_test(NAME xeen_mutation_values COMMAND mmodern_mutation_value_tests)
	add_executable(mmodern_antidote_rules_tests tests/XeenAntidoteRulesTests.cpp)
	target_link_libraries(mmodern_antidote_rules_tests PRIVATE mmodern_xeen)
	mmodern_part_a_test(NAME xeen_antidote_rules COMMAND mmodern_antidote_rules_tests)

	add_executable(mmodern_consequence_original tests/XeenConsequenceOriginal.cpp)
	target_link_libraries(mmodern_consequence_original PRIVATE mmodern_app SDL2::SDL2 mmodern_platform_libs)
	target_link_options(mmodern_consequence_original PRIVATE "-Wl,--wrap=${XEEN_WRAP_RANDOM_DRAW}"
        "-Wl,--wrap=${XEEN_WRAP_PLAYER_CONSTRUCTOR}")
    mmodern_data_test(NAME xeen_combat_impact_original COMMAND mmodern_consequence_original "${MMODERN_XEEN_DATA_DIR}")
    mmodern_test_properties(xeen_combat_impact_original PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software;MMODERN_M49_IMPACT_ONLY=1")

	add_executable(mmodern_resource_cold_original tests/XeenConsequenceOriginal.cpp)
	target_compile_definitions(mmodern_resource_cold_original PRIVATE MMODERN_M45_COLD_CACHE)
	target_link_libraries(mmodern_resource_cold_original PRIVATE mmodern_app SDL2::SDL2)
	target_link_options(mmodern_resource_cold_original PRIVATE "-Wl,--wrap=${XEEN_WRAP_RANDOM_DRAW}"
        "-Wl,--wrap=${XEEN_WRAP_PLAYER_CONSTRUCTOR}")
	mmodern_data_test(NAME xeen_resource_cold_original COMMAND mmodern_resource_cold_original "${MMODERN_XEEN_DATA_DIR}")
	mmodern_test_properties(xeen_resource_cold_original PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software")
    add_executable(mmodern_consequence_sprite_tests tests/XeenConsequenceSpriteTests.cpp)
    target_link_libraries(mmodern_consequence_sprite_tests PRIVATE mmodern_xeen)
    mmodern_part_a_test(NAME xeen_consequence_sprites COMMAND mmodern_consequence_sprite_tests)
	add_executable(mmodern_consequence_rules_tests tests/XeenConsequenceRulesTests.cpp)
	target_link_libraries(mmodern_consequence_rules_tests PRIVATE mmodern_xeen)
	mmodern_part_a_test(NAME xeen_consequence_rules COMMAND mmodern_consequence_rules_tests)
	add_executable(mmodern_rest_tests tests/XeenRestTests.cpp)
	target_link_libraries(mmodern_rest_tests PRIVATE mmodern_xeen)
	mmodern_part_a_test(NAME xeen_rest_rules COMMAND mmodern_rest_tests)
	add_executable(mmodern_rest_original_tests tests/XeenRestOriginalTests.cpp)
	target_link_libraries(mmodern_rest_original_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_data_test(NAME xeen_rest_original COMMAND mmodern_rest_original_tests "${MMODERN_XEEN_DATA_DIR}")
	add_executable(mmodern_equipment_original_tests tests/XeenEquipmentOriginalTests.cpp)
	target_link_libraries(mmodern_equipment_original_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_data_test(NAME xeen_equipment_original COMMAND mmodern_equipment_original_tests "${MMODERN_XEEN_DATA_DIR}")
	add_executable(mmodern_death_original_tests tests/XeenDeathOriginalTests.cpp)
	target_link_libraries(mmodern_death_original_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_data_test(NAME xeen_death_original COMMAND mmodern_death_original_tests "${MMODERN_XEEN_DATA_DIR}")
	add_executable(mmodern_typed_damage_tests tests/XeenTypedDamageTests.cpp)
	target_link_libraries(mmodern_typed_damage_tests PRIVATE mmodern_xeen)
	mmodern_part_a_test(NAME xeen_typed_damage COMMAND mmodern_typed_damage_tests)
	add_executable(mmodern_monster_ability_tests tests/XeenMonsterAbilityTests.cpp)
	target_link_libraries(mmodern_monster_ability_tests PRIVATE mmodern_xeen)
	mmodern_part_a_test(NAME xeen_monster_abilities COMMAND mmodern_monster_ability_tests)
	add_executable(mmodern_monster_ability_gameplay_tests tests/XeenMonsterAbilityGameplayTests.cpp)
	target_link_libraries(mmodern_monster_ability_gameplay_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2)
	mmodern_part_a_test(NAME xeen_monster_ability_gameplay COMMAND mmodern_monster_ability_gameplay_tests)
	mmodern_data_test(NAME xeen_monster_fixture_original COMMAND mmodern_monster_ability_gameplay_tests "${MMODERN_XEEN_DATA_DIR}")
	add_executable(mmodern_whole_map_foundation_tests tests/XeenWholeMapFoundationTests.cpp)
	add_executable(mmodern_barrier_tests tests/XeenBarrierTests.cpp)
	target_link_libraries(mmodern_barrier_tests PRIVATE mmodern_xeen)
	mmodern_part_a_test(NAME xeen_barriers COMMAND mmodern_barrier_tests)
	add_executable(mmodern_barrier_original_tests tests/XeenBarrierOriginalTests.cpp)
	target_link_libraries(mmodern_barrier_original_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2)
	mmodern_data_test(NAME xeen_barriers_original COMMAND mmodern_barrier_original_tests "${MMODERN_XEEN_DATA_DIR}")
	add_executable(mmodern_opening_original_tests tests/XeenOpeningOriginalTests.cpp)
	target_link_libraries(mmodern_opening_original_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2)
	mmodern_data_test(NAME xeen_opening_original COMMAND mmodern_opening_original_tests "${MMODERN_XEEN_DATA_DIR}")
	target_link_libraries(mmodern_whole_map_foundation_tests PRIVATE mmodern_xeen)
	mmodern_part_a_test(NAME xeen_whole_map_foundations COMMAND mmodern_whole_map_foundation_tests)
	add_executable(mmodern_service_continuation_tests tests/XeenServiceContinuationTests.cpp)
	target_link_libraries(mmodern_service_continuation_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2)
	mmodern_data_test(NAME xeen_service_continuation COMMAND mmodern_service_continuation_tests "${MMODERN_XEEN_DATA_DIR}")
	add_executable(mmodern_city_event_publication_tests tests/XeenCityEventPublicationTests.cpp)
	target_link_libraries(mmodern_city_event_publication_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2)
	mmodern_data_test(NAME xeen_city_event_publication COMMAND mmodern_city_event_publication_tests "${MMODERN_XEEN_DATA_DIR}")

	add_executable(mmodern_world_event_tests tests/XeenWorldEventTests.cpp)
	target_link_libraries(mmodern_world_event_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2 mmodern_dos_export_support)
	mmodern_data_test(NAME xeen_world_event COMMAND mmodern_world_event_tests "${MMODERN_XEEN_DATA_DIR}")

 add_executable(mmodern_dos_export_tests tests/XeenDosExportTests.cpp)
 target_link_libraries(mmodern_dos_export_tests PRIVATE mmodern_dos_export_support mmodern_test_save_support)
 if(NOT WIN32)
  # Pull filesystem::rename from the archive so its libc call can be wrapped;
  # calls already bound inside the shared libstdc++ cannot be interposed by --wrap.
  target_link_options(mmodern_dos_export_tests PRIVATE -static-libstdc++ "LINKER:--wrap,rename")
 endif()
 add_dependencies(mmodern_dos_export_tests mmodern_dos_export)
 mmodern_part_a_test(NAME xeen_dos_export_unit COMMAND mmodern_dos_export_tests)
 mmodern_data_test(NAME xeen_dos_export_original COMMAND mmodern_dos_export_tests "${MMODERN_XEEN_DATA_DIR}")
 mmodern_data_test(NAME xeen_dos_export_earned COMMAND mmodern_world_event_tests "${MMODERN_XEEN_DATA_DIR}" --dos-export)
 mmodern_test_properties(xeen_dos_export_unit xeen_dos_export_original xeen_dos_export_earned PROPERTIES LABELS fast)
	add_executable(mmodern_regional_rules_tests tests/XeenRegionalRulesTests.cpp)
	target_link_libraries(mmodern_regional_rules_tests PRIVATE mmodern_xeen)
	mmodern_part_a_test(NAME xeen_regional_rules COMMAND mmodern_regional_rules_tests)

	add_executable(mmodern_journey_event_tests tests/XeenJourneyEventTests.cpp)
	target_link_libraries(mmodern_journey_event_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2)
	mmodern_part_a_test(NAME xeen_journey_event COMMAND mmodern_journey_event_tests)
	mmodern_test_properties(xeen_journey_event PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software")

	add_executable(mmodern_expedition_gameplay_tests tests/XeenExpeditionGameplayTests.cpp tests/XeenRestoreReplayProbe.cpp)
	target_link_libraries(mmodern_expedition_gameplay_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2)
	xeen_restore_replay(mmodern_expedition_gameplay_tests)
	mmodern_part_a_test(NAME xeen_expedition_gameplay COMMAND mmodern_expedition_gameplay_tests)
	mmodern_test_properties(xeen_expedition_gameplay PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software")

	add_executable(mmodern_expedition_tests tests/XeenExpeditionTests.cpp)
	target_link_libraries(mmodern_expedition_tests PRIVATE mmodern_app mmodern_xeen)
	target_link_options(mmodern_expedition_tests PRIVATE "LINKER:--wrap,${XEEN_WRAP_RANDOM_DRAW}")
	mmodern_part_a_test(NAME xeen_expedition COMMAND mmodern_expedition_tests)
	add_executable(mmodern_journey_tests tests/XeenJourneyTests.cpp)
	target_link_libraries(mmodern_journey_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_part_a_test(NAME xeen_journey COMMAND mmodern_journey_tests)

	add_executable(mmodern_event_foundation_tests tests/XeenEventFoundationTests.cpp)
	target_link_libraries(mmodern_event_foundation_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2)
	mmodern_part_a_test(NAME xeen_event_foundation COMMAND mmodern_event_foundation_tests)
	add_executable(mmodern_event_value_tests tests/XeenEventValueTests.cpp)
	target_link_libraries(mmodern_event_value_tests PRIVATE mmodern_xeen)
	mmodern_part_a_test(NAME xeen_event_values COMMAND mmodern_event_value_tests)
	mmodern_part_a_test(NAME xeen_clouds_invariant_b5 COMMAND mmodern_event_value_tests b5)
	add_executable(mmodern_map_event_tests tests/XeenMapEventTests.cpp)
	target_link_libraries(mmodern_map_event_tests PRIVATE mmodern_xeen)
	mmodern_part_a_test(NAME xeen_map_events COMMAND mmodern_map_event_tests)
	mmodern_data_test(NAME xeen_event_foundation_original COMMAND mmodern_event_foundation_tests "${MMODERN_XEEN_DATA_DIR}")

	add_executable(mmodern_travel_input_tests tests/XeenTravelInputTests.cpp)
	target_link_libraries(mmodern_travel_input_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2)
	mmodern_part_a_test(NAME xeen_travel_input COMMAND mmodern_travel_input_tests)
	mmodern_data_test(NAME xeen_travel_input_original COMMAND mmodern_travel_input_tests "${MMODERN_XEEN_DATA_DIR}")

	add_executable(mmodern_gold_vein_tests tests/XeenGoldVeinTests.cpp)
	target_link_libraries(mmodern_gold_vein_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2)
	mmodern_data_test(NAME xeen_gold_vein_original COMMAND mmodern_gold_vein_tests "${MMODERN_XEEN_DATA_DIR}")

	add_executable(mmodern_journey_gameplay_tests tests/XeenJourneyGameplayTests.cpp tests/XeenRestoreReplayProbe.cpp)
	target_link_libraries(mmodern_journey_gameplay_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2)
	mmodern_part_a_test(NAME xeen_journey_gameplay COMMAND mmodern_journey_gameplay_tests)
	mmodern_part_a_test(NAME xeen_journey_sdl COMMAND mmodern_journey_gameplay_tests sdl)
	mmodern_test_properties(xeen_journey_sdl PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software")
	xeen_restore_replay(mmodern_journey_gameplay_tests)

	add_executable(mmodern_m39_cli_witness src/main.cpp tests/XeenM39CliWitness.cpp tests/XeenRestoreReplayProbe.cpp)
	target_link_libraries(mmodern_m39_cli_witness PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2 mmodern_platform_libs)
	target_link_options(mmodern_m39_cli_witness PRIVATE "-Wl,--wrap=${XEEN_WRAP_PLAY_GAMEPLAY}")
	target_link_options(mmodern_m39_cli_witness PRIVATE
        "-Wl,--wrap=${XEEN_WRAP_BEGIN_CAST}"
        "-Wl,--wrap=${XEEN_WRAP_RESPOND_CAST}"
        "-Wl,--wrap=${XEEN_WRAP_SERVICE_CAST}")
	target_link_options(mmodern_m39_cli_witness PRIVATE "-Wl,--wrap=SDL_UpdateTexture" "-Wl,--wrap=SDL_RenderCopy")
	target_link_options(mmodern_m39_cli_witness PRIVATE "-Wl,--wrap=SDL_FilterEvents" "-Wl,--wrap=SDL_PollEvent" "-Wl,--wrap=SDL_WaitEventTimeout")
	xeen_restore_replay(mmodern_m39_cli_witness)

	add_executable(mmodern_combat_casting_process_tests tests/XeenCombatCastingProcessTests.cpp)
	target_link_libraries(mmodern_combat_casting_process_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl)
	add_dependencies(mmodern_combat_casting_process_tests mmodern_m39_cli_witness)
	mmodern_data_test(NAME xeen_combat_casting_process COMMAND mmodern_combat_casting_process_tests $<TARGET_FILE:mmodern_m39_cli_witness> "${MMODERN_XEEN_DATA_DIR}")

	add_executable(mmodern_combat_casting_control_tests tests/XeenCombatCastingControlTests.cpp)
    target_link_libraries(mmodern_combat_casting_control_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl)
    add_dependencies(mmodern_combat_casting_control_tests mmodern_m39_cli_witness)
    # Every control is its own child process; the three groups run in parallel.
    foreach(m39_group faults effects native)
        mmodern_data_test(NAME xeen_combat_casting_${m39_group} COMMAND mmodern_combat_casting_control_tests $<TARGET_FILE:mmodern_m39_cli_witness> "${MMODERN_XEEN_DATA_DIR}" ${m39_group})
    endforeach()
    mmodern_data_test(NAME xeen_combat_player_ready_input COMMAND mmodern_combat_casting_control_tests $<TARGET_FILE:mmodern_m39_cli_witness> "${MMODERN_XEEN_DATA_DIR}")
    mmodern_test_properties(xeen_combat_player_ready_input PROPERTIES ENVIRONMENT "MMODERN_M39_ONLY_INPUT=1")

	add_executable(mmodern_m38_cli_witness src/main.cpp tests/XeenM38CliWitness.cpp tests/XeenRestoreReplayProbe.cpp)
	target_link_libraries(mmodern_m38_cli_witness PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2 mmodern_platform_libs)
	target_link_options(mmodern_m38_cli_witness PRIVATE "-Wl,--wrap=${XEEN_WRAP_PLAY_GAMEPLAY}")
	target_link_options(mmodern_m38_cli_witness PRIVATE "-Wl,--wrap=SDL_UpdateTexture" "-Wl,--wrap=SDL_RenderCopy")
	xeen_restore_replay(mmodern_m38_cli_witness)

	add_executable(mmodern_smith_process_tests tests/XeenSmithProcessTests.cpp)
	target_link_libraries(mmodern_smith_process_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl)
	add_dependencies(mmodern_smith_process_tests mmodern_m38_cli_witness)
	mmodern_data_test(NAME xeen_smith_process COMMAND mmodern_smith_process_tests $<TARGET_FILE:mmodern_m38_cli_witness> "${MMODERN_XEEN_DATA_DIR}")

	add_executable(mmodern_m37_cli_witness src/main.cpp tests/XeenM37CliWitness.cpp tests/XeenRestoreReplayProbe.cpp)
	target_link_libraries(mmodern_m37_cli_witness PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2 mmodern_platform_libs)
	target_link_options(mmodern_m37_cli_witness PRIVATE "-Wl,--wrap=${XEEN_WRAP_PLAY_GAMEPLAY}")
	xeen_restore_replay(mmodern_m37_cli_witness)

	add_executable(mmodern_vertigo_process_tests tests/XeenVertigoProcessTests.cpp)
	target_link_libraries(mmodern_vertigo_process_tests PRIVATE mmodern_app mmodern_xeen)
	add_dependencies(mmodern_vertigo_process_tests mmodern_m37_cli_witness)
	mmodern_data_test(NAME xeen_vertigo_process COMMAND mmodern_vertigo_process_tests $<TARGET_FILE:mmodern_m37_cli_witness> "${MMODERN_XEEN_DATA_DIR}")
	add_executable(mmodern_combat_tests tests/XeenCombatTests.cpp)

	add_executable(mmodern_combat_gameplay_tests tests/XeenCombatGameplayTests.cpp)
	target_link_libraries(mmodern_combat_gameplay_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2)
	target_link_options(mmodern_combat_gameplay_tests PRIVATE "LINKER:--wrap,${XEEN_WRAP_RANDOM_DRAW}")
	mmodern_part_a_test(NAME xeen_combat_gameplay COMMAND mmodern_combat_gameplay_tests)
	mmodern_part_a_test(NAME xeen_combat_sdl COMMAND mmodern_combat_gameplay_tests sdl)
	mmodern_test_properties(xeen_combat_sdl PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software")
	mmodern_part_a_test(NAME xeen_combat_cli COMMAND mmodern_combat_gameplay_tests cli $<TARGET_FILE:mmodern>)
	target_link_libraries(mmodern_combat_tests PRIVATE mmodern_app mmodern_xeen)
	target_link_options(mmodern_combat_tests PRIVATE "LINKER:--wrap,${XEEN_WRAP_RANDOM_DRAW}")
	mmodern_part_a_test(NAME xeen_combat COMMAND mmodern_combat_tests)
	add_executable(mmodern_combat_authority_tests tests/XeenCombatAuthorityTests.cpp)
	target_link_libraries(mmodern_combat_authority_tests PRIVATE mmodern_app mmodern_xeen)
	target_link_options(mmodern_combat_authority_tests PRIVATE "LINKER:--wrap,${XEEN_WRAP_RANDOM_DRAW}")
	mmodern_part_a_test(NAME xeen_combat_authority COMMAND mmodern_combat_authority_tests)
	add_executable(mmodern_combat_persistence_tests tests/XeenCombatPersistenceTests.cpp)
	target_link_libraries(mmodern_combat_persistence_tests PRIVATE mmodern_app mmodern_xeen)
	target_link_options(mmodern_combat_persistence_tests PRIVATE "LINKER:--wrap,${XEEN_WRAP_RANDOM_DRAW}")
	mmodern_part_a_test(NAME xeen_combat_persistence COMMAND mmodern_combat_persistence_tests)
	add_executable(mmodern_resource_admission_tests tests/XeenResourceAdmissionTests.cpp)
	target_link_libraries(mmodern_resource_admission_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_part_a_test(NAME xeen_resource_admission COMMAND mmodern_resource_admission_tests)

	add_executable(mmodern_completed_restore_tests tests/XeenCompletedRestoreTests.cpp tests/XeenRestoreReplayProbe.cpp)

	add_executable(mmodern_journey_persistence_tests tests/XeenJourneyPersistenceTests.cpp tests/XeenRestoreReplayProbe.cpp)
	target_link_libraries(mmodern_journey_persistence_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_part_a_test(NAME xeen_journey_persistence COMMAND mmodern_journey_persistence_tests)

	target_link_libraries(mmodern_completed_restore_tests PRIVATE mmodern_app mmodern_xeen)
	xeen_restore_replay(mmodern_completed_restore_tests)
	xeen_restore_replay(mmodern_journey_persistence_tests)
	mmodern_part_a_test(NAME xeen_completed_restore COMMAND mmodern_completed_restore_tests)
	add_executable(mmodern_encounter_flow_tests tests/XeenEncounterFlowTests.cpp)
	target_link_libraries(mmodern_encounter_flow_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_part_a_test(NAME xeen_encounter_flow COMMAND mmodern_encounter_flow_tests)

	add_executable(mmodern_encounter_gameplay_tests tests/XeenEncounterGameplayTests.cpp)
	target_link_libraries(mmodern_encounter_gameplay_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2)
	mmodern_part_a_test(NAME xeen_encounter_gameplay COMMAND mmodern_encounter_gameplay_tests)
	mmodern_test_properties(xeen_encounter_gameplay PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software")
	add_executable(mmodern_monster_format_tests tests/XeenMonsterFormatTests.cpp)
	target_link_libraries(mmodern_monster_format_tests PRIVATE mmodern_xeen)
	mmodern_part_a_test(NAME xeen_monster_format COMMAND mmodern_monster_format_tests)
	add_executable(mmodern_actor_approach_tests tests/XeenActorApproachTests.cpp)
	target_link_libraries(mmodern_actor_approach_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_part_a_test(NAME xeen_actor_approach COMMAND mmodern_actor_approach_tests)
	add_executable(mmodern_encounter_save_tests tests/XeenEncounterSaveTests.cpp)
	target_link_libraries(mmodern_encounter_save_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_part_a_test(NAME xeen_encounter_save COMMAND mmodern_encounter_save_tests)
	add_executable(mmodern_item_transfer_tests tests/XeenItemTransferTests.cpp)
	target_link_libraries(mmodern_item_transfer_tests PRIVATE mmodern_app mmodern_xeen ZLIB::ZLIB)
	mmodern_part_a_test(NAME xeen_item_transfer COMMAND mmodern_item_transfer_tests)

	add_executable(mmodern_inventory_gameplay_tests tests/XeenInventoryGameplayTests.cpp)
	target_link_libraries(mmodern_inventory_gameplay_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2 ZLIB::ZLIB)
	mmodern_part_a_test(NAME xeen_inventory_gameplay COMMAND mmodern_inventory_gameplay_tests)
	mmodern_part_a_test(NAME xeen_inventory_sdl COMMAND mmodern_inventory_gameplay_tests sdl)
	mmodern_test_properties(xeen_inventory_sdl PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software")

	add_executable(mmodern_equipment_gameplay_tests tests/XeenEquipmentGameplayTests.cpp)
	target_link_libraries(mmodern_equipment_gameplay_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2 ZLIB::ZLIB)
	mmodern_part_a_test(NAME xeen_equipment_gameplay COMMAND mmodern_equipment_gameplay_tests)
    add_executable(mmodern_journal_tests tests/XeenJournalTests.cpp)
    target_link_libraries(mmodern_journal_tests PRIVATE mmodern_app mmodern_xeen)
    mmodern_part_a_test(NAME xeen_journal COMMAND mmodern_journal_tests)
    mmodern_data_test(NAME xeen_journal_original COMMAND mmodern_journal_tests "${MMODERN_XEEN_DATA_DIR}")
    mmodern_test_properties(xeen_journal xeen_journal_original PROPERTIES LABELS "fast")
    # M110: the Russian language layer. The data test needs the Russian World
    # of Xeen translation (MMODERN_RU_DATA_DIR) and is left out without it.
    add_executable(mmodern_language_layer_tests tests/XeenLanguageLayerTests.cpp)
    target_link_libraries(mmodern_language_layer_tests PRIVATE mmodern_xeen)
    mmodern_part_a_test(NAME xeen_language_layer COMMAND mmodern_language_layer_tests)
    mmodern_test_properties(xeen_language_layer PROPERTIES LABELS "fast")
    add_executable(mmodern_russian_layer_data_tests tests/XeenRussianLayerDataTests.cpp)
    target_link_libraries(mmodern_russian_layer_data_tests PRIVATE mmodern_test_save_support mmodern_xeen mmodern_scummvm_xeen)
    target_include_directories(mmodern_russian_layer_data_tests PRIVATE
        "${SCUMMVM_BUILD_DIR}" "${SCUMMVM_SOURCE_DIR}" "${SCUMMVM_SOURCE_DIR}/engines")
    target_compile_definitions(mmodern_russian_layer_data_tests PRIVATE ${MMODERN_SCUMMVM_DEFINES})
    if(MMODERN_RU_DATA_DIR)
        mmodern_data_test(NAME xeen_russian_layer_original COMMAND mmodern_russian_layer_data_tests
            "${MMODERN_XEEN_DATA_DIR}" "${MMODERN_RU_DATA_DIR}")
        mmodern_test_properties(xeen_russian_layer_original PROPERTIES LABELS "fast")
    endif()
    # M110 Part B: Russian program text, cutscene text and item names.
    add_executable(mmodern_russian_program_text_tests tests/XeenRussianProgramTextTests.cpp)
    target_link_libraries(mmodern_russian_program_text_tests PRIVATE mmodern_app mmodern_xeen mmodern_scummvm_xeen)
    if(MMODERN_RU_DATA_DIR)
        mmodern_data_test(NAME xeen_russian_program_text_original COMMAND mmodern_russian_program_text_tests
            "${MMODERN_XEEN_DATA_DIR}" "${MMODERN_RU_DATA_DIR}")
        mmodern_test_properties(xeen_russian_program_text_original PROPERTIES LABELS "fast")
    endif()
    # M110 Part C: Russian rules and input; the data test reads both programs.
    add_executable(mmodern_russian_rules_tests tests/XeenRussianRulesTests.cpp)
    target_link_libraries(mmodern_russian_rules_tests PRIVATE mmodern_app mmodern_xeen mmodern_scummvm_xeen)
    mmodern_part_a_test(NAME xeen_russian_rules COMMAND mmodern_russian_rules_tests)
    mmodern_test_properties(xeen_russian_rules PROPERTIES LABELS "fast")
    if(MMODERN_RU_DATA_DIR)
        # Runs four cutscenes in two languages at two devices (about two minutes).
        mmodern_data_test(NAME xeen_russian_rules_original COMMAND mmodern_russian_rules_tests
            "${MMODERN_XEEN_DATA_DIR}" "${MMODERN_RU_DATA_DIR}")
    endif()
    add_executable(mmodern_inn_tests tests/XeenInnTests.cpp)
    target_link_libraries(mmodern_inn_tests PRIVATE mmodern_app mmodern_xeen)
    mmodern_data_test(NAME xeen_inn COMMAND mmodern_inn_tests "${MMODERN_XEEN_DATA_DIR}")
    mmodern_test_properties(xeen_inn PROPERTIES LABELS "fast")
    add_executable(mmodern_party_size_tests tests/XeenPartySizeTests.cpp)
    target_link_libraries(mmodern_party_size_tests PRIVATE mmodern_app mmodern_xeen)
    mmodern_part_a_test(NAME xeen_party_size COMMAND mmodern_party_size_tests)
    mmodern_data_test(NAME xeen_party_size_original COMMAND mmodern_party_size_tests "${MMODERN_XEEN_DATA_DIR}")
    mmodern_test_properties(xeen_party_size xeen_party_size_original PROPERTIES LABELS "fast")
	add_executable(mmodern_dialog_tests tests/XeenDialogTests.cpp)

	add_executable(mmodern_party_interface_tests tests/XeenPartyInterfaceTests.cpp)
	add_executable(mmodern_exploration_tests tests/XeenExplorationTests.cpp)
	add_executable(mmodern_mapping_gameplay_tests tests/XeenMappingGameplayTests.cpp)

	add_executable(mmodern_m60_exploration_tests tests/XeenM60ExplorationTests.cpp)
	add_executable(mmodern_mainland_event_tests tests/XeenMainlandEventTests.cpp)
    add_executable(mmodern_item_spell_rules_tests tests/XeenItemSpellRulesTests.cpp)
    add_executable(mmodern_travel_spell_rules_tests tests/XeenTravelSpellRulesTests.cpp)
    target_link_libraries(mmodern_travel_spell_rules_tests PRIVATE mmodern_xeen)
    mmodern_part_a_test(NAME xeen_travel_spell_rules COMMAND mmodern_travel_spell_rules_tests)
    add_executable(mmodern_travel_spell_original_tests tests/XeenTravelSpellOriginalTests.cpp)
    target_link_libraries(mmodern_travel_spell_original_tests PRIVATE mmodern_app mmodern_xeen)
    mmodern_data_test(NAME xeen_travel_spell_original COMMAND mmodern_travel_spell_original_tests "${MMODERN_XEEN_DATA_DIR}")
    target_link_libraries(mmodern_item_spell_rules_tests PRIVATE mmodern_xeen)
    mmodern_part_a_test(NAME xeen_item_spell_rules COMMAND mmodern_item_spell_rules_tests)
    add_executable(mmodern_item_spell_original_tests tests/XeenItemSpellOriginalTests.cpp)
    target_link_libraries(mmodern_item_spell_original_tests PRIVATE mmodern_app mmodern_xeen)
    mmodern_data_test(NAME xeen_item_spell_original COMMAND mmodern_item_spell_original_tests "${MMODERN_XEEN_DATA_DIR}")
    add_executable(mmodern_senses_rules_tests tests/XeenSensesRulesTests.cpp)
    target_link_libraries(mmodern_senses_rules_tests PRIVATE mmodern_xeen)
    mmodern_part_a_test(NAME xeen_senses_rules COMMAND mmodern_senses_rules_tests)
    add_executable(mmodern_traversal_lighting_tests tests/XeenTraversalLightingTests.cpp)
    target_link_libraries(mmodern_traversal_lighting_tests PRIVATE mmodern_app mmodern_xeen)
    mmodern_part_a_test(NAME xeen_traversal_lighting COMMAND mmodern_traversal_lighting_tests)
    add_executable(mmodern_senses_original_tests tests/XeenSensesOriginalTests.cpp)
    target_link_libraries(mmodern_senses_original_tests PRIVATE mmodern_app mmodern_xeen)
    mmodern_data_test(NAME xeen_senses_original COMMAND mmodern_senses_original_tests "${MMODERN_XEEN_DATA_DIR}")
	add_executable(mmodern_party_buff_original_tests tests/XeenPartyBuffOriginalTests.cpp)
	target_link_libraries(mmodern_party_buff_original_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_data_test(NAME xeen_party_buff_original COMMAND mmodern_party_buff_original_tests "${MMODERN_XEEN_DATA_DIR}")
	add_executable(mmodern_party_buff_rules_tests tests/XeenPartyBuffRulesTests.cpp)
	target_link_libraries(mmodern_party_buff_rules_tests PRIVATE mmodern_xeen)
	mmodern_part_a_test(NAME xeen_party_buff_rules COMMAND mmodern_party_buff_rules_tests)
	add_executable(mmodern_party_healing_rules_tests tests/XeenPartyHealingRulesTests.cpp)
	target_link_libraries(mmodern_party_healing_rules_tests PRIVATE mmodern_xeen)
	mmodern_part_a_test(NAME xeen_party_healing_rules COMMAND mmodern_party_healing_rules_tests)

	add_executable(mmodern_party_healing_combat_tests tests/XeenPartyHealingCombatTests.cpp)

	add_executable(mmodern_combat_items_tests tests/XeenCombatItemsTests.cpp)
	target_link_libraries(mmodern_combat_items_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2 ZLIB::ZLIB)
	target_link_options(mmodern_combat_items_tests PRIVATE "LINKER:--wrap,${XEEN_WRAP_RANDOM_DRAW}")
	mmodern_part_a_test(NAME xeen_combat_items COMMAND mmodern_combat_items_tests)
	mmodern_data_test(NAME xeen_combat_items_original COMMAND mmodern_combat_items_tests "${MMODERN_XEEN_DATA_DIR}")
	add_executable(mmodern_skill_indicator_tests tests/XeenSkillIndicatorTests.cpp)
	target_link_libraries(mmodern_skill_indicator_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2 ZLIB::ZLIB)
	mmodern_part_a_test(NAME xeen_skill_indicators COMMAND mmodern_skill_indicator_tests)
	mmodern_data_test(NAME xeen_skill_indicators_original COMMAND mmodern_skill_indicator_tests "${MMODERN_XEEN_DATA_DIR}")
	mmodern_test_properties(xeen_combat_items xeen_combat_items_original PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software")

	target_link_libraries(mmodern_party_healing_combat_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2 ZLIB::ZLIB)
	target_link_options(mmodern_party_healing_combat_tests PRIVATE "LINKER:--wrap,${XEEN_WRAP_RANDOM_DRAW}")
	mmodern_part_a_test(NAME xeen_party_healing_combat COMMAND mmodern_party_healing_combat_tests)
	mmodern_test_properties(xeen_party_healing_combat PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software")
	add_executable(mmodern_party_healing_original_tests tests/XeenPartyHealingOriginalTests.cpp)
	target_link_libraries(mmodern_party_healing_original_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_data_test(NAME xeen_party_healing_original COMMAND mmodern_party_healing_original_tests "${MMODERN_XEEN_DATA_DIR}")

	target_link_libraries(mmodern_m60_exploration_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_data_test(NAME xeen_m60_exploration_original COMMAND mmodern_m60_exploration_tests "${MMODERN_XEEN_DATA_DIR}")
	mmodern_test_properties(xeen_m60_exploration_original PROPERTIES LABELS "fast")
	target_link_libraries(mmodern_mainland_event_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_data_test(NAME xeen_mainland_events_original COMMAND mmodern_mainland_event_tests "${MMODERN_XEEN_DATA_DIR}")
	mmodern_test_properties(xeen_mainland_events_original PROPERTIES LABELS "fast")
	target_link_libraries(mmodern_mapping_gameplay_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_data_test(NAME xeen_mapping_gameplay_original COMMAND mmodern_mapping_gameplay_tests "${MMODERN_XEEN_DATA_DIR}")
	mmodern_test_properties(xeen_mapping_gameplay_original PROPERTIES LABELS "fast")
	target_link_libraries(mmodern_exploration_tests PRIVATE mmodern_xeen)
	mmodern_part_a_test(NAME xeen_exploration COMMAND mmodern_exploration_tests)
	mmodern_test_properties(xeen_exploration PROPERTIES LABELS "fast")

	target_link_libraries(mmodern_party_interface_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_data_test(NAME xeen_party_interface_original COMMAND mmodern_party_interface_tests "${MMODERN_XEEN_DATA_DIR}")
	mmodern_test_properties(xeen_party_interface_original PROPERTIES LABELS "fast")
	add_executable(mmodern_interface_tests tests/XeenInterfaceTests.cpp)
	target_link_libraries(mmodern_interface_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_data_test(NAME xeen_interface_original COMMAND mmodern_interface_tests "${MMODERN_XEEN_DATA_DIR}")
	mmodern_test_properties(xeen_interface_original PROPERTIES LABELS "fast")
	add_executable(mmodern_service_dialog_tests tests/XeenServiceDialogTests.cpp)
	add_executable(mmodern_darkside_service_tests tests/XeenDarksideServiceTests.cpp)
	target_link_libraries(mmodern_darkside_service_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_data_test(NAME xeen_darkside_services_original COMMAND mmodern_darkside_service_tests "${MMODERN_XEEN_DATA_DIR}")
	add_executable(mmodern_m61_part_a_tests tests/XeenM61PartATests.cpp)

	add_executable(mmodern_m61_part_b_tests tests/XeenM61PartBTests.cpp)
	add_executable(mmodern_member_prompt_click_tests tests/XeenMemberPromptClickTests.cpp)
	target_link_libraries(mmodern_member_prompt_click_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2)
	mmodern_data_test(NAME xeen_member_prompt_click COMMAND mmodern_member_prompt_click_tests "${MMODERN_XEEN_DATA_DIR}")
	mmodern_test_properties(xeen_member_prompt_click PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software")

	target_link_libraries(mmodern_m61_part_b_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_data_test(NAME xeen_m61_part_b COMMAND mmodern_m61_part_b_tests "${MMODERN_XEEN_DATA_DIR}")
	mmodern_test_properties(xeen_m61_part_b PROPERTIES LABELS "fast")
	target_link_libraries(mmodern_m61_part_a_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_data_test(NAME xeen_m61_part_a COMMAND mmodern_m61_part_a_tests "${MMODERN_XEEN_DATA_DIR}" "${SCUMMVM_SOURCE_DIR}")
	add_executable(mmodern_food_bank_flow_tests tests/XeenFoodBankFlowTests.cpp tests/XeenFoodBankSaveTests.cpp)
	target_link_libraries(mmodern_food_bank_flow_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_data_test(NAME xeen_food_bank_flow COMMAND mmodern_food_bank_flow_tests "${MMODERN_XEEN_DATA_DIR}")
	target_link_libraries(mmodern_service_dialog_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_data_test(NAME xeen_service_dialogs COMMAND mmodern_service_dialog_tests "${MMODERN_XEEN_DATA_DIR}")
	target_link_libraries(mmodern_dialog_tests PRIVATE mmodern_xeen)
	mmodern_part_a_test(NAME xeen_dialogs COMMAND mmodern_dialog_tests)
	mmodern_data_test(NAME xeen_dialogs_original COMMAND mmodern_dialog_tests "${MMODERN_XEEN_DATA_DIR}")
	mmodern_test_properties(xeen_dialogs xeen_dialogs_original PROPERTIES LABELS "fast")
    add_executable(mmodern_jump_rules_tests tests/XeenJumpRulesTests.cpp)
    target_link_libraries(mmodern_jump_rules_tests PRIVATE mmodern_xeen)
    mmodern_part_a_test(NAME xeen_jump_rules COMMAND mmodern_jump_rules_tests)
    mmodern_data_test(NAME xeen_jump_rules_original COMMAND mmodern_jump_rules_tests "${MMODERN_XEEN_DATA_DIR}")
    mmodern_test_properties(xeen_jump_rules xeen_jump_rules_original PROPERTIES LABELS "fast")
    add_executable(mmodern_map_view_tests tests/XeenMapViewTests.cpp)
    target_link_libraries(mmodern_map_view_tests PRIVATE mmodern_xeen)
    mmodern_part_a_test(NAME xeen_map_view COMMAND mmodern_map_view_tests)
    mmodern_data_test(NAME xeen_map_view_original COMMAND mmodern_map_view_tests "${MMODERN_XEEN_DATA_DIR}")
    mmodern_test_properties(xeen_map_view xeen_map_view_original PROPERTIES LABELS "fast")
    add_executable(mmodern_dos_text_tests tests/XeenDosTextTests.cpp)
    add_executable(mmodern_wall_clock_tests tests/XeenWallClockTests.cpp)
    target_link_libraries(mmodern_wall_clock_tests PRIVATE mmodern_xeen)
    target_compile_definitions(mmodern_wall_clock_tests PRIVATE MMODERN_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
    mmodern_part_a_test(NAME xeen_wall_clock COMMAND mmodern_wall_clock_tests)
    add_executable(mmodern_title_resource_tests tests/XeenTitleResourceTests.cpp)

    add_executable(mmodern_title_flow_tests tests/XeenTitleFlowTests.cpp tests/XeenTitleApplicationControls.cpp)
    target_link_libraries(mmodern_title_flow_tests PRIVATE mmodern_app mmodern_xeen mmodern_test_save_support SDL2::SDL2 mmodern_platform_libs)
    target_link_options(mmodern_title_flow_tests PRIVATE
      "LINKER:--wrap,${XEEN_WRAP_CUTSCENE_DATA}"
      "LINKER:--wrap,${XEEN_WRAP_PRESENTATION_CLOCK}"
      "LINKER:--wrap,${XEEN_WRAP_PLAY_GAMEPLAY}"
      "LINKER:--wrap,${XEEN_WRAP_SHOW_INTERACTIVE}"
      "LINKER:--wrap,${XEEN_WRAP_RENDER_TEXT}"
      "LINKER:--wrap,${XEEN_WRAP_SLOT_DIRECTORY}")
    # Fake-clock intros/startup and endings run apart from the serial native/
    # real-clock checks. Intros use private settings and need no gameplay save.
    mmodern_data_test(NAME xeen_title_flow COMMAND mmodern_title_flow_tests "${MMODERN_XEEN_DATA_DIR}" "${MMODERN_M92_TITLE_PRESET}" --serial)
    mmodern_data_test(NAME xeen_title_flow_intros COMMAND mmodern_title_flow_tests "${MMODERN_XEEN_DATA_DIR}" "" --intros)
    mmodern_data_test(NAME xeen_title_flow_endings COMMAND mmodern_title_flow_tests "${MMODERN_XEEN_DATA_DIR}" "${MMODERN_M92_TITLE_PRESET}" --endings)
    mmodern_data_test(NAME xeen_title_flow_darkside_endings COMMAND mmodern_title_flow_tests "${MMODERN_XEEN_DATA_DIR}" "${MMODERN_M92_TITLE_PRESET}" --darkside-endings)
    mmodern_test_properties(xeen_title_flow xeen_title_flow_endings PROPERTIES FIXTURES_REQUIRED m92_title_preset)
    mmodern_test_properties(xeen_title_flow xeen_title_flow_intros xeen_title_flow_endings PROPERTIES LABELS "fast")
    mmodern_test_properties(xeen_title_flow xeen_title_flow_intros xeen_title_flow_endings PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software")
    mmodern_test_properties(xeen_title_flow_darkside_endings PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software")
    target_link_libraries(mmodern_title_resource_tests PRIVATE mmodern_xeen)
    mmodern_data_test(NAME xeen_title_resources COMMAND mmodern_title_resource_tests "${MMODERN_XEEN_DATA_DIR}")
    mmodern_test_properties(xeen_title_resources PROPERTIES LABELS "fast")
    target_link_libraries(mmodern_dos_text_tests PRIVATE mmodern_xeen)
    mmodern_part_a_test(NAME xeen_dos_text COMMAND mmodern_dos_text_tests)
    mmodern_data_test(NAME xeen_dos_text_original COMMAND mmodern_dos_text_tests "${MMODERN_XEEN_DATA_DIR}" "${MMODERN_XEEN_UI_DATA}")
    mmodern_test_properties(xeen_dos_text xeen_dos_text_original PROPERTIES LABELS "fast")
    add_executable(mmodern_data_source_tests tests/XeenDataSourceTests.cpp)
    target_link_libraries(mmodern_data_source_tests PRIVATE mmodern_xeen)
    mmodern_part_a_test(NAME xeen_data_source COMMAND mmodern_data_source_tests "${CMAKE_CURRENT_BINARY_DIR}/m53-source-tests")
    mmodern_data_test(NAME xeen_data_source_original COMMAND mmodern_data_source_tests "${CMAKE_CURRENT_BINARY_DIR}/m53-source-original-tests" "${MMODERN_XEEN_DATA_DIR}")
    add_executable(mmodern_archive_source_tests tests/XeenArchiveSourceTests.cpp)
    target_link_libraries(mmodern_archive_source_tests PRIVATE mmodern_app mmodern_xeen mmodern_scummvm_xeen)
    mmodern_part_a_test(NAME xeen_archive_source COMMAND mmodern_archive_source_tests "${CMAKE_CURRENT_BINARY_DIR}/m53-archive-tests")
    mmodern_data_test(NAME xeen_archive_source_original COMMAND mmodern_archive_source_tests "${CMAKE_CURRENT_BINARY_DIR}/m53-archive-original-tests" "${MMODERN_XEEN_DATA_DIR}")
    mmodern_test_properties(xeen_data_source xeen_data_source_original xeen_archive_source xeen_archive_source_original PROPERTIES LABELS "fast")
	add_executable(mmodern_item_catalog_tests tests/XeenItemCatalogTests.cpp)
	target_link_libraries(mmodern_item_catalog_tests PRIVATE mmodern_xeen)
	mmodern_part_a_test(NAME xeen_item_catalog COMMAND mmodern_item_catalog_tests)
	add_executable(mmodern_item_catalog_asset_tests tests/XeenItemCatalogAssetTests.cpp)
	target_link_libraries(mmodern_item_catalog_asset_tests PRIVATE
		mmodern_xeen mmodern_scummvm_xeen)
	mmodern_part_a_test(NAME xeen_item_catalog_assets COMMAND mmodern_item_catalog_asset_tests)
 add_executable(mmodern_item_catalog_fixture tests/XeenItemCatalogFixture.cpp)
 mmodern_part_a_test(NAME xeen_item_catalog_generation
  COMMAND "${CMAKE_COMMAND}"
   "-DGenerator=${MMODERN_ITEM_CATALOG_GENERATOR}" "-DGit=${GIT_EXECUTABLE}"
   "-DFixtureWriter=$<TARGET_FILE:mmodern_item_catalog_fixture>"
   "-DTestRoot=${CMAKE_CURRENT_BINARY_DIR}/item-catalog-generation-tests"
   "-DProductionOutput=${MMODERN_ITEM_CATALOG_GENERATED_DIR}/XeenItemCatalogEnglish.inc"
   "-DProductionOutputSha256=${MMODERN_ITEM_CATALOG_GENERATED_SHA256}"
   "-DDialogOutput=${MMODERN_ITEM_CATALOG_GENERATED_DIR}/XeenDialogDrawing.inc"
   "-DDialogOutputSha256=${MMODERN_DIALOG_DRAWING_GENERATED_SHA256}"
   -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/XeenItemCatalogGenerationTests.cmake")

	add_executable(mmodern_outdoor_animation_tests tests/XeenOutdoorAnimationTests.cpp)
	target_link_libraries(mmodern_outdoor_animation_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2 ZLIB::ZLIB)
	mmodern_part_a_test(NAME xeen_outdoor_animation COMMAND mmodern_outdoor_animation_tests)
	mmodern_part_a_test(NAME xeen_outdoor_animation_sdl COMMAND mmodern_outdoor_animation_tests sdl)
	mmodern_test_properties(xeen_outdoor_animation_sdl PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software")
	foreach(suite item_reward reward_execution reward_flow reward_gameplay)
		if(suite STREQUAL "item_reward")
			set(test_source ItemReward)
		elseif(suite STREQUAL "reward_execution")
			set(test_source RewardExecution)
		elseif(suite STREQUAL "reward_flow")
			set(test_source RewardFlow)
		else()
			set(test_source RewardGameplay)
		endif()

		add_executable(mmodern_${suite}_tests tests/Xeen${test_source}Tests.cpp)
		target_link_libraries(mmodern_${suite}_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2 ZLIB::ZLIB)
		mmodern_part_a_test(NAME xeen_${suite} COMMAND mmodern_${suite}_tests)
	endforeach()
	mmodern_part_a_test(NAME xeen_reward_sdl COMMAND mmodern_reward_gameplay_tests sdl)
	mmodern_test_properties(xeen_reward_sdl PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software")

	add_executable(mmodern_save_cli_tests tests/XeenSaveCliTests.cpp)
	target_link_libraries(mmodern_save_cli_tests PRIVATE mmodern_app mmodern_xeen ZLIB::ZLIB)
	mmodern_part_a_test(NAME xeen_save_cli COMMAND mmodern_save_cli_tests $<TARGET_FILE:mmodern>)
	add_executable(mmodern_save_sdl_tests tests/XeenSaveSdlTests.cpp)
	target_link_libraries(mmodern_save_sdl_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2 ZLIB::ZLIB)
	mmodern_part_a_test(NAME xeen_save_sdl COMMAND mmodern_save_sdl_tests)
	mmodern_test_properties(xeen_save_sdl PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software")

	add_executable(mmodern_save_file_tests tests/XeenSaveFileTests.cpp)
	target_link_libraries(mmodern_save_file_tests PRIVATE mmodern_app mmodern_xeen ZLIB::ZLIB)
	mmodern_part_a_test(NAME xeen_save_file COMMAND mmodern_save_file_tests)
	add_executable(mmodern_save_flow_tests tests/XeenSaveFlowTests.cpp)
	target_link_libraries(mmodern_save_flow_tests PRIVATE mmodern_app mmodern_xeen ZLIB::ZLIB)
	mmodern_part_a_test(NAME xeen_save_flow COMMAND mmodern_save_flow_tests)
	add_executable(mmodern_armor_repair_tests tests/XeenArmorRepairTests.cpp)
	target_link_libraries(mmodern_armor_repair_tests PRIVATE mmodern_xeen)
	mmodern_part_a_test(NAME xeen_armor_repair COMMAND mmodern_armor_repair_tests)
	add_executable(mmodern_save_format_tests tests/XeenSaveFormatTests.cpp)
	# Audit helper uses this build's reader; preserve it before changing the format.
	add_executable(mmodern_save_dump tests/XeenSaveDump.cpp)
	target_link_libraries(mmodern_save_dump PRIVATE mmodern_xeen ZLIB::ZLIB)
	mmodern_part_a_test(NAME xeen_save_dump COMMAND mmodern_save_dump --self-test)
	target_link_libraries(mmodern_save_format_tests PRIVATE mmodern_xeen ZLIB::ZLIB)
	mmodern_part_a_test(NAME xeen_save_format COMMAND mmodern_save_format_tests)

	add_executable(mmodern_darkside_tests tests/XeenDarksideTests.cpp)
	add_executable(mmodern_outdoor_spawn_tests tests/XeenOutdoorSpawnTests.cpp)
	target_link_libraries(mmodern_outdoor_spawn_tests PRIVATE mmodern_app)
	mmodern_data_test(NAME xeen_outdoor_spawn_original COMMAND mmodern_outdoor_spawn_tests "${MMODERN_XEEN_DATA_DIR}")

	target_link_libraries(mmodern_darkside_tests PRIVATE mmodern_app mmodern_test_save_support mmodern_dos_export_support)
	mmodern_data_test(NAME xeen_darkside_original COMMAND mmodern_darkside_tests "${MMODERN_XEEN_DATA_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/m101-frames")
	mmodern_data_test(NAME xeen_mixed_maps_original COMMAND mmodern_darkside_tests "${MMODERN_XEEN_DATA_DIR}" temporary mixed)
	mmodern_data_test(NAME xeen_mixed_travel_original COMMAND mmodern_darkside_tests "${MMODERN_XEEN_DATA_DIR}" temporary mixed-travel)
	mmodern_data_test(NAME xeen_darkside_quests_original COMMAND mmodern_darkside_tests "${MMODERN_XEEN_DATA_DIR}" temporary quests)
	mmodern_data_test(NAME xeen_fall_defeat_original COMMAND mmodern_darkside_tests "${MMODERN_XEEN_DATA_DIR}" temporary fall-defeat)
	mmodern_data_test(NAME xeen_dial_original COMMAND mmodern_darkside_tests "${MMODERN_XEEN_DATA_DIR}" temporary dial)
	mmodern_data_test(NAME xeen_darkside_cd_disc_original COMMAND mmodern_darkside_tests "${MMODERN_XEEN_DATA_DIR}" temporary cd-disc)
	mmodern_data_test(NAME xeen_darkside_repeats_original COMMAND mmodern_darkside_tests "${MMODERN_XEEN_DATA_DIR}" temporary repeats)
	mmodern_data_test(NAME xeen_darkside_restores_original COMMAND mmodern_darkside_tests "${MMODERN_XEEN_DATA_DIR}" temporary restores)
	mmodern_data_test(NAME xeen_darkside_crossing_original COMMAND mmodern_darkside_tests "${MMODERN_XEEN_DATA_DIR}" temporary crossing)
	mmodern_data_test(NAME xeen_darkside_keys_original COMMAND mmodern_darkside_tests "${MMODERN_XEEN_DATA_DIR}" temporary keys)
	add_executable(mmodern_save_state_tests tests/XeenSaveStateTests.cpp)
	target_link_libraries(mmodern_save_state_tests PRIVATE mmodern_app mmodern_xeen ZLIB::ZLIB)
	mmodern_part_a_test(NAME xeen_save_state COMMAND mmodern_save_state_tests)

	add_executable(mmodern_quest_flag_tests tests/XeenQuestFlagTests.cpp)
	target_link_libraries(mmodern_quest_flag_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_part_a_test(NAME xeen_quest_flags COMMAND mmodern_quest_flag_tests)
	add_executable(mmodern_npc_tests tests/XeenNpcTests.cpp)
	target_link_libraries(mmodern_npc_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2)
	mmodern_part_a_test(NAME xeen_npc COMMAND mmodern_npc_tests)
	mmodern_part_a_test(NAME xeen_npc_sdl COMMAND mmodern_npc_tests sdl)
	mmodern_test_properties(xeen_npc_sdl PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software")
	add_executable(mmodern_quest_grant_tests tests/XeenQuestGrantTests.cpp)
	add_executable(mmodern_who_will_tests tests/XeenWhoWillTests.cpp)
	target_link_libraries(mmodern_who_will_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2)
	mmodern_part_a_test(NAME xeen_who_will COMMAND mmodern_who_will_tests)
	mmodern_part_a_test(NAME xeen_who_will_sdl COMMAND mmodern_who_will_tests sdl)
	mmodern_test_properties(xeen_who_will_sdl PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software")
	target_link_libraries(mmodern_quest_grant_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_part_a_test(NAME xeen_quest_grants COMMAND mmodern_quest_grant_tests)

	add_executable(mmodern_quest_item_tests tests/XeenQuestItemTests.cpp)
	target_link_libraries(mmodern_quest_item_tests PRIVATE mmodern_xeen)
	mmodern_part_a_test(NAME xeen_quest_items COMMAND mmodern_quest_item_tests)
	add_executable(mmodern_myra_smoke tests/MyraIntegrationTest.cpp)
	target_link_libraries(mmodern_myra_smoke PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2)
	mmodern_data_test(NAME xeen_myra_original COMMAND mmodern_myra_smoke
		"${MMODERN_XEEN_DATA_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/myra-original-direct")
	mmodern_data_test(NAME xeen_myra_original_sdl COMMAND mmodern_myra_smoke
		"${MMODERN_XEEN_DATA_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/myra-original-sdl" sdl)
	mmodern_test_properties(xeen_myra_original xeen_myra_original_sdl PROPERTIES
		LABELS "fast" ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software")
	add_executable(mmodern_visual_remove_tests tests/XeenVisualRemoveTests.cpp)
	target_link_libraries(mmodern_visual_remove_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_part_a_test(NAME xeen_visual_remove COMMAND mmodern_visual_remove_tests)
	add_executable(mmodern_outdoor_composer_tests tests/XeenOutdoorComposerTests.cpp)
	target_link_libraries(mmodern_outdoor_composer_tests PRIVATE mmodern_xeen)
	mmodern_part_a_test(NAME xeen_outdoor_composer COMMAND mmodern_outdoor_composer_tests)
	add_executable(mmodern_outdoor_object_tests tests/XeenOutdoorObjectTests.cpp)
	target_link_libraries(mmodern_outdoor_object_tests PRIVATE mmodern_xeen)
	mmodern_part_a_test(NAME xeen_outdoor_objects COMMAND mmodern_outdoor_object_tests)
	add_executable(mmodern_object_visual_tests tests/XeenObjectVisualTests.cpp)
	target_link_libraries(mmodern_object_visual_tests PRIVATE mmodern_xeen)
	mmodern_part_a_test(NAME xeen_object_visual COMMAND mmodern_object_visual_tests)
	add_executable(mmodern_object_sprite_tests tests/XeenObjectSpriteTests.cpp)
	target_link_libraries(mmodern_object_sprite_tests PRIVATE mmodern_xeen)
	mmodern_part_a_test(NAME xeen_object_sprite COMMAND mmodern_object_sprite_tests)
	add_executable(mmodern_object_visual_smoke tests/ObjectVisualIntegrationTest.cpp)
	target_link_libraries(mmodern_object_visual_smoke PRIVATE mmodern_xeen mmodern_scummvm_xeen)
	target_include_directories(mmodern_object_visual_smoke PRIVATE
		"${SCUMMVM_BUILD_DIR}" "${SCUMMVM_SOURCE_DIR}" "${SCUMMVM_SOURCE_DIR}/engines")
	target_compile_definitions(mmodern_object_visual_smoke PRIVATE ${MMODERN_SCUMMVM_DEFINES})
	add_executable(mmodern_remove_tests tests/XeenRemoveTests.cpp)
	target_link_libraries(mmodern_remove_tests PRIVATE mmodern_xeen)
	mmodern_part_a_test(NAME xeen_remove COMMAND mmodern_remove_tests)
	add_executable(mmodern_session_persistence_tests tests/XeenSessionPersistenceTests.cpp)
	target_link_libraries(mmodern_session_persistence_tests PRIVATE mmodern_xeen)
	mmodern_part_a_test(NAME xeen_session_persistence COMMAND mmodern_session_persistence_tests)
	add_executable(mmodern_session_identity_tests tests/XeenSessionIdentityTests.cpp)
	target_link_libraries(mmodern_session_identity_tests PRIVATE mmodern_xeen)
	mmodern_part_a_test(NAME xeen_session_identity COMMAND mmodern_session_identity_tests)
	# Parser tests link only standard C++; no ScummVM, SDL, or game installation.
	add_executable(mmodern_map_format_tests
		tests/XeenMapFormatTests.cpp
		src/formats/xeen/XeenEventFormat.cpp
		src/formats/xeen/XeenMapFormat.cpp
	)
	target_link_libraries(mmodern_map_format_tests PRIVATE mmodern_core)
	mmodern_part_a_test(NAME xeen_map_formats COMMAND mmodern_map_format_tests)
	add_executable(mmodern_event_format_tests
		tests/XeenEventFormatTests.cpp
		src/formats/xeen/XeenEventFormat.cpp
	)
	target_link_libraries(mmodern_event_format_tests PRIVATE mmodern_core)
	mmodern_part_a_test(NAME xeen_event_formats COMMAND mmodern_event_format_tests)
	add_executable(mmodern_event_loader_tests
		tests/XeenEventLoaderTests.cpp
		src/formats/xeen/XeenEventFormat.cpp
		src/games/xeen/XeenEventDiagnostics.cpp
		src/games/xeen/XeenEventLoader.cpp
		src/games/xeen/XeenEventScript.cpp
	)
	target_link_libraries(mmodern_event_loader_tests PRIVATE mmodern_core)
	mmodern_part_a_test(NAME xeen_event_loader COMMAND mmodern_event_loader_tests)
	add_executable(mmodern_event_text_tests
		tests/XeenEventTextTests.cpp
		src/formats/xeen/XeenEventTextFormat.cpp
		src/games/xeen/XeenEventTextLoader.cpp
	)
	target_link_libraries(mmodern_event_text_tests PRIVATE mmodern_core)
	mmodern_part_a_test(NAME xeen_event_text COMMAND mmodern_event_text_tests)
	add_executable(mmodern_event_script_tests
		tests/XeenEventScriptTests.cpp
		src/games/xeen/XeenEventDiagnostics.cpp
		src/games/xeen/XeenEventScript.cpp
		src/games/xeen/XeenEventTrigger.cpp
	)
	target_link_libraries(mmodern_event_script_tests PRIVATE mmodern_core)
	mmodern_part_a_test(NAME xeen_event_script COMMAND mmodern_event_script_tests)
	add_executable(mmodern_event_decoder_tests
		tests/XeenEventDecoderTests.cpp
		src/games/xeen/XeenEventDecoder.cpp
        src/games/xeen/XeenEntranceScene.cpp
	)
	target_link_libraries(mmodern_event_decoder_tests PRIVATE mmodern_core)
	mmodern_part_a_test(NAME xeen_event_decoder COMMAND mmodern_event_decoder_tests)
	add_executable(mmodern_event_interpreter_tests
		tests/XeenEventInterpreterTests.cpp
	)
	target_link_libraries(mmodern_event_interpreter_tests PRIVATE
		mmodern_xeen mmodern_scummvm_xeen)
	mmodern_part_a_test(NAME xeen_event_interpreter COMMAND mmodern_event_interpreter_tests)
 add_executable(mmodern_cd_event_original_tests tests/XeenCdEventOriginalTests.cpp)
 target_link_libraries(mmodern_cd_event_original_tests PRIVATE mmodern_xeen)
 mmodern_data_test(NAME xeen_cd_event_original COMMAND mmodern_cd_event_original_tests "${MMODERN_XEEN_DATA_DIR}")
	add_executable(mmodern_event_presentation_tests
		tests/XeenEventPresentationTests.cpp
	)
	target_link_libraries(mmodern_event_presentation_tests PRIVATE
		mmodern_xeen mmodern_scummvm_xeen)
	mmodern_part_a_test(NAME xeen_event_presentation COMMAND mmodern_event_presentation_tests)
	add_executable(mmodern_event_ui_tests
		tests/XeenEventUiTests.cpp
	)
	target_link_libraries(mmodern_event_ui_tests PRIVATE
		mmodern_xeen mmodern_scummvm_xeen)
	mmodern_part_a_test(NAME xeen_event_ui COMMAND mmodern_event_ui_tests)
    add_executable(mmodern_event_dispatch_original_tests tests/XeenEventDispatchOriginalTests.cpp)
    target_link_libraries(mmodern_event_dispatch_original_tests PRIVATE mmodern_app mmodern_platform_sdl SDL2::SDL2 mmodern_platform_libs)
    mmodern_data_test(NAME xeen_event_dispatch_original COMMAND mmodern_event_dispatch_original_tests "${MMODERN_XEEN_DATA_DIR}")
    add_executable(mmodern_clouds_invariant_tests tests/XeenCloudsInvariantTests.cpp)
    target_link_libraries(mmodern_clouds_invariant_tests PRIVATE mmodern_app mmodern_platform_sdl SDL2::SDL2 mmodern_platform_libs)
    mmodern_data_test(NAME xeen_clouds_invariant_i1 COMMAND mmodern_clouds_invariant_tests "${MMODERN_XEEN_DATA_DIR}" i1)
    mmodern_data_test(NAME xeen_clouds_invariant_i2 COMMAND mmodern_clouds_invariant_tests "${MMODERN_XEEN_DATA_DIR}" i2)
    mmodern_data_test(NAME xeen_clouds_invariant_i3 COMMAND mmodern_clouds_invariant_tests "${MMODERN_XEEN_DATA_DIR}" i3)
    mmodern_data_test(NAME xeen_clouds_invariant_i5 COMMAND mmodern_clouds_invariant_tests "${MMODERN_XEEN_DATA_DIR}" i5)
    mmodern_data_test(NAME xeen_clouds_invariant_i6 COMMAND mmodern_clouds_invariant_tests "${MMODERN_XEEN_DATA_DIR}" i6)
    mmodern_data_test(NAME xeen_clouds_invariant_b1b2 COMMAND mmodern_clouds_invariant_tests "${MMODERN_XEEN_DATA_DIR}" b1b2)
    mmodern_test_properties(xeen_clouds_invariant_b1b2 PROPERTIES RUN_SERIAL TRUE ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    add_executable(mmodern_chest_original_tests tests/XeenChestOriginalTests.cpp)
    target_link_libraries(mmodern_chest_original_tests PRIVATE mmodern_app mmodern_platform_sdl SDL2::SDL2 mmodern_platform_libs)
    mmodern_data_test(NAME xeen_chest_original_map70 COMMAND mmodern_chest_original_tests "${MMODERN_XEEN_DATA_DIR}" map70)
    mmodern_data_test(NAME xeen_chest_original_map9 COMMAND mmodern_chest_original_tests "${MMODERN_XEEN_DATA_DIR}" map9)
    mmodern_data_test(NAME xeen_chest_original_map44 COMMAND mmodern_chest_original_tests "${MMODERN_XEEN_DATA_DIR}" map44)
    mmodern_data_test(NAME xeen_chest_original_route COMMAND mmodern_chest_original_tests "${MMODERN_XEEN_DATA_DIR}" route)
    mmodern_data_test(NAME xeen_chest_original_map47 COMMAND mmodern_chest_original_tests "${MMODERN_XEEN_DATA_DIR}" map47)
    add_executable(mmodern_event_dispatch_tests tests/XeenEventDispatchTests.cpp)
    target_link_libraries(mmodern_event_dispatch_tests PRIVATE mmodern_app mmodern_platform_sdl SDL2::SDL2 mmodern_platform_libs)
    mmodern_part_a_test(NAME xeen_event_dispatch COMMAND mmodern_event_dispatch_tests)
    mmodern_part_a_test(NAME xeen_event_dispatch_sdl COMMAND mmodern_event_dispatch_tests sdl)
    mmodern_test_properties(xeen_event_dispatch_sdl PROPERTIES RUN_SERIAL TRUE ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software")
	add_executable(mmodern_event_system_tests
		tests/XeenEventSystemTests.cpp
	)
	target_link_libraries(mmodern_event_system_tests PRIVATE
		mmodern_xeen mmodern_scummvm_xeen)
	mmodern_part_a_test(NAME xeen_event_system COMMAND mmodern_event_system_tests)
	add_executable(mmodern_manual_event_tests
		tests/XeenManualEventTests.cpp
	)
	target_link_libraries(mmodern_manual_event_tests PRIVATE
		mmodern_xeen mmodern_scummvm_xeen)
	mmodern_part_a_test(NAME xeen_manual_event COMMAND mmodern_manual_event_tests)
	add_executable(mmodern_navigation_flow_tests
		tests/XeenNavigationFlowTests.cpp
		src/app/XeenNavigationFlow.cpp
	)
	target_link_libraries(mmodern_navigation_flow_tests PRIVATE
		mmodern_xeen mmodern_scummvm_xeen)
	mmodern_part_a_test(NAME xeen_navigation_flow COMMAND mmodern_navigation_flow_tests)
	add_executable(mmodern_sdl_input_tests tests/SdlInputTests.cpp)
 add_executable(mmodern_setup_screen_tests tests/XeenSetupScreenTests.cpp)
 target_link_libraries(mmodern_setup_screen_tests PRIVATE mmodern_app mmodern_xeen mmodern_scummvm_xeen mmodern_platform_sdl SDL2::SDL2)
 mmodern_part_a_test(NAME xeen_setup_screen COMMAND mmodern_setup_screen_tests)
 mmodern_test_properties(xeen_setup_screen PROPERTIES LABELS fast RESOURCE_LOCK xeen_setup_screen)
 add_executable(mmodern_setup_original_tests tests/XeenSetupOriginalTests.cpp)
 target_link_libraries(mmodern_setup_original_tests PRIVATE mmodern_platform_libs mmodern_app mmodern_xeen mmodern_scummvm_xeen mmodern_platform_sdl SDL2::SDL2)
 target_link_options(mmodern_setup_original_tests PRIVATE "-Wl,--wrap=_ZN7mmodern19windowPlacementFileB5cxx11Ev")
 mmodern_data_test(NAME xeen_setup_screen_original COMMAND mmodern_setup_original_tests "${MMODERN_XEEN_DATA_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/m114-original-test" "${MMODERN_RU_DATA_DIR}")
 mmodern_test_properties(xeen_setup_screen_original PROPERTIES LABELS process RUN_SERIAL TRUE)
 add_executable(mmodern_presentation_clock_tests tests/PresentationClockTests.cpp)
 add_executable(mmodern_fm_effect_sites_tests tests/XeenFmEffectSitesTests.cpp)

 add_executable(mmodern_m86_presentation_tests tests/XeenM86PresentationTests.cpp)
 target_link_libraries(mmodern_m86_presentation_tests PRIVATE mmodern_app mmodern_xeen mmodern_scummvm_xeen)
 target_link_options(mmodern_m86_presentation_tests PRIVATE "LINKER:--wrap,${XEEN_WRAP_STOP_DIGITAL}")
 mmodern_data_test(NAME xeen_m86_presentation COMMAND mmodern_m86_presentation_tests "${MMODERN_XEEN_DATA_DIR}")
 target_link_libraries(mmodern_fm_effect_sites_tests PRIVATE mmodern_app mmodern_xeen mmodern_scummvm_xeen)
 mmodern_data_test(NAME xeen_fm_effect_sites COMMAND mmodern_fm_effect_sites_tests "${MMODERN_XEEN_DATA_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/docs/rules/audio.md")
 mmodern_test_properties(xeen_fm_effect_sites PROPERTIES ENVIRONMENT "SDL_AUDIODRIVER=dummy")
 add_executable(mmodern_promus_tests tests/PromusDriverTests.cpp)
 target_link_libraries(mmodern_promus_tests PRIVATE mmodern_app mmodern_xeen mmodern_scummvm_xeen)
 mmodern_data_test(NAME xeen_promus COMMAND mmodern_promus_tests "${MMODERN_XEEN_DATA_DIR}" "${MMODERN_PROMUS_CAPTURES_DIR}")
 mmodern_test_properties(xeen_promus PROPERTIES ENVIRONMENT "SDL_AUDIODRIVER=dummy")
 add_executable(mmodern_m94_opl_tests tests/MonoOplTests.cpp)
 target_link_libraries(mmodern_m94_opl_tests PRIVATE mmodern_app mmodern_xeen mmodern_scummvm_xeen)
 mmodern_data_test(NAME xeen_m94_opl COMMAND mmodern_m94_opl_tests "${MMODERN_XEEN_DATA_DIR}" "${MMODERN_M94_CAPTURES_DIR}")
 mmodern_test_properties(xeen_m94_opl PROPERTIES ENVIRONMENT "SDL_AUDIODRIVER=dummy")
 add_executable(mmodern_m94_rolmus_tests tests/RolmusDriverTests.cpp)
 target_link_libraries(mmodern_m94_rolmus_tests PRIVATE mmodern_app mmodern_xeen mmodern_scummvm_xeen)
 target_link_options(mmodern_m94_rolmus_tests PRIVATE "LINKER:--wrap,${XEEN_WRAP_MIDI_QUEUE_SIZE}")
 target_include_directories(mmodern_m94_rolmus_tests PRIVATE "${SCUMMVM_SOURCE_DIR}" "${SCUMMVM_BUILD_DIR}")
 mmodern_data_test(NAME xeen_m94_rolmus COMMAND mmodern_m94_rolmus_tests "${MMODERN_XEEN_DATA_DIR}" "${MMODERN_M94_CAPTURES_DIR}" "${MMODERN_MT32_ROM_DIR}")
 mmodern_test_properties(xeen_m94_rolmus PROPERTIES ENVIRONMENT "SDL_AUDIODRIVER=dummy")
 # The separate oracle executable can replay captured foreground BX values;
 # the game and ordinary unit test compile the production fixed-BX profile.
 add_executable(mmodern_m94_rolmus_oracle_tests tests/RolmusDriverTests.cpp src/games/xeen/RolmusDriver.cpp)
 target_link_libraries(mmodern_m94_rolmus_oracle_tests PRIVATE mmodern_app mmodern_xeen mmodern_scummvm_xeen)
 target_include_directories(mmodern_m94_rolmus_oracle_tests PRIVATE "${SCUMMVM_SOURCE_DIR}" "${SCUMMVM_BUILD_DIR}")
 target_compile_definitions(mmodern_m94_rolmus_oracle_tests PRIVATE MMODERN_ROLMUS_ORACLE=1)
 mmodern_data_test(NAME xeen_m94_rolmus_oracle COMMAND mmodern_m94_rolmus_oracle_tests "${MMODERN_XEEN_DATA_DIR}" "${MMODERN_M94_CAPTURES_DIR}" "${MMODERN_MT32_ROM_DIR}")
 mmodern_test_properties(xeen_m94_rolmus_oracle PROPERTIES ENVIRONMENT "SDL_AUDIODRIVER=dummy")
 add_executable(mmodern_fm_output_tests tests/XeenFmOutputTests.cpp)
 target_link_libraries(mmodern_fm_output_tests PRIVATE mmodern_app mmodern_xeen mmodern_scummvm_xeen)
 mmodern_data_test(NAME xeen_fm_output COMMAND mmodern_fm_output_tests "${MMODERN_XEEN_DATA_DIR}" "${MMODERN_MT32_ROM_DIR}")
 mmodern_test_properties(xeen_fm_output PROPERTIES LABELS fast ENVIRONMENT "SDL_AUDIODRIVER=dummy")
 add_executable(mmodern_audio_golden_tests tests/XeenAudioGoldenTests.cpp)
 target_link_libraries(mmodern_audio_golden_tests PRIVATE mmodern_app mmodern_xeen mmodern_scummvm_xeen)
 target_link_options(mmodern_audio_golden_tests PRIVATE "LINKER:--wrap,SDL_NewAudioStream" "LINKER:--wrap,SDL_AudioStreamGet")
 mmodern_data_test(NAME xeen_audio_golden COMMAND mmodern_audio_golden_tests "${MMODERN_XEEN_DATA_DIR}")
 mmodern_test_properties(xeen_audio_golden PROPERTIES LABELS fast ENVIRONMENT "SDL_AUDIODRIVER=dummy")
 add_executable(mmodern_audio_devices_tests tests/XeenAudioDevicesTests.cpp)
 target_link_libraries(mmodern_audio_devices_tests PRIVATE mmodern_app mmodern_xeen)
 foreach(mode config none sb2 sbpro sb16)
  mmodern_part_a_test(NAME xeen_audio_devices_${mode} COMMAND mmodern_audio_devices_tests ${mode})
  mmodern_test_properties(xeen_audio_devices_${mode} PROPERTIES LABELS fast)
 endforeach()
 add_executable(mmodern_audio_selection_tests tests/XeenAudioSelectionTests.cpp)
 target_link_libraries(mmodern_audio_selection_tests PRIVATE mmodern_app mmodern_xeen mmodern_scummvm_xeen)
 mmodern_data_test(NAME xeen_audio_selection COMMAND mmodern_audio_selection_tests "${MMODERN_XEEN_DATA_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/m94-audio-selection")
 mmodern_test_properties(xeen_audio_selection PROPERTIES LABELS fast ENVIRONMENT "SDL_AUDIODRIVER=dummy")
 add_executable(mmodern_digital_audio_tests tests/XeenDigitalAudioTests.cpp)
 target_link_libraries(mmodern_digital_audio_tests PRIVATE mmodern_app mmodern_xeen mmodern_scummvm_xeen SDL2::SDL2)
 target_link_options(mmodern_digital_audio_tests PRIVATE "LINKER:--wrap,SDL_OpenAudioDevice" "LINKER:--wrap,SDL_AudioStreamGet" "LINKER:--wrap,SDL_GetAudioDeviceStatus")
 mmodern_data_test(NAME xeen_digital_audio COMMAND mmodern_digital_audio_tests "${MMODERN_XEEN_DATA_DIR}" "${MMODERN_MT32_ROM_DIR}")
 mmodern_test_properties(xeen_digital_audio PROPERTIES LABELS fast ENVIRONMENT "SDL_AUDIODRIVER=dummy")
 add_executable(mmodern_digital_sites_tests tests/XeenDigitalSitesTests.cpp)
 add_executable(mmodern_cutscene_tests tests/XeenCutsceneTests.cpp)

 add_executable(mmodern_ending_presentation_tests tests/XeenEndingPresentationTests.cpp)
 add_executable(mmodern_darkside_ending_tests tests/XeenDarksideEndingTests.cpp)
 target_link_libraries(mmodern_darkside_ending_tests PRIVATE mmodern_app mmodern_xeen mmodern_scummvm_xeen)
 mmodern_data_test(NAME xeen_darkside_ending COMMAND mmodern_darkside_ending_tests "${MMODERN_XEEN_DATA_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/darkside-ending-tests" --main)
 mmodern_data_test(NAME xeen_darkside_ending_captions COMMAND mmodern_darkside_ending_tests "${MMODERN_XEEN_DATA_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/darkside-ending-tests" --captions)
 mmodern_test_properties(xeen_darkside_ending xeen_darkside_ending_captions PROPERTIES ENVIRONMENT "SDL_AUDIODRIVER=dummy")
 add_executable(mmodern_world_ending_tests tests/XeenWorldEndingTests.cpp)
 target_link_libraries(mmodern_world_ending_tests PRIVATE mmodern_app mmodern_xeen mmodern_scummvm_xeen)
 mmodern_data_test(NAME xeen_world_ending COMMAND mmodern_world_ending_tests "${MMODERN_XEEN_DATA_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/world-ending-tests" --main)
 mmodern_test_properties(xeen_world_ending PROPERTIES ENVIRONMENT "SDL_AUDIODRIVER=dummy")

 target_link_libraries(mmodern_ending_presentation_tests PRIVATE mmodern_app mmodern_xeen mmodern_scummvm_xeen mmodern_platform_libs)
 # Five whole ending playbacks in three processes; only --main writes files.
 mmodern_data_test(NAME xeen_ending_presentation COMMAND mmodern_ending_presentation_tests "${MMODERN_XEEN_DATA_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/ending-presentation-tests" --main)
 mmodern_data_test(NAME xeen_ending_presentation_stride COMMAND mmodern_ending_presentation_tests "${MMODERN_XEEN_DATA_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/ending-presentation-tests" --stride)
 mmodern_data_test(NAME xeen_ending_presentation_captions COMMAND mmodern_ending_presentation_tests "${MMODERN_XEEN_DATA_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/ending-presentation-tests" --captions)
 mmodern_test_properties(xeen_ending_presentation xeen_ending_presentation_stride xeen_ending_presentation_captions
  PROPERTIES LABELS fast ENVIRONMENT "SDL_AUDIODRIVER=dummy")
 target_link_libraries(mmodern_cutscene_tests PRIVATE mmodern_app mmodern_xeen mmodern_scummvm_xeen)
 mmodern_data_test(NAME xeen_cutscene COMMAND mmodern_cutscene_tests "${MMODERN_XEEN_DATA_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/cutscene-tests")
 mmodern_test_properties(xeen_cutscene PROPERTIES LABELS fast ENVIRONMENT "SDL_AUDIODRIVER=dummy")
 target_link_libraries(mmodern_digital_sites_tests PRIVATE mmodern_app mmodern_xeen mmodern_scummvm_xeen)
 mmodern_data_test(NAME xeen_digital_sites COMMAND mmodern_digital_sites_tests "${MMODERN_XEEN_DATA_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/docs/rules/audio.md")
 mmodern_test_properties(xeen_digital_sites PROPERTIES LABELS fast ENVIRONMENT "SDL_AUDIODRIVER=dummy")
 mmodern_test_properties(xeen_death_original xeen_rest_original PROPERTIES ENVIRONMENT "SDL_AUDIODRIVER=dummy")
 add_executable(mmodern_sdl_audio_device_tests tests/SdlAudioDeviceTests.cpp)
 target_link_libraries(mmodern_sdl_audio_device_tests PRIVATE mmodern_platform_sdl SDL2::SDL2)
 target_link_options(mmodern_sdl_audio_device_tests PRIVATE "LINKER:--wrap,SDL_OpenAudioDevice" "LINKER:--wrap,SDL_GetDefaultAudioInfo" "LINKER:--wrap,SDL_GetAudioDeviceStatus" "LINKER:--wrap,SDL_GetTicks64" "LINKER:--wrap,SDL_AudioStreamGet")
 mmodern_part_a_test(NAME sdl_audio_device COMMAND mmodern_sdl_audio_device_tests)
 mmodern_test_properties(sdl_audio_device PROPERTIES LABELS fast ENVIRONMENT "SDL_AUDIODRIVER=dummy")
 add_executable(mmodern_sound_timing_tests tests/XeenSoundTimingTests.cpp)
 target_link_libraries(mmodern_sound_timing_tests PRIVATE mmodern_xeen mmodern_scummvm_xeen)
 add_executable(mmodern_arena_tests tests/XeenArenaTests.cpp)
 target_link_libraries(mmodern_arena_tests PRIVATE mmodern_app mmodern_xeen)
 mmodern_part_a_test(NAME xeen_arena COMMAND mmodern_arena_tests)
 mmodern_data_test(NAME xeen_arena_original COMMAND mmodern_arena_tests "${MMODERN_XEEN_DATA_DIR}")
 add_executable(mmodern_entrance_scene_tests tests/XeenEntranceSceneTests.cpp)
 target_link_libraries(mmodern_entrance_scene_tests PRIVATE mmodern_app mmodern_xeen)
 mmodern_part_a_test(NAME xeen_entrance_scene COMMAND mmodern_entrance_scene_tests)
 mmodern_data_test(NAME xeen_entrance_scene_original COMMAND mmodern_entrance_scene_tests "${MMODERN_XEEN_DATA_DIR}")
 mmodern_test_properties(xeen_entrance_scene_original PROPERTIES ENVIRONMENT "SDL_AUDIODRIVER=dummy")
 target_include_directories(mmodern_sound_timing_tests PRIVATE src)
 mmodern_part_a_test(NAME xeen_sound_timing COMMAND mmodern_sound_timing_tests)
 mmodern_data_test(NAME xeen_sound_timing_original COMMAND mmodern_sound_timing_tests "${MMODERN_XEEN_DATA_DIR}")
 mmodern_test_properties(xeen_sound_timing xeen_sound_timing_original PROPERTIES LABELS fast)
 target_include_directories(mmodern_presentation_clock_tests PRIVATE src)
 mmodern_part_a_test(NAME presentation_clock COMMAND mmodern_presentation_clock_tests)
 mmodern_test_properties(presentation_clock PROPERTIES LABELS fast)
	target_link_libraries(mmodern_sdl_input_tests PRIVATE
		mmodern_platform_sdl mmodern_xeen SDL2::SDL2)
	target_link_options(mmodern_sdl_input_tests PRIVATE "-Wl,--wrap=SDL_GetTicks"
		"-Wl,--wrap=SDL_Delay" "-Wl,--wrap=SDL_UpdateTexture" "-Wl,--wrap=SDL_RenderPresent")
	mmodern_part_a_test(NAME sdl_input COMMAND mmodern_sdl_input_tests)
	mmodern_part_a_test(NAME sdl_main_screen COMMAND mmodern_sdl_input_tests mouse)
	add_executable(mmodern_sdl_motion_tests tests/SdlMotionTests.cpp)
	target_link_libraries(mmodern_sdl_motion_tests PRIVATE mmodern_platform_sdl SDL2::SDL2)
	target_link_options(mmodern_sdl_motion_tests PRIVATE "-Wl,--wrap=SDL_GetTicks"
		"-Wl,--wrap=SDL_Delay" "-Wl,--wrap=SDL_PumpEvents"
		"-Wl,--wrap=SDL_UpdateTexture" "-Wl,--wrap=SDL_RenderPresent")
	mmodern_part_a_test(NAME sdl_motion COMMAND mmodern_sdl_motion_tests)
	mmodern_test_properties(sdl_motion PROPERTIES ENVIRONMENT
		"SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software")
	mmodern_test_properties(sdl_input sdl_main_screen PROPERTIES ENVIRONMENT
		"SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software")
	add_executable(mmodern_sdl_window_placement_tests tests/SdlWindowPlacementTests.cpp)
	target_link_libraries(mmodern_sdl_window_placement_tests PRIVATE mmodern_platform_sdl SDL2::SDL2)
	mmodern_part_a_test(NAME sdl_window_placement COMMAND mmodern_sdl_window_placement_tests)
	mmodern_test_properties(sdl_window_placement PROPERTIES ENVIRONMENT
		"SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software")
	add_executable(mmodern_game_flags_tests
		tests/XeenGameFlagsTests.cpp
		src/formats/xeen/XeenGameFlagsFormat.cpp
		src/games/xeen/XeenGameFlags.cpp
	)
	target_link_libraries(mmodern_game_flags_tests PRIVATE mmodern_core)
	mmodern_part_a_test(NAME xeen_game_flags COMMAND mmodern_game_flags_tests)
	mmodern_part_a_test(NAME xeen_events_cli_negative
		COMMAND mmodern --inspect-events unused -1)
	mmodern_part_a_test(NAME xeen_events_cli_trailing
		COMMAND mmodern --inspect-events unused 31abc)
	mmodern_part_a_test(NAME xeen_events_cli_zero
		COMMAND mmodern --inspect-events unused 0)
	mmodern_part_a_test(NAME xeen_events_cli_out_of_range
		COMMAND mmodern --inspect-events unused 10000)
	mmodern_part_a_test(NAME xeen_events_cli_missing_argument
		COMMAND mmodern --inspect-events)
	mmodern_part_a_test(NAME xeen_events_cli_excess_argument
		COMMAND mmodern --inspect-events unused 31 2 9 north extra)
	mmodern_part_a_test(NAME xeen_events_cli_x_without_y
		COMMAND mmodern --inspect-events unused 31 2)
	mmodern_part_a_test(NAME xeen_events_cli_negative_x
		COMMAND mmodern --inspect-events unused 31 -1 9)
	mmodern_part_a_test(NAME xeen_events_cli_coordinate_overflow
		COMMAND mmodern --inspect-events unused 31 2 256)
	mmodern_part_a_test(NAME xeen_events_cli_coordinate_trailing
		COMMAND mmodern --inspect-events unused 31 2abc 9)
	mmodern_part_a_test(NAME xeen_events_cli_bad_direction
		COMMAND mmodern --inspect-events unused 31 2 9 northeast)
	mmodern_test_properties(
		xeen_events_cli_negative
		xeen_events_cli_trailing
		xeen_events_cli_zero
		xeen_events_cli_out_of_range
		xeen_events_cli_missing_argument
		xeen_events_cli_excess_argument
		xeen_events_cli_x_without_y
		xeen_events_cli_negative_x
		xeen_events_cli_coordinate_overflow
		xeen_events_cli_coordinate_trailing
		xeen_events_cli_bad_direction
		PROPERTIES WILL_FAIL TRUE
	)
	add_executable(mmodern_outdoor_scene_tests
		tests/XeenOutdoorSceneTests.cpp
	)
	target_link_libraries(mmodern_outdoor_scene_tests PRIVATE mmodern_xeen)
	mmodern_part_a_test(NAME xeen_outdoor_scene COMMAND mmodern_outdoor_scene_tests)
	add_executable(mmodern_movement_tests
		tests/XeenMovementTests.cpp
		src/games/xeen/XeenMovement.cpp
		src/games/xeen/XeenWorld.cpp
		src/games/xeen/XeenEventDecoder.cpp
        src/games/xeen/XeenEntranceScene.cpp
	)
	target_link_libraries(mmodern_movement_tests PRIVATE mmodern_core)
	mmodern_part_a_test(NAME xeen_movement COMMAND mmodern_movement_tests)
	add_executable(mmodern_world_tests
		tests/XeenWorldTests.cpp
		src/games/xeen/XeenWorld.cpp
		src/games/xeen/XeenEventDecoder.cpp
        src/games/xeen/XeenEntranceScene.cpp
	)
	target_link_libraries(mmodern_world_tests PRIVATE mmodern_core)
	mmodern_part_a_test(NAME xeen_world COMMAND mmodern_world_tests)
	add_executable(mmodern_map_state_tests tests/XeenMapStateTests.cpp)
	target_link_libraries(mmodern_map_state_tests PRIVATE mmodern_xeen)
	mmodern_part_a_test(NAME xeen_map_state COMMAND mmodern_map_state_tests)
	add_executable(mmodern_indoor_scene_tests
		tests/XeenIndoorSceneTests.cpp
	)
	target_link_libraries(mmodern_indoor_scene_tests PRIVATE mmodern_xeen)
	mmodern_part_a_test(NAME xeen_indoor_scene COMMAND mmodern_indoor_scene_tests)
	add_executable(mmodern_darkside_scenery_tests tests/XeenDarksideSceneryTests.cpp)
	target_link_libraries(mmodern_darkside_scenery_tests PRIVATE mmodern_xeen)
	mmodern_data_test(NAME xeen_darkside_scenery_original COMMAND mmodern_darkside_scenery_tests "${MMODERN_XEEN_DATA_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/darkside-tower-edge.ppm")
	add_executable(mmodern_indoor_composer_tests tests/XeenIndoorComposerTests.cpp)
	target_link_libraries(mmodern_indoor_composer_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_part_a_test(NAME xeen_indoor_composer COMMAND mmodern_indoor_composer_tests)
	add_executable(mmodern_character_format_tests
		tests/XeenCharacterFormatTests.cpp
	)
	target_link_libraries(mmodern_character_format_tests PRIVATE
		mmodern_xeen mmodern_scummvm_xeen)
	mmodern_part_a_test(NAME xeen_character_formats COMMAND mmodern_character_format_tests)
	add_executable(mmodern_character_rules_tests
		tests/XeenCharacterRulesTests.cpp
		src/games/xeen/XeenParty.cpp
		src/games/xeen/XeenCharacter.cpp
		src/games/xeen/XeenCharacterRules.cpp
	)
	target_link_libraries(mmodern_character_rules_tests PRIVATE mmodern_core)
	mmodern_part_a_test(NAME xeen_character_rules COMMAND mmodern_character_rules_tests)
	add_executable(mmodern_combat_casting_rules_tests tests/XeenCombatCastingRulesTests.cpp)
	target_link_libraries(mmodern_combat_casting_rules_tests PRIVATE mmodern_xeen)
	mmodern_part_a_test(NAME xeen_combat_casting_rules COMMAND mmodern_combat_casting_rules_tests)
	add_executable(mmodern_learned_spell_rules_tests tests/XeenLearnedSpellRulesTests.cpp)
	target_link_libraries(mmodern_learned_spell_rules_tests PRIVATE mmodern_xeen)
	mmodern_part_a_test(NAME xeen_learned_spell_rules COMMAND mmodern_learned_spell_rules_tests)
		add_executable(mmodern_regional_action_original tests/XeenRegionalActionOriginal.cpp)
		target_link_libraries(mmodern_regional_action_original PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2)
		mmodern_data_test(NAME xeen_regional_action_original COMMAND mmodern_regional_action_original "${MMODERN_XEEN_DATA_DIR}")
	add_executable(mmodern_equipment_tests
		tests/XeenEquipmentTests.cpp
		src/games/xeen/XeenEquipment.cpp
		src/games/xeen/XeenItemTransfer.cpp
		src/games/xeen/XeenCharacter.cpp
		src/games/xeen/XeenCharacterRules.cpp
		src/games/xeen/XeenParty.cpp
	)
	target_link_libraries(mmodern_equipment_tests PRIVATE mmodern_core)
	mmodern_part_a_test(NAME xeen_equipment COMMAND mmodern_equipment_tests)
	add_executable(mmodern_party_visual_state_tests
		tests/XeenPartyVisualStateTests.cpp
		src/games/xeen/XeenCharacter.cpp
		src/games/xeen/XeenCharacterRules.cpp
		src/games/xeen/XeenPartyVisualState.cpp
	)
	target_link_libraries(mmodern_party_visual_state_tests PRIVATE mmodern_core)
	mmodern_part_a_test(NAME xeen_party_visual_state COMMAND mmodern_party_visual_state_tests)
	add_executable(mmodern_party_smoke tests/PartyIntegrationTest.cpp)
	target_link_libraries(mmodern_party_smoke PRIVATE mmodern_xeen mmodern_scummvm_xeen)
	if(MINGW)
		target_link_options(mmodern_party_smoke PRIVATE -Wl,--gc-sections)
	endif()

	add_executable(mmodern_item_catalog_smoke
		tests/ItemCatalogIntegrationTest.cpp)
	target_link_libraries(mmodern_item_catalog_smoke PRIVATE
		mmodern_xeen mmodern_scummvm_xeen mmodern_platform_libs)
	if(MINGW)

		target_link_options(mmodern_item_catalog_smoke PRIVATE -Wl,--gc-sections)
	endif()
	add_executable(mmodern_event_script_smoke
		tests/EventScriptIntegrationTest.cpp)
	target_link_libraries(mmodern_event_script_smoke PRIVATE
		mmodern_xeen mmodern_scummvm_xeen)
	if(MINGW)
		target_link_options(mmodern_event_script_smoke PRIVATE -Wl,--gc-sections)
	endif()
	add_executable(mmodern_event_text_smoke
		tests/EventTextIntegrationTest.cpp)
	target_link_libraries(mmodern_event_text_smoke PRIVATE
		mmodern_xeen mmodern_scummvm_xeen)
	if(MINGW)
		target_link_options(mmodern_event_text_smoke PRIVATE -Wl,--gc-sections)
	endif()
	add_executable(mmodern_game_flags_smoke
		tests/GameFlagsIntegrationTest.cpp)
	target_link_libraries(mmodern_game_flags_smoke PRIVATE
		mmodern_xeen mmodern_scummvm_xeen)
	if(MINGW)
		target_link_options(mmodern_game_flags_smoke PRIVATE -Wl,--gc-sections)
	endif()
	add_executable(mmodern_event_interpreter_smoke
		tests/EventInterpreterIntegrationTest.cpp)
	target_link_libraries(mmodern_event_interpreter_smoke PRIVATE
		mmodern_xeen mmodern_scummvm_xeen)
	if(MINGW)
		target_link_options(mmodern_event_interpreter_smoke PRIVATE -Wl,--gc-sections)
	endif()
	add_executable(mmodern_event_system_smoke
		tests/EventSystemIntegrationTest.cpp)
	target_link_libraries(mmodern_event_system_smoke PRIVATE
		mmodern_xeen mmodern_scummvm_xeen)
	if(MINGW)
		target_link_options(mmodern_event_system_smoke PRIVATE -Wl,--gc-sections)
	endif()
	add_executable(mmodern_manual_event_smoke
		tests/ManualEventIntegrationTest.cpp)
	target_link_libraries(mmodern_manual_event_smoke PRIVATE
		mmodern_xeen mmodern_scummvm_xeen)
	if(MINGW)
		target_link_options(mmodern_manual_event_smoke PRIVATE -Wl,--gc-sections)
	endif()
	add_executable(mmodern_navigation_flow_smoke
		tests/NavigationFlowIntegrationTest.cpp
		src/app/XeenNavigationFlow.cpp)
	target_link_libraries(mmodern_navigation_flow_smoke PRIVATE
		mmodern_xeen mmodern_scummvm_xeen)
	if(MINGW)
		target_link_options(mmodern_navigation_flow_smoke PRIVATE -Wl,--gc-sections)
	endif()
	# Original-data checks that were opt-in smoke targets until M66; they pass on
	# the CD data and take under three seconds each, so they run with the suite.
	mmodern_data_test(NAME xeen_regional_original COMMAND mmodern_regional_original "${MMODERN_XEEN_DATA_DIR}")
	mmodern_data_test(NAME xeen_party_original COMMAND mmodern_party_smoke "${MMODERN_XEEN_DATA_DIR}")
	mmodern_data_test(NAME xeen_item_catalog_original COMMAND mmodern_item_catalog_smoke "${MMODERN_XEEN_DATA_DIR}")
	mmodern_data_test(NAME xeen_item_catalog_reference COMMAND mmodern_item_catalog_smoke --verify-reference "${MMODERN_XEEN_DATA_DIR}" "${SCUMMVM_SOURCE_DIR}")
	mmodern_data_test(NAME xeen_object_visual_original COMMAND mmodern_object_visual_smoke "${MMODERN_XEEN_DATA_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/object-visual-original")
	foreach(xeen_smoke event_script event_text game_flags event_interpreter event_system manual_event navigation_flow)
		mmodern_data_test(NAME xeen_${xeen_smoke}_original COMMAND mmodern_${xeen_smoke}_smoke "${MMODERN_XEEN_DATA_DIR}")
	endforeach()
	# M44 original-data scenarios; digests and regeneration are in tests/XeenM44BaselineDigests.h.
	# EXCLUDE_FROM_ALL: .github/workflows/ci.yml builds it by name; keep that list in step.

	add_executable(mmodern_m44_baseline_witness EXCLUDE_FROM_ALL src/main.cpp tests/XeenM44BaselineWitness.cpp)
	target_link_libraries(mmodern_m44_baseline_witness PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl SDL2::SDL2 mmodern_platform_libs)
	target_link_options(mmodern_m44_baseline_witness PRIVATE "LINKER:--wrap,${XEEN_WRAP_PLAY_GAMEPLAY}" "LINKER:--wrap,${XEEN_WRAP_RANDOM_DRAW}")
	# EXCLUDE_FROM_ALL: .github/workflows/ci.yml builds it by name; keep that list in step.

	add_executable(mmodern_m44_baseline_restart EXCLUDE_FROM_ALL tests/XeenM44BaselineRestart.cpp)

	add_executable(mmodern_m44_scenario_tests tests/XeenM44ScenarioTests.cpp)
	target_link_libraries(mmodern_m44_scenario_tests PRIVATE mmodern_app mmodern_xeen mmodern_platform_sdl mmodern_platform_libs)
	add_dependencies(mmodern_m44_scenario_tests mmodern_m44_baseline_witness mmodern_m44_baseline_restart mmodern_m43_cli_witness)
	foreach(scenario mainland services temple roundtrip)
		if(scenario STREQUAL "temple")
			set(m44_generator $<TARGET_FILE:mmodern_m43_cli_witness>)
		else()
			set(m44_generator $<TARGET_FILE:mmodern_m44_baseline_witness>)
		endif()
		mmodern_data_test(NAME xeen_m44_scenario_${scenario} COMMAND mmodern_m44_scenario_tests ${scenario} ${m44_generator}
			$<TARGET_FILE:mmodern_m44_baseline_witness> $<TARGET_FILE:mmodern_m44_baseline_restart> "${MMODERN_XEEN_DATA_DIR}")
	endforeach()
	add_executable(mmodern_scene_presentation_tests tests/XeenScenePresentationTests.cpp)
	add_executable(mmodern_monster_status_tests tests/XeenMonsterStatusTests.cpp)
	target_link_libraries(mmodern_monster_status_tests PRIVATE mmodern_xeen)
	mmodern_part_a_test(NAME xeen_monster_status COMMAND mmodern_monster_status_tests)
 add_executable(mmodern_combat_presentation_tests tests/XeenCombatPresentationTests.cpp)
 target_link_libraries(mmodern_combat_presentation_tests PRIVATE mmodern_app mmodern_xeen)
 mmodern_data_test(NAME xeen_combat_presentation COMMAND mmodern_combat_presentation_tests "${MMODERN_XEEN_DATA_DIR}")
	target_link_libraries(mmodern_scene_presentation_tests PRIVATE mmodern_app mmodern_xeen)
	mmodern_part_a_test(NAME xeen_scene_presentation COMMAND mmodern_scene_presentation_tests)
	add_executable(mmodern_scene_inventory_original tests/XeenSceneInventoryOriginal.cpp)
	target_link_libraries(mmodern_scene_inventory_original PRIVATE mmodern_xeen)
	mmodern_data_test(NAME xeen_scene_inventory_original COMMAND mmodern_scene_inventory_original "${MMODERN_XEEN_DATA_DIR}")
	# Every test is labelled `fast` (default iteration: ctest -L fast) or `process`
	# (full run: plain ctest). `process` = runs 30 s or more, or spawns child
	# processes / drives a CLI witness executable.
	set(xeen_process_tests
		xeen_clouds_invariant_b1b2
		xeen_new_game_process
		xeen_rest_original xeen_equipment_original
		xeen_scene_inventory_original
		xeen_resource_cold_original
		xeen_purchase_flow xeen_purchase_allocation xeen_purchase_process
		xeen_temple_flow xeen_temple_process xeen_temple_depleted_process
		xeen_training_flow xeen_training_allocation xeen_training_original xeen_training_process
		xeen_input_scheduling xeen_service_day_process xeen_combat_clock_process
		xeen_main_screen
		xeen_journey_resources xeen_journey_persistence xeen_regional_persistence
		xeen_combat_casting_process xeen_combat_casting_faults xeen_combat_casting_effects xeen_combat_casting_native
		xeen_combat_player_ready_input
		xeen_smith_process xeen_vertigo_process xeen_save_format xeen_save_cli
		xeen_combat_gameplay xeen_equipment_gameplay xeen_inventory_gameplay
		xeen_m44_scenario_mainland xeen_m44_scenario_services xeen_m44_scenario_temple
		xeen_m44_scenario_roundtrip
		xeen_title_flow_endings xeen_title_flow_darkside_endings xeen_ending_presentation xeen_setup_screen_original)
	if(NOT MMODERN_ORIGINAL_DATA_TESTS)
		get_property(mmodern_data_tests GLOBAL PROPERTY MMODERN_DATA_TESTS)
		list(REMOVE_ITEM xeen_process_tests ${mmodern_data_tests})
	endif()
	get_property(xeen_all_tests DIRECTORY PROPERTY TESTS)
	foreach(xeen_test IN LISTS xeen_process_tests)
		if(WIN32 AND NOT xeen_test IN_LIST xeen_all_tests)
			message(FATAL_ERROR "Process-labelled test does not exist: ${xeen_test}")
		endif()
	endforeach()
	# Timeouts only detect a hung test. They are generous and independent of
	# machine load, so the complete suite runs with several parallel jobs
	# (tools/run-full-ctest.ps1); nothing here measures speed.
	foreach(xeen_test IN LISTS xeen_all_tests)
        set_property(TEST ${xeen_test} APPEND PROPERTY ENVIRONMENT "MMODERN_SETTINGS_FILE=${CMAKE_CURRENT_BINARY_DIR}/test-settings/${xeen_test}/settings.ini")
		if(xeen_test IN_LIST xeen_process_tests)
			mmodern_test_properties(${xeen_test} PROPERTIES LABELS process TIMEOUT 1800)
		else()
			mmodern_test_properties(${xeen_test} PROPERTIES LABELS fast TIMEOUT 300)
		endif()
	endforeach()
	# Serial group: tests that assert real-clock scheduling or elapsed time, or
	# drive a native window, never share the machine with another test.
	# xeen_save_state bounds the all-maps restore at one second: about 0.4 s on the
	# development machine, 1.0-1.4 s beside three other tests on a 4-processor CI runner.
	mmodern_test_properties(xeen_input_scheduling xeen_main_screen xeen_native_responsiveness
		xeen_title_flow xeen_myra_original_sdl xeen_save_cli xeen_combat_cli xeen_save_state
		sdl_input sdl_main_screen sdl_motion sdl_window_placement xeen_member_prompt_click xeen_setup_screen xeen_setup_screen_original PROPERTIES RUN_SERIAL TRUE)
	# CTest starts the costliest tests first. CTestCostData.txt is not reliable
	# for that: a run without -j (`ctest -R name`) divides the test's average by
	# its run count, which left 100-second tests for the end of the suite. These
	# are rough build-rel seconds with 8 jobs; only their order matters.
	foreach(xeen_cost IN ITEMS
			xeen_service_day_process=134 xeen_m44_scenario_services=107 xeen_smith_process=104
			xeen_title_flow_endings=108 xeen_ending_presentation=80 xeen_training_process=73
			xeen_coverage_original_guard=69 xeen_combat_clock_process=65 xeen_world_event=53
			xeen_training_original=47 xeen_combat_casting_faults=42 xeen_cutscene=41
			xeen_purchase_process=37 xeen_vertigo_process=32 xeen_m44_scenario_mainland=31
			xeen_ending_presentation_stride=27 xeen_ending_presentation_captions=27
			xeen_scene_inventory_original=27 xeen_title_flow_darkside_endings=26
			xeen_party_interface_original=25 xeen_m61_part_b=21
			xeen_combat_casting_effects=20 xeen_new_game_process=18 xeen_myra_original=17
			xeen_cd_audio=17 xeen_combat_casting_process=17 xeen_item_catalog_generation=16
			xeen_temple_depleted_process=16 xeen_temple_process=13 xeen_m44_scenario_roundtrip=12
			xeen_m44_scenario_temple=12 xeen_m60_exploration_original=10 xeen_title_flow_intros=10
			xeen_dial_original=17 xeen_darkside_repeats_original=11)
		string(REPLACE "=" ";" xeen_cost "${xeen_cost}")
		list(GET xeen_cost 0 xeen_test)
		list(GET xeen_cost 1 xeen_seconds)
		mmodern_test_properties(${xeen_test} PROPERTIES COST ${xeen_seconds})
	endforeach()
	# Tests that write the same fixed working-directory or temp path never overlap.
	mmodern_test_properties(xeen_npc xeen_npc_sdl PROPERTIES RESOURCE_LOCK npc_asset_dir)
	mmodern_test_properties(xeen_journey_gameplay xeen_journey_sdl PROPERTIES RESOURCE_LOCK journey_gameplay_dir)
	mmodern_test_properties(xeen_reward_gameplay xeen_reward_sdl PROPERTIES RESOURCE_LOCK reward_gameplay_dir)
	mmodern_test_properties(xeen_combat_impact_original xeen_resource_cold_original PROPERTIES RESOURCE_LOCK consequence_temp_saves)
endif()

# Debug information is about 97% of every test and tool executable, and writing
# it on each relink dominated incremental builds (measured 2026-10-19: 505 s to
# relink 183 executables after one source change). The game keeps it; set
# MMODERN_TEST_DEBUG_INFO=ON to debug a test or tool outside a Debug build.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	set(mmodern_test_debug_info_default ON)
else()
	set(mmodern_test_debug_info_default OFF)
endif()
option(MMODERN_TEST_DEBUG_INFO "Keep debug information in test and tool executables"
	${mmodern_test_debug_info_default})
if((MINGW OR (CMAKE_EXECUTABLE_FORMAT STREQUAL "ELF" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")) AND NOT MMODERN_TEST_DEBUG_INFO)
	get_property(mmodern_executables DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
	foreach(mmodern_target IN LISTS mmodern_executables)
		get_target_property(mmodern_target_type ${mmodern_target} TYPE)
		if(mmodern_target_type STREQUAL "EXECUTABLE" AND NOT mmodern_target STREQUAL "mmodern")

			target_link_options(${mmodern_target} PRIVATE -Wl,--strip-debug)
		endif()
	endforeach()
endif()

# No test needs the speakers: audio output is covered through injected and
# dummy devices, and a real device makes the parallel suite play every
# test's music and effects at once.
get_property(mmodern_tests DIRECTORY PROPERTY TESTS)
if(mmodern_tests)
	set_property(TEST ${mmodern_tests} APPEND PROPERTY ENVIRONMENT "SDL_AUDIODRIVER=dummy")
endif()
if(BUILD_TESTING)
	list(LENGTH mmodern_tests mmodern_registered_count)
	set(mmodern_left_out_count 0)
	if(NOT MMODERN_ORIGINAL_DATA_TESTS)
		get_property(mmodern_data_tests GLOBAL PROPERTY MMODERN_DATA_TESTS)
		list(LENGTH mmodern_data_tests mmodern_left_out_count)
	endif()
	message(STATUS "Tests: ${mmodern_registered_count} registered, ${mmodern_left_out_count} data tests left out (MMODERN_ORIGINAL_DATA_TESTS=${MMODERN_ORIGINAL_DATA_TESTS})")
endif()

mmodern_guard_wraps()
