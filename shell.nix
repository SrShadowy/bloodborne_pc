# Build environment: `nix-shell native_probe/shell.nix --run 'bash native_probe/run.sh'`
{ pkgs ? import <nixpkgs> {} }:
let
  # libdecor (window frames on GNOME's Wayland) without its GTK 3 plugin (GTK 3, tinysparql, ICU:
  # ~100 MB); its cairo plugin draws the frames.
  libdecor = pkgs.libdecor.overrideAttrs (old: {
    buildInputs = pkgs.lib.remove pkgs.gtk3 old.buildInputs;
    mesonFlags = old.mesonFlags ++ [ "-Dgtk=disabled" ];
  });
  # SDL3 names zenity (its message boxes) by store path, and zenity brings a whole GTK4 with
  # GStreamer into the AppImage (~200 MB). This one runs the host's zenity when there is one.
  sdl3 = pkgs.sdl3.override {
    inherit libdecor;
    zenity = pkgs.writeShellScriptBin "zenity" ''
      host=$(command -v zenity) && exec "$host" "$@"
      exit 1
    '';
  };
  vulkan-headers = pkgs.vulkan-headers.overrideAttrs (old: {
    version = "1.4.363.0";
    src = pkgs.fetchFromGitHub {
      owner = "KhronosGroup";
      repo = "Vulkan-Headers";
      rev = "vulkan-sdk-1.4.363.0";
      hash = "sha256-E/SBlMW1oUeUT0MtWzQp0k+OSZLc9ho7g+1oTd7z7+Y=";
    };
  });
in
pkgs.mkShell {
  packages = with pkgs; [
    gcc gnumake cmake ninja pkg-config python3 binutils gnused
    vulkan-headers vulkan-loader sdl3 alsa-lib xorg.libXau
    # GPU library (gpu/): shadPS4 video core dependencies
    ffmpeg-headless boost fmt magic-enum robin-map xxhash vulkan-memory-allocator glslang spirv-cross xbyak zydis spirv-headers miniz libx11 libxcb libxau xorgproto wayland alsa-lib
    # Online module (gpu/bbnet): shadNet client, HTTPS, UPnP
    protobuf nlohmann_json openssl zlib miniupnpc
  ];
}
