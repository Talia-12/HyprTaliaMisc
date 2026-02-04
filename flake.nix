{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    nix-filter.url = "github:numtide/nix-filter";
  };

  outputs =
    {
      self,
      nixpkgs,
      nix-filter,
      ...
    }:
    let
      supportedSystems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems =
        fn:
        nixpkgs.lib.genAttrs supportedSystems (
          system: fn system nixpkgs.legacyPackages.${system}
        );
    in
    {
      packages = forAllSystems (
        system: pkgs:
        let
          hyprlandPackage = pkgs.hyprland;
        in
        rec {
          HyprTaliaMisc = pkgs.gcc14Stdenv.mkDerivation {
            pname = "HyprTaliaMisc";
            version = "0.0.1";
            src = nix-filter.lib {
              root = ./.;
              include = [
                "src"
                ./Makefile
              ];
            };

            nativeBuildInputs = with pkgs; [ pkg-config ];
            buildInputs = [ hyprlandPackage.dev ] ++ hyprlandPackage.buildInputs;

            installPhase = ''
              mkdir -p $out/lib
              install ./out/hyprtaliamisc.so $out/lib/libHyprTaliaMisc.so
            '';

            meta = with pkgs.lib; {
              homepage = "https://github.com/Talia-12/HyprTaliaMisc";
              description = "My misc. Hyprland additions";
              license = licenses.mit;
              platforms = platforms.linux;
            };
          };

          default = HyprTaliaMisc;
        }
      );

      devShells = forAllSystems (
        system: pkgs: {
          default = pkgs.mkShell {
            name = "HyprTaliaMisc";

            nativeBuildInputs = with pkgs; [
              clang-tools
              jq
            ];

            inputsFrom = [ self.packages.${system}.HyprTaliaMisc ];
          };
        }
      );
    };
}
