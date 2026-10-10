# 显式小范围测试发布配置；仅公共材料，不影响默认离线构建或地图下载。
# Explicit external beta cache preset; public material only, no map download configuration.
set(NOVEN_UPDATE_SOURCE_ROOT "https://raw.githubusercontent.com/lingyunalingyun/Noven-Update-Channel/main/stable/" CACHE STRING "Approved beta update root")
set(NOVEN_UPDATE_KEY_ID "noven-stable-2026" CACHE STRING "Operator update key identity")
set(NOVEN_UPDATE_PUBLIC_KEY_HEX "4543533120000000de2e4fa1b6df2f744c5ae1132fd0c6b005d4c48fd77240ac7bc168de2b61ff78514fc44af8adb517627dddd45076078f287850adf9c87d52dc8b71ab4e583af7" CACHE STRING "CNG P-256 public blob")
