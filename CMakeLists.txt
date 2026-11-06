cmake_minimum_required(VERSION 3.16)

project(openkp LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Default build type
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose build type" FORCE)
endif()

# Configuration Options
option(BUILD_CLIENT "Build OpenKP client" ON)
option(ENABLE_SDL2 "Enable SDL2 video and audio backend" ON)
option(ENABLE_WAYLAND "Enable Wayland video backend (Linux)" ON)
option(ENABLE_X11 "Enable X11 video backend (Linux)" ON)
option(ENABLE_OPENAL "Enable OpenAL sound" ON)
option(ENABLE_CURL "Enable HTTP downloads via libcurl" ON)
option(ENABLE_PNG "Enable PNG texture loading" ON)
option(ENABLE_JPEG "Enable JPEG texture loading" ON)
option(ENABLE_ZLIB "Enable zlib compression" ON)
option(ENABLE_FFMPEG "Enable FFmpeg cinematic and OGG playback" ON)
option(ENABLE_DEBUG "Enable renderer debugging features" ON)
option(ENABLE_TESTS "Enable internal self-tests" ON)
option(ENABLE_MVD "Enable MVD demo client/server routines" ON)
option(ENABLE_SAVEGAMES "Enable save games support" ON)

# Kingpin / OpenKP directories
set(BASEGAME "main" CACHE STRING "Base game directory name")
set(DEFGAME "" CACHE STRING "Default mod game directory")
set(DATADIR "." CACHE STRING "Read-only data path")
set(LIBDIR "." CACHE STRING "Library path")
set(HOMEDIR "" CACHE STRING "Per-user config/data directory")
set(VID_GEOMETRY "1920x1080" CACHE STRING "Default window resolution")
set(VID_MODELIST "desktop 1280x720 1600x900 1920x1080 2560x1440 3440x1440 3840x2160" CACHE STRING "Video modes list")

# Detect Version & Revision from Git
execute_process(
    COMMAND git rev-list --count HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_REV
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

if(GIT_REV AND GIT_SHA)
    set(OPENKP_VERSION "r${GIT_REV}~${GIT_SHA}")
    set(OPENKP_REVISION ${GIT_REV})
else()
    set(OPENKP_VERSION "r3834~unknown")
    set(OPENKP_REVISION 3834)
endif()

message(STATUS "Configuring OpenKP ${OPENKP_VERSION}")

# Target architecture & OS detection
if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(BUILDSTRING "Win64")
        set(CPUSTRING "x86_64")
    else()
        set(BUILDSTRING "Win32")
        set(CPUSTRING "x86")
    endif()
else()
    set(BUILDSTRING "${CMAKE_SYSTEM_NAME}")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64)")
        set(CPUSTRING "x86_64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "(i[3-6]86|x86)")
        set(CPUSTRING "i386")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "(aarch64|arm64)")
        set(CPUSTRING "arm64")
    else()
        set(CPUSTRING "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
endif()

# Endianness
include(TestBigEndian)
test_big_endian(IS_BIG_ENDIAN)
if(IS_BIG_ENDIAN)
    set(USE_BIG_ENDIAN 1)
    set(USE_LITTLE_ENDIAN 0)
else()
    set(USE_BIG_ENDIAN 0)
    set(USE_LITTLE_ENDIAN 1)
endif()

# Check system functions
include(CheckSymbolExists)
set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
check_symbol_exists(memccpy "string.h" HAVE_MEMCCPY)
check_symbol_exists(strchrnul "string.h" HAVE_STRCHRNUL)
check_symbol_exists(strnlen "string.h" HAVE_STRNLEN)
check_symbol_exists(linux/errqueue.h "" HAVE_LINUX_ERRQUEUE_H)

# Find Packages & Dependencies
include(FindPackageHandleStandardArgs)
find_package(PkgConfig QUIET)
find_package(Threads REQUIRED)

set(CLIENT_LIBS "")
set(EXTRA_CLIENT_SRC "")
set(TEXTURE_FORMATS "tga")
set(USE_TGA 1)

# ZLIB
if(ENABLE_ZLIB)
    find_package(ZLIB QUIET)
    if(ZLIB_FOUND)
        set(USE_ZLIB 1)
        list(APPEND CLIENT_LIBS ZLIB::ZLIB)
    endif()
endif()

# PNG
if(ENABLE_PNG)
    find_package(PNG QUIET)
    if(PNG_FOUND)
        set(USE_PNG 1)
        string(APPEND TEXTURE_FORMATS " png")
        list(APPEND CLIENT_LIBS PNG::PNG)
    endif()
endif()

# JPEG
if(ENABLE_JPEG)
    find_package(JPEG QUIET)
    if(JPEG_FOUND)
        set(USE_JPG 1)
        string(APPEND TEXTURE_FORMATS " jpg")
        list(APPEND CLIENT_LIBS JPEG::JPEG)
    endif()
endif()

set(R_TEXTURE_FORMATS "${TEXTURE_FORMATS}")

# CURL
if(ENABLE_CURL)
    find_package(CURL QUIET)
    if(CURL_FOUND)
        set(USE_CURL 1)
        list(APPEND EXTRA_CLIENT_SRC src/client/http.c)
        list(APPEND CLIENT_LIBS CURL::libcurl)
    endif()
endif()

# OpenAL
if(ENABLE_OPENAL)
    find_package(OpenAL QUIET)
    if(OPENAL_FOUND)
        set(USE_OPENAL 1)
        list(APPEND EXTRA_CLIENT_SRC src/client/sound/al.c src/client/sound/qal.c)
        list(APPEND CLIENT_LIBS OpenAL::OpenAL)
    endif()
endif()

# SDL2
if(ENABLE_SDL2)
    find_package(SDL2 QUIET)
    if(NOT SDL2_FOUND AND PKG_CONFIG_FOUND)
        pkg_check_modules(SDL2 QUIET sdl2)
    endif()
    if(TARGET SDL2::SDL2)
        set(USE_SDL_ENABLED 1)
        list(APPEND CLIENT_LIBS SDL2::SDL2)
    elseif(SDL2_FOUND)
        set(USE_SDL_ENABLED 1)
        list(APPEND CLIENT_LIBS ${SDL2_LIBRARIES})
        include_directories(${SDL2_INCLUDE_DIRS})
    endif()
endif()

# Software Sound (DMA)
set(USE_SNDDMA 1)
list(APPEND EXTRA_CLIENT_SRC src/client/sound/dma.c)
if(USE_SDL_ENABLED)
    list(APPEND EXTRA_CLIENT_SRC src/unix/sound/sdl.c)
endif()

# FFmpeg
if(ENABLE_FFMPEG AND PKG_CONFIG_FOUND)
    pkg_check_modules(FFMPEG QUIET libavcodec>=59.37.100 libavformat>=59.27.100 libavutil>=57.28.100 libswresample>=4.7.100 libswscale>=6.7.100)
    if(FFMPEG_FOUND)
        set(USE_AVCODEC 1)
        list(APPEND EXTRA_CLIENT_SRC src/client/cin.c src/client/sound/ogg.c)
        list(APPEND CLIENT_LIBS ${FFMPEG_LIBRARIES})
        include_directories(${FFMPEG_INCLUDE_DIRS})
    endif()
endif()

# Linux-specific Video: Wayland & X11
if(UNIX AND NOT APPLE)
    list(APPEND CLIENT_LIBS m rt dl)

    # X11
    if(ENABLE_X11 AND PKG_CONFIG_FOUND)
        pkg_check_modules(X11_PKGS QUIET x11 xi gl)
        if(X11_PKGS_FOUND)
            set(USE_X11 1)
            list(APPEND EXTRA_CLIENT_SRC src/unix/video/x11.c src/unix/video/keytables/evdev.c src/unix/video/keytables/at.c)
            list(APPEND CLIENT_LIBS ${X11_PKGS_LIBRARIES})
            include_directories(${X11_PKGS_INCLUDE_DIRS})

            pkg_check_modules(XRANDR QUIET xrandr)
            if(XRANDR_FOUND)
                set(USE_XRANDR 1)
                list(APPEND CLIENT_LIBS ${XRANDR_LIBRARIES})
            endif()
        endif()
    endif()

    # Wayland
    if(ENABLE_WAYLAND AND PKG_CONFIG_FOUND)
        pkg_check_modules(WAYLAND QUIET wayland-client>=1.18 wayland-cursor>=1.18 wayland-egl>=9.0.0 wayland-protocols>=1.15 libdecor-0>=0.1.0 egl>=1.5)
        find_program(WAYLAND_SCANNER wayland-scanner)

        if(WAYLAND_FOUND AND WAYLAND_SCANNER)
            set(USE_WAYLAND 1)
            execute_process(
                COMMAND pkg-config --variable=pkgdatadir wayland-protocols
                OUTPUT_VARIABLE WAYLAND_PROTOCOLS_DIR
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )

            set(WL_PROTO_SRCS "")
            set(WL_PROTO_HDRS "")
            set(WL_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/wayland-gen")
            file(MAKE_DIRECTORY ${WL_GEN_DIR})

            set(WL_PROTOS
                "unstable/pointer-constraints/pointer-constraints-unstable-v1.xml"
                "unstable/primary-selection/primary-selection-unstable-v1.xml"
                "unstable/relative-pointer/relative-pointer-unstable-v1.xml"
            )

            foreach(proto ${WL_PROTOS})
                get_filename_component(proto_base ${proto} NAME_WLE)
                set(proto_xml "${WAYLAND_PROTOCOLS_DIR}/${proto}")
                set(proto_c "${WL_GEN_DIR}/${proto_base}.c")
                set(proto_h "${WL_GEN_DIR}/${proto_base}.h")

                add_custom_command(
                    OUTPUT ${proto_c}
                    COMMAND ${WAYLAND_SCANNER} private-code ${proto_xml} ${proto_c}
                    DEPENDS ${proto_xml}
                    VERBATIM
                )
                add_custom_command(
                    OUTPUT ${proto_h}
                    COMMAND ${WAYLAND_SCANNER} client-header ${proto_xml} ${proto_h}
                    DEPENDS ${proto_xml}
                    VERBATIM
                )

                list(APPEND WL_PROTO_SRCS ${proto_c})
                list(APPEND WL_PROTO_HDRS ${proto_h})
            endforeach()

            list(APPEND EXTRA_CLIENT_SRC src/unix/video/wayland.c ${WL_PROTO_SRCS} ${WL_PROTO_HDRS})
            if(NOT USE_X11)
                list(APPEND EXTRA_CLIENT_SRC src/unix/video/keytables/evdev.c)
            endif()
            list(APPEND CLIENT_LIBS ${WAYLAND_LIBRARIES})
            include_directories(${WAYLAND_INCLUDE_DIRS} ${WL_GEN_DIR})
        endif()
    endif()

    if(USE_SDL_ENABLED)
        list(APPEND EXTRA_CLIENT_SRC src/unix/video/sdl.c)
    endif()

    list(APPEND EXTRA_CLIENT_SRC src/unix/system.c src/unix/hunk.c src/unix/tty.c)
    set(USE_SYSCON 1)
endif()

# Windows platform specifics
if(WIN32)
    list(APPEND EXTRA_CLIENT_SRC
        src/windows/system.c
        src/windows/hunk.c
        src/windows/client.c
        src/windows/wgl.c
        src/windows/debug.c
        src/windows/wave.c
        src/windows/dsound.c
        src/windows/res/q2pro.rc
    )
    list(APPEND CLIENT_LIBS ws2_32 shell32 ole32 opengl32 winmm)
    set(USE_DBGHELP 1)
endif()

# Common Features & Protocols
set(USE_AUTOREPLY 1)
set(USE_DEBUG ${ENABLE_DEBUG})
set(USE_FPS 0)
set(USE_GLES 0)
set(USE_ICMP 1)
set(USE_MD3 1)
set(USE_MD5 1)
set(USE_MVD_CLIENT ${ENABLE_MVD})
set(USE_MVD_SERVER ${ENABLE_MVD})
set(USE_PACKETDUP 0)
set(USE_PROTOCOL_EXTENSIONS 1)
set(USE_SAVEGAMES ${ENABLE_SAVEGAMES})
set(USE_TESTS ${ENABLE_TESTS})
set(USE_UI 1)
set(USE_CLIENT_GTV 0)
set(DEFGLPROFILE "")

if(USE_MVD_CLIENT)
    list(APPEND EXTRA_CLIENT_SRC
        src/server/mvd/client.c
        src/server/mvd/game.c
        src/server/mvd/parse.c
        src/server/mvd.c
    )
endif()

if(USE_SAVEGAMES)
    list(APPEND EXTRA_CLIENT_SRC src/server/save.c)
endif()

if(USE_TESTS)
    list(APPEND EXTRA_CLIENT_SRC src/common/tests.c)
endif()

if(USE_DEBUG)
    list(APPEND EXTRA_CLIENT_SRC src/refresh/debug.c)
endif()

# Generate config.h
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/config.h
)

# Source files
set(COMMON_SRC
    src/common/bsp.c
    src/common/cmd.c
    src/common/cmodel.c
    src/common/common.c
    src/common/crc.c
    src/common/cvar.c
    src/common/error.c
    src/common/field.c
    src/common/fifo.c
    src/common/files.c
    src/common/hash_map.c
    src/common/math.c
    src/common/mdfour.c
    src/common/msg.c
    src/common/net/chan.c
    src/common/net/net.c
    src/common/pmove/common.c
    src/common/pmove/new.c
    src/common/pmove/old.c
    src/common/prompt.c
    src/common/sizebuf.c
    src/common/utils.c
    src/common/zone.c
    src/shared/shared.c
)

set(CLIENT_SRC
    src/client/ascii.c
    src/client/console.c
    src/client/demo.c
    src/client/download.c
    src/client/effects.c
    src/client/entities.c
    src/client/input.c
    src/client/keys.c
    src/client/locs.c
    src/client/main.c
    src/client/newfx.c
    src/client/parse.c
    src/client/precache.c
    src/client/predict.c
    src/client/refresh.c
    src/client/screen.c
    src/client/sound/main.c
    src/client/sound/mem.c
    src/client/tent.c
    src/client/view.c
    src/common/async.c
    src/server/commands.c
    src/server/entities.c
    src/server/game.c
    src/server/init.c
    src/server/main.c
    src/server/send.c
    src/server/user.c
    src/server/world.c
    src/shared/m_flash.c
)

set(UI_SRC
    src/client/ui/demos.c
    src/client/ui/kpmenu.c
    src/client/ui/menu.c
    src/client/ui/playerconfig.c
    src/client/ui/playermodels.c
    src/client/ui/script.c
    src/client/ui/servers.c
    src/client/ui/ui.c
)

set(REFRESH_SRC
    src/refresh/draw.c
    src/refresh/hq2x.c
    src/refresh/images.c
    src/refresh/legacy.c
    src/refresh/main.c
    src/refresh/mesh.c
    src/refresh/models.c
    src/refresh/qgl.c
    src/refresh/shader.c
    src/refresh/sky.c
    src/refresh/state.c
    src/refresh/surf.c
    src/refresh/tess.c
    src/refresh/texture.c
    src/refresh/world.c
)

# OpenKP Executable
add_executable(openkp
    ${COMMON_SRC}
    ${CLIENT_SRC}
    ${UI_SRC}
    ${REFRESH_SRC}
    ${EXTRA_CLIENT_SRC}
)

target_include_directories(openkp PRIVATE
    inc
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_compile_definitions(openkp PRIVATE
    HAVE_CONFIG_H=1
    USE_CLIENT=1
    USE_REF=1
)

if(WIN32)
    target_compile_definitions(openkp PRIVATE _USE_MATH_DEFINES _WIN32_WINNT=0x0601)
    set_target_properties(openkp PROPERTIES WIN32_EXECUTABLE TRUE)
else()
    target_compile_definitions(openkp PRIVATE _GNU_SOURCE)
endif()

# Compiler warnings and optimization flags
if(CMAKE_C_COMPILER_ID MATCHES "(GNU|Clang)")
    target_compile_options(openkp PRIVATE
        -fvisibility=hidden
        -Werror=vla
        -Wformat-security
        -Wno-microsoft-anon-tag
        -Wpointer-arith
        -Wstrict-prototypes
        -fms-extensions
        -fno-math-errno
        -fno-trapping-math
        -fsigned-char
    )
endif()

target_link_libraries(openkp PRIVATE
    Threads::Threads
    ${CLIENT_LIBS}
)

# Installation
install(TARGETS openkp RUNTIME DESTINATION bin)
