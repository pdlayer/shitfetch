{
  description = "shitfetch";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f (import nixpkgs { inherit system; }));
    in {
      packages = forAllSystems (pkgs: rec {
        default = pkgs.stdenv.mkDerivation {
          pname = "shitfetch";
          version = "0.1.0";
          src = ./.;

          nativeBuildInputs = [ pkgs.pkg-config ];
          buildInputs = [ pkgs.libdrm ];

          installFlags = [ "PREFIX=$(out)" ];

          postInstall = ''
            install -Dm644 shitfetch.bash $out/share/bash-completion/completions/shitfetch
            install -Dm644 shitfetch.fish $out/share/fish/vendor_completions.d/shitfetch.fish
            install -Dm644 shitfetch.zsh $out/share/zsh/site-functions/_shitfetch
          '';
        };
      });

      apps = forAllSystems (pkgs: {
        default = {
          type = "app";
          program = "${self.packages.${pkgs.system}.default}/bin/shitfetch";
        };
      });
    };
}
