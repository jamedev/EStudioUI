# Pin the complete upstream archive, including the permissive Loki notice.
vcpkg_download_distfile(ARCHIVE
    URLS "https://codeload.github.com/nucleic/kiwi/tar.gz/refs/tags/1.4.8"
    FILENAME "nucleic-kiwi-1.4.8.tar.gz"
    SHA512 487bfd8c59b85b67724da81e2c758e2800b4bf8e76929fc7ea75fe03a3759cc8a57943c8d8940b8d5e75d6e748e236250129bc929303e999a5431f4212b753f2
)
vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}")
file(INSTALL "${SOURCE_PATH}/kiwi" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE" "${SOURCE_PATH}/kiwi/AssocVector.h")
