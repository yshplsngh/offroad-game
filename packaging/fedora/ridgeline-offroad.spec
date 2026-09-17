# ridgeline-offroad.spec - RPM source of truth for the native Linux game.
#
# Builds entirely from source and Fedora packages, offline (mock-friendly):
#   - libworldcore.so (C++20 GDExtension) is compiled with Fedora's flags via %%cmake
#   - the game is exported with Fedora's own godot + godot-runner as the release
#     template, so no upstream binary is downloaded or bundled from elsewhere
# Nothing from the browser reference (Node, Vite, three, node_modules) is used.
#
# Local builds without the Fedora toolchain installed can override:
#   --define 'godot_bin /path/to/godot'
#   --define 'godot_template /path/to/linux_release.x86_64'

%global godot_cpp_tag godot-4.5-stable
%{!?godot_bin: %global godot_bin %{_bindir}/godot}
%{!?godot_template: %global godot_template %{_bindir}/godot-runner}

# The exported engine binary is a copy of godot-runner, already stripped by
# its own package; debuginfo is only meaningful for libworldcore.
%global _missing_build_ids_terminate_build 0
# godot-cpp's generated bindings are the bulk of the sources; a debugsource
# subpackage of them is of no use and can come out empty with some toolchains.
%undefine _debugsource_packages

Name:           ridgeline-offroad
Version:        0.1.0
Release:        2%{?dist}
Summary:        Open-world offroad 4x4 driving simulator

License:        LicenseRef-Proprietary
URL:            https://github.com/yshplsngh/offroad-game
Source0:        %{name}-%{version}.tar.gz
Source1:        https://github.com/godotengine/godot-cpp/archive/%{godot_cpp_tag}/godot-cpp-%{godot_cpp_tag}.tar.gz

ExclusiveArch:  x86_64

BuildRequires:  gcc-c++
BuildRequires:  cmake >= 3.22
BuildRequires:  ninja-build
BuildRequires:  python3
BuildRequires:  godot = 4.7.2
BuildRequires:  godot-runner = 4.7.2
BuildRequires:  desktop-file-utils
BuildRequires:  libappstream-glib

Requires:       hicolor-icon-theme
# Linked libraries are found by rpm's automatic ELF dependency scan. The engine
# dlopen()s its GPU, display and audio backends, which that scan cannot see.
Requires:       vulkan-loader%{?_isa}
Requires:       libwayland-client%{?_isa}
Requires:       libwayland-cursor%{?_isa}
Requires:       libxkbcommon%{?_isa}
Requires:       libdecor%{?_isa}
Requires:       libX11%{?_isa}
Requires:       libXcursor%{?_isa}
Requires:       libXext%{?_isa}
Requires:       libXi%{?_isa}
Requires:       libXinerama%{?_isa}
Requires:       libXrandr%{?_isa}
Requires:       libXrender%{?_isa}
Requires:       fontconfig%{?_isa}
Requires:       dbus-libs%{?_isa}
Requires:       pulseaudio-libs%{?_isa}
Requires:       alsa-lib%{?_isa}
Recommends:     mesa-vulkan-drivers%{?_isa}

%description
Ridgeline Offroad is an open-world offroad 4x4 simulator with procedural
vehicles, analytic streamed terrain, mud and mountains. It runs as a native
Godot 4 executable with a C++20 world/vehicle extension.

%prep
%autosetup -n %{name}-%{version}
mkdir -p native/third_party
tar -xzf %{SOURCE1} -C native/third_party
mv native/third_party/godot-cpp-%{godot_cpp_tag} native/third_party/godot-cpp

%build
# Native extension, release variant, Fedora compiler/linker flags.
%define __cmake_in_source_build 0
%cmake -S native/worldcore -G Ninja \
    -DWORLDCORE_GODOT=ON \
    -DWORLDCORE_TESTS=ON \
    -DGODOTCPP_TARGET=template_release
%cmake_build

# Export the game: godot-runner is the release template.
export HOME=$PWD/.home
mkdir -p "$HOME" build/export
sed -i 's|^custom_template/release=.*|custom_template/release="%{godot_template}"|' native/godot/export_presets.cfg
%{godot_bin} --headless --path native/godot --import
%{godot_bin} --headless --path native/godot \
    --export-release "Linux/x86_64" "$PWD/build/export/%{name}"
test -x build/export/%{name}
test -f build/export/%{name}.pck
test -f build/export/libworldcore.linux.template_release.x86_64.so

%check
# Terrain and physics ports must still match the browser oracle's golden data.
%ctest
# The packaged export must boot, stream terrain and exit cleanly without a GPU.
export HOME=$PWD/.home
timeout 120 build/export/%{name} --headless -- --smoke
desktop-file-validate packaging/fedora/%{name}.desktop

%install
install -d %{buildroot}%{_libexecdir}/%{name}
install -m 0755 build/export/%{name} %{buildroot}%{_libexecdir}/%{name}/%{name}
install -m 0644 build/export/%{name}.pck %{buildroot}%{_libexecdir}/%{name}/%{name}.pck
install -m 0755 build/export/libworldcore.linux.template_release.x86_64.so \
    %{buildroot}%{_libexecdir}/%{name}/libworldcore.linux.template_release.x86_64.so

install -Dm 0755 packaging/fedora/%{name}.sh %{buildroot}%{_bindir}/%{name}
install -Dm 0644 packaging/fedora/%{name}.desktop %{buildroot}%{_datadir}/applications/%{name}.desktop
install -Dm 0644 packaging/fedora/icons/%{name}.svg \
    %{buildroot}%{_datadir}/icons/hicolor/scalable/apps/%{name}.svg

install -Dm 0644 native/third_party/godot-cpp/LICENSE.md \
    %{buildroot}%{_datadir}/licenses/%{name}/godot-cpp-LICENSE.md
install -Dm 0644 packaging/fedora/CREDITS.md %{buildroot}%{_datadir}/licenses/%{name}/CREDITS.md
# The exported executable is the Godot engine runtime: ship its MIT licence and
# third-party notices (recorded from godot 4.7.2-stable).
install -Dm 0644 packaging/fedora/licenses/GODOT-LICENSE.txt %{buildroot}%{_datadir}/licenses/%{name}/GODOT-LICENSE.txt
install -Dm 0644 packaging/fedora/licenses/GODOT-COPYRIGHT.txt %{buildroot}%{_datadir}/licenses/%{name}/GODOT-COPYRIGHT.txt

%files
%dir %{_datadir}/licenses/%{name}
%{_datadir}/licenses/%{name}/godot-cpp-LICENSE.md
%{_datadir}/licenses/%{name}/CREDITS.md
%{_datadir}/licenses/%{name}/GODOT-LICENSE.txt
%{_datadir}/licenses/%{name}/GODOT-COPYRIGHT.txt
%{_bindir}/%{name}
%dir %{_libexecdir}/%{name}
%{_libexecdir}/%{name}/%{name}
%{_libexecdir}/%{name}/%{name}.pck
%{_libexecdir}/%{name}/libworldcore.linux.template_release.x86_64.so
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/scalable/apps/%{name}.svg

%changelog
* Thu Sep 17 2026 Yashpal Singh <priyanshuvarshney029@gmail.com> - 0.1.0-2
- Native C++ terrain streaming scheduler
- Require the GPU/display/audio libraries the engine loads at runtime
- Ship the Godot engine licence and third-party copyright notices

* Thu Sep 17 2026 Yashpal Singh <priyanshuvarshney029@gmail.com> - 0.1.0-1
- Native foundation: Godot 4.7 project, worldcore GDExtension, analytic terrain
