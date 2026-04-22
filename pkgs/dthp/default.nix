{
  stdenv
}:

stdenv.mkDerivation {
  pname = "dthp";
  version = "1.0.0";
  src = ./src;

  installPhase = ''
    mkdir -p $out/bin
    cp ./dthp $out/bin/
  '';

}
