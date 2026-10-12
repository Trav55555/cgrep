#include <stdio.h>
#include "cgrep.h"

int main(int argc, char **argv)
{
    return cgrep_main(argc, argv, stdin, stdout, stderr);
}
