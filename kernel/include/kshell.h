#pragma once

#include "filesystem.h"

#define NODE_STACK_MAX_LENGTH 64

void shell_set_root_node(fs_node_t node);
void shell_start();
