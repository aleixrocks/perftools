{writeShellScript}:

writeShellScript "setaff.sh" ''
  set -e

  verbose=False
  cores="0-$(( $(nproc) - 1 ))"
  mode="colocation"

  function prdbg() {
    if [ "$verbose" == True ]; then
      >&2 echo "$rankid: $@"
    fi
  }

  # Expand ranges: "0-3,8-10" -> "0,1,2,3,8,9,10"
  function expand_ranges() {
      local input="$1"
      local result=()
      
      IFS=',' read -ra parts <<< "$input"
      for part in "''${parts[@]}"; do
          if [[ $part =~ ^([0-9]+)-([0-9]+)$ ]]; then
              # It's a range
              local start="''${BASH_REMATCH[1]}"
              local end="''${BASH_REMATCH[2]}"
              for ((i=start; i<=end; i++)); do
                  result+=("$i")
              done
          else
              # It's a single number
              result+=("$part")
          fi
      done
      
      # Join with commas
      local IFS=','
      echo "''${result[*]}"
  }
  
  # Compress to ranges: "0,1,2,3,8,9,10" -> "0-3,8-10"
  function compress_ranges() {
      local input="$1"
      local result=()
      
      # Convert to sorted array of unique numbers
      IFS=',' read -ra nums <<< "$input"
      # Sort numerically and remove duplicates
      local sorted=($(printf '%s\n' "''${nums[@]}" | sort -n | uniq))
      
      local i=0
      while [ $i -lt ''${#sorted[@]} ]; do
          local start=''${sorted[$i]}
          local end=$start
          
          # Find consecutive numbers
          while [ $((i + 1)) -lt ''${#sorted[@]} ] && [ ''${sorted[$((i + 1))]} -eq $((end + 1)) ]; do
              ((i++))
              end=''${sorted[$i]}
          done
          
          # Add to result
          if [ $start -eq $end ]; then
              result+=("$start")
          elif [ $((end - start)) -eq 1 ]; then
              # Two consecutive numbers, don't use range notation
              result+=("$start" "$end")
          else
              result+=("$start-$end")
          fi
          
          ((i++))
      done
      
      # Join with commas
      local IFS=','
      echo "''${result[*]}"
  }

  function run_mode_colocation() {
    if [ $total_cores_per_rank -gt $ncores ]; then
      >&2 echo "Error: setaff: Allocated $ncores cores ($cores), but needs $total_cores_per_rank cores in total (nranks X nomp)" 
      exit 1
    fi

    if [ $total_cores_per_rank -lt $ncores ]; then
      >&2 echo "Warning: setaff: The number of allocated cores ($ncores) is less than the total number of cores requested ($total_cores_per_rank)"
    fi

    local starting_core=$((cores_per_rank * rankid))
    local last_core=$((starting_core + cores_per_rank - 1))

    prdbg "starting_core: $starting_core"
    prdbg "last_core: $last_core"

    local cpumask_raw=""
    for ((i = $starting_core; i <= $last_core; i++)); do
      local cpu=''${core_list[$i]}
      cpumask_raw="''${cpumask_raw},$cpu"
      prdbg "cpuloop $i: $cpu"
      local i=$((i++))
    done
    mode_cpumask_raw="''${cpumask_raw:1}"
    prdbg "mode_cpumask_raw: $mode_cpumask_raw"
  }

  function run_mode_nosv() {
    if [ $(( ncores % nnosv  )) -ne 0 ]; then
      >&2 echo "Error: setaff: The number of cores ($cores) is not divible by the number of nosv instances"
      exit 1
    fi

    local cores_per_nosv=$((ncores / nnosv))
    local ranks_per_nosv=$((nranks / nnosv))
    local nosvid=$((rankid/ranks_per_nosv))

    prdbg "cores_per_nosv: $cores_per_nosv"
    prdbg "ranks_per_nosv: $ranks_per_nosv"
    prdbg "nosvid: $nosvid"

    local starting_cpu=$((nosvid * cores_per_nosv))
    local last_cpu=$((starting_cpu + cores_per_nosv - 1))

    prdbg "starting_core: $starting_core"
    prdbg "last_core: $last_core"

    local cpumask_raw=""
    for ((i = $starting_cpu; i <= $last_cpu; i++)); do
        local cpu=''${core_list[$i]}
        cpumask_raw="''${cpumask_raw},$cpu"
        prdbg "cpuloop $i: $cpu"
        local i=$((i++))
    done

    mode_cpumask_raw="''${cpumask_raw:1}"
    mode_env="env NOSV_CONFIG_OVERRIDE=$NOSV_CONFIG_OVERRIDE,shared_memory.name=nosv-sc-$nosvid"

    prdbg "mode_cpumask_raw: $mode_cpumask_raw"
    prdbg "mode_env: $mode_env"
  }

  # Argument parsing
  while getopts "vc:n:" opt; do
    case "$opt" in
    c)
      cores=$OPTARG
    ;;
    v)
      verbose=True
    ;;
    n)
      nnosv=$OPTARG
      mode="nosv"
    ;;
    esac
  done

  shift $((OPTIND-1))

  # MPI env parsing
  if [ "$PMI_RANK" != "" ]; then
    # hydra mode
    rankid=''${PMI_RANK:-0}
    nranks=''${PMI_SIZE:-1}
  elif [ "$SLURM_PROCID" != "" ]; then
    # slurm mode
    rankid=''${SLURM_PROCID:-''${SLURM_LOCALID:-0}}
    nranks=''${SLURM_NTASKS:-''${SLURM_NPROCS:-1}}
    #cores_per_task=''${SLURM_CPUS_PER_TASK:-1}
  else
    >&2 echo "Error: No MPI env is set"
    exit 1
  fi

  if [ "$OMP_NUM_THREADS" == "" ]; then
    >&2 echo "Error: OMP_NUM_THREADS must be set"
    exit 1
  fi

  # Core list computation

  core_list_raw=$(expand_ranges $cores)

  prdbg "mode: $mode"
  prdbg "core_list_raw: $core_list_raw"
  IFS=',' read -ra core_list <<< "$core_list_raw"
  prdbg "core_list: ''${core_list[@]}"
  ncores=''${#core_list[@]}
  prdbg "ncores: $ncores"
  cores_per_rank=OMP_NUM_THREADS
  prdbg "cores_per_rank: $cores_per_rank"
  total_cores_per_rank=$((cores_per_rank * nranks))
  prdbg "total_cores_per_rank: $total_cores_per_rank"

  eval "run_mode_$mode"
  prdbg "mode_cpumask_raw: $mode_cpumask_raw"
  cpumask=$(compress_ranges $mode_cpumask_raw)

  cmd="exec taskset -c $cpumask $mode_env $@"
  echo "setaff: $rankid: $cmd"

  # Run the command with taskset
  eval "$cmd"
''

