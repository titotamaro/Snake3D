# HLSL -> SPIR-V shader compilation with DXC (ships with the Vulkan SDK).
#
# Shader files are named <Name>.<stage>.hlsl where <stage> is one of vs, ps or cs, and each has a `main` entry point.
# Register shifts match NVRHI's default VulkanBindingOffsets (t:0, s:128, b:256, u:384).

find_program(OB_DXC_EXECUTABLE
	NAMES dxc
	HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
	DOC "DirectX Shader Compiler with SPIR-V support")

if(NOT OB_DXC_EXECUTABLE)
	message(FATAL_ERROR "dxc was not found. Install the Vulkan SDK (https://vulkan.lunarg.com) and make sure VULKAN_SDK is set.")
endif()

message(STATUS "Ouroboros: using DXC at ${OB_DXC_EXECUTABLE}")

function(ob_add_shaders target)
	cmake_parse_arguments(ARG "" "OUTPUT_DIR" "SOURCES;INCLUDES" ${ARGN})

	set(outputs "")
	foreach(source IN LISTS ARG_SOURCES)
		get_filename_component(fileName "${source}" NAME)
		string(REGEX MATCH "\\.(vs|ps|cs)\\.hlsl$" stageMatch "${fileName}")
		if(NOT stageMatch)
			message(FATAL_ERROR "Shader '${fileName}' must be named <Name>.<vs|ps|cs>.hlsl")
		endif()
		set(stage "${CMAKE_MATCH_1}")
		string(REGEX REPLACE "\\.hlsl$" ".spv" outputName "${fileName}")
		set(output "${ARG_OUTPUT_DIR}/${outputName}")
		get_filename_component(includeDir "${source}" DIRECTORY)

		add_custom_command(
			OUTPUT "${output}"
			COMMAND "${CMAKE_COMMAND}" -E make_directory "${ARG_OUTPUT_DIR}"
			COMMAND "${OB_DXC_EXECUTABLE}"
				-spirv -T ${stage}_6_0 -E main
				-fspv-target-env=vulkan1.3 -fvk-use-dx-layout -Zpc -O3 -WX
				-fvk-t-shift 0 all -fvk-s-shift 128 all -fvk-b-shift 256 all -fvk-u-shift 384 all
				-I "${includeDir}"
				-Fo "${output}" "${source}"
			MAIN_DEPENDENCY "${source}"
			DEPENDS ${ARG_INCLUDES}
			COMMENT "Compiling shader ${fileName}"
			VERBATIM)
		list(APPEND outputs "${output}")
	endforeach()

	add_custom_target(${target} DEPENDS ${outputs} SOURCES ${ARG_SOURCES} ${ARG_INCLUDES})
	set_target_properties(${target} PROPERTIES FOLDER "Ouroboros")
endfunction()
