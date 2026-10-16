# Third-party dependencies, all fetched at configure time and pinned to exact versions.

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

# Third-party code is built with its own warning levels and never with warnings-as-errors.
set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)

# ------------------------------------------------------------------------------------------------
# GLFW - windowing and input
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(USE_MSVC_RUNTIME_LIBRARY_DLL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
	GIT_REPOSITORY https://github.com/glfw/glfw.git
	GIT_TAG 3.4
	GIT_SHALLOW TRUE)

# ------------------------------------------------------------------------------------------------
# NVRHI - rendering hardware interface (Vulkan backend only)
set(NVRHI_WITH_VULKAN ON CACHE BOOL "" FORCE)
set(NVRHI_WITH_DX11 OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_DX12 OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_NVAPI OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_RTXMU OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_AFTERMATH OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_VALIDATION ON CACHE BOOL "" FORCE)
set(NVRHI_INSTALL OFF CACHE BOOL "" FORCE)
set(NVRHI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(NVRHI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(nvrhi
	GIT_REPOSITORY https://github.com/NVIDIA-RTX/NVRHI.git
	GIT_TAG 6b96fb03e07539f08327aea76c56d55f1de9d906)

# ------------------------------------------------------------------------------------------------
# Math, logging, ECS, serialization
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glm
	GIT_REPOSITORY https://github.com/g-truc/glm.git
	GIT_TAG 1.0.1
	GIT_SHALLOW TRUE)

set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(spdlog
	GIT_REPOSITORY https://github.com/gabime/spdlog.git
	GIT_TAG v1.15.3
	GIT_SHALLOW TRUE)

FetchContent_Declare(entt
	GIT_REPOSITORY https://github.com/skypjack/entt.git
	GIT_TAG v3.14.0
	GIT_SHALLOW TRUE)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_Declare(json
	URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
	URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d)

# ------------------------------------------------------------------------------------------------
# Jolt - physics
set(USE_STATIC_MSVC_RUNTIME_LIBRARY ON CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
# Jolt must be built with the same exception/RTTI settings as Ouroboros: on MSVC, _HAS_EXCEPTIONS=0 changes the
# definition of std::exception, and mixing both in one program corrupts exception messages (ODR violation).
set(CPP_EXCEPTIONS_ENABLED ON CACHE BOOL "" FORCE)
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(JoltPhysics
	GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
	GIT_TAG v5.3.0
	GIT_SHALLOW TRUE
	SOURCE_SUBDIR Build)

# ------------------------------------------------------------------------------------------------
# Header-only / non-CMake sources (SOURCE_SUBDIR points at a missing directory so nothing is added)
FetchContent_Declare(lua
	URL https://www.lua.org/ftp/lua-5.4.7.tar.gz
	URL_HASH SHA256=9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30
	SOURCE_SUBDIR _ob_no_cmake)
FetchContent_Declare(sol2
	GIT_REPOSITORY https://github.com/ThePhD/sol2.git
	GIT_TAG v3.5.0
	GIT_SHALLOW TRUE
	SOURCE_SUBDIR _ob_no_cmake)
FetchContent_Declare(imgui
	GIT_REPOSITORY https://github.com/ocornut/imgui.git
	GIT_TAG v1.91.9b-docking
	GIT_SHALLOW TRUE
	SOURCE_SUBDIR _ob_no_cmake)
FetchContent_Declare(imguizmo
	GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
	GIT_TAG 105e4fde45e4e74aae8973d98750820ef7aca7e2
	SOURCE_SUBDIR _ob_no_cmake)
FetchContent_Declare(miniaudio
	GIT_REPOSITORY https://github.com/mackron/miniaudio.git
	GIT_TAG 0.11.21
	GIT_SHALLOW TRUE
	SOURCE_SUBDIR _ob_no_cmake)
FetchContent_Declare(cgltf
	GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
	GIT_TAG v1.14
	GIT_SHALLOW TRUE
	SOURCE_SUBDIR _ob_no_cmake)
FetchContent_Declare(stb
	GIT_REPOSITORY https://github.com/nothings/stb.git
	GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
	SOURCE_SUBDIR _ob_no_cmake)
FetchContent_Declare(doctest
	GIT_REPOSITORY https://github.com/doctest/doctest.git
	GIT_TAG v2.4.11
	GIT_SHALLOW TRUE
	SOURCE_SUBDIR _ob_no_cmake)

FetchContent_MakeAvailable(glfw nvrhi glm spdlog entt json JoltPhysics
	lua sol2 imgui imguizmo miniaudio cgltf stb doctest)

# ------------------------------------------------------------------------------------------------
# Targets for the dependencies that do not ship (usable) CMake projects

# Lua
set(OB_LUA_SOURCE_DIR "${lua_SOURCE_DIR}/src")
file(GLOB OB_LUA_SOURCES CONFIGURE_DEPENDS "${OB_LUA_SOURCE_DIR}/*.c")
list(REMOVE_ITEM OB_LUA_SOURCES "${OB_LUA_SOURCE_DIR}/lua.c" "${OB_LUA_SOURCE_DIR}/luac.c")
# Lua is compiled as C++ so that Lua errors unwind with C++ exceptions (destructors run, sol2 interop is safe).
set_source_files_properties(${OB_LUA_SOURCES} PROPERTIES LANGUAGE CXX)
add_library(ob_lua STATIC ${OB_LUA_SOURCES})
target_include_directories(ob_lua SYSTEM PUBLIC "${OB_LUA_SOURCE_DIR}")
if(MSVC)
	target_compile_options(ob_lua PRIVATE /W0 /TP)
else()
	target_compile_options(ob_lua PRIVATE -w -x c++)
endif()
if(UNIX AND NOT APPLE)
	target_compile_definitions(ob_lua PRIVATE LUA_USE_LINUX)
	target_link_libraries(ob_lua PRIVATE m dl)
elseif(APPLE)
	target_compile_definitions(ob_lua PRIVATE LUA_USE_MACOSX)
endif()

# sol2
add_library(ob_sol2 INTERFACE)
target_include_directories(ob_sol2 SYSTEM INTERFACE "${sol2_SOURCE_DIR}/include")
target_link_libraries(ob_sol2 INTERFACE ob_lua)
target_compile_definitions(ob_sol2 INTERFACE SOL_ALL_SAFETIES_ON=1 SOL_PRINT_ERRORS=0 SOL_USING_CXX_LUA=1)

# Dear ImGui (docking branch) with the GLFW platform backend; rendering is done by Ouroboros' NVRHI backend
add_library(ob_imgui STATIC
	"${imgui_SOURCE_DIR}/imgui.cpp"
	"${imgui_SOURCE_DIR}/imgui_demo.cpp"
	"${imgui_SOURCE_DIR}/imgui_draw.cpp"
	"${imgui_SOURCE_DIR}/imgui_tables.cpp"
	"${imgui_SOURCE_DIR}/imgui_widgets.cpp"
	"${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp"
	"${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp")
target_include_directories(ob_imgui SYSTEM PUBLIC
	"${imgui_SOURCE_DIR}"
	"${imgui_SOURCE_DIR}/backends"
	"${imgui_SOURCE_DIR}/misc/cpp")
target_compile_definitions(ob_imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_link_libraries(ob_imgui PUBLIC glfw)

# ImGuizmo
add_library(ob_imguizmo STATIC "${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp")
target_include_directories(ob_imguizmo SYSTEM PUBLIC "${imguizmo_SOURCE_DIR}/src")
target_link_libraries(ob_imguizmo PUBLIC ob_imgui)

# Single-header libraries
add_library(ob_miniaudio INTERFACE)
target_include_directories(ob_miniaudio SYSTEM INTERFACE "${miniaudio_SOURCE_DIR}")
add_library(ob_cgltf INTERFACE)
target_include_directories(ob_cgltf SYSTEM INTERFACE "${cgltf_SOURCE_DIR}")
add_library(ob_stb INTERFACE)
target_include_directories(ob_stb SYSTEM INTERFACE "${stb_SOURCE_DIR}")
add_library(ob_doctest INTERFACE)
target_include_directories(ob_doctest SYSTEM INTERFACE "${doctest_SOURCE_DIR}")

# Fonts shipped with Dear ImGui are used by the editor and runtime HUD
set(OB_IMGUI_FONT_FILE "${imgui_SOURCE_DIR}/misc/fonts/Roboto-Medium.ttf")

set(OB_THIRD_PARTY_TARGETS glfw nvrhi nvrhi_vk spdlog Jolt ob_lua ob_imgui ob_imguizmo)
foreach(target IN LISTS OB_THIRD_PARTY_TARGETS)
	if(TARGET ${target})
		set_target_properties(${target} PROPERTIES FOLDER "ThirdParty")
	endif()
endforeach()
