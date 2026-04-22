{
  stdenv
, lib
}:

stdenv.mkDerivation {
  pname="aff";
  version="1.0.0";
  src=./src;
  separateDebugInfo=true;
  dontStrip=true;
  installPhase = ''
    mkdir -p $out/bin
    cp ./aff $out/bin
  '';
}
