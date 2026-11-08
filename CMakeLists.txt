cmake_minimum_required(VERSION 3.22)

# Mac: one app for Apple Silicon and Intel, macOS 11 or later. (Must be set before project().)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

project(Stinger VERSION 0.3.2 LANGUAGES C CXX)

# Shown in the window title and the zip name.
set(STINGER_VERSION_LABEL "${PROJECT_VERSION}-alpha")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static MSVC runtime: the .exe runs on another PC without a VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.8
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
FetchContent_MakeAvailable(JUCE)

# Steinberg ASIO SDK (licensed separately, not in git). Windows only.
if(WIN32)
    set(ASIO_SDK_DIR "${CMAKE_SOURCE_DIR}/sdk/ASIOSDK" CACHE PATH "Folder containing the ASIO SDK (with common/iasiodrv.h)")
    if(NOT EXISTS "${ASIO_SDK_DIR}/common/iasiodrv.h")
        message(FATAL_ERROR
            "ASIO SDK not found. Download it from https://www.steinberg.net/asiosdk "
            "and unzip it so that ${ASIO_SDK_DIR}/common/iasiodrv.h exists.")
    endif()
endif()

# Engine code shared by the app and the tests. JUCE modules are compiled once, into this
# library (linked PRIVATE); their include paths and defines are re-exported below.
add_library(StingerCore STATIC
    Source/Cue.cpp
    Source/CueAudio.cpp
    Source/PlaybackEngine.cpp
    Source/Show.cpp
    Source/Autosave.cpp
    Source/ShowCheck.cpp
    Source/DeviceWatch.cpp
    Source/Loudness.cpp
    Source/Schedule.cpp
    Source/OscBridge.cpp
    Source/BackupLink.cpp
    Source/Playlists.cpp
    Source/Controls.cpp
    Source/CueFx.cpp
    Source/StockFx.cpp
    Source/PeakLimiter.cpp
    Source/MultibandLimiter.cpp
    Source/PluginHost.cpp
    Source/ShowPlugins.cpp
    Source/StreamDeckServer.cpp
    Source/Downloader.cpp)
target_include_directories(StingerCore PUBLIC Source)
if(WIN32)
    target_include_directories(StingerCore PUBLIC "${ASIO_SDK_DIR}/common")
    # WinRT MIDI: Bluetooth MIDI devices (footswitches, pads) show up once paired in Windows.
    target_compile_definitions(StingerCore PUBLIC JUCE_ASIO=1 JUCE_WASAPI=1 JUCE_USE_WINRT_MIDI=1)
endif()
# Android has no plugin format to host: buttons get the stock EQ and compressor instead (StockFx).
if(ANDROID)
    target_compile_definitions(StingerCore PUBLIC JUCE_PLUGINHOST_VST3=0)
else()
    target_compile_definitions(StingerCore PUBLIC
        JUCE_PLUGINHOST_VST3=1
        JUCE_MODAL_LOOPS_PERMITTED=1)   # the tests run the message loop for a while (Stream Deck connection)
endif()
target_compile_definitions(StingerCore PUBLIC
    JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_PLUGINHOST_LV2=0
    JUCE_DIRECTSOUND=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_APPLICATION_NAME_STRING="Stinger"
    JUCE_APPLICATION_VERSION_STRING="${STINGER_VERSION_LABEL}")
target_link_libraries(StingerCore
    PRIVATE
        juce::juce_audio_utils
        juce::juce_osc
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
target_include_directories(StingerCore INTERFACE
    $<TARGET_PROPERTY:StingerCore,INCLUDE_DIRECTORIES>)
target_compile_definitions(StingerCore INTERFACE
    $<TARGET_PROPERTY:StingerCore,COMPILE_DEFINITIONS>)

juce_add_gui_app(Stinger
    PRODUCT_NAME "Stinger"
    COMPANY_NAME "LEFTNoiseAudio"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/installer/Stinger.png"
    BUNDLE_ID com.leftnoiseaudio.stinger
    DOCUMENT_EXTENSIONS stinger
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Stinger records from your audio interface inputs into a button."
    VERSION ${PROJECT_VERSION})

# Shared by the desktop and Android apps.
target_sources(Stinger PRIVATE
    Source/CueGrid.cpp
    Source/NowPlaying.cpp
    Source/PlaylistPanel.cpp
    Source/WaveformEditor.cpp
    Source/Look.cpp)

if(ANDROID)
    # Touch pages for the phone's own screen (see android/). JUCE's Java code loads the app by the name juce_jni.
    target_sources(Stinger PRIVATE Source/MobileApp.cpp Source/MobilePages.cpp Source/DexBridge.cpp Source/StockFxEditor.cpp Source/AndroidFiles.cpp Source/DexDeck.cpp Source/DexStreamDeck.cpp)
    # Surface Duo: the shared dual-screen code in C:/Duo (its folder comes from android/app/build.gradle.kts as DUO_DIR)
    if(NOT DUO_DIR)
        message(FATAL_ERROR "DUO_DIR not set: the Android build passes the Duo folder (C:/Duo)")
    endif()
    target_sources(Stinger PRIVATE "${DUO_DIR}/juce/DuoBridge.cpp")
    target_include_directories(Stinger PRIVATE "${DUO_DIR}/juce")
    # Samsung DeX: the computer app's screens, on the monitor. Android only: the computer build below is untouched.
    target_sources(Stinger PRIVATE
        Source/MainComponent.cpp
        Source/CueSettings.cpp
        Source/FxPanel.cpp
        Source/GlobalHotkeys.cpp
        Source/ControlSurface.cpp
        Source/ControlsWindow.cpp
        Source/ImportPanel.cpp
        Source/Remote.cpp
        Source/ShowCheckPanel.cpp
        Source/AudioPanel.cpp
        Source/CountdownDisplay.cpp
        Source/SchedulePanel.cpp
        Source/RunOfShowPanel.cpp
        Source/OscPanel.cpp
        Source/BackupPanel.cpp
        third_party/qrcodegen/qrcodegen.cpp)
    set_target_properties(Stinger PROPERTIES OUTPUT_NAME juce_jni)
else()
    target_sources(Stinger PRIVATE
        Source/Main.cpp
        Source/MainComponent.cpp
        Source/CueSettings.cpp
        Source/FxPanel.cpp
        Source/GlobalHotkeys.cpp
        Source/ControlSurface.cpp
        Source/ControlsWindow.cpp
        Source/ImportPanel.cpp
        Source/Remote.cpp
        Source/ShowCheckPanel.cpp
        Source/AudioPanel.cpp
        Source/CountdownDisplay.cpp
        Source/SchedulePanel.cpp
        Source/RunOfShowPanel.cpp
        Source/OscPanel.cpp
        Source/BackupPanel.cpp
        third_party/qrcodegen/qrcodegen.cpp)
endif()

# The Wi-Fi remote's web page, served by Stinger itself.
juce_add_binary_data(StingerRemoteData SOURCES remote/remote.html)

target_link_libraries(Stinger PRIVATE
    StingerRemoteData
    StingerCore
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# Engine tests: synthetic audio through the engine, no audio device needed. (On the PC, not on Android.)
if(NOT ANDROID)
    add_executable(EngineTests Tests/EngineTests.cpp)
    target_link_libraries(EngineTests PRIVATE
        StingerCore
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
endif()

if(APPLE)
    target_link_libraries(Stinger PRIVATE "-framework Carbon")   # global hotkeys (RegisterEventHotKey)
endif()
