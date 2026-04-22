// nest_level.c – build with:
//   clang -shared -fPIC -O2 -fopenmp -o libnest_level.so nest_level.c
#define _GNU_SOURCE
#include <omp.h>
#include <omp-tools.h>
#include <stdio.h>
#include <stdint.h>

static int max_level_seen = 0;

/* ---------- Callback that fires at every parallel begin ---------- */
static void on_parallel_begin(
    ompt_data_t *encountering_task_data,
    const ompt_frame_t *encountering_task_frame, ompt_data_t *parallel_data,
    uint32_t requested_team_size, int flag, const void *codeptr_ra)
{

    int level = omp_get_level();          /* total nesting depth */
    if (level > max_level_seen) max_level_seen = level;
}

/* ---------- OMPT tool boiler-plate ---------- */
static ompt_set_callback_t ompt_set_callback = NULL;

static int tool_init(ompt_function_lookup_t lookup,
                     int initial_device_num,
                     ompt_data_t *tool_data)
{
    ompt_set_callback = (ompt_set_callback_t)lookup("ompt_set_callback");
    ompt_set_callback(ompt_callback_parallel_begin,
                      (ompt_callback_t)on_parallel_begin);
    return 1;      /* success */
}

static void tool_finalize(ompt_data_t *tool_data)
{
    /* Runs once, after the application exits */
    printf("\n[nest_level] deepest omp_get_level() observed = %d\n",
           max_level_seen);
}

ompt_start_tool_result_t *ompt_start_tool(unsigned int omp_version,
                                          const char *runtime_version)
{
    static ompt_start_tool_result_t result = {tool_init, tool_finalize, 0};
    return &result;
}

