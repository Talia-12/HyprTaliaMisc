{
  inputs = {
    # hyprland.url = "git+https://github.com/hyprwm/Hyprland?submodules=1&ref=v0.53.3";
    hyprland = {
      type = "git";
      url = "https://github.com/hyprwm/Hyprland";
      submodules = true;
      ref = "refs/tags/v0.53.3";
    };


    nix-filter.url = "github:numtide/nix-filter";
  };

  outputs =
    {
      self,
      hyprland,
      nix-filter,
      ...
    }:
    let
      inherit (hyprland.inputs) nixpkgs;
      forHyprlandSystems =
        fn:
        nixpkgs.lib.genAttrs (builtins.attrNames hyprland.packages) (
          system: fn system nixpkgs.legacyPackages.${system}
        );
    in
    {
      packages = forHyprlandSystems (
        system: pkgs:
        let
          hyprlandPackage = hyprland.packages.${system}.hyprland;
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

      devShells = forHyprlandSystems (
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
