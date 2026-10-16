# Ouroboros editor (Ouroboros Studio)

file(GLOB_RECURSE OB_EDITOR_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

add_executable(OuroborosEditor ${OB_EDITOR_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" FILES ${OB_EDITOR_SOURCES})
target_include_directories(OuroborosEditor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(OuroborosEditor PRIVATE Ouroboros)
ob_set_target_options(OuroborosEditor)
ob_copy_engine_resources(OuroborosEditor)
set_target_properties(OuroborosEditor PROPERTIES
	FOLDER "Ouroboros"
	VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
	VS_DEBUGGER_COMMAND_ARGUMENTS "--project \"${CMAKE_SOURCE_DIR}/SnakeGame/Snake.oproj\"")

# The editor exports games by copying the runtime executable, so build it alongside.
add_dependencies(OuroborosEditor OuroborosRuntime)
