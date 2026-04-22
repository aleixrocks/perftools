{
  description = "My performance analysis tool collection";

  inputs = {
    nixpkgs.url = github:NixOS/nixpkgs/nixos-25.11;
  };

  outputs = { self, nixpkgs }:
  let
    lib = nixpkgs.lib;
    pkgs = import nixpkgs { system = "x86_64-linux"; };
  in {
    legacyPackages.x86_64-linux = with pkgs; {
      aff = callPackage pkgs/aff {};
      dthp = callPackage pkgs/dthp/default.nix {};
      lavg  = callPackage pkgs/lavg {};
      libnest = callPackage pkgs/libnest {};
      mbm = callPackage pkgs/mbm/default.nix {};
      nex = callPackage pkgs/nex/default.nix {};
      pthtree = callPackage pkgs/pthtree {};
      setaff = callPackage pkgs/setaff {};
      wicf = callPackage pkgs/bpf/wicf {};
      inherit perf bpftrace;
    };
  };
}
