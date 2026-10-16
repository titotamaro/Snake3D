# Ouroboros engine core library

file(GLOB_RECURSE OB_ENGINE_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

add_library(Ouroboros STATIC ${OB_ENGINE_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" FILES ${OB_ENGINE_SOURCES})
set_target_properties(Ouroboros PROPERTIES FOLDER "Ouroboros")

target_include_directories(Ouroboros PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_precompile_headers(Ouroboros PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/src/obpch.h>")

target_link_libraries(Ouroboros
	PUBLIC
		glfw
		nvrhi
		nvrhi_vk
		Vulkan::Headers
		glm::glm
		spdlog::spdlog
		EnTT::EnTT
		nlohmann_json::nlohmann_json
		Jolt
		ob_sol2
		ob_imgui
		ob_imguizmo
	PRIVATE
		ob_miniaudio
		ob_cgltf
		ob_stb)

if(WIN32)
	target_link_libraries(Ouroboros PRIVATE ws2_32)
elseif(UNIX)
	find_package(Threads REQUIRED)
	target_link_libraries(Ouroboros PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
endif()

target_compile_definitions(Ouroboros
	PUBLIC
		GLM_FORCE_DEPTH_ZERO_TO_ONE
		GLM_FORCE_RADIANS
		GLM_ENABLE_EXPERIMENTAL
		GLFW_INCLUDE_NONE
		VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
		OB_VERSION_STRING="${PROJECT_VERSION}")

ob_set_target_options(Ouroboros)

# ------------------------------------------------------------------------------------------------
# Engine resources (compiled shaders, fonts) are staged into ${CMAKE_BINARY_DIR}/Resources and copied next to
# every executable by ob_copy_engine_resources().

set(OB_RESOURCES_BUILD_DIR "${CMAKE_BINARY_DIR}/Resources" CACHE INTERNAL "")

file(GLOB OB_SHADER_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Shaders/*.hlsl")
file(GLOB OB_SHADER_INCLUDES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Shaders/*.hlsli")
ob_add_shaders(OuroborosShaders
	OUTPUT_DIR "${OB_RESOURCES_BUILD_DIR}/Shaders"
	SOURCES ${OB_SHADER_SOURCES}
	INCLUDES ${OB_SHADER_INCLUDES})

add_custom_target(OuroborosResources
	COMMAND "${CMAKE_COMMAND}" -E make_directory "${OB_RESOURCES_BUILD_DIR}/Fonts"
	COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${OB_IMGUI_FONT_FILE}" "${OB_RESOURCES_BUILD_DIR}/Fonts/Roboto-Medium.ttf"
	COMMENT "Staging Ouroboros engine resources"
	VERBATIM)
add_dependencies(OuroborosResources OuroborosShaders)
set_target_properties(OuroborosResources PROPERTIES FOLDER "Ouroboros")
add_dependencies(Ouroboros OuroborosResources)

function(ob_copy_engine_resources target)
	add_custom_command(TARGET ${target} POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy_directory "${OB_RESOURCES_BUILD_DIR}" "$<TARGET_FILE_DIR:${target}>/Resources"
		COMMENT "Copying engine resources next to ${target}"
		VERBATIM)
endfunction()
