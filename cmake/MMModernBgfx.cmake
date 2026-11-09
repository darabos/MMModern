include(FetchContent)
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS ON CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_SHADER ON CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_BIN2C OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_GEOMETRY OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_TEXTURE OFF CACHE BOOL "" FORCE)
set(BGFX_CUSTOM_TARGETS OFF CACHE BOOL "" FORCE)
set(BGFX_CONFIG_MULTITHREADED OFF CACHE BOOL "" FORCE)
FetchContent_Declare(mmodern_bgfx
	GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git
	GIT_TAG 655364897f1cde0ada1f9486e771c1de8b5bf4cd
	GIT_SUBMODULES "")
FetchContent_MakeAvailable(mmodern_bgfx)
FetchContent_GetProperties(bgfx SOURCE_DIR MMODERN_BGFX_SOURCE_DIR)

set(MMODERN_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/sdl/shaders")
set(MMODERN_SHADER_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/bgfx")
file(MAKE_DIRECTORY "${MMODERN_SHADER_OUTPUT}")
if(APPLE)
	set(MMODERN_SHADER_PLATFORM osx)
	set(MMODERN_SHADER_PROFILE metal)
elseif(WIN32)
	set(MMODERN_SHADER_PLATFORM windows)
	set(MMODERN_SHADER_PROFILE s_5_0)
else()
	set(MMODERN_SHADER_PLATFORM linux)
	set(MMODERN_SHADER_PROFILE spirv)
endif()
set(MMODERN_SHADER_HEADERS "")
foreach(stage IN ITEMS vs fs)
	if(stage STREQUAL vs)
		set(type vertex)
	else()
		set(type fragment)
	endif()
	set(output "${MMODERN_SHADER_OUTPUT}/${stage}_textured.h")
	add_custom_command(OUTPUT "${output}"
		COMMAND $<TARGET_FILE:bgfx::shaderc>
			-f "${MMODERN_SHADER_DIR}/${stage}_textured.sc" -o "${output}"
			--type "${type}" --platform "${MMODERN_SHADER_PLATFORM}"
			--profile "${MMODERN_SHADER_PROFILE}"
			--varyingdef "${MMODERN_SHADER_DIR}/varying.def.sc"
			-i "${MMODERN_BGFX_SOURCE_DIR}/src" --bin2c "${stage}_textured"
		DEPENDS bgfx::shaderc
			"${MMODERN_SHADER_DIR}/${stage}_textured.sc"
			"${MMODERN_SHADER_DIR}/varying.def.sc"
		VERBATIM)
	list(APPEND MMODERN_SHADER_HEADERS "${output}")
endforeach()
add_custom_target(mmodern_bgfx_shaders DEPENDS ${MMODERN_SHADER_HEADERS})
