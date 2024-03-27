#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#ifndef _WIN32
#include <getopt.h>
#include <unistd.h>
#endif
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "reptester.h"
#include "rdtsc_utils.h"

#define MY_ERROR(...) {                    \
        fprintf(stderr, __VA_ARGS__);   \
        exit(1);                        \
    }

struct test_context {
    char *name;
    size_t buffer_size;
    uint8_t *buffer;
    uint64_t min_cpu_ticks;
    uint64_t max_cpu_ticks;
};

void env_setup(void *context) {
    struct test_context *ctx = (struct test_context *)context;

    printf("[%s] name[%s]\n", __FUNCTION__, ctx->name);

    ctx->buffer_size = 1024*1024*1024;
    ctx->buffer = malloc(ctx->buffer_size);
    if (!ctx->buffer) {
         MY_ERROR("Malloc failed for size[%zu]\n", ctx->buffer_size);
    }
    printf("[%s] Allocated Buffer              [%lu] bytes @ [%p]\n", __FUNCTION__, ctx->buffer_size, ctx->buffer);
}

void env_teardown(void *context) {
    struct test_context *ctx = (struct test_context *)context;
    printf("[%s] name[%s]\n", __FUNCTION__, ctx->name);

    free(ctx->buffer);
    ctx->buffer = 0;
}

void test_setup(void *context) {
    // struct test_context *ctx = (struct test_context *)context;
}

void test_main(void *context) {
    bool new_new_line = false;
    struct test_context *ctx = (struct test_context *)context;

    uint64_t data = 0x5a5a5a5a5a5a5a5a;
    uint32_t count = ctx->buffer_size / sizeof(uint64_t);
    uint64_t *ptr = (uint64_t *)ctx->buffer;

    uint64_t start = GET_CPU_TICKS();
    for (uint32_t i=0; i<count; i++) {
        *ptr = data | i;
        ptr++;
    }
    uint64_t elapsed_ticks = GET_CPU_TICKS() - start;

    if (ctx->min_cpu_ticks==0 || elapsed_ticks < ctx->min_cpu_ticks) {
        ctx->min_cpu_ticks = elapsed_ticks;
        printf(" | New MinTime");
        print_data_speed(ctx->buffer_size, elapsed_ticks);
        new_new_line = true;
    }
    if (elapsed_ticks > ctx->max_cpu_ticks) {
        ctx->max_cpu_ticks = elapsed_ticks;
        printf(" | New MaxTime");
        print_data_speed(ctx->buffer_size, elapsed_ticks);
        new_new_line = true;
    }

    if (new_new_line) {
        printf("\n");
    }
}

void test_teardown(void *context) {
    // struct test_context *ctx = (struct test_context *)context;
}


void print_stats(void *context) {
    struct test_context *ctx = (struct test_context *)context;
    struct rusage usage = {};

    printf("[%s] name[%s]\n", __FUNCTION__, ctx->name);
    printf("\n");

    printf("[%s] Slowest Speed", __FUNCTION__);
    print_data_speed(ctx->buffer_size, ctx->max_cpu_ticks);
    printf("\n\n");

    printf("[%s] Fastest Speed", __FUNCTION__);
    print_data_speed(ctx->buffer_size, ctx->min_cpu_ticks);
    printf("\n\n");

    int ret = getrusage(RUSAGE_SELF, &usage);
    if (ret != 0) {
        MY_ERROR("Failed to call getrusage errno(%d)[%s]\n", errno, strerror(errno));
    }
    printf("[%s] Soft PageFautls (No IO): %lu  | Hard PageFautls (IO): %lu\n", __FUNCTION__, usage.ru_minflt, usage.ru_majflt);
    printf("\n");
}




void usage(void) {
    fprintf(stderr, "Rep Test 1 Usage:\n");
    fprintf(stderr, "-h             This help dialog.\n");
    fprintf(stderr, "-t <runtime>   Set runtime in seconds. (defaults to 10seconds)\n");
}

int main (int argc, char *argv[]) {
    int opt;
    int runtime = 10;

    while( (opt = getopt(argc, argv, "ht:")) != -1) {
        switch (opt) {
            case 'h':
                usage();
                exit(0);
                break;

            case 't':
                runtime = atoi(optarg);
                break;

            default:
                fprintf(stderr, "MY_ERROR Invalid command line option\n");
                usage();
                exit(1);
                break;
        }
    }

    printf("==============\n");
    printf("REP Test 3\n");
    printf("==============\n");


    printf("Using runtime   [%d]seconds\n", runtime);

    struct rep_tester_config foo = {};
    foo.env_setup = env_setup;
    foo.test_setup = test_setup;
    foo.test_main = test_main;
    foo.test_teardown = test_teardown;
    foo.env_teardown = env_teardown;
    foo.print_stats = print_stats;
    foo.test_runtime_seconds = runtime;

    struct test_context my_context = {};
    my_context.name = "WriteTest_no_malloc";


    printf("\n\n");

    rep_tester(&foo, &my_context);
    
    printf("\n\n");


    return 0;
}
