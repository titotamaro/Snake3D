# Compiler settings applied to Ouroboros' own targets (third-party code keeps its own flags).
#
# Ouroboros' C++ code is compiled with high warning levels (and warnings as errors by default). The only C sources in
# Ouroboros targets are the single-header library implementation units in Ouroboros/src/Ouroboros/Vendor, which are
# compiled with warnings disabled.

function(ob_set_target_options target)
	if(MSVC)
		target_compile_options(${target} PRIVATE
			$<$<COMPILE_LANGUAGE:CXX>:/W4 /permissive- /Zc:__cplusplus /Zc:preprocessor>
			$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<BOOL:${OB_WARNINGS_AS_ERRORS}>>:/WX>
			$<$<COMPILE_LANGUAGE:C>:/W0>
			/utf-8 /MP /bigobj)
		target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
	else()
		target_compile_options(${target} PRIVATE
			$<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers>
			$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<BOOL:${OB_WARNINGS_AS_ERRORS}>>:-Werror>
			$<$<COMPILE_LANGUAGE:C>:-w>)
	endif()

	target_compile_definitions(${target} PRIVATE
		$<$<CONFIG:Debug>:OB_DEBUG>
		$<$<CONFIG:Release>:OB_RELEASE>
		$<$<CONFIG:RelWithDebInfo>:OB_RELEASE>
		$<$<CONFIG:MinSizeRel>:OB_RELEASE>)
endfunction()
