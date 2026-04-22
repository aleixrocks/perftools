{
  stdenv
}:

stdenv.mkDerivation {
  name = "pthtree";
  version = "1.0.0";

  src = ./src;
  dontConfigure = true;
  separateDebugInfo = true;
  dontStrip = true;

  makeFlags = [
    "prefix=$(out)"
  ];
}
