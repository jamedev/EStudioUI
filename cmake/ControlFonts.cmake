# Demo/test fonts only. Runtime library consumers supply their own font resources.
set(ESTUDIOUI_CONTROL_FONT_DIR "${CMAKE_CURRENT_BINARY_DIR}/control-fonts" CACHE PATH
    "Directory containing the pinned demo/test fallback fonts")
file(MAKE_DIRECTORY "${ESTUDIOUI_CONTROL_FONT_DIR}")
set(control_font_revision "45e3d5269e4a17d823f6d84470bc084dc5ae46f6")
set(control_font "${ESTUDIOUI_CONTROL_FONT_DIR}/NotoSansArabic.ttf")
if(EXISTS "${control_font}")
    file(SHA256 "${control_font}" control_font_hash)
else()
    set(control_font_hash "")
endif()
if(NOT control_font_hash STREQUAL "63111b5b2e074dd48cc67692e0a2726d86ee94c1c37fe8598257b7b4e87e869e")
    message(STATUS "Fetching pinned NotoSansArabic demo/test font")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/google/fonts/${control_font_revision}/ofl/notosansarabic/NotoSansArabic%5Bwdth%2Cwght%5D.ttf"
        "${control_font}" EXPECTED_HASH SHA256=63111b5b2e074dd48cc67692e0a2726d86ee94c1c37fe8598257b7b4e87e869e TLS_VERIFY ON)
endif()
set(control_font "${ESTUDIOUI_CONTROL_FONT_DIR}/NotoSansDevanagari.ttf")
if(EXISTS "${control_font}")
    file(SHA256 "${control_font}" control_font_hash)
else()
    set(control_font_hash "")
endif()
if(NOT control_font_hash STREQUAL "14ec4af41f27482216d1c2229f417ff9b1425e1babb014e57d1d40d03229853e")
    message(STATUS "Fetching pinned NotoSansDevanagari demo/test font")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/google/fonts/${control_font_revision}/ofl/notosansdevanagari/NotoSansDevanagari%5Bwdth%2Cwght%5D.ttf"
        "${control_font}" EXPECTED_HASH SHA256=14ec4af41f27482216d1c2229f417ff9b1425e1babb014e57d1d40d03229853e TLS_VERIFY ON)
endif()
set(control_font "${ESTUDIOUI_CONTROL_FONT_DIR}/NotoSansHebrew.ttf")
if(EXISTS "${control_font}")
    file(SHA256 "${control_font}" control_font_hash)
else()
    set(control_font_hash "")
endif()
if(NOT control_font_hash STREQUAL "7ef36a2c3593758cdb622e1bdef4f84523e92fbc3ccc667438dd80ff54c2de88")
    message(STATUS "Fetching pinned NotoSansHebrew demo/test font")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/google/fonts/${control_font_revision}/ofl/notosanshebrew/NotoSansHebrew%5Bwdth%2Cwght%5D.ttf"
        "${control_font}" EXPECTED_HASH SHA256=7ef36a2c3593758cdb622e1bdef4f84523e92fbc3ccc667438dd80ff54c2de88 TLS_VERIFY ON)
endif()
set(control_font "${ESTUDIOUI_CONTROL_FONT_DIR}/NotoSansItalic.ttf")
if(EXISTS "${control_font}")
    file(SHA256 "${control_font}" control_font_hash)
else()
    set(control_font_hash "")
endif()
if(NOT control_font_hash STREQUAL "58e6e0ebd1931b29a365aa2d3e2ee9a9e831a3af7cf3ad1462d4e72154f0b291")
    message(STATUS "Fetching pinned NotoSansItalic demo/test font")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/google/fonts/${control_font_revision}/ofl/notosans/NotoSans-Italic%5Bwdth%2Cwght%5D.ttf"
        "${control_font}" EXPECTED_HASH SHA256=58e6e0ebd1931b29a365aa2d3e2ee9a9e831a3af7cf3ad1462d4e72154f0b291 TLS_VERIFY ON)
endif()
