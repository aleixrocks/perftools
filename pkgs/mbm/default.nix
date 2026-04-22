{
  stdenv
, lib
, python3
}:

let
  python = python3.withPackages (pythonPackages: with pythonPackages; [
    matplotlib
  ]);
in stdenv.mkDerivation {
  pname = "mbm";
  version = "0.1";
  src = ./src;

  propagatedBuildInputs = [
    python
  ];

  passthru = {
    python = python;
  };

  dontUnpack = true;
  dontBuild = true;

  installPhase = ''
    mkdir -p $out/bin $out/share
    cp $src/*.py $out/bin/
    cp $src/*.sh $out/share
    patchShebangs $out/bin
  '';
}
