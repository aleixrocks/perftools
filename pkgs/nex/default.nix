{
  stdenv
, lib
, python3
}:

let
  nex_python = python3.withPackages (pythonPackages: with pythonPackages; [
      pyyaml
      cerberus
      pandas
      seaborn
      gitpython
  ]);
in stdenv.mkDerivation {
  pname = "nex";
  version = "0.1";
  #src = ./src;
  src = builtins.fetchGit {
    url = "git@gitlab-internal.bsc.es:arocanon/nex.git";
    ref = "master";
    rev = "9d3af001db0c900c7b12590fe5fc95c9a1cfec94";
  };

  propagatedBuildInputs = [
    nex_python
  ];

  passthru = {
    python = nex_python;
  };

  dontUnpack = true;
  dontBuild = true;

  installPhase = ''
    mkdir -p $out/bin $out/share
    cp $src/*.py $out/bin/
    cp $src/nex $out/bin/
    cp $src/tests/exp.yaml $out/share
    patchShebangs $out/bin/nex
  '';
}
