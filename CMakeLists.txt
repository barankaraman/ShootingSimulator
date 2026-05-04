cmake_minimum_required(VERSION 3.20)
project(IronManShooter)

set(CMAKE_CXX_STANDARD 17)

# ── Bağımlılıkları bul ────────────────────────────────────────
# macOS:   brew install glfw glew glm assimp
# Windows: vcpkg install glfw3 glew glm assimp (x64-windows)
find_package(glfw3   REQUIRED)
find_package(GLEW    REQUIRED)
find_package(glm     REQUIRED)
find_package(assimp  REQUIRED)

if(APPLE)
    # ── GLEW düzeltmesi ───────────────────────────────────────────
    # Homebrew'un glew-targets.cmake dosyası INTERFACE_LINK_LIBRARIES içinde
    # '/Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk/.../OpenGL.framework'
    # yolunu sert kodlamış. Bu SDK, Command Line Tools'da bulunmuyor.
    # Çözüm: GLEW::glew hedefindeki bu referansı temizleyip kendi framework
    # bağlantımızı kullanıyoruz.
    if(TARGET GLEW::glew)
        set_target_properties(GLEW::glew PROPERTIES INTERFACE_LINK_LIBRARIES "")
    endif()
    if(TARGET GLEW::GLEW)
        set_target_properties(GLEW::GLEW PROPERTIES INTERFACE_LINK_LIBRARIES "")
    endif()

    # Assimp da aynı sorunu taşıyor: INTERFACE_LINK_LIBRARIES içinde
    # MacOSX26.sdk/libz.tbd sert kodlu. Temizleyip -lz ile değiştiriyoruz.
    if(TARGET assimp::assimp)
        set_target_properties(assimp::assimp PROPERTIES INTERFACE_LINK_LIBRARIES "")
    endif()
endif()

# ── Executable ───────────────────────────────────────────────
add_executable(IronManShooter
    src/main.cpp
    src/stb_impl.cpp
)

# ── Bağlantı ─────────────────────────────────────────────────
target_link_libraries(IronManShooter PRIVATE
    glfw
    GLEW::GLEW
    glm::glm
    assimp::assimp
)

if(WIN32)
    target_link_libraries(IronManShooter PRIVATE
        opengl32
        ws2_32       # Winsock2
        winmm        # PlaySound
    )
    # MSVC: M_PI ve benzeri sabitler için
    target_compile_definitions(IronManShooter PRIVATE _USE_MATH_DEFINES NOMINMAX)
elseif(APPLE)
    target_link_libraries(IronManShooter PRIVATE
        "-framework OpenGL"
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
        "-lz"
    )
    # macOS OpenGL deprecation uyarısını sustur
    target_compile_definitions(IronManShooter PRIVATE GL_SILENCE_DEPRECATION)
endif()

set_target_properties(IronManShooter PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
# CLion için: Run > Edit Configurations > Working directory = $ProjectFileDir$
