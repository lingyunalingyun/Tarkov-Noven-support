cmake_minimum_required(VERSION 3.25)

project(NovenTarkovSupport
    VERSION 0.1.0
    LANGUAGES C CXX
)

if(NOT WIN32)
    message(FATAL_ERROR "Noven Tarkov Support currently targets Windows only.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(NovenPluginTransport STATIC src/plugins/PluginProtocol.cpp src/plugins/PluginPipe.cpp src/plugins/PluginManifest.cpp src/plugins/PluginUiDocument.cpp src/plugins/PluginNativePath.cpp src/plugins/CatalogData.cpp src/plugins/PluginScanData.cpp src/plugins/PluginStorageData.cpp)
target_include_directories(NovenPluginTransport PUBLIC src)
target_compile_definitions(NovenPluginTransport PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
target_link_libraries(NovenPluginTransport PUBLIC bcrypt advapi32)
add_library(NovenPluginRuntime STATIC src/plugins/PluginRuntimeManager.cpp src/plugins/PluginStateStore.cpp src/plugins/PluginRuntimeController.cpp src/plugins/PluginDiscovery.cpp src/plugins/CatalogPluginService.cpp src/plugins/PluginStorageService.cpp)
add_library(NovenPluginHttp STATIC src/plugins/PluginHttpService.cpp src/plugins/PluginHttpWindows.cpp)
target_link_libraries(NovenPluginHttp PUBLIC NovenPluginTransport winhttp ws2_32)
target_link_libraries(NovenPluginRuntime PUBLIC NovenPluginTransport)
set(NOVEN_PLUGIN_REGISTRY_URL "" CACHE STRING "First-party raw.githubusercontent.com Registry HTTPS URL (empty until published)")
add_library(NovenMarketplace STATIC src/plugins/PluginRegistry.cpp src/plugins/MarketplaceService.cpp src/plugins/MarketplaceHttp.cpp)
target_link_libraries(NovenMarketplace PUBLIC NovenPluginRuntime winhttp)
if(MSVC)
    target_compile_options(NovenMarketplace PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()
add_executable(NovenPluginHost WIN32 src/plugins/PluginHostMain.cpp src/plugins/PluginNativeHost.cpp)
target_include_directories(NovenPluginHost PRIVATE sdk)
target_link_libraries(NovenPluginHost PRIVATE NovenPluginTransport NovenPluginHttp shell32)
if(MSVC)
    target_compile_options(NovenPluginTransport PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(NovenPluginHttp PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(NovenPluginRuntime PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(NovenPluginHost PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()

set(ONNXRUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime")
option(NOVEN_BUILD_PLUGIN_EXAMPLES "Build first-party native plugin examples (never installed automatically)" OFF)
if(NOVEN_BUILD_PLUGIN_EXAMPLES)
    add_library(NovenHttpPlugin SHARED examples/http-plugin/http_plugin.c)
    target_include_directories(NovenHttpPlugin PRIVATE sdk)
    set_target_properties(NovenHttpPlugin PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON OUTPUT_NAME "noven-http")
    if(MSVC)
        target_compile_options(NovenHttpPlugin PRIVATE /W4 /utf-8)
    endif()
    add_library(NovenStoragePlugin SHARED examples/storage-plugin/storage_plugin.c)
    target_include_directories(NovenStoragePlugin PRIVATE sdk)
    set_target_properties(NovenStoragePlugin PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON OUTPUT_NAME "noven-storage")
    if(MSVC)
        target_compile_options(NovenStoragePlugin PRIVATE /W4 /utf-8)
    endif()
    add_library(NovenScanPlugin SHARED examples/scan-plugin/scan_plugin.c)
    target_include_directories(NovenScanPlugin PRIVATE sdk)
    set_target_properties(NovenScanPlugin PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON OUTPUT_NAME "noven-scan")
    if(MSVC)
        target_compile_options(NovenScanPlugin PRIVATE /W4 /utf-8)
    endif()
    add_library(NovenHistoryPlugin SHARED examples/history-plugin/history_plugin.c)
    target_include_directories(NovenHistoryPlugin PRIVATE sdk)
    set_target_properties(NovenHistoryPlugin PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON OUTPUT_NAME "noven-history")
    if(MSVC)
        target_compile_options(NovenHistoryPlugin PRIVATE /W4 /utf-8)
    endif()
    add_library(NovenCatalogPlugin SHARED examples/catalog-plugin/catalog_plugin.c)
    target_include_directories(NovenCatalogPlugin PRIVATE sdk)
    set_target_properties(NovenCatalogPlugin PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON OUTPUT_NAME "noven-catalog")
    if(MSVC)
        target_compile_options(NovenCatalogPlugin PRIVATE /W4 /utf-8)
    endif()
    add_library(NovenHelloPlugin SHARED examples/hello-plugin/hello_plugin.c)
    target_include_directories(NovenHelloPlugin PRIVATE sdk)
    set_target_properties(NovenHelloPlugin PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON OUTPUT_NAME "noven-hello")
    if(MSVC)
        target_compile_options(NovenHelloPlugin PRIVATE /W4 /utf-8)
    endif()
endif()
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
    src/plugins/PluginManifest.cpp
    src/plugins/PluginDiscovery.cpp
    src/ui/PluginsPage.cpp
    src/ui/MessageDialog.cpp
    src/ui/PluginPageView.cpp
    src/events/EventCatalog.cpp
    src/events/EventBrowser.cpp
    src/events/EventCache.cpp
    src/events/EventHttp.cpp
    src/events/OfficialEventSource.cpp
    src/events/WikiEventSource.cpp
    src/events/TarkovDevEventEnricher.cpp
    src/events/TarkovChangesEnricher.cpp
    src/events/EventEnrichment.cpp
    src/events/EventService.cpp
    src/events/EventTranslation.cpp
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
    src/ui/EventsPage.cpp
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
    NovenPluginRuntime
    user32
    shell32
    imm32
    d3d11
    d3dcompiler
    dxgi
    d2d1
    dwrite
    dwmapi
    comctl32
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
add_dependencies(NovenTarkovSupport NovenPluginHost)
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
    add_executable(NovenMarketplaceServiceTests tests/MarketplaceServiceTests.cpp)
    target_link_libraries(NovenMarketplaceServiceTests PRIVATE NovenMarketplace)
    if(MSVC)
        target_compile_options(NovenMarketplaceServiceTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME marketplace_service COMMAND NovenMarketplaceServiceTests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/plugin-registry-v1.json")
    add_executable(NovenPluginRegistryTests tests/PluginRegistryTests.cpp)
    target_link_libraries(NovenPluginRegistryTests PRIVATE NovenMarketplace)
    if(MSVC)
        target_compile_options(NovenPluginRegistryTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME plugin_registry COMMAND NovenPluginRegistryTests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/plugin-registry-v1.json")
    add_executable(NovenPluginHttpServiceTests tests/PluginHttpServiceTests.cpp)
    target_link_libraries(NovenPluginHttpServiceTests PRIVATE NovenPluginHttp)
    if(MSVC)
        target_compile_options(NovenPluginHttpServiceTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME plugin_http_service COMMAND NovenPluginHttpServiceTests "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(plugin_http_service PROPERTIES TIMEOUT 15)
    add_executable(NovenPluginHttpContractTests tests/PluginHttpContractTests.cpp)
    target_link_libraries(NovenPluginHttpContractTests PRIVATE NovenPluginRuntime)
    if(MSVC)
        target_compile_options(NovenPluginHttpContractTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME plugin_http_contract COMMAND NovenPluginHttpContractTests)
    set(NOVEN_STORAGE_FIXTURES)
    foreach(storage_mode RANGE 0 5)
        add_library(NovenStorageFixture${storage_mode} SHARED tests/StoragePluginFixture.c)
        target_include_directories(NovenStorageFixture${storage_mode} PRIVATE sdk)
        target_compile_definitions(NovenStorageFixture${storage_mode} PRIVATE NOVEN_STORAGE_FIXTURE_MODE=${storage_mode})
        set_target_properties(NovenStorageFixture${storage_mode} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
        if(MSVC)
            target_compile_options(NovenStorageFixture${storage_mode} PRIVATE /W4 /utf-8)
        endif()
        list(APPEND NOVEN_STORAGE_FIXTURES "$<TARGET_FILE:NovenStorageFixture${storage_mode}>")
    endforeach()
    add_executable(NovenStoragePluginRuntimeTests tests/StoragePluginRuntimeTests.cpp)
    target_link_libraries(NovenStoragePluginRuntimeTests PRIVATE NovenPluginRuntime)
    add_dependencies(NovenStoragePluginRuntimeTests NovenPluginHost)
    foreach(storage_mode RANGE 0 5)
        add_dependencies(NovenStoragePluginRuntimeTests NovenStorageFixture${storage_mode})
    endforeach()
    if(MSVC)
        target_compile_options(NovenStoragePluginRuntimeTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME storage_plugin_runtime COMMAND NovenStoragePluginRuntimeTests "$<TARGET_FILE:NovenPluginHost>" ${NOVEN_STORAGE_FIXTURES})
    set_tests_properties(storage_plugin_runtime PROPERTIES TIMEOUT 90)
    add_executable(NovenStoragePluginBoundaryTests tests/StoragePluginBoundaryTests.cpp)
    target_include_directories(NovenStoragePluginBoundaryTests PRIVATE sdk)
    target_link_libraries(NovenStoragePluginBoundaryTests PRIVATE NovenPluginRuntime)
    if(MSVC)
        target_compile_options(NovenStoragePluginBoundaryTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME storage_plugin_boundary COMMAND NovenStoragePluginBoundaryTests "${CMAKE_CURRENT_SOURCE_DIR}")
    if(NOVEN_BUILD_PLUGIN_EXAMPLES)
        add_executable(NovenPluginHttpTestHost WIN32 tests/PluginHttpTestHost.cpp src/plugins/PluginNativeHost.cpp)
        target_include_directories(NovenPluginHttpTestHost PRIVATE sdk)
        target_link_libraries(NovenPluginHttpTestHost PRIVATE NovenPluginHttp shell32)
        add_executable(NovenPluginHttpSampleTests tests/PluginHttpSampleTests.cpp)
        target_link_libraries(NovenPluginHttpSampleTests PRIVATE NovenPluginRuntime)
        add_dependencies(NovenPluginHttpSampleTests NovenPluginHttpTestHost NovenHttpPlugin)
        if(MSVC)
            target_compile_options(NovenPluginHttpTestHost PRIVATE /W4 /permissive- /utf-8 /EHsc)
            target_compile_options(NovenPluginHttpSampleTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
        endif()
        add_test(NAME plugin_http_sample COMMAND NovenPluginHttpSampleTests "$<TARGET_FILE:NovenPluginHttpTestHost>" "$<TARGET_FILE:NovenHttpPlugin>" "${CMAKE_CURRENT_SOURCE_DIR}/examples/http-plugin/manifest.json")
        set(NOVEN_HTTP_FIXTURES)
        foreach(http_mode RANGE 0 6)
            add_library(NovenHttpFixture${http_mode} SHARED tests/HttpPluginFixture.c)
            target_include_directories(NovenHttpFixture${http_mode} PRIVATE sdk)
            target_compile_definitions(NovenHttpFixture${http_mode} PRIVATE NOVEN_HTTP_FIXTURE_MODE=${http_mode})
            set_target_properties(NovenHttpFixture${http_mode} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
            if(MSVC)
                target_compile_options(NovenHttpFixture${http_mode} PRIVATE /W4 /utf-8)
            endif()
            list(APPEND NOVEN_HTTP_FIXTURES "$<TARGET_FILE:NovenHttpFixture${http_mode}>")
        endforeach()
        add_executable(NovenPluginHttpRuntimeTests tests/PluginHttpRuntimeTests.cpp)
        target_link_libraries(NovenPluginHttpRuntimeTests PRIVATE NovenPluginRuntime)
        add_dependencies(NovenPluginHttpRuntimeTests NovenPluginHttpTestHost)
        foreach(http_mode RANGE 0 6)
            add_dependencies(NovenPluginHttpRuntimeTests NovenHttpFixture${http_mode})
        endforeach()
        if(MSVC)
            target_compile_options(NovenPluginHttpRuntimeTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
        endif()
        add_test(NAME plugin_http_runtime COMMAND NovenPluginHttpRuntimeTests "$<TARGET_FILE:NovenPluginHttpTestHost>" ${NOVEN_HTTP_FIXTURES})
        add_executable(NovenStoragePluginSampleTests tests/StoragePluginSampleTests.cpp)
        target_link_libraries(NovenStoragePluginSampleTests PRIVATE NovenPluginRuntime)
        add_dependencies(NovenStoragePluginSampleTests NovenPluginHost NovenStoragePlugin)
        if(MSVC)
            target_compile_options(NovenStoragePluginSampleTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
        endif()
        add_test(NAME storage_plugin_sample COMMAND NovenStoragePluginSampleTests "$<TARGET_FILE:NovenPluginHost>" "$<TARGET_FILE:NovenStoragePlugin>" "${CMAKE_CURRENT_SOURCE_DIR}/examples/storage-plugin/manifest.json")
        set_tests_properties(storage_plugin_sample PROPERTIES TIMEOUT 60)
    endif()
    add_executable(NovenPluginStorageServiceTests tests/PluginStorageServiceTests.cpp)
    target_link_libraries(NovenPluginStorageServiceTests PRIVATE NovenPluginRuntime)
    if(MSVC)
        target_compile_options(NovenPluginStorageServiceTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME plugin_storage_service COMMAND NovenPluginStorageServiceTests)
    add_library(NovenCatalogPluginFixture SHARED tests/CatalogPluginFixture.c)
    set_target_properties(NovenCatalogPluginFixture PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
    target_include_directories(NovenCatalogPluginFixture PRIVATE sdk)
    add_executable(NovenCatalogPluginHostTests tests/CatalogPluginHostTests.cpp)
    target_link_libraries(NovenCatalogPluginHostTests PRIVATE NovenPluginRuntime)
    add_dependencies(NovenCatalogPluginHostTests NovenPluginHost NovenCatalogPluginFixture)
    if(MSVC)
        target_compile_options(NovenCatalogPluginFixture PRIVATE /W4 /utf-8)
        target_compile_options(NovenCatalogPluginHostTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME catalog_plugin_host COMMAND NovenCatalogPluginHostTests "$<TARGET_FILE:NovenPluginHost>" "$<TARGET_FILE:NovenCatalogPluginFixture>")
    set_tests_properties(catalog_plugin_host PROPERTIES TIMEOUT 15)
    add_executable(NovenCatalogPluginRuntimeTests tests/CatalogPluginRuntimeTests.cpp)
    target_link_libraries(NovenCatalogPluginRuntimeTests PRIVATE NovenPluginRuntime)
    add_dependencies(NovenCatalogPluginRuntimeTests NovenPluginHost NovenCatalogPluginFixture)
    if(MSVC)
        target_compile_options(NovenCatalogPluginRuntimeTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME catalog_plugin_runtime COMMAND NovenCatalogPluginRuntimeTests "$<TARGET_FILE:NovenPluginHost>" "$<TARGET_FILE:NovenCatalogPluginFixture>")
    set_tests_properties(catalog_plugin_runtime PROPERTIES TIMEOUT 20)
    set(NOVEN_CATALOG_FIXTURES "$<TARGET_FILE:NovenCatalogPluginFixture>")
    foreach(catalog_mode RANGE 1 9)
        add_library(NovenCatalogFixture${catalog_mode} SHARED tests/CatalogPluginFixture.c)
        target_include_directories(NovenCatalogFixture${catalog_mode} PRIVATE sdk)
        target_compile_definitions(NovenCatalogFixture${catalog_mode} PRIVATE NOVEN_CATALOG_FIXTURE_MODE=${catalog_mode})
        set_target_properties(NovenCatalogFixture${catalog_mode} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
        if(MSVC)
            target_compile_options(NovenCatalogFixture${catalog_mode} PRIVATE /W4 /utf-8)
        endif()
        list(APPEND NOVEN_CATALOG_FIXTURES "$<TARGET_FILE:NovenCatalogFixture${catalog_mode}>")
    endforeach()
    add_executable(NovenCatalogPluginFaultTests tests/CatalogPluginFaultTests.cpp)
    target_link_libraries(NovenCatalogPluginFaultTests PRIVATE NovenPluginRuntime)
    add_dependencies(NovenCatalogPluginFaultTests NovenPluginHost NovenCatalogPluginFixture)
    foreach(catalog_mode RANGE 1 9)
        add_dependencies(NovenCatalogPluginFaultTests NovenCatalogFixture${catalog_mode})
    endforeach()
    if(MSVC)
        target_compile_options(NovenCatalogPluginFaultTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME catalog_plugin_faults COMMAND NovenCatalogPluginFaultTests "$<TARGET_FILE:NovenPluginHost>" ${NOVEN_CATALOG_FIXTURES})
    set_tests_properties(catalog_plugin_faults PROPERTIES TIMEOUT 30)
    add_executable(NovenCatalogPluginPeerTests tests/CatalogPluginPeerTests.cpp)
    target_link_libraries(NovenCatalogPluginPeerTests PRIVATE NovenPluginRuntime)
    add_dependencies(NovenCatalogPluginPeerTests NovenPluginHostTestPeer NovenCatalogPluginFixture)
    if(MSVC)
        target_compile_options(NovenCatalogPluginPeerTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME catalog_plugin_peer COMMAND NovenCatalogPluginPeerTests "$<TARGET_FILE:NovenPluginHostTestPeer>" "$<TARGET_FILE:NovenCatalogPluginFixture>")
    set_tests_properties(catalog_plugin_peer PROPERTIES TIMEOUT 30)
    add_executable(NovenCatalogPluginServiceTests tests/CatalogPluginServiceTests.cpp)
    target_link_libraries(NovenCatalogPluginServiceTests PRIVATE NovenPluginRuntime)
    if(MSVC)
        target_compile_options(NovenCatalogPluginServiceTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME catalog_plugin_service COMMAND NovenCatalogPluginServiceTests)
    add_executable(NovenHistoryPluginServiceTests tests/HistoryPluginServiceTests.cpp)
    target_link_libraries(NovenHistoryPluginServiceTests PRIVATE NovenPluginRuntime)
    if(MSVC)
        target_compile_options(NovenHistoryPluginServiceTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME history_plugin_service COMMAND NovenHistoryPluginServiceTests)
    add_executable(NovenScanPluginServiceTests tests/ScanPluginServiceTests.cpp)
    target_link_libraries(NovenScanPluginServiceTests PRIVATE NovenPluginRuntime)
    if(MSVC)
        target_compile_options(NovenScanPluginServiceTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME scan_plugin_service COMMAND NovenScanPluginServiceTests)
    set(NOVEN_SCAN_FIXTURES)
    foreach(scan_mode RANGE 0 7)
        add_library(NovenScanFixture${scan_mode} SHARED tests/ScanPluginFixture.c)
        target_include_directories(NovenScanFixture${scan_mode} PRIVATE sdk)
        target_compile_definitions(NovenScanFixture${scan_mode} PRIVATE NOVEN_SCAN_FIXTURE_MODE=${scan_mode})
        set_target_properties(NovenScanFixture${scan_mode} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
        if(MSVC)
            target_compile_options(NovenScanFixture${scan_mode} PRIVATE /W4 /utf-8)
        endif()
        list(APPEND NOVEN_SCAN_FIXTURES "$<TARGET_FILE:NovenScanFixture${scan_mode}>")
    endforeach()
    add_executable(NovenScanPluginRuntimeTests tests/ScanPluginRuntimeTests.cpp)
    target_link_libraries(NovenScanPluginRuntimeTests PRIVATE NovenPluginRuntime)
    add_dependencies(NovenScanPluginRuntimeTests NovenPluginHost)
    foreach(scan_mode RANGE 0 7)
        add_dependencies(NovenScanPluginRuntimeTests NovenScanFixture${scan_mode})
    endforeach()
    if(MSVC)
        target_compile_options(NovenScanPluginRuntimeTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME scan_plugin_runtime COMMAND NovenScanPluginRuntimeTests "$<TARGET_FILE:NovenPluginHost>" ${NOVEN_SCAN_FIXTURES})
    add_executable(NovenScanPluginBoundaryTests tests/ScanPluginBoundaryTests.cpp)
    target_include_directories(NovenScanPluginBoundaryTests PRIVATE sdk)
    target_link_libraries(NovenScanPluginBoundaryTests PRIVATE NovenPluginRuntime)
    if(MSVC)
        target_compile_options(NovenScanPluginBoundaryTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME scan_plugin_boundary COMMAND NovenScanPluginBoundaryTests "${CMAKE_CURRENT_SOURCE_DIR}")
    set(NOVEN_HISTORY_FIXTURES)
    foreach(history_mode 0 1 2 3 4 7)
        add_library(NovenHistoryFixture${history_mode} SHARED tests/CatalogPluginFixture.c)
        target_include_directories(NovenHistoryFixture${history_mode} PRIVATE sdk)
        target_compile_definitions(NovenHistoryFixture${history_mode} PRIVATE NOVEN_HISTORY_FIXTURE NOVEN_CATALOG_FIXTURE_MODE=${history_mode})
        set_target_properties(NovenHistoryFixture${history_mode} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
        if(MSVC)
            target_compile_options(NovenHistoryFixture${history_mode} PRIVATE /W4 /utf-8)
        endif()
        list(APPEND NOVEN_HISTORY_FIXTURES "$<TARGET_FILE:NovenHistoryFixture${history_mode}>")
    endforeach()
    add_executable(NovenHistoryPluginRuntimeTests tests/HistoryPluginRuntimeTests.cpp)
    target_link_libraries(NovenHistoryPluginRuntimeTests PRIVATE NovenPluginRuntime)
    add_dependencies(NovenHistoryPluginRuntimeTests NovenPluginHost NovenHistoryFixture0 NovenHistoryFixture1 NovenHistoryFixture2 NovenHistoryFixture3 NovenHistoryFixture4 NovenHistoryFixture7)
    if(MSVC)
        target_compile_options(NovenHistoryPluginRuntimeTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME history_plugin_runtime COMMAND NovenHistoryPluginRuntimeTests "$<TARGET_FILE:NovenPluginHost>" ${NOVEN_HISTORY_FIXTURES})
    set_tests_properties(history_plugin_runtime PROPERTIES TIMEOUT 30)
    add_executable(NovenMessageDialogTests tests/MessageDialogTests.cpp src/ui/MessageDialog.cpp)
    target_include_directories(NovenMessageDialogTests PRIVATE src)
    target_compile_definitions(NovenMessageDialogTests PRIVATE UNICODE _UNICODE NOMINMAX _WIN32_WINNT=0x0A00)
    if(MSVC)
        target_compile_options(NovenMessageDialogTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    target_link_libraries(NovenMessageDialogTests PRIVATE user32 gdi32 dwmapi comctl32)
    add_test(NAME message_dialog COMMAND NovenMessageDialogTests)
    set_tests_properties(message_dialog PROPERTIES TIMEOUT 30)
    add_executable(NovenAppSettingsTests tests/AppSettingsTests.cpp)
    target_include_directories(NovenAppSettingsTests PRIVATE src)
    target_compile_definitions(NovenAppSettingsTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(NovenAppSettingsTests PRIVATE /utf-8)
    add_test(NAME app_settings COMMAND NovenAppSettingsTests)
    add_executable(NovenEventsPageTests tests/EventsPageTests.cpp src/ui/EventsPage.cpp src/events/EventBrowser.cpp
        src/events/EventCatalog.cpp src/data/ItemCatalog.cpp src/ui/localization/LocalizationService.cpp)
    target_include_directories(NovenEventsPageTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(NovenEventsPageTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(NovenEventsPageTests PRIVATE d2d1 dwrite imm32)
    if(MSVC)
        target_compile_options(NovenEventsPageTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME events_page COMMAND NovenEventsPageTests "${CMAKE_CURRENT_SOURCE_DIR}/assets/i18n")
    add_executable(NovenEventBrowserTests tests/EventBrowserTests.cpp src/events/EventBrowser.cpp src/events/EventCatalog.cpp)
    target_include_directories(NovenEventBrowserTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    if(MSVC)
        target_compile_options(NovenEventBrowserTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME event_browser COMMAND NovenEventBrowserTests)
    add_executable(NovenEventTranslationTests tests/EventTranslationTests.cpp src/events/EventTranslation.cpp src/events/EventHttp.cpp)
    target_include_directories(NovenEventTranslationTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(NovenEventTranslationTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(NovenEventTranslationTests PRIVATE winhttp)
    if(MSVC)
        target_compile_options(NovenEventTranslationTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME event_translation COMMAND NovenEventTranslationTests)
    add_executable(NovenEventServiceTests tests/EventServiceTests.cpp src/events/EventService.cpp src/events/EventCache.cpp src/events/EventTranslation.cpp
        src/events/EventCatalog.cpp src/events/EventHttp.cpp src/events/OfficialEventSource.cpp src/events/WikiEventSource.cpp src/events/EventEnrichment.cpp
        src/events/TarkovDevEventEnricher.cpp src/events/TarkovChangesEnricher.cpp src/data/ItemCatalog.cpp src/data/TaskCatalog.cpp src/data/MapCatalog.cpp)
    target_include_directories(NovenEventServiceTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(NovenEventServiceTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(NovenEventServiceTests PRIVATE winhttp)
    if(MSVC)
        target_compile_options(NovenEventServiceTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME event_service COMMAND NovenEventServiceTests)
    add_executable(NovenEventChangesTests tests/EventChangesTests.cpp src/events/TarkovChangesEnricher.cpp src/events/EventHttp.cpp src/events/EventCatalog.cpp)
    target_include_directories(NovenEventChangesTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(NovenEventChangesTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(NovenEventChangesTests PRIVATE winhttp)
    if(MSVC)
        target_compile_options(NovenEventChangesTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME event_changes COMMAND NovenEventChangesTests)
    add_executable(NovenEventEnricherTests tests/EventEnricherTests.cpp src/events/TarkovDevEventEnricher.cpp src/events/EventCatalog.cpp
        src/data/ItemCatalog.cpp src/data/TaskCatalog.cpp src/data/MapCatalog.cpp)
    target_include_directories(NovenEventEnricherTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(NovenEventEnricherTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(NovenEventEnricherTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME event_enricher COMMAND NovenEventEnricherTests "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    add_executable(NovenWikiEventSourceTests tests/WikiEventSourceTests.cpp src/events/WikiEventSource.cpp src/events/EventHttp.cpp src/events/EventCatalog.cpp)
    target_include_directories(NovenWikiEventSourceTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(NovenWikiEventSourceTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(NovenWikiEventSourceTests PRIVATE winhttp)
    if(MSVC)
        target_compile_options(NovenWikiEventSourceTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME wiki_event_source COMMAND NovenWikiEventSourceTests)
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
    add_test(NAME raid_event_parser COMMAND NovenRaidEventParserTests "${CMAKE_CURRENT_SOURCE_DIR}/assets/data/map_maps.tsv")
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
    add_executable(NovenPageRegistryTests tests/PageRegistryTests.cpp)
    target_include_directories(NovenPageRegistryTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    if(MSVC)
        target_compile_options(NovenPageRegistryTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME page_registry COMMAND NovenPageRegistryTests)

    add_executable(NovenPluginManifestTests tests/PluginManifestTests.cpp src/plugins/PluginManifest.cpp)
    target_include_directories(NovenPluginManifestTests PRIVATE src)
    target_compile_definitions(NovenPluginManifestTests PRIVATE UNICODE _UNICODE NOMINMAX)
    if(MSVC)
        target_compile_options(NovenPluginManifestTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME plugin_manifest COMMAND NovenPluginManifestTests)
    add_executable(NovenPluginProtocolTests tests/PluginProtocolTests.cpp src/plugins/PluginProtocol.cpp src/plugins/PluginManifest.cpp src/plugins/PluginUiDocument.cpp src/plugins/CatalogData.cpp src/plugins/PluginScanData.cpp src/plugins/PluginStorageData.cpp)
    target_include_directories(NovenPluginProtocolTests PRIVATE src)
    target_compile_definitions(NovenPluginProtocolTests PRIVATE UNICODE _UNICODE NOMINMAX)
    if(MSVC)
        target_compile_options(NovenPluginProtocolTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME plugin_protocol COMMAND NovenPluginProtocolTests)
    add_executable(NovenPluginHostTests tests/PluginHostTests.cpp)
    set(NOVEN_ABI_FIXTURES)
    foreach(fixture_mode RANGE 0 14)
        add_library(NovenPluginFixture${fixture_mode} SHARED tests/PluginNativeFixture.cpp)
        target_include_directories(NovenPluginFixture${fixture_mode} PRIVATE sdk)
        target_compile_definitions(NovenPluginFixture${fixture_mode} PRIVATE NOVEN_FIXTURE_MODE=${fixture_mode})
        if(MSVC)
            target_compile_options(NovenPluginFixture${fixture_mode} PRIVATE /W4 /utf-8 /EHsc)
        endif()
        list(APPEND NOVEN_ABI_FIXTURES "$<TARGET_FILE:NovenPluginFixture${fixture_mode}>")
    endforeach()
    add_executable(NovenPluginNativeHostTests tests/PluginNativeHostTests.cpp)
    target_link_libraries(NovenPluginNativeHostTests PRIVATE NovenPluginTransport)
    add_dependencies(NovenPluginNativeHostTests NovenPluginHost)
    foreach(fixture_mode RANGE 0 14)
        add_dependencies(NovenPluginNativeHostTests NovenPluginFixture${fixture_mode})
    endforeach()
    if(MSVC)
        target_compile_options(NovenPluginNativeHostTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME plugin_native_host COMMAND NovenPluginNativeHostTests "$<TARGET_FILE:NovenPluginHost>" ${NOVEN_ABI_FIXTURES})
    add_executable(NovenPluginNativeRuntimeTests tests/PluginNativeRuntimeTests.cpp)
    target_link_libraries(NovenPluginNativeRuntimeTests PRIVATE NovenPluginRuntime)
    add_dependencies(NovenPluginNativeRuntimeTests NovenPluginNativeHostTests)
    if(MSVC)
        target_compile_options(NovenPluginNativeRuntimeTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME plugin_native_runtime COMMAND NovenPluginNativeRuntimeTests "$<TARGET_FILE:NovenPluginHost>" ${NOVEN_ABI_FIXTURES})
    add_executable(NovenPluginRuntimeControllerTests tests/PluginRuntimeControllerTests.cpp)
    target_link_libraries(NovenPluginRuntimeControllerTests PRIVATE NovenPluginRuntime)
    add_dependencies(NovenPluginRuntimeControllerTests NovenPluginHost NovenPluginFixture0 NovenPluginFixture13)
    if(MSVC)
        target_compile_options(NovenPluginRuntimeControllerTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME plugin_runtime_controller COMMAND NovenPluginRuntimeControllerTests "$<TARGET_FILE:NovenPluginHost>" "$<TARGET_FILE:NovenPluginFixture0>" "$<TARGET_FILE:NovenPluginFixture13>")
    if(NOVEN_BUILD_PLUGIN_EXAMPLES)
        add_executable(NovenScanPluginSampleTests tests/ScanPluginSampleTests.cpp)
        target_link_libraries(NovenScanPluginSampleTests PRIVATE NovenPluginRuntime)
        add_dependencies(NovenScanPluginSampleTests NovenPluginHost NovenScanPlugin)
        if(MSVC)
            target_compile_options(NovenScanPluginSampleTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
        endif()
        add_test(NAME scan_plugin_sample COMMAND NovenScanPluginSampleTests "$<TARGET_FILE:NovenPluginHost>" "$<TARGET_FILE:NovenScanPlugin>" "${CMAKE_CURRENT_SOURCE_DIR}/examples/scan-plugin/manifest.json")
        add_executable(NovenCatalogPluginSampleTests tests/CatalogPluginSampleTests.cpp src/data/ItemCatalog.cpp src/data/TaskCatalog.cpp src/data/MapCatalog.cpp)
        target_link_libraries(NovenCatalogPluginSampleTests PRIVATE NovenPluginRuntime)
        add_dependencies(NovenCatalogPluginSampleTests NovenPluginHost NovenCatalogPlugin)
        if(MSVC)
            target_compile_options(NovenCatalogPluginSampleTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
        endif()
        add_test(NAME catalog_plugin_sample COMMAND NovenCatalogPluginSampleTests "$<TARGET_FILE:NovenPluginHost>" "$<TARGET_FILE:NovenCatalogPlugin>" "${CMAKE_CURRENT_SOURCE_DIR}/examples/catalog-plugin/manifest.json" "${CMAKE_CURRENT_SOURCE_DIR}/assets")
        set_tests_properties(catalog_plugin_sample PROPERTIES TIMEOUT 30)
        add_executable(NovenHistoryPluginSampleTests tests/HistoryPluginSampleTests.cpp)
        target_link_libraries(NovenHistoryPluginSampleTests PRIVATE NovenPluginRuntime)
        add_dependencies(NovenHistoryPluginSampleTests NovenPluginHost NovenHistoryPlugin)
        if(MSVC)
            target_compile_options(NovenHistoryPluginSampleTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
        endif()
        add_test(NAME history_plugin_sample COMMAND NovenHistoryPluginSampleTests "$<TARGET_FILE:NovenPluginHost>" "$<TARGET_FILE:NovenHistoryPlugin>" "${CMAKE_CURRENT_SOURCE_DIR}/examples/history-plugin/manifest.json")
        set_tests_properties(history_plugin_sample PROPERTIES TIMEOUT 30)
        add_executable(NovenPluginSampleTests tests/PluginSampleTests.cpp)
        target_link_libraries(NovenPluginSampleTests PRIVATE NovenPluginRuntime)
        add_dependencies(NovenPluginSampleTests NovenPluginHost NovenHelloPlugin)
        if(MSVC)
            target_compile_options(NovenPluginSampleTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
        endif()
        add_test(NAME plugin_sample COMMAND NovenPluginSampleTests "$<TARGET_FILE:NovenPluginHost>" "$<TARGET_FILE:NovenHelloPlugin>" "${CMAKE_CURRENT_SOURCE_DIR}/examples/hello-plugin/manifest.json")
    endif()
    add_executable(NovenPluginPagesTests tests/PluginPagesTests.cpp)
    target_link_libraries(NovenPluginPagesTests PRIVATE NovenPluginRuntime)
    if(MSVC)
        target_compile_options(NovenPluginPagesTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME plugin_pages COMMAND NovenPluginPagesTests)
    add_executable(NovenPluginPageViewTests tests/PluginPageViewTests.cpp src/ui/PluginPageView.cpp src/ui/localization/LocalizationService.cpp)
    target_link_libraries(NovenPluginPageViewTests PRIVATE NovenPluginRuntime d2d1 dwrite)
    if(MSVC)
        target_compile_options(NovenPluginPageViewTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME plugin_page_view COMMAND NovenPluginPageViewTests)
    add_executable(NovenPluginUiDocumentTests tests/PluginUiDocumentTests.cpp)
    add_executable(NovenPluginAbiHeaderTests tests/PluginAbiHeaderTests.c)
    target_include_directories(NovenPluginAbiHeaderTests PRIVATE sdk)
    set_target_properties(NovenPluginAbiHeaderTests PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
    if(MSVC)
        target_compile_options(NovenPluginAbiHeaderTests PRIVATE /W4 /utf-8)
    endif()
    add_test(NAME plugin_abi_header COMMAND NovenPluginAbiHeaderTests)
    target_link_libraries(NovenPluginUiDocumentTests PRIVATE NovenPluginTransport)
    if(MSVC)
        target_compile_options(NovenPluginUiDocumentTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME plugin_ui_document COMMAND NovenPluginUiDocumentTests)
    target_link_libraries(NovenPluginHostTests PRIVATE NovenPluginTransport shell32)
    add_dependencies(NovenPluginHostTests NovenPluginHost)
    if(MSVC)
        target_compile_options(NovenPluginHostTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME plugin_host COMMAND NovenPluginHostTests "$<TARGET_FILE:NovenPluginHost>")
    add_executable(NovenPluginRuntimeTests tests/PluginRuntimeTests.cpp)
    add_executable(NovenPluginStateStoreTests tests/PluginStateStoreTests.cpp)
    add_executable(NovenPluginNativePathTests tests/PluginNativePathTests.cpp)
    target_link_libraries(NovenPluginNativePathTests PRIVATE NovenPluginTransport)
    if(MSVC)
        target_compile_options(NovenPluginNativePathTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME plugin_native_path COMMAND NovenPluginNativePathTests)
    target_link_libraries(NovenPluginStateStoreTests PRIVATE NovenPluginRuntime)
    if(MSVC)
        target_compile_options(NovenPluginStateStoreTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME plugin_state_store COMMAND NovenPluginStateStoreTests)
    target_link_libraries(NovenPluginRuntimeTests PRIVATE NovenPluginRuntime)
    add_dependencies(NovenPluginRuntimeTests NovenPluginHost)
    if(MSVC)
        target_compile_options(NovenPluginRuntimeTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME plugin_runtime COMMAND NovenPluginRuntimeTests "$<TARGET_FILE_DIR:NovenPluginHost>")
    add_executable(NovenPluginHostTestPeer WIN32 tests/PluginHostTestPeer.cpp)
    target_link_libraries(NovenPluginHostTestPeer PRIVATE NovenPluginTransport shell32)
    add_executable(NovenPluginRuntimeFaultTests tests/PluginRuntimeFaultTests.cpp)
    target_link_libraries(NovenPluginRuntimeFaultTests PRIVATE NovenPluginRuntime)
    add_dependencies(NovenPluginRuntimeFaultTests NovenPluginHost NovenPluginHostTestPeer)
    if(MSVC)
        target_compile_options(NovenPluginHostTestPeer PRIVATE /W4 /permissive- /utf-8 /EHsc)
        target_compile_options(NovenPluginRuntimeFaultTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME plugin_runtime_faults COMMAND NovenPluginRuntimeFaultTests "$<TARGET_FILE:NovenPluginHostTestPeer>")
    add_executable(NovenPluginExecutionBoundaryTests tests/PluginExecutionBoundaryTests.cpp src/plugins/PluginDiscovery.cpp src/ui/PluginsPage.cpp src/ui/localization/LocalizationService.cpp)
    target_link_libraries(NovenPluginExecutionBoundaryTests PRIVATE NovenPluginRuntime d2d1 dwrite imm32)
    if(MSVC)
        target_compile_options(NovenPluginExecutionBoundaryTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME plugin_execution_boundary COMMAND NovenPluginExecutionBoundaryTests "${CMAKE_CURRENT_SOURCE_DIR}/assets/i18n")
    add_executable(NovenPluginDiscoveryTests tests/PluginDiscoveryTests.cpp src/plugins/PluginDiscovery.cpp src/plugins/PluginManifest.cpp)
    target_include_directories(NovenPluginDiscoveryTests PRIVATE src)
    target_compile_definitions(NovenPluginDiscoveryTests PRIVATE UNICODE _UNICODE NOMINMAX)
    if(MSVC)
        target_compile_options(NovenPluginDiscoveryTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME plugin_discovery COMMAND NovenPluginDiscoveryTests)
    add_executable(NovenPluginsPageTests tests/PluginsPageTests.cpp src/ui/PluginsPage.cpp src/plugins/PluginManifest.cpp src/ui/localization/LocalizationService.cpp)
    target_include_directories(NovenPluginsPageTests PRIVATE src)
    target_compile_definitions(NovenPluginsPageTests PRIVATE UNICODE _UNICODE NOMINMAX)
    if(MSVC)
        target_compile_options(NovenPluginsPageTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    target_link_libraries(NovenPluginsPageTests PRIVATE NovenPluginRuntime d2d1 dwrite imm32)
    add_test(NAME plugins_page COMMAND NovenPluginsPageTests "${CMAKE_CURRENT_SOURCE_DIR}/assets/i18n")
    add_executable(NovenSidebarLayoutTests tests/SidebarLayoutTests.cpp)
    target_include_directories(NovenSidebarLayoutTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    if(MSVC)
        target_compile_options(NovenSidebarLayoutTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME sidebar_layout COMMAND NovenSidebarLayoutTests)

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
        src/ui/PluginPageView.cpp
        tests/LocalizationUiTests.cpp
        src/ui/MainWindowUi.cpp src/ui/PageHost.cpp src/ui/Sidebar.cpp
        src/ui/EventsPage.cpp src/events/EventBrowser.cpp src/events/EventCatalog.cpp
        src/ui/RaidHistoryPage.cpp src/raid/RaidHistoryBrowser.cpp src/ui/PluginsPage.cpp
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
    target_link_libraries(NovenLocalizationUiTests PRIVATE NovenPluginRuntime d2d1 dwrite winhttp windowscodecs ole32 windowsapp imm32 shell32)
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
