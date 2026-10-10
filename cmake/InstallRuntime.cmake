# install() 是唯一运行包来源，不复制构建目录。
# install() is the sole runtime payload source, never the build directory.
if(NOVEN_MARKETPLACE_REVIEW_FIXTURE OR NOVEN_RESOURCE_REVIEW_FIXTURE)
    install(CODE "message(FATAL_ERROR \"Review fixtures cannot be installed as a production payload\")" COMPONENT Runtime)
    return()
endif()
install(CODE "if(NOT CMAKE_INSTALL_CONFIG_NAME STREQUAL \"Release\")\nmessage(FATAL_ERROR \"Runtime packaging requires Release\")\nendif()" COMPONENT Runtime)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/noven-installed.layout" "${CMAKE_CURRENT_BINARY_DIR}/packaging/noven-installed.layout" COPYONLY)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/package-version.txt" "${PROJECT_VERSION}")
install(TARGETS NovenTarkovSupport NovenPluginHost RUNTIME DESTINATION . COMPONENT Runtime)
install(FILES "${ONNXRUNTIME_DLL}" "${CMAKE_CURRENT_BINARY_DIR}/packaging/noven-installed.layout" "${CMAKE_CURRENT_SOURCE_DIR}/NOTICE.md" DESTINATION . COMPONENT Runtime)
install(FILES "${TEXT_DETECTOR_MODEL}" "${TEXT_RECOGNIZER_MODEL}" "${TEXT_RECOGNIZER_DICTIONARY}" DESTINATION assets/models COMPONENT Runtime)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/data/" DESTINATION assets/data COMPONENT Runtime FILES_MATCHING PATTERN "*.tsv" PATTERN "*.json")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/i18n/" DESTINATION assets/i18n COMPONENT Runtime FILES_MATCHING PATTERN "*.json")
# 可选地图由资源服务管理；Core 仅保留静态目录和来源记录，不带图片或缩放包。
# Optional maps belong to ResourceService; Core retains catalog/provenance, not imagery or zoom packs.
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/docs/MAP_ATTRIBUTION.md" DESTINATION docs COMPONENT Runtime)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/maps/interchange/SOURCE.md" DESTINATION docs/maps/interchange COMPONENT Runtime)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/maps/icons/SOURCE.md" "${CMAKE_CURRENT_SOURCE_DIR}/assets/maps/icons/LICENSE.tarkov-dev.txt" DESTINATION docs/maps/icons COMPONENT Runtime)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime/LICENSE" DESTINATION docs RENAME LICENSE.onnxruntime.txt COMPONENT Runtime)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime/LICENSE" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime/ThirdPartyNotices.txt" DESTINATION licenses/onnxruntime COMPONENT Runtime)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/licenses/paddleocr/LICENSE-2.0.txt" DESTINATION licenses/paddleocr COMPONENT Runtime)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/licenses/README.md" DESTINATION licenses COMPONENT Runtime)
