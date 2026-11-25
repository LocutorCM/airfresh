cmake_minimum_required(VERSION 3.22)
project(EC_AIR VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Monterey (12.0) en adelante, Universal
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version - Monterey en adelante")
set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "Architectures - Universal Binary")

# OBLIGATORIO para macos-15-intel + Xcode 16: forzar min-version en compilador y linker
if(APPLE)
  add_compile_options(-mmacosx-version-min=12.0)
  add_link_options(-mmacosx-version-min=12.0)
endif()

include(FetchContent)

FetchContent_Declare(
  JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.8
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(EC_AIR
    COMPANY_NAME "EC AUDIO"
    COMPANY_WEBSITE "https://ecaudio.com"
    COMPANY_EMAIL "info@ecaudio.com"
    PLUGIN_MANUFACTURER_CODE ECAu
    PLUGIN_CODE AiAr
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "EC AIR EXCITER"
    VERSION "1.0.0"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES "Fx" "Enhancement" "Exciter"
    AU_MAIN_TYPE "kAudioUnitType_Effect"
)

juce_generate_juce_header(EC_AIR)

# === IMAGEN DE FONDO DEL PLUGIN - LA QUE SUBISTE A GITHUB ===
# Debe estar en: Assets/EC-Air-Exciter-Background.png
# Tiene que ser PNG, no WebP
# Si no existe, compila con fondo plano y NO truena el build
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Assets/EC-Air-Exciter-Background.png)
    juce_add_binary_data(ECAirAssets
        SOURCES
            Assets/EC-Air-Exciter-Background.png
    )
    target_compile_definitions(EC_AIR PRIVATE EC_HAS_BACKGROUND_IMAGE=1)
    message(STATUS "Background image found: Assets/EC-Air-Exciter-Background.png")
else()
    message(WARNING "Background image NOT found: Assets/EC-Air-Exciter-Background.png - building without background image")
    # Target vacio para que no falle el link
    juce_add_binary_data(ECAirAssets SOURCES)
    target_compile_definitions(EC_AIR PRIVATE EC_HAS_BACKGROUND_IMAGE=0)
endif()

target_sources(EC_AIR PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
)

target_compile_definitions(EC_AIR PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(EC_AIR PRIVATE
    ECAirAssets
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_basics
)
