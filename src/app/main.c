#include <stdio.h>
#include <string.h>

#ifndef TRADING_ENGINE_VERSION
#define TRADING_ENGINE_VERSION "0.0.0-dev"
#endif

static void print_usage(const char *prog) {
    printf("C Trading Engine %s\n", TRADING_ENGINE_VERSION);
    printf("\n");
    printf("Usage: %s [options]\n", prog);
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help       Show this help and exit\n");
    printf("  -v, --version    Show version and exit\n");
    printf("      --data-dir   Engine data directory (reserved; not used yet)\n");
    printf("\n");
    printf("The engine runtime is not implemented yet (stage 0: repository skeleton).\n");
}

static void print_version(void) {
    printf("trading-engine %s\n", TRADING_ENGINE_VERSION);
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(argv[i], "--data-dir") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --data-dir requires a value\n");
                return 2;
            }
            i++;
            continue;
        }
        fprintf(stderr, "error: unknown option '%s' (try --help)\n", argv[i]);
        return 2;
    }

    fprintf(stderr, "error: engine runtime not implemented yet (stage 0); see --help\n");
    return 1;
}
