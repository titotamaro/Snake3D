# Ouroboros runtime: the player executable that ships with exported games (no editing ability).

file(GLOB_RECURSE OB_RUNTIME_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

add_executable(OuroborosRuntime ${OB_RUNTIME_SOURCES})
target_link_libraries(OuroborosRuntime PRIVATE Ouroboros)
ob_set_target_options(OuroborosRuntime)
ob_copy_engine_resources(OuroborosRuntime)
set_target_properties(OuroborosRuntime PROPERTIES FOLDER "Ouroboros")

if(WIN32)
	# Shipping builds open no console window; debug builds keep it for logging.
	set_target_properties(OuroborosRuntime PROPERTIES WIN32_EXECUTABLE $<NOT:$<CONFIG:Debug>>)
	target_link_options(OuroborosRuntime PRIVATE $<$<NOT:$<CONFIG:Debug>>:/ENTRY:mainCRTStartup>)
endif()
