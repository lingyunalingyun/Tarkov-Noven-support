cmake_minimum_required(VERSION 3.25)

project(NovenTarkovSupport
    VERSION 0.1.0
    LANGUAGES CXX
)

if(NOT WIN32)
    message(FATAL_ERROR "Noven Tarkov Support currently targets Windows only.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ONNXRUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime")
set(ONNXRUNTIME_INCLUDE_DIR "${ONNXRUNTIME_ROOT}/include")
set(ONNXRUNTIME_DLL "${ONNXRUNTIME_ROOT}/bin/onnxruntime.dll")
set(TEXT_DETECTOR_MODEL "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/ppocrv5_mobile_det.onnx")
set(TEXT_RECOGNIZER_MODEL "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/ppocrv5_mobile_rec.onnx")
set(TEXT_RECOGNIZER_DICTIONARY "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/ppocrv5_mobile_rec_dict.txt")
set(ITEM_CATALOG "${CMAKE_CURRENT_SOURCE_DIR}/assets/data/items_catalog.tsv")
set(ITEM_CATALOG_METADATA "${CMAKE_CURRENT_SOURCE_DIR}/assets/data/items_catalog.meta.json")

if(NOT EXISTS "${ONNXRUNTIME_INCLUDE_DIR}/onnxruntime_c_api.h")
    message(FATAL_ERROR "ONNX Runtime headers are missing. See README.md for preparation steps.")
endif()
if(NOT EXISTS "${ONNXRUNTIME_DLL}")
    message(FATAL_ERROR "ONNX Runtime DLL is missing. See README.md for preparation steps.")
endif()
if(NOT EXISTS "${TEXT_DETECTOR_MODEL}")
    message(FATAL_ERROR "Text detector model is missing: ${TEXT_DETECTOR_MODEL}")
endif()
if(NOT EXISTS "${TEXT_RECOGNIZER_MODEL}")
    message(FATAL_ERROR "Text recognizer model is missing: ${TEXT_RECOGNIZER_MODEL}")
endif()
if(NOT EXISTS "${TEXT_RECOGNIZER_DICTIONARY}")
    message(FATAL_ERROR "Text recognizer dictionary is missing: ${TEXT_RECOGNIZER_DICTIONARY}")
endif()
if(NOT EXISTS "${ITEM_CATALOG}")
    message(FATAL_ERROR "Item catalog is missing: ${ITEM_CATALOG}")
endif()
if(NOT EXISTS "${ITEM_CATALOG_METADATA}")
    message(FATAL_ERROR "Item catalog metadata is missing: ${ITEM_CATALOG_METADATA}")
endif()

add_executable(NovenTarkovSupport WIN32
    src/main.cpp
    src/app/App.cpp
    src/raid/RaidEventParser.cpp
    src/raid/RaidSessionDetector.cpp
    src/raid/EftLogReader.cpp
    src/raid/RaidSessionStore.cpp
    src/raid/LocalRaidService.cpp
    src/raid/RaidHistoryBrowser.cpp
    src/capture/DxgiDesktopDuplicationBackend.cpp
    src/capture/Roi.cpp
    src/common/DebugLog.cpp
    src/data/DataRefreshService.cpp
    src/data/ItemCatalog.cpp
    src/data/ItemEconomyStore.cpp
    src/data/PriceBrowser.cpp
    src/data/HideoutCatalog.cpp
    src/data/HideoutBrowser.cpp
    src/data/TaskCatalog.cpp
    src/data/TaskBrowser.cpp
    src/ui/HideoutPage.cpp
    src/ui/TasksPage.cpp
    src/ui/MapPage.cpp
    src/ui/RaidHistoryPage.cpp
    src/data/MapCatalog.cpp
    src/data/MapAssetStore.cpp
    src/data/MapAssetUpdater.cpp
    src/data/MapAssetComposer.cpp
    src/data/PriceHistory.cpp
    src/data/RecentScanStore.cpp
    src/hotkey/GlobalHotkey.cpp
    src/ocr/DetectionDebugWriter.cpp
    src/ocr/OnnxRuntimeSession.cpp
    src/ocr/TextDetector.cpp
    src/ocr/TextRecognizer.cpp
    src/overlay/DebugVisualizationWindow.cpp
    src/overlay/OverlayRenderer.cpp
    src/overlay/OverlayTypes.cpp
    src/overlay/OverlayWindow.cpp
    src/scanner/DebugImageWriter.cpp
    src/scanner/InventoryRecognition.cpp
    src/scanner/AdaptiveTextExpansion.cpp
    src/scanner/LocalTextGrouping.cpp
    src/scanner/ProgressiveScan.cpp
    src/scanner/ScanTrigger.cpp
    src/scanner/SpatialCandidateSelector.cpp
    src/scanner/SpatialDebugWriter.cpp
    src/scanner/TooltipHeuristic.cpp
    src/scanner/TooltipPlacementModel.cpp
    src/ui/MainWindowUi.cpp
    src/ui/localization/LocalizationService.cpp
    src/ui/ItemImageCache.cpp
    src/ui/PageHost.cpp
    src/ui/Sidebar.cpp
)

target_include_directories(NovenTarkovSupport PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${ONNXRUNTIME_INCLUDE_DIR}"
)

target_compile_definitions(NovenTarkovSupport PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
)

if(MSVC)
    target_compile_options(NovenTarkovSupport PRIVATE
        /W4
        /permissive-
        /utf-8
        /EHsc
    )
else()
    target_compile_options(NovenTarkovSupport PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_link_options(NovenTarkovSupport PRIVATE
        -municode
    )
endif()

target_link_libraries(NovenTarkovSupport PRIVATE
    user32
    shell32
    imm32
    d3d11
    d3dcompiler
    dxgi
    d2d1
    dwrite
    dwmapi
    windowsapp
    winhttp
    windowscodecs
    ole32
    bcrypt
)

# 每次构建复制整个语言目录；添加语言包不需要修改源列表或重新链接。
# Copy the locale directory on every build; adding a pack needs no source-list edit or relink.
add_custom_target(NovenTranslations ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/i18n"
        "$<TARGET_FILE_DIR:NovenTarkovSupport>/assets/i18n"
)
add_dependencies(NovenTarkovSupport NovenTranslations)
add_custom_target(NovenHideoutAssets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/data"
        "$<TARGET_FILE_DIR:NovenTarkovSupport>/assets/data")
add_dependencies(NovenTarkovSupport NovenHideoutAssets)
add_custom_target(NovenMapAssets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/maps"
        "$<TARGET_FILE_DIR:NovenTarkovSupport>/assets/maps"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/NOTICE.md"
        "$<TARGET_FILE_DIR:NovenTarkovSupport>/NOTICE.md"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:NovenTarkovSupport>/docs"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/docs/MAP_ATTRIBUTION.md"
        "$<TARGET_FILE_DIR:NovenTarkovSupport>/docs/MAP_ATTRIBUTION.md")
add_dependencies(NovenTarkovSupport NovenMapAssets)

add_custom_command(TARGET NovenTarkovSupport POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ONNXRUNTIME_DLL}"
        "$<TARGET_FILE_DIR:NovenTarkovSupport>/onnxruntime.dll"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:NovenTarkovSupport>/assets/models"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${TEXT_DETECTOR_MODEL}"
        "$<TARGET_FILE_DIR:NovenTarkovSupport>/assets/models/ppocrv5_mobile_det.onnx"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${TEXT_RECOGNIZER_MODEL}"
        "$<TARGET_FILE_DIR:NovenTarkovSupport>/assets/models/ppocrv5_mobile_rec.onnx"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${TEXT_RECOGNIZER_DICTIONARY}"
        "$<TARGET_FILE_DIR:NovenTarkovSupport>/assets/models/ppocrv5_mobile_rec_dict.txt"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:NovenTarkovSupport>/assets/data"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ITEM_CATALOG}"
        "$<TARGET_FILE_DIR:NovenTarkovSupport>/assets/data/items_catalog.tsv"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ITEM_CATALOG_METADATA}"
        "$<TARGET_FILE_DIR:NovenTarkovSupport>/assets/data/items_catalog.meta.json"
)

include(CTest)
if(BUILD_TESTING)
    add_executable(NovenEventSourceTests tests/EventSourceTests.cpp src/events/OfficialEventSource.cpp src/events/EventHttp.cpp src/events/EventCatalog.cpp)
    target_include_directories(NovenEventSourceTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(NovenEventSourceTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(NovenEventSourceTests PRIVATE winhttp)
    if(MSVC)
        target_compile_options(NovenEventSourceTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME event_source COMMAND NovenEventSourceTests)
    add_executable(NovenEventCacheTests tests/EventCacheTests.cpp src/events/EventCache.cpp src/events/EventCatalog.cpp)
    target_include_directories(NovenEventCacheTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(NovenEventCacheTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(NovenEventCacheTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME event_cache COMMAND NovenEventCacheTests)
    add_executable(NovenEventCatalogTests tests/EventCatalogTests.cpp src/events/EventCatalog.cpp)
    target_include_directories(NovenEventCatalogTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    if(MSVC)
        target_compile_options(NovenEventCatalogTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME event_catalog COMMAND NovenEventCatalogTests)
    add_executable(NovenRaidEventParserTests tests/RaidEventParserTests.cpp src/raid/RaidEventParser.cpp)
    target_include_directories(NovenRaidEventParserTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    if(MSVC)
        target_compile_options(NovenRaidEventParserTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME raid_event_parser COMMAND NovenRaidEventParserTests)
    add_executable(NovenRaidSessionDetectorTests tests/RaidSessionDetectorTests.cpp
        src/raid/RaidSessionDetector.cpp src/raid/RaidEventParser.cpp)
    target_include_directories(NovenRaidSessionDetectorTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(NovenRaidSessionDetectorTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(NovenRaidSessionDetectorTests PRIVATE ole32)
    if(MSVC)
        target_compile_options(NovenRaidSessionDetectorTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME raid_session_detector COMMAND NovenRaidSessionDetectorTests)
    add_executable(NovenRaidHistoryBrowserTests tests/RaidHistoryBrowserTests.cpp src/raid/RaidHistoryBrowser.cpp)
    target_include_directories(NovenRaidHistoryBrowserTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(NovenRaidHistoryBrowserTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(NovenRaidHistoryBrowserTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME raid_history_browser COMMAND NovenRaidHistoryBrowserTests)
    add_executable(NovenRaidHistoryPageTests tests/RaidHistoryPageTests.cpp
        src/ui/RaidHistoryPage.cpp src/raid/RaidHistoryBrowser.cpp src/data/ItemCatalog.cpp src/ui/localization/LocalizationService.cpp)
    target_include_directories(NovenRaidHistoryPageTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(NovenRaidHistoryPageTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(NovenRaidHistoryPageTests PRIVATE d2d1 dwrite imm32)
    if(MSVC)
        target_compile_options(NovenRaidHistoryPageTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME raid_history_page COMMAND NovenRaidHistoryPageTests "${CMAKE_CURRENT_SOURCE_DIR}/assets/i18n")
    add_executable(NovenEftLogReaderTests tests/EftLogReaderTests.cpp src/raid/EftLogReader.cpp src/raid/RaidEventParser.cpp)
    target_include_directories(NovenEftLogReaderTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(NovenEftLogReaderTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(NovenEftLogReaderTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME eft_log_reader COMMAND NovenEftLogReaderTests)
    add_executable(NovenRaidSessionStoreTests tests/RaidSessionStoreTests.cpp
        src/raid/RaidSessionStore.cpp src/raid/RaidSessionDetector.cpp src/raid/RaidEventParser.cpp)
    target_include_directories(NovenRaidSessionStoreTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(NovenRaidSessionStoreTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(NovenRaidSessionStoreTests PRIVATE ole32)
    if(MSVC)
        target_compile_options(NovenRaidSessionStoreTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME raid_session_store COMMAND NovenRaidSessionStoreTests)
    add_executable(NovenLocalRaidServiceTests tests/LocalRaidServiceTests.cpp
        src/raid/LocalRaidService.cpp src/raid/EftLogReader.cpp src/raid/RaidSessionStore.cpp
        src/raid/RaidSessionDetector.cpp src/raid/RaidEventParser.cpp)
    target_include_directories(NovenLocalRaidServiceTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(NovenLocalRaidServiceTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(NovenLocalRaidServiceTests PRIVATE ole32)
    if(MSVC)
        target_compile_options(NovenLocalRaidServiceTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME local_raid_service COMMAND NovenLocalRaidServiceTests)
    configure_file(
        "${ITEM_CATALOG}"
        "${CMAKE_CURRENT_BINARY_DIR}/test-data/items_catalog.tsv"
        COPYONLY
    )
    configure_file(
        "${ITEM_CATALOG_METADATA}"
        "${CMAKE_CURRENT_BINARY_DIR}/test-data/items_catalog.meta.json"
        COPYONLY
    )
    add_executable(NovenTarkovSupportCatalogTests
        tests/ItemCatalogTests.cpp
        src/data/ItemCatalog.cpp
    )
    target_include_directories(NovenTarkovSupportCatalogTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_compile_definitions(NovenTarkovSupportCatalogTests PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
    )
    if(MSVC)
        target_compile_options(NovenTarkovSupportCatalogTests PRIVATE
            /W4
            /permissive-
            /utf-8
            /EHsc
        )
    else()
        target_compile_options(NovenTarkovSupportCatalogTests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
    add_test(NAME item_catalog COMMAND NovenTarkovSupportCatalogTests "test-data/items_catalog.tsv")

    add_executable(NovenTarkovSupportSpatialTests
        tests/SpatialCandidateSelectorTests.cpp
        src/data/ItemCatalog.cpp
        src/capture/Roi.cpp
        src/scanner/ProgressiveScan.cpp
        src/scanner/SpatialCandidateSelector.cpp
    )
    target_include_directories(NovenTarkovSupportSpatialTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_compile_definitions(NovenTarkovSupportSpatialTests PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
    )
    if(MSVC)
        target_compile_options(NovenTarkovSupportSpatialTests PRIVATE
            /W4
            /permissive-
            /utf-8
            /EHsc
        )
    else()
        target_compile_options(NovenTarkovSupportSpatialTests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
    add_test(NAME spatial_candidate_selector COMMAND NovenTarkovSupportSpatialTests)

    add_executable(NovenTarkovSupportProgressiveScanTests
        tests/ProgressiveScanTests.cpp
        src/scanner/AdaptiveTextExpansion.cpp
        src/scanner/ProgressiveScan.cpp
        src/scanner/SpatialCandidateSelector.cpp
        src/scanner/TooltipHeuristic.cpp
        src/scanner/TooltipPlacementModel.cpp
    )
    target_include_directories(NovenTarkovSupportProgressiveScanTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_compile_definitions(NovenTarkovSupportProgressiveScanTests PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
    )
    if(MSVC)
        target_compile_options(NovenTarkovSupportProgressiveScanTests PRIVATE
            /W4
            /permissive-
            /utf-8
            /EHsc
        )
    else()
        target_compile_options(NovenTarkovSupportProgressiveScanTests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
    add_test(NAME progressive_scan COMMAND NovenTarkovSupportProgressiveScanTests)

    add_executable(NovenTarkovSupportTextGroupingTests
        tests/LocalTextGroupingTests.cpp
        src/data/ItemCatalog.cpp
        src/scanner/LocalTextGrouping.cpp
    )
    target_include_directories(NovenTarkovSupportTextGroupingTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_compile_definitions(NovenTarkovSupportTextGroupingTests PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
    )
    if(MSVC)
        target_compile_options(NovenTarkovSupportTextGroupingTests PRIVATE
            /W4
            /permissive-
            /utf-8
            /EHsc
        )
    else()
        target_compile_options(NovenTarkovSupportTextGroupingTests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
    add_test(NAME local_text_grouping COMMAND NovenTarkovSupportTextGroupingTests
        "test-data/items_catalog.tsv")

    add_executable(NovenTarkovSupportEconomyTests
        tests/ItemEconomyStoreTests.cpp
        src/data/ItemEconomyStore.cpp
    )
    target_include_directories(NovenTarkovSupportEconomyTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_compile_definitions(NovenTarkovSupportEconomyTests PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
    )
    if(MSVC)
        target_compile_options(NovenTarkovSupportEconomyTests PRIVATE
            /W4
            /permissive-
            /utf-8
            /EHsc
        )
    else()
        target_compile_options(NovenTarkovSupportEconomyTests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
    add_test(NAME item_economy_store COMMAND NovenTarkovSupportEconomyTests)

    add_executable(NovenTarkovSupportOverlayTests
        tests/OverlayTests.cpp
        src/overlay/OverlayTypes.cpp
    )
    target_include_directories(NovenTarkovSupportOverlayTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_compile_definitions(NovenTarkovSupportOverlayTests PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
    )
    if(MSVC)
        target_compile_options(NovenTarkovSupportOverlayTests PRIVATE
            /W4
            /permissive-
            /utf-8
            /EHsc
        )
    else()
        target_compile_options(NovenTarkovSupportOverlayTests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
    add_test(NAME overlay_logic COMMAND NovenTarkovSupportOverlayTests)

    add_executable(NovenTarkovSupportInventoryRecognitionTests
        tests/InventoryRecognitionTests.cpp
        src/data/ItemCatalog.cpp
        src/data/ItemEconomyStore.cpp
        src/overlay/OverlayTypes.cpp
        src/scanner/InventoryRecognition.cpp
        src/scanner/LocalTextGrouping.cpp
        src/scanner/SpatialCandidateSelector.cpp
    )
    target_include_directories(NovenTarkovSupportInventoryRecognitionTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_compile_definitions(NovenTarkovSupportInventoryRecognitionTests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00
    )
    if(MSVC)
        target_compile_options(NovenTarkovSupportInventoryRecognitionTests PRIVATE
            /W4 /permissive- /utf-8 /EHsc
        )
    endif()
    add_test(NAME inventory_recognition COMMAND NovenTarkovSupportInventoryRecognitionTests
        "test-data/items_catalog.tsv")

    add_executable(NovenTarkovSupportNavigationTests
        tests/MainPageTests.cpp
        src/ui/localization/LocalizationService.cpp
        src/ui/PageHost.cpp
        src/ui/Sidebar.cpp
    )
    target_include_directories(NovenTarkovSupportNavigationTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    if(MSVC)
        target_compile_options(NovenTarkovSupportNavigationTests PRIVATE
            /W4 /permissive- /utf-8 /EHsc
        )
    endif()
    target_link_libraries(NovenTarkovSupportNavigationTests PRIVATE imm32)
    add_test(NAME main_navigation COMMAND NovenTarkovSupportNavigationTests)

    add_executable(NovenTarkovSupportRecentScanTests
        tests/RecentScanStoreTests.cpp
        src/data/RecentScanStore.cpp
        src/common/DebugLog.cpp
    )
    target_include_directories(NovenTarkovSupportRecentScanTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_compile_definitions(NovenTarkovSupportRecentScanTests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00
    )
    if(MSVC)
        target_compile_options(NovenTarkovSupportRecentScanTests PRIVATE
            /W4 /permissive- /utf-8 /EHsc
        )
    endif()
    add_test(NAME recent_scans COMMAND NovenTarkovSupportRecentScanTests)
    add_executable(NovenTarkovSupportItemImageTests
        tests/ItemImageCacheTests.cpp
        src/ui/ItemImageCache.cpp
        src/common/DebugLog.cpp
    )
    target_include_directories(NovenTarkovSupportItemImageTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(NovenTarkovSupportItemImageTests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    if(MSVC)
        target_compile_options(NovenTarkovSupportItemImageTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    target_link_libraries(NovenTarkovSupportItemImageTests PRIVATE winhttp windowscodecs ole32)
    add_test(NAME item_images COMMAND NovenTarkovSupportItemImageTests)
    add_executable(NovenTarkovSupportPriceBrowserTests
        tests/PriceBrowserTests.cpp
        src/data/ItemCatalog.cpp
        src/data/ItemEconomyStore.cpp
        src/data/PriceBrowser.cpp)
    target_include_directories(NovenTarkovSupportPriceBrowserTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(NovenTarkovSupportPriceBrowserTests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    if(MSVC)
        target_compile_options(NovenTarkovSupportPriceBrowserTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME price_browser COMMAND NovenTarkovSupportPriceBrowserTests
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/data/items_catalog.tsv")
    add_executable(NovenLocalizationTests
        tests/LocalizationTests.cpp
        src/ui/localization/LocalizationService.cpp)
    target_include_directories(NovenLocalizationTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(NovenLocalizationTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(NovenLocalizationTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME localization COMMAND NovenLocalizationTests "${CMAKE_CURRENT_SOURCE_DIR}/assets/i18n")
    add_executable(NovenLocalizationUiTests
        tests/LocalizationUiTests.cpp
        src/ui/MainWindowUi.cpp src/ui/PageHost.cpp src/ui/Sidebar.cpp
        src/ui/RaidHistoryPage.cpp src/raid/RaidHistoryBrowser.cpp
        src/ui/HideoutPage.cpp src/ui/TasksPage.cpp src/ui/MapPage.cpp src/data/HideoutCatalog.cpp src/data/HideoutBrowser.cpp
        src/data/TaskCatalog.cpp src/data/TaskBrowser.cpp
        src/data/MapCatalog.cpp
        src/data/ItemCatalog.cpp src/data/ItemEconomyStore.cpp src/data/PriceBrowser.cpp
        src/data/PriceHistory.cpp
        src/ui/ItemImageCache.cpp src/ui/localization/LocalizationService.cpp
        src/common/DebugLog.cpp)
    target_include_directories(NovenLocalizationUiTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(NovenLocalizationUiTests PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    if(MSVC)
        target_compile_options(NovenLocalizationUiTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    target_link_libraries(NovenLocalizationUiTests PRIVATE d2d1 dwrite winhttp windowscodecs ole32 windowsapp imm32 shell32)
    add_test(NAME localization_ui COMMAND NovenLocalizationUiTests "${CMAKE_CURRENT_SOURCE_DIR}/assets/i18n")
    add_executable(NovenPriceHistoryTests tests/PriceHistoryTests.cpp
        src/data/PriceHistory.cpp src/common/DebugLog.cpp)
    target_include_directories(NovenPriceHistoryTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(NovenPriceHistoryTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(NovenPriceHistoryTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    target_link_libraries(NovenPriceHistoryTests PRIVATE winhttp windowsapp)
    add_test(NAME price_history COMMAND NovenPriceHistoryTests)
    add_executable(NovenHideoutTests tests/HideoutTests.cpp
        src/data/HideoutCatalog.cpp src/data/HideoutBrowser.cpp
        src/data/ItemCatalog.cpp src/data/ItemEconomyStore.cpp
        src/ui/HideoutPage.cpp src/ui/localization/LocalizationService.cpp src/common/DebugLog.cpp)
    target_include_directories(NovenHideoutTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(NovenHideoutTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(NovenHideoutTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    target_link_libraries(NovenHideoutTests PRIVATE d2d1 dwrite imm32)
    add_test(NAME hideout COMMAND NovenHideoutTests "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    add_executable(NovenTaskTests tests/TaskTests.cpp src/data/TaskCatalog.cpp src/data/TaskBrowser.cpp src/data/ItemCatalog.cpp)
    target_include_directories(NovenTaskTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(NovenTaskTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(NovenTaskTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME tasks COMMAND NovenTaskTests "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    add_executable(NovenMapPrototypeTests tests/MapPrototypeTests.cpp src/ui/MapPage.cpp src/data/MapCatalog.cpp src/ui/localization/LocalizationService.cpp)
    target_include_directories(NovenMapPrototypeTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(NovenMapPrototypeTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(NovenMapPrototypeTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME map_prototype COMMAND NovenMapPrototypeTests "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    target_link_libraries(NovenMapPrototypeTests PRIVATE d2d1 dwrite imm32 windowscodecs ole32)
    add_executable(NovenMapCatalogTests tests/MapCatalogTests.cpp src/data/MapCatalog.cpp)
    target_include_directories(NovenMapCatalogTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(NovenMapCatalogTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(NovenMapCatalogTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME map_catalog COMMAND NovenMapCatalogTests "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    add_executable(NovenTaskMapLinksTests tests/TaskMapLinksTests.cpp
        src/ui/TasksPage.cpp src/data/TaskCatalog.cpp src/data/TaskBrowser.cpp
        src/data/ItemCatalog.cpp src/data/MapCatalog.cpp
        src/ui/localization/LocalizationService.cpp src/common/DebugLog.cpp)
    target_include_directories(NovenTaskMapLinksTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(NovenTaskMapLinksTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(NovenTaskMapLinksTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    target_link_libraries(NovenTaskMapLinksTests PRIVATE d2d1 dwrite imm32 user32)
    add_test(NAME task_map_links COMMAND NovenTaskMapLinksTests "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    add_executable(NovenMapAllMapsTests tests/MapAllMapsTests.cpp src/ui/MapPage.cpp
        src/data/MapCatalog.cpp src/ui/localization/LocalizationService.cpp)
    target_include_directories(NovenMapAllMapsTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(NovenMapAllMapsTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(NovenMapAllMapsTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    target_link_libraries(NovenMapAllMapsTests PRIVATE d2d1 dwrite imm32 windowscodecs ole32)
    add_test(NAME map_all_maps COMMAND NovenMapAllMapsTests "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/data" "${CMAKE_CURRENT_SOURCE_DIR}/assets/data/pve")
    add_executable(NovenMapAssetTests tests/MapAssetTests.cpp src/data/MapAssetStore.cpp src/data/MapAssetUpdater.cpp)
    add_executable(NovenMapAssetComposerTests tests/MapAssetComposerTests.cpp src/data/MapAssetStore.cpp src/data/MapAssetUpdater.cpp src/data/MapAssetComposer.cpp)
    foreach(target NovenMapAssetTests NovenMapAssetComposerTests)
        target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_compile_definitions(${target} PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /EHsc)
        endif()
        target_link_libraries(${target} PRIVATE winhttp windowscodecs ole32 bcrypt)
    endforeach()
    add_test(NAME map_assets COMMAND NovenMapAssetTests)
    add_test(NAME map_asset_composer COMMAND NovenMapAssetComposerTests)
    add_executable(NovenMapPerformanceTests tests/MapPerformanceTests.cpp src/ui/MapPage.cpp src/data/MapCatalog.cpp
        src/ui/localization/LocalizationService.cpp)
    target_include_directories(NovenMapPerformanceTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(NovenMapPerformanceTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(NovenMapPerformanceTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    target_link_libraries(NovenMapPerformanceTests PRIVATE d2d1 dwrite imm32 windowscodecs ole32 psapi)
endif()
