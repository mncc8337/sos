#include "kshell.h"
#include "kbd.h"
#include "rtc.h"
#include "timer.h"
#include "tty.h"
#include "filesystem.h"

#include "string.h"
#include "stdio.h"
#include "stdlib.h"

#include "time.h"

typedef enum {
    ERR_SHELL_NOT_FOUND,
    ERR_SHELL_SUCCESS,
    ERR_SHELL_TARGET_NOT_FOUND,
    ERR_SHELL_NOT_A_DIR
} SHELL_ERR;

static char input[512];
static unsigned int input_len = 0;

// a stack that contain the path of current dir
static fs_node_t node_stack[NODE_STACK_MAX_LENGTH];
static unsigned int node_stack_offset = 0;

static fs_node_t* node_stack_top() {
    return node_stack + node_stack_offset;
}
static bool node_stack_push(fs_node_t node) {
    if(node_stack_offset == NODE_STACK_MAX_LENGTH - 1)
        return false;

    node_stack[++node_stack_offset] = node;
    return true;
}
static void node_stack_pop() {
    if(node_stack_offset > 0) node_stack_offset--;
}

static volatile bool key_handled = true;
static volatile key_t current_key;
static void kbd_listener(key_t k) {
    if(k.released) return;
    key_handled = false;
    current_key = k;
}
static void tick_listener(unsigned int ticks) {
    (void)(ticks);

    // put some sheduled tasks here
}

static int indent_level = 0;
static int max_depth = 0;
static bool show_hidden = false;
static bool list_dir(fs_node_t node) {
    if((node.hidden || node.name[0] == '.') && !show_hidden) return true;
    if(indent_level >= max_depth) return true;
    indent_level++;

    for(int i = 0; i < indent_level-1; i++)
        printf("|   ");
    printf("|---");

    if(node.isdir) tty_set_attr(LIGHT_BLUE);
    puts(node.name);
    if(node.isdir) tty_set_attr(LIGHT_GREY);

    if(node.isdir && !strcmp(node.name, ".") && !strcmp(node.name, ".."))
        fs_list_dir(&node, list_dir);

    indent_level--;

    return true;
}
static int path_find_last_node(char* path, fs_node_t* parent, fs_node_t* node) {
    node->valid = false;
    *parent = *(node_stack_top());

    if(path[0] == '/') {
        *node = node_stack[0];
        *parent = node_stack[0];
        path++;
    }

    char* nodename = strtok(path, "/");
    while(nodename != NULL) {
        if(node->valid) *parent = *node;
        // root dir does not have the . and .. dir so we need to handle it differently
        if((strcmp(nodename, ".") || strcmp(nodename, ".."))
                && parent->name[0] == '/')
            *node = node_stack[0];
        else *node = fs_find(parent, nodename);
        if(!node->valid) {
            memcpy(node->name, nodename, strlen(nodename)+1);
            if(strtok(NULL, "/") != NULL)
                return ERR_SHELL_NOT_FOUND;
            return ERR_SHELL_TARGET_NOT_FOUND;
        }
        nodename = strtok(NULL, "/");
        if(nodename != NULL && !node->isdir) // this is illegal
            return ERR_SHELL_NOT_A_DIR;
    }

    return ERR_SHELL_SUCCESS;
}

static void help(char* arg) {
    if(arg == NULL) {
        puts("help .<n dot> echo ticks ls read cd mkdir rm touch write mv cp stat pwd");
    }
    else {
        if(arg[0] == '.') printf("go back %d dir\n", strlen(arg)-1);
        else if(strcmp(arg, "echo")) puts("echo <string>");
        else if(strcmp(arg, "ticks")) puts("ticks <no-args>");
        else if(strcmp(arg, "ls")) puts(
                "ls <args> <directory>\n"
                "available arg:\n"
                "    -a          show hidden\n"
                "    -d <num>    tree depth");
        else if(strcmp(arg, "read")) puts("read <path>");
        else if(strcmp(arg, "cd")) puts("cd <path>");
        else if(strcmp(arg, "mkdir")) puts("mkdir <path>");
        else if(strcmp(arg, "rm")) puts("rm <path>");
        else if(strcmp(arg, "touch")) puts("touch <path>");
        else if(strcmp(arg, "write")) puts("write <path>");
        else if(strcmp(arg, "mv")) puts("mv <source-path> <destination-path>");
        else if(strcmp(arg, "cp")) puts("cp <source-path> <destination-path>");
        else if(strcmp(arg, "stat")) puts("stat <path>");
        else if(strcmp(arg, "pwd")) puts("pwd <no-args>");
        else if(strcmp(arg, "datetime")) puts("datetime <no-args>");
    }
}

static void echo(char* args) {
    puts(args);
}

static void ticks(char* args) {
    (void)(args);
    printf("%d\n", timer_get_ticks());
}

static void ls(char* args) {
    fs_node_t* current_node = node_stack_top();
    if(!current_node->valid) {
        puts("no fs installed");
        return;
    }
    max_depth = 1;
    show_hidden = false;
    char* ls_name = NULL;

    char* arg = strtok(args, " ");
    while(arg != NULL) {
        if(arg[0] == '-') {
            if(strcmp(arg, "-d")) {
                arg = strtok(NULL, " ");
                if(arg == NULL) {
                    puts("not enough arguement");
                    return;
                }
                max_depth = atoi(arg);
            }
            else if(strcmp(arg, "-a"))
                show_hidden = true;
            else {
                printf("unknown argument: %s\n", arg);
                return;
            }
        }
        else
            ls_name = arg;

        arg = strtok(NULL, " ");
    }

    if(ls_name == NULL) {
        // list current dir
        fs_list_dir(current_node, list_dir);
        return;
    }
    else {
        fs_node_t node_parent;
        fs_node_t node;
        SHELL_ERR err = path_find_last_node(ls_name, &node_parent, &node);
        if(err == ERR_SHELL_NOT_FOUND
                || err == ERR_SHELL_NOT_A_DIR
                || err == ERR_SHELL_TARGET_NOT_FOUND
                || !node.isdir) {
            printf("no such directory '%s'\n", node.name);
            return;
        }

        fs_list_dir(&node, list_dir);
    }
}

static void read(char* path) {
    fs_node_t* current_node = node_stack_top();
    if(!current_node->valid) {
        puts("no fs installed");
        return;
    }

    if(path == NULL) {
        puts("no file input");
        return;
    }

    fs_node_t node_parent;
    fs_node_t node;
    SHELL_ERR err = path_find_last_node(path, &node_parent, &node);
    if(err == ERR_SHELL_NOT_FOUND || err == ERR_SHELL_NOT_A_DIR) {
        printf("no such directory '%s'\n", node.name);
        return;
    }
    if(err == ERR_SHELL_TARGET_NOT_FOUND || node.isdir) {
        printf("no such file '%s'\n", node.name);
        return;
    }

    FILE f = file_open(&node, FILE_READ);
    char chr;
    while(file_read(&f, (uint8_t*)(&chr), 1) != ERR_FS_EOF) {
        putchar(chr);
    }
}

static void cd(char* path) {
    fs_node_t* current_node = node_stack_top();
    if(!current_node->valid) {
        puts("no fs installed");
        return;
    }
    if(path == NULL) {
        puts("no path provided");
        return;
    }

    if(path[0] == '/') {
        // to rootdir
        node_stack_offset = 0;
        path++;
    }

    char* nodename = strtok(path, "/");
    while(nodename != NULL) {
        if(strcmp(nodename, "..")) node_stack_pop();
        else if(!strcmp(nodename, ".")) {
            fs_node_t tmp = fs_find(node_stack_top(), nodename);
            if(!tmp.valid) {
                printf("no such directory '%s'\n", nodename);
                return;
            }
            if(!tmp.isdir) {
                printf("no such directory '%s'\n", nodename);
                return;
            }

            bool pushed = node_stack_push(tmp);
            if(!pushed) {
                printf("reached node stack limit, cannot cd into '%s'\n", nodename);
                return;
            }
        }

        nodename = strtok(NULL, "/");
    }
}

static void mkdir(char* path) {
    fs_node_t* current_node = node_stack_top();
    if(!current_node->valid) {
        puts("no fs installed");
        return;
    }

    if(path == NULL) {
        puts("no name provided");
        return;
    }

    fs_node_t _curr_node = *current_node;

    if(path[0] == '/') {
        _curr_node = node_stack[0];
        path++;
    }

    char* dirname = strtok(path, "/");
    while(dirname != NULL) {
        fs_node_t node = fs_find(&_curr_node, dirname);
        if(node.valid) {
            printf("a file or directory with name '%s' has already existed\n", dirname);
            return;
        }

        fs_node_t newdir = fs_mkdir(&_curr_node, dirname);
        if(!newdir.valid) {
            printf("failed to create directory '%s'\n", dirname);
            return;
        }
        _curr_node = newdir;
        dirname = strtok(NULL, "/");
    }
}

static void rm(char* path) {
    fs_node_t* current_node = node_stack_top();
    if(!current_node->valid) {
        puts("no fs installed");
        return;
    }
    if(path == NULL) {
        puts("no name provided");
        return;
    }
    
    fs_node_t node_parent;
    fs_node_t node;
    SHELL_ERR serr = path_find_last_node(path, &node_parent, &node);
    if(serr == ERR_SHELL_NOT_FOUND || serr == ERR_SHELL_NOT_A_DIR) {
        printf("no such directory '%s'\n", node.name);
        return;
    }
    if(serr == ERR_SHELL_TARGET_NOT_FOUND) {
        printf("no such file or directory '%s'\n", node.name);
        return;
    }

    FS_ERR err = fs_rm_recursive(&node_parent, node);
    if(err != ERR_FS_SUCCESS)
        printf("cannot remove '%s'. error code %d\n", node.name, err);
}

static void touch(char* path) {
    fs_node_t* current_node = node_stack_top();
    if(!current_node->valid) {
        puts("no fs installed");
        return;
    }
    if(path == NULL) {
        puts("no name provided");
        return;
    }

    fs_node_t node_parent;
    fs_node_t node;
    SHELL_ERR err = path_find_last_node(path, &node_parent, &node);
    if(err == ERR_SHELL_NOT_FOUND || err == ERR_SHELL_NOT_A_DIR) {
        printf("'%s' is not a directory\n", node.name);
        return;
    }
    if(err == ERR_SHELL_SUCCESS) {
        // we dont want it to find a valid node
        printf("a file or directory with name '%s' has already existed\n", path);
        return;
    }
    // at this point the error should be ERR_SHELL_TARGET_NOT_FOUND
    // which is what we wanted

    node = fs_touch(&node_parent, node.name);
    if(!node.valid) printf("cannot create '%s', out of space\n", node.name);
}

static void write(char* path) {
    fs_node_t* current_node = node_stack_top();
    if(!current_node->valid) {
        puts("no fs installed");
        return;
    }
    if(path == NULL) {
        puts("no name provided");
        return;
    }

    fs_node_t node_parent;
    fs_node_t node;
    SHELL_ERR err = path_find_last_node(path, &node_parent, &node);
    if(err == ERR_SHELL_NOT_FOUND || err == ERR_SHELL_NOT_A_DIR) {
        printf("no such directory '%s'\n", node.name);
        return;
    }
    if(err == ERR_SHELL_TARGET_NOT_FOUND) {
        // try to create one
        node = fs_touch(&node_parent, node.name);
        if(!node.valid) {
            printf("cannot create file '%s', out of space\n", node.name);
            return;
        }
    }

    FILE f = file_open(&node, FILE_WRITE);

    input_len = 0;
    puts("writing mode. press ESC to exit");

    while(current_key.keycode != 0) { // 0 is esc keycode
        while(key_handled) continue; // wait for new key
        key_handled = true;

        if(current_key.mapped == '\b') {
            if(input_len == 0) continue;
            tty_set_cursor(tty_get_cursor() - 1); // move back
            tty_print_char(' ', -1, 0, false); // delete printed char
            input_len--;
            continue;
        }

        putchar(current_key.mapped);
        input[input_len++] = current_key.mapped;
        if(current_key.mapped == '\n' || input_len >= 512) {
            file_write(&f, (uint8_t*)input, input_len);
            input_len = 0;
        }
    }

    if(input_len > 0)
        file_write(&f, (uint8_t*)input, input_len);
    file_close(&f);
}

static void mv(char* args) {
    fs_node_t* current_node = node_stack_top();
    if(!current_node->valid) {
        puts("no fs installed");
        return;
    }

    char* source_path = strtok(args, " ");
    if(source_path == NULL) {
        puts("no source provided");
        return;
    }
    char* target_path = strtok(NULL, " ");
    if(target_path == NULL) {
        puts("no target provided");
        return;
    }

    fs_node_t source_node_parent;
    fs_node_t source_node;
    SHELL_ERR err = path_find_last_node(source_path, &source_node_parent, &source_node);
    if(err == ERR_SHELL_NOT_FOUND || err == ERR_SHELL_NOT_A_DIR) {
        printf("no such directory '%s'\n", source_node.name);
        return;
    }
    if(err == ERR_SHELL_TARGET_NOT_FOUND) {
        printf("no such file or directory '%s'\n", source_node.name);
        return;
    }

    fs_node_t target_node_parent;
    fs_node_t target_node;
    err = path_find_last_node(target_path, &target_node_parent, &target_node);
    if(err == ERR_SHELL_NOT_FOUND || err == ERR_SHELL_NOT_A_DIR) {
        printf("no such directory '%s'\n", target_node.name);
        return;
    }
    if(err == ERR_SHELL_SUCCESS) {
        if(!target_node.isdir) {
            printf("a file with name '%s' has already existed\n", target_node.name);
            return;
        }
        else {
            target_node_parent = target_node;
            memcpy(target_node.name, source_node.name, strlen(source_node.name)+1);
        }
    }

    FS_ERR ferr = fs_move(&source_node, &target_node_parent, target_node.name);
    if(ferr != ERR_FS_SUCCESS)
        printf("failed to move %s to %s with name %s. error code %d\n", source_node.name, target_node_parent.name, target_node.name, ferr);
}

static void cp(char* args) {
    fs_node_t* current_node = node_stack_top();
    if(!current_node->valid) {
        puts("no fs installed");
        return;
    }

    char* source_path = strtok(args, " ");
    if(source_path == NULL) {
        puts("no source provided");
        return;
    }
    char* target_path = strtok(NULL, " ");
    if(target_path == NULL) {
        puts("no target provided");
        return;
    }

    fs_node_t source_node_parent;
    fs_node_t source_node;
    SHELL_ERR err = path_find_last_node(source_path, &source_node_parent, &source_node);
    if(err == ERR_SHELL_NOT_FOUND || err == ERR_SHELL_NOT_A_DIR) {
        printf("no such directory '%s'\n", source_node.name);
        return;
    }
    if(err == ERR_SHELL_TARGET_NOT_FOUND) {
        printf("no such file or directory '%s'\n", source_node.name);
        return;
    }

    fs_node_t target_node_parent;
    fs_node_t target_node;
    err = path_find_last_node(target_path, &target_node_parent, &target_node);
    if(err == ERR_SHELL_NOT_FOUND || err == ERR_SHELL_NOT_A_DIR) {
        printf("no such directory '%s'\n", target_node.name);
        return;
    }
    if(err == ERR_SHELL_SUCCESS) {
        if(!target_node.isdir) {
            printf("a file with name '%s' has already existed\n", target_node.name);
            return;
        }
        else {
            target_node_parent = target_node;
            memcpy(target_node.name, source_node.name, strlen(source_node.name)+1);
        }
    }

    fs_node_t copied;
    FS_ERR ferr = fs_copy_recursive(&source_node, &target_node_parent, &copied, target_node.name);
    if(ferr != ERR_FS_SUCCESS)
        printf("failed to copy %s to %s with name %s. error code %d\n", source_node.name, target_node_parent.name, target_node.name, ferr);
}

static void stat(char* path) {
    fs_node_t* current_node = node_stack_top();
    if(!current_node->valid) {
        puts("no fs installed");
        return;
    }

    if(path == NULL) {
        puts("no input");
        return;
    }

    fs_node_t node_parent;
    fs_node_t node;
    SHELL_ERR err = path_find_last_node(path, &node_parent, &node);
    if(err == ERR_SHELL_NOT_FOUND || err == ERR_SHELL_NOT_A_DIR) {
        printf("no such directory '%s'\n", node.name);
        return;
    }
    if(err == ERR_SHELL_TARGET_NOT_FOUND || node.isdir) {
        printf("no such file '%s'\n", node.name);
        return;
    }

    printf("stat of '%s':\n", node.name);
    printf("    filesystem: %s\n", (node.fs->type == 1 ? "FAT32" : (node.fs->type == 2 ? "ext2" : "unknown")));
    printf("    parent: '%s'\n", node.parent_node->name);
    printf("    type: %s\n", (node.isdir ? "directory" : "file"));
    printf("    hidden: %s\n", (node.hidden ? "true" : "false"));
    printf("    size: %d bytes\n", node.size);
    // TODO: creation/last access/modified datetime
    printf("    start cluster: 0x%x\n", node.start_cluster);
}

static void pwd(char* args) {
    (void)(args);

    fs_node_t* current_node = node_stack_top();
    if(!current_node->valid) {
        puts("no fs installed");
        return;
    }

    if(node_stack_offset == 0) {
        puts("/");
        return;
    }

    for(unsigned int i = 1; i <= node_stack_offset; i++)
        printf("/%s", node_stack[i].name);
    putchar('\n');
}

static void datetime(char* arg) {
    (void)(arg);
    time_t curr_time = rtc_get_current_time();

    printf("seconds since epoch: %d\n", curr_time);
}

static void process_prompt() {
    char* cmd_name = strtok(input, " ");
    char* remain_arg = cmd_name + strlen(cmd_name) + 1;
    while(remain_arg[0] == '\0' || remain_arg[0] == ' ') remain_arg++;
    if(remain_arg - cmd_name >= (signed)input_len) remain_arg = NULL;

    if(strcmp(cmd_name, "help")) help(remain_arg);
    else if(cmd_name[0] == '.') { // special command to go to parent dir
        unsigned int back_cnt = strlen(cmd_name) - 1;
        if(back_cnt > node_stack_offset) node_stack_offset = 0;
        else node_stack_offset -= back_cnt;
    }
    else if(strcmp(cmd_name, "echo")) echo(remain_arg);
    else if(strcmp(cmd_name, "ticks")) ticks(remain_arg);
    else if(strcmp(cmd_name, "ls")) ls(remain_arg);
    else if(strcmp(cmd_name, "read")) read(remain_arg);
    else if(strcmp(cmd_name, "cd")) cd(remain_arg);
    else if(strcmp(cmd_name, "mkdir")) mkdir(remain_arg);
    else if(strcmp(cmd_name, "rm")) rm(remain_arg);
    else if(strcmp(cmd_name, "touch")) touch(remain_arg);
    else if(strcmp(cmd_name, "write")) write(remain_arg);
    else if(strcmp(cmd_name, "mv")) mv(remain_arg);
    else if(strcmp(cmd_name, "cp")) cp(remain_arg);
    else if(strcmp(cmd_name, "stat")) stat(remain_arg);
    else if(strcmp(cmd_name, "pwd")) pwd(remain_arg);
    else if(strcmp(cmd_name, "datetime")) datetime(remain_arg);
    else if(input_len == 0); // just skip
    else puts("unknow command");
    printf("[kernel@kshell %s ]$ ", node_stack[node_stack_offset].name);
    input_len = 0;
    input[0] = '\0';
}

void shell_set_root_node(fs_node_t node) {
    node_stack[0] = node;
    node_stack_offset = 0;
}
void shell_start() {
    install_tick_listener(tick_listener);
    install_key_listener(kbd_listener);
    puts("welcome to the shell");
    puts("type `help` t show all command. `help <command>` to see all available argument");
    printf("[kernel@kshell %s ]$ ", node_stack[node_stack_offset].name);

    while(true) {
        if(key_handled) continue; // wait for new key
        key_handled = true;

        // ignore non printable characters
        if(current_key.mapped == '\0') continue;

        if(current_key.mapped == '\b') {
            if(input_len == 0) continue;
            tty_set_cursor(tty_get_cursor() - 1); // move back
            tty_print_char(' ', -1, 0, false); // delete printed char
            input_len--;
            continue;
        }

        putchar(current_key.mapped);
        if(current_key.mapped != '\n') {
            input[input_len++] = current_key.mapped;
        }
        else {
            input[input_len] = '\0';
            process_prompt();
        }
    }
}
