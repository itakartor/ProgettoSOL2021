#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>

#include "queue.h" //ho incluso la definizione di coda
#include "util.h"
Queue* parser(char* argv[],int argc);