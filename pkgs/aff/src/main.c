#define _GNU_SOURCE
#include <stdio.h>
#include <sched.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

int get_num_cpus() {
    return sysconf(_SC_NPROCESSORS_CONF);
}

void print_hex(cpu_set_t *mask, int num_cpus) {
    int num_bits = num_cpus;
    int num_hex_digits = (num_bits + 3) / 4;  // Round up to nearest hex digit
    
    printf("0x");
    for (int digit = 0; digit < num_hex_digits; digit++) {
        int value = 0;
        for (int bit = 0; bit < 4; bit++) {
            int cpu = digit * 4 + bit;
            if (cpu < num_cpus && CPU_ISSET(cpu, mask)) {
                value |= (1 << bit);
            }
        }
        printf("%x", value);
    }
}

void print_binary(cpu_set_t *mask, int num_cpus) {
    for (int i = 0; i < num_cpus; i++) {
        printf("%d", CPU_ISSET(i, mask) ? 1 : 0);
    }
}

void print_decimal(cpu_set_t *mask) {
    int first = 1;
    for (int i = 0; i < CPU_SETSIZE; i++) {
        if (CPU_ISSET(i, mask)) {
            if (!first) printf(",");
            printf("%d", i);
            first = 0;
        }
    }
}

void print_grouped(cpu_set_t *mask) {
    int first = 1;
    int in_range = 0;
    int range_start = -1;
    
    for (int i = 0; i < CPU_SETSIZE; i++) {
        if (CPU_ISSET(i, mask)) {
            if (!in_range) {
                if (!first) printf(",");
                printf("%d", i);
                range_start = i;
                in_range = 1;
                first = 0;
            }
        } else {
            if (in_range) {
                if (i - 1 > range_start) {
                    printf("-%d", i - 1);
                }
                in_range = 0;
            }
        }
    }
    
    if (in_range && CPU_SETSIZE - 1 > range_start) {
        printf("-%d", CPU_SETSIZE - 1);
    }
}

void print_help(const char *prog_name) {
    printf("Usage: %s [OPTIONS]\n", prog_name);
    printf("\nPrint the current process PID and CPU affinity.\n");
    printf("\nOptions:\n");
    printf("  -h, --help           Show this help message and exit\n");
    printf("  -d, --decimal        Print affinity as comma-separated decimal list (default)\n");
    printf("  -g, --grouped        Print affinity as grouped ranges (e.g., 0-3,7-8)\n");
    printf("  -b, --binary         Print affinity as binary string (all CPUs)\n");
    printf("  -x, --hex            Print affinity as hexadecimal (all CPUs)\n");
    printf("\nExamples:\n");
    printf("  %s                   # Default decimal output\n", prog_name);
    printf("  %s -b                # Binary output\n", prog_name);
    printf("  %s --hex             # Hexadecimal output\n", prog_name);
}

int main(int argc, char *argv[]) {
    pid_t pid = getpid();
    cpu_set_t mask;
    
    char *mode = "decimal";
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--decimal") == 0) {
            mode = "decimal";
        } else if (strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--grouped") == 0) {
            mode = "grouped";
        } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--binary") == 0) {
            mode = "binary";
        } else if (strcmp(argv[i], "-x") == 0 || strcmp(argv[i], "--hex") == 0) {
            mode = "hex";
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
            return 1;
        }
    }
    
    CPU_ZERO(&mask);
    if (sched_getaffinity(pid, sizeof(mask), &mask) == -1) {
        perror("sched_getaffinity");
        return 1;
    }
    
    int num_cpus = get_num_cpus();
    
    printf("PID: %6d, CPU affinity: ", pid);
    
    if (strcmp(mode, "hex") == 0) {
        print_hex(&mask, num_cpus);
    } else if (strcmp(mode, "binary") == 0) {
        print_binary(&mask, num_cpus);
    } else if (strcmp(mode, "grouped") == 0) {
        print_grouped(&mask);
    } else {
        print_decimal(&mask);
    }
    
    printf("\n");
    
    return 0;
}
