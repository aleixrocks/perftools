{
  writeShellApplication
, bpftrace
, python3
}:

writeShellApplication {
  name = "my-script";

  runtimeInputs = [
    bpftrace
    python3
  ];

  text = builtins.readFile ./src/wicf;
}
