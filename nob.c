#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "nob.h"

#define BUILD_FOLDER "./build/"

static Cmd cmd = {0};

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);

    if (!nob_mkdir_if_not_exists(BUILD_FOLDER)) return 1;

    cmd_append(&cmd, "cc", "-o", BUILD_FOLDER"platform", "main.c");
    cmd_append(&cmd, "-Wall", "-Wextra", "-Werror");
    cmd_append(&cmd, "-lraylib", "-lm", "-L~/Coding/raylib/raylib-5.5_linux_amd64/include");
    if (!cmd_run(&cmd)) return 1;

    cmd_append(&cmd, BUILD_FOLDER"platform");
    if (!cmd_run(&cmd)) return 1;

    return 0;
}
