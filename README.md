# Ridgeline Offroad

Open-world offroad 4x4 simulator for Linux, Windows and macOS: procedural
trucks, an analytic streamed landscape of forest, mud, rivers and mountains, and
a sim-leaning rock crawler driving model.

Native build: **Godot 4.7** (Vulkan Mobile renderer, Jolt physics) with a
**C++20 GDExtension** (`native/worldcore`) for the world model, streaming and
vehicle controller. Fedora (**RPM**) is the platform being built and packaged
first; the code is kept portable to Windows and macOS.

Status, design rules and what is left: see [PLAN.md](PLAN.md).

## Build and run

Needs a C++20 compiler, CMake ≥ 3.22, Ninja, Python 3 and Godot 4.7
(`sudo dnf install gcc-c++ cmake ninja-build godot godot-runner` on Fedora),
plus [godot-cpp](https://github.com/godotengine/godot-cpp) `godot-4.5-stable`
checked out at `native/third_party/godot-cpp`.

```bash
# core library + tests
cmake -S native/worldcore -B native/build/core -G Ninja
cmake --build native/build/core && ctest --test-dir native/build/core

# the GDExtension, then play
cmake -S native/worldcore -B native/build/gde -G Ninja -DWORLDCORE_GODOT=ON -DWORLDCORE_TESTS=OFF
cmake --build native/build/gde --target worldcore
godot --path native/godot

# Fedora package
packaging/fedora/build-rpm.sh
```

On macOS, `tools/setup-macos.sh` installs the toolchain (Homebrew: cmake,
ninja, the Godot 4.7 cask), checks out godot-cpp and runs the builds and tests
above, producing the universal `libworldcore.macos.*.dylib` the project
expects.

## Controls

`WASD` drive · `Space` handbrake · `Q`/`E` gears · `L` low/high range ·
`X` diff lock · `R` recover · `F` winch hook · `G` reel in · `C` camera ·
`V` next vehicle · `P` pause · `/` help · `` ` `` stats
