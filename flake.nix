{
  description = "ATIS display firmware for the Soldered Inkplate 6COLOR";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "aarch64-darwin" "x86_64-darwin" "aarch64-linux" "x86_64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in
    {
      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          packages = [
            pkgs.arduino-cli
            # The Inkplate board package bundles an Intel-only esptool; `make setup` swaps in this one.
            pkgs.esptool
            pkgs.python3
            pkgs.gnumake
            pkgs.bun
          ];

          # Keep the Arduino cores and libraries inside the project (see Makefile).
          shellHook = ''
            export ARDUINO_DIRECTORIES_DATA="$PWD/.arduino/data"
            export ARDUINO_DIRECTORIES_USER="$PWD/.arduino/user"
            export ARDUINO_DIRECTORIES_DOWNLOADS="$PWD/.arduino/dl"
          '';
        };
      });
    };
}
