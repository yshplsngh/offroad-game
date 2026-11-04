cmake_minimum_required(VERSION 3.22)
project(worldcore VERSION 0.1.0 LANGUAGES CXX)

# worldcore - the native world: analytic terrain, streaming scheduler, vehicle
# controller. The core library has no engine dependency so it can be tested
# against the browser oracle's golden data on any machine; the Godot binding
# (GDExtension) is a thin layer on top, built when godot-cpp is available.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(WORLDCORE_TESTS "Build golden-data tests" ON)
option(WORLDCORE_GODOT "Build the Godot GDExtension (needs godot-cpp)" OFF)
set(GODOT_CPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/godot-cpp" CACHE PATH "godot-cpp checkout")

find_package(Threads REQUIRED)

add_library(worldcore_core STATIC
  src/field.cpp
  src/chunk_mesh.cpp
  src/drivetrain.cpp
  src/vehicle.cpp
  src/stream.cpp
  src/scatter.cpp
  src/flora.cpp
  src/vegetation.cpp
  src/meshgen.cpp
  src/vehicle_mesh.cpp
  src/ground_texture.cpp
)
target_include_directories(worldcore_core PUBLIC include)
target_link_libraries(worldcore_core PUBLIC Threads::Threads)
if(MSVC)
  target_compile_options(worldcore_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(worldcore_core PRIVATE -Wall -Wextra)
endif()

# Build-time asset baker (native/godot/generated/ is its output, never hand-edited).
add_executable(worldcore_bake tools/bake.cpp)
target_link_libraries(worldcore_bake PRIVATE worldcore_core)
target_include_directories(worldcore_bake PRIVATE tests)
set(WORLDCORE_GENERATED ${CMAKE_CURRENT_SOURCE_DIR}/../godot/generated)
add_custom_command(
  OUTPUT ${WORLDCORE_GENERATED}/flora.bin ${WORLDCORE_GENERATED}/vehicles.bin ${WORLDCORE_GENERATED}/ground.bin
  COMMAND ${CMAKE_COMMAND} -E make_directory ${WORLDCORE_GENERATED}
  COMMAND worldcore_bake ${WORLDCORE_GENERATED} ${CMAKE_CURRENT_SOURCE_DIR}/../godot/data
  DEPENDS worldcore_bake ${CMAKE_CURRENT_SOURCE_DIR}/../godot/data/vehicles.json
  COMMENT "Baking flora, vehicle meshes and ground textures")
add_custom_target(worldcore_assets ALL DEPENDS ${WORLDCORE_GENERATED}/flora.bin ${WORLDCORE_GENERATED}/vehicles.bin
                  ${WORLDCORE_GENERATED}/ground.bin)

if(WORLDCORE_TESTS)
  enable_testing()
  add_executable(test_field tests/test_field.cpp)
  target_link_libraries(test_field PRIVATE worldcore_core)
  add_test(NAME golden_terrain
           COMMAND test_field ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/terrain.json)
  add_executable(test_vehicle tests/test_vehicle.cpp)
  target_link_libraries(test_vehicle PRIVATE worldcore_core)
  add_test(NAME golden_vehicle
           COMMAND test_vehicle ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/physics.json
                                ${CMAKE_CURRENT_SOURCE_DIR}/../godot/data)
  add_executable(test_scatter tests/test_scatter.cpp)
  target_link_libraries(test_scatter PRIVATE worldcore_core)
  add_test(NAME golden_scatter
           COMMAND test_scatter ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/scatter.json)
  add_executable(test_flora tests/test_flora.cpp)
  target_link_libraries(test_flora PRIVATE worldcore_core)
  add_test(NAME golden_flora
           COMMAND test_flora ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/scatter.json)
  add_executable(test_vegetation tests/test_vegetation.cpp)
  target_link_libraries(test_vegetation PRIVATE worldcore_core)
  add_test(NAME vegetation_batches COMMAND test_vegetation)
  add_executable(test_meshgen tests/test_meshgen.cpp)
  target_link_libraries(test_meshgen PRIVATE worldcore_core)
  add_test(NAME meshgen COMMAND test_meshgen)
  add_executable(test_vehicle_mesh tests/test_vehicle_mesh.cpp)
  target_link_libraries(test_vehicle_mesh PRIVATE worldcore_core)
  add_test(NAME vehicle_mesh COMMAND test_vehicle_mesh ${CMAKE_CURRENT_SOURCE_DIR}/../godot/data)
  add_executable(test_ground_texture tests/test_ground_texture.cpp)
  target_link_libraries(test_ground_texture PRIVATE worldcore_core)
  add_test(NAME ground_texture COMMAND test_ground_texture)
  add_executable(test_stream tests/test_stream.cpp)
  target_link_libraries(test_stream PRIVATE worldcore_core)
  add_test(NAME stream_scheduler COMMAND test_stream)
  add_executable(test_controller tests/test_controller.cpp)
  target_link_libraries(test_controller PRIVATE worldcore_core)
  add_test(NAME golden_controller
           COMMAND test_controller ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/controller.json
                                   ${CMAKE_CURRENT_SOURCE_DIR}/../godot/data)
endif()

if(WORLDCORE_GODOT)
  add_subdirectory(${GODOT_CPP_DIR} godot-cpp EXCLUDE_FROM_ALL)
  add_library(worldcore SHARED
    gdextension/register_types.cpp
    gdextension/terrain_field.cpp
    gdextension/offroad_vehicle.cpp
    gdextension/ground_patch.cpp
    gdextension/terrain_streamer.cpp
    gdextension/vegetation_streamer.cpp
    gdextension/vehicle_mesh_library.cpp
  )
  target_link_libraries(worldcore PRIVATE worldcore_core godot-cpp)
  add_dependencies(worldcore worldcore_assets)
  if(UNIX AND NOT APPLE)
    # ELF only. The Linux editor unloads extensions before process exit; exit
    # handlers registered from inside the library would then jump into unmapped
    # code. The extension is not reloadable, so never unmap it.
    target_link_options(worldcore PRIVATE -Wl,-z,nodelete)
  endif()
  # Platform/target/arch suffix from godot-cpp, e.g. .linux.template_release.x86_64,
  # .windows.template_debug.x86_64, .macos.template_release.universal - the names
  # native/godot/worldcore.gdextension lists for every platform.
  get_target_property(WORLDCORE_SUFFIX godot-cpp GODOTCPP_SUFFIX)
  set(WORLDCORE_BIN ${CMAKE_CURRENT_SOURCE_DIR}/../godot/bin)
  set_target_properties(worldcore PROPERTIES
    OUTPUT_NAME "worldcore${WORLDCORE_SUFFIX}"
    PREFIX lib
    LIBRARY_OUTPUT_DIRECTORY ${WORLDCORE_BIN}   # .so / .dylib
    RUNTIME_OUTPUT_DIRECTORY ${WORLDCORE_BIN})  # .dll
  foreach(config IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(worldcore PROPERTIES
      LIBRARY_OUTPUT_DIRECTORY_${config} ${WORLDCORE_BIN}
      RUNTIME_OUTPUT_DIRECTORY_${config} ${WORLDCORE_BIN})
  endforeach()
  install(TARGETS worldcore LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
endif()
