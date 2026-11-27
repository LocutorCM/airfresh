cmake_minimum_required(VERSION 3.22)

# === FIX: Esto tiene que ir ANTES del project() para que sí lo respete Xcode ===
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version - Monterey en adelante")
set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "Architectures - Universal Binary")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(EC_AIR VERSION 1.0.0)

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
    # FIX V4: Antes ECAu / AiAr con minusculas, auval lo rechazaba
    # Ahora todo en MAYUSCULAS, asi Logic si lo valida
    PLUGIN_MANUFACTURER_CODE ECAU
    PLUGIN_CODE ECAR
    FORMATS AU VST3 Standalone
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

# === IMAGEN DE FONDO DEL PLUGIN ===
# Debe estar en: Assets/EC-Air-Exciter-Background.png
# Si no existe, compila con fondo plano y NO truena el build
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Assets/EC-Air-Exciter-Background.png)
    juce_add_binary_data(ECAirAssets
        HEADER_NAME "ECAirAssets.h"
        NAMESPACE ECAirAssets
        SOURCES
            Assets/EC-Air-Exciter-Background.png
    )
    target_compile_definitions(EC_AIR PRIVATE EC_HAS_BACKGROUND_IMAGE=1)
    message(STATUS "Background image found: Assets/EC-Air-Exciter-Background.png")
else()
    message(WARNING "Background image NOT found - building without background image")
    # FIX: Antes aqui creabas un BinaryData vacio y JUCE 8 tronaba en GitHub
    # Ahora simplemente no creamos el target, y el Editor ya tiene el #if para compilar sin imagen
    target_compile_definitions(EC_AIR PRIVATE EC_HAS_BACKGROUND_IMAGE=0)
    # Target dummy para que el link no falle
    add_library(ECAirAssets INTERFACE)
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
    # FIX: Para que el AU no falle la validacion en Logic en Macs M1/M2/M3
    JUCE_STRICT_REFCOUNTEDPOINTER=1
)

target_link_libraries(EC_AIR PRIVATE
    ECAirAssets
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_basics
)
