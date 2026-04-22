{
  stdenv
, python3
}:

stdenv.mkDerivation {
  name = "lavg";
  version = "1.0.0";

  buildInputs = [
    (python3.withPackages (pythonPackages: with pythonPackages; [
      pandas
      matplotlib
    ]))
  ];

  src = ./src;
  dontConfigure = true;
  separateDebugInfo = true;
  dontStrip = true;

  makeFlags = [
    "prefix=$(out)"
  ];
}
