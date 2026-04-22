{
  stdenv
, openmp
}:

stdenv.mkDerivation {
  name = "test";
  version = "1.0.0";

  src = ./src;
  dontConfigure = true;
  separateDebugInfo = true;
  dontStrip = true;

  buildInputs = [
    openmp
  ];

  makeFlags = [
    "prefix=$(out)"
  ];
}
