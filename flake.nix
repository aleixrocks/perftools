{
  description = "My performance analysis tool collection";

  inputs = {
    nixpkgs.url = github:NixOS/nixpkgs/nixos-25.11;
  };

  outputs = { self, nixpkgs }:
  let
    lib = nixpkgs.lib;
    pkgs = import nixpkgs { system = "x86_64-linux"; };
    mkPackages = p: {
      aff = p.callPackage pkgs/aff {};
      dthp = p.callPackage pkgs/dthp/default.nix {};
      lavg  = p.callPackage pkgs/lavg {};
      libnest = p.callPackage pkgs/libnest {};
      mbm = p.callPackage pkgs/mbm/default.nix {};
      nex = p.callPackage pkgs/nex/default.nix {};
      pthtree = p.callPackage pkgs/pthtree {};
      setaff = p.callPackage pkgs/setaff {};
      wicf = p.callPackage pkgs/bpf/wicf {};
    };
  in {
    legacyPackages.x86_64-linux = mkPackages pkgs;
    overlays.default = final: prev: mkPackages final;
  };
}
