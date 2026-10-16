# Ouroboros tests
#
#   OuroborosTests (doctest)      unit tests; the "gpu" test suite needs a Vulkan 1.3 device
#   integration tests             run the real runtime/editor executables headlessly
#
# Labels: unit, gpu, integration. CI machines without a GPU run `ctest -LE gpu`.

file(GLOB_RECURSE OB_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

add_executable(OuroborosTests ${OB_TEST_SOURCES})
target_link_libraries(OuroborosTests PRIVATE Ouroboros ob_doctest)
target_include_directories(OuroborosTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(OuroborosTests PRIVATE
	OB_TEST_PROJECT_FILE="${CMAKE_SOURCE_DIR}/SnakeGame/Snake.oproj"
	OB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Data")
ob_set_target_options(OuroborosTests)
ob_copy_engine_resources(OuroborosTests)
set_target_properties(OuroborosTests PROPERTIES FOLDER "Ouroboros")
add_dependencies(OuroborosTests OuroborosRuntime)

set(OB_PROJECT_FILE "${CMAKE_SOURCE_DIR}/SnakeGame/Snake.oproj")

add_test(NAME UnitTests COMMAND OuroborosTests --test-suite-exclude=gpu)
set_tests_properties(UnitTests PROPERTIES LABELS "unit")

add_test(NAME GpuTests COMMAND OuroborosTests --test-suite=gpu)
set_tests_properties(GpuTests PROPERTIES LABELS "gpu")

# The feature test scene exercises every component and the complete scripting API.
add_test(NAME FeatureTestScene
	COMMAND OuroborosRuntime --project "${OB_PROJECT_FILE}" --scene Scenes/FeatureTest.oscene
		--headless --frames 600 --test --validation --no-audio)
set_tests_properties(FeatureTestScene PROPERTIES LABELS "integration;gpu" TIMEOUT 300)

# The game itself starts and runs without errors.
add_test(NAME SnakeGameSmoke
	COMMAND OuroborosRuntime --project "${OB_PROJECT_FILE}" --headless --frames 240 --test --validation --no-audio)
set_tests_properties(SnakeGameSmoke PROPERTIES LABELS "integration;gpu" TIMEOUT 300)

if(OB_BUILD_EDITOR)
	# An agent drives the headless editor through the command API (edit, play, input, screenshots).
	add_test(NAME EditorAutomation
		COMMAND OuroborosEditor --project "${OB_PROJECT_FILE}" --headless --no-server --no-audio --validation
			--exec "${CMAKE_CURRENT_SOURCE_DIR}/Data/EditorAutomation.jsonl" --exit-after-exec
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
	set_tests_properties(EditorAutomation PROPERTIES LABELS "integration;gpu" TIMEOUT 300)

	# Export the game through the editor, then run the exported executable.
	add_test(NAME ExportGame
		COMMAND OuroborosEditor --project "${OB_PROJECT_FILE}" --headless --no-server --no-audio
			--exec "${CMAKE_CURRENT_SOURCE_DIR}/Data/ExportGame.jsonl" --exit-after-exec
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
	set_tests_properties(ExportGame PROPERTIES LABELS "integration;gpu" TIMEOUT 300 FIXTURES_SETUP ExportedGame)

	if(WIN32)
		set(OB_EXPORTED_EXECUTABLE "${CMAKE_BINARY_DIR}/ExportTest/Snake3D.exe")
	else()
		set(OB_EXPORTED_EXECUTABLE "${CMAKE_BINARY_DIR}/ExportTest/Snake3D")
	endif()
	add_test(NAME RunExportedGame
		COMMAND "${OB_EXPORTED_EXECUTABLE}" --headless --frames 120 --test --no-audio)
	set_tests_properties(RunExportedGame PROPERTIES LABELS "integration;gpu" TIMEOUT 300 FIXTURES_REQUIRED ExportedGame)
endif()
