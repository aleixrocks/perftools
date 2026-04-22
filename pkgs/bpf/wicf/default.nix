{
  writeShellApplication
, bpftrace
, python3
}:

writeShellApplication {
  name = "wicf";

  runtimeInputs = [
    bpftrace
    python3
  ];

  text = builtins.readFile ./src/wicf;
}
