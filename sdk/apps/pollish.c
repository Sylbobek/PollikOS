/* pollish: the PollikOS interactive shell.
 *
 * Runs as a normal Ring 3 process with stdin/stdout/stderr connected to the
 * console TTY. The shell owns the canonical line editor (echo, cursor motion,
 * history), the tokenizer and the builtins; external commands are real child
 * processes resolved through PATH with spawn/spawnp + waitpid. */
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pollikos/fs.h>
#include <pollikos/process.h>
#include <pollikos/time.h>

#define LINE_MAX 256
#define ARG_MAX 32
#define HISTORY_MAX 128
#define HISTORY_LEN 256
#define HISTORY_FILE "/home/.pollik_history"
#define LIBC_SRC_DIR "/usr/src/libc"
#define LIBC_MARKER "/usr/lib/.native-libc"
#define LIBC_LOCK "/usr/lib/.native-libc.lock"
#define LIBC_OBJECTS_MAX 32
#define ESC "\x1b"

static int last_status;

/* ------------------------------------------------------------------ output */
static void emit(const char *data, size_t length) {
    size_t done = 0;
    while (done < length) {
        ssize_t written = write(1, data + done, length - done);
        if (written <= 0) return;
        done += (size_t)written;
    }
}
static void out(const char *text) { emit(text, strlen(text)); }
static void line_out(const char *text) { out(text); out("\n"); }

/* ----------------------------------------------------------------- history */
static char history[HISTORY_MAX][HISTORY_LEN];
static int history_count, history_head;
static const char *history_at(int index) { return history[(history_head + index) % HISTORY_MAX]; }
static void history_add(const char *text) {
    if (!text[0]) return;
    if (history_count && strcmp(history_at(history_count - 1), text) == 0) return;
    int index;
    if (history_count < HISTORY_MAX) {
        index = (history_head + history_count) % HISTORY_MAX;
        ++history_count;
    } else {
        index = history_head;
        history_head = (history_head + 1) % HISTORY_MAX;
    }
    snprintf(history[index], HISTORY_LEN, "%s", text);
}
static void history_load(void) {
    FILE *file = fopen(HISTORY_FILE, "rb");
    if (!file) return;
    char text[HISTORY_LEN];
    while (fgets(text, sizeof(text), file) != NULL) {
        size_t length = strlen(text);
        while (length && (text[length-1] == '\n' || text[length-1] == '\r')) text[--length] = 0;
        history_add(text);
    }
    fclose(file);
}
static void history_append(const char *text) {
    FILE *file = fopen(HISTORY_FILE, "ab");
    if (!file) return;
    fwrite(text, 1, strlen(text), file);
    fwrite("\n", 1, 1, file);
    fclose(file);
}

/* --------------------------------------------------------------- tokenizer */
static char token_storage[ARG_MAX][LINE_MAX];
static const char *arguments[ARG_MAX + 1];
static uint8_t token_quoted[ARG_MAX];
static void append_text(char *out, size_t *length, const char *text, size_t maximum) {
    while (*text && *length + 1 < maximum) out[(*length)++] = *text++;
}
static int name_start(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
}
static int name_char(char ch) {
    return name_start(ch) || (ch >= '0' && ch <= '9');
}
/* Expand $? and $NAME from the edit buffer itself (never from stdin). */
static void expand_dollar(const char **position, char *out, size_t *length, size_t maximum) {
    const char *text = *position;
    if (*text == '?') {
        char digits[16];
        snprintf(digits, sizeof(digits), "%d", last_status);
        append_text(out, length, digits, maximum);
        *position = text + 1;
        return;
    }
    if (name_start(*text)) {
        char name[64];
        size_t used = 0;
        while (name_char(*text) && used + 1 < sizeof(name)) name[used++] = *text++;
        name[used] = 0;
        const char *value = getenv(name);
        if (value) append_text(out, length, value, maximum);
        *position = text;
        return;
    }
    if (*length + 1 < maximum) out[(*length)++] = '$';
}
static int parse(const char *input) {
    const char *p = input;
    int count = 0;
    while (*p && count < ARG_MAX) {
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p) break;
        char *out = token_storage[count];
        size_t length = 0;
        int quote = 0, started = 0, quoted = 0;
        while (*p && (quote || (*p != ' ' && *p != '\t'))) {
            char ch = *p++;
            if (quote == '\'') {
                if (ch == '\'') { quote = 0; quoted = 1; }
                else if (length + 1 < LINE_MAX) out[length++] = ch;
                started = 1;
                continue;
            }
            if (quote == '"') {
                if (ch == '"') { quote = 0; quoted = 1; continue; }
                if (ch == '\\' && *p) ch = *p++;
                else if (ch == '$') { expand_dollar(&p, out, &length, LINE_MAX); started = 1; continue; }
            } else {
                if (ch == '\'') { quote = '\''; started = 1; quoted = 1; continue; }
                if (ch == '"') { quote = '"'; started = 1; quoted = 1; continue; }
                if (ch == '\\' && *p) ch = *p++;
                else if (ch == '$') { expand_dollar(&p, out, &length, LINE_MAX); started = 1; continue; }
            }
            if (length + 1 < LINE_MAX) out[length++] = ch;
            started = 1;
        }
        out[length] = 0;
        if (started) {
            token_quoted[count] = (uint8_t)quoted;
            arguments[count++] = out;
        } else if (count < ARG_MAX) break;
    }
    arguments[count] = 0;
    return count;
}

/* --------------------------------------------------------------- builtins */
static int builtin_cd(int argc, const char *const *argv) {
    const char *target = argc > 1 ? argv[1] : "/";
    if (chdir(target) != 0) {
        printf("cd: %s: %s\n", target, strerror(errno));
        return 1;
    }
    return 0;
}
static int builtin_pwd(int argc, const char *const *argv) {
    (void)argc; (void)argv;
    char cwd[256];
    if (!getcwd(cwd, sizeof(cwd))) {
        printf("pwd: %s\n", strerror(errno));
        return 1;
    }
    line_out(cwd);
    return 0;
}
static int builtin_echo(int argc, const char *const *argv) {
    for (int index = 1; index < argc; ++index) {
        if (index > 1) out(" ");
        out(argv[index]);
    }
    out("\n");
    return 0;
}
static int builtin_clear(int argc, const char *const *argv) {
    (void)argc; (void)argv;
    out(ESC "[2J" ESC "[3J" ESC "[H");
    return 0;
}
static int builtin_history(int argc, const char *const *argv) {
    if (argc > 1 && strcmp(argv[1], "-c") == 0) {
        history_count = history_head = 0;
        return 0;
    }
    for (int index = 0; index < history_count; ++index)
        printf("%4d  %s\n", index + 1, history_at(index));
    return 0;
}
static int builtin_help(int argc, const char *const *argv) {
    (void)argc; (void)argv;
    line_out("PollikOS shell builtins:");
    line_out("  cd [dir]        change directory (no argument: /)");
    line_out("  pwd             print the working directory");
    line_out("  ls | dir [dir]  list directory entries ('/' marks directories)");
    line_out("  cat | type FILE print file contents");
    line_out("  mkdir DIR       create a directory");
    line_out("  rm | del FILE   remove a file");
    line_out("  rmdir DIR       remove an empty directory");
    line_out("  mv | rename A B rename or move a path");
    line_out("  touch FILE      create an empty file");
    line_out("  echo ARGS       print arguments ($? and $NAME expand)");
    line_out("  history [-c]    list or clear session history");
    line_out("  env | set       list the environment; set NAME=VALUE adds one");
    line_out("  status          print the last command exit status");
    line_out("  clear | cls     clear the screen and scrollback");
    line_out("  help            this text");
    line_out("  exit [code]     leave the shell");
    line_out("Pipelines and redirection (operators need spaces):");
    line_out("  cmd1 | cmd2     pipe stdout into stdin");
    line_out("  cmd < in        read stdin from a file");
    line_out("  cmd > out       write stdout to a file");
    line_out("  cmd >> out      append stdout to a file");
    line_out("Anything else runs /bin, PATH, ./ or /absolute programs.");
    return 0;
}
static int builtin_ls(int argc, const char *const *argv) {
    const char *path = argc > 1 ? argv[1] : ".";
    DIR *directory = opendir(path);
    if (!directory) {
        printf("ls: %s: %s\n", path, strerror(errno));
        return 1;
    }
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL)
        printf("%s%s\n", entry->d_name,
               entry->d_type == POLLIKOS_TYPE_DIRECTORY ? "/" : "");
    closedir(directory);
    return 0;
}
static int builtin_cat(int argc, const char *const *argv) {
    if (argc < 2) {
        char buffer[512];
        size_t got;
        while ((got = fread(buffer, 1, sizeof(buffer), stdin)) > 0) emit(buffer, got);
        return 0;
    }
    int result = 0;
    for (int index = 1; index < argc; ++index) {
        FILE *file = fopen(argv[index], "rb");
        if (!file) {
            printf("cat: %s: %s\n", argv[index], strerror(errno));
            result = 1;
            continue;
        }
        char buffer[512];
        size_t got;
        while ((got = fread(buffer, 1, sizeof(buffer), file)) > 0) emit(buffer, got);
        fclose(file);
    }
    return result;
}
static int builtin_mkdir(int argc, const char *const *argv) {
    if (argc < 2) { line_out("mkdir: missing operand"); return 1; }
    if (mkdir(argv[1], 0755) != 0) {
        printf("mkdir: %s: %s\n", argv[1], strerror(errno));
        return 1;
    }
    return 0;
}
static int builtin_rm(int argc, const char *const *argv) {
    if (argc < 2) { line_out("rm: missing operand"); return 1; }
    if (unlink(argv[1]) != 0) {
        printf("rm: %s: %s\n", argv[1], strerror(errno));
        return 1;
    }
    return 0;
}
static int builtin_rmdir(int argc, const char *const *argv) {
    if (argc < 2) { line_out("rmdir: missing operand"); return 1; }
    if (rmdir(argv[1]) != 0) {
        printf("rmdir: %s: %s\n", argv[1], strerror(errno));
        return 1;
    }
    return 0;
}
static int builtin_mv(int argc, const char *const *argv) {
    if (argc < 3) { line_out("mv: source and destination required"); return 1; }
    if (rename(argv[1], argv[2]) != 0) {
        printf("mv: %s -> %s: %s\n", argv[1], argv[2], strerror(errno));
        return 1;
    }
    return 0;
}
static int builtin_touch(int argc, const char *const *argv) {
    if (argc < 2) { line_out("touch: missing operand"); return 1; }
    FILE *file = fopen(argv[1], "ab");
    if (!file) {
        printf("touch: %s: %s\n", argv[1], strerror(errno));
        return 1;
    }
    fclose(file);
    return 0;
}
static int builtin_env(int argc, const char *const *argv) {
    (void)argc; (void)argv;
    for (char **entry = environ; entry && *entry; ++entry) line_out(*entry);
    return 0;
}
static int builtin_set(int argc, const char *const *argv) {
    if (argc < 2) return builtin_env(argc, argv);
    for (int index = 1; index < argc; ++index) {
        const char *name = argv[index];
        const char *equals = strchr(name, '=');
        if (!equals) continue;
        char key[64];
        size_t length = (size_t)(equals - name);
        if (length >= sizeof(key)) length = sizeof(key) - 1;
        memcpy(key, name, length);
        key[length] = 0;
        setenv(key, equals + 1, 1);
    }
    return 0;
}
static int builtin_status(int argc, const char *const *argv) {
    (void)argc; (void)argv;
    printf("%d\n", last_status);
    return 0;
}
static int builtin_exit(int argc, const char *const *argv) {
    exit(argc > 1 ? atoi(argv[1]) : 0);
    return 0;
}

/* --------------------------------------------------------------- launcher */
static int spawn_wait(const char *path, const char *const *argv) {
    long pid = pollikos_spawn(path, argv, 0);
    if (pid <= 0) return -1;
    pollikos_wait_t status;
    if (pollikos_waitpid(pid, &status) != pid) return -1;
    return status.kind == POLLIKOS_WAIT_EXITED ? status.code : 128 + (int)status.kind;
}
static int run_external(const char *const *argv) {
    const char *command = argv[0];
    long pid = strchr(command, '/') ? pollikos_spawn(command, argv, 0)
                                    : pollikos_spawnp(command, argv, 0);
    if (pid <= 0) {
        if (errno == ENOENT || pid == -(long)USER_ENOENT || pid == -(long)USER_ENOEXEC)
            printf("'%s' is not recognized as a PollikOS command or executable.\n", command);
        else
            printf("%s: cannot execute: %s\n", command, strerror(errno));
        return 127;
    }
    pollikos_wait_t status;
    if (pollikos_waitpid(pid, &status) != pid) {
        printf("%s: wait failed: %s\n", command, strerror(errno));
        return 126;
    }
    if (status.kind == POLLIKOS_WAIT_EXITED) return status.code;
    if (status.kind == POLLIKOS_WAIT_FAULTED)
        printf("[pollish] %s: fault vector %d at 0x%lx\n", command,
               (int)status.code, (unsigned long)status.address);
    else
        printf("[pollish] %s: killed (reason %d)\n", command, (int)status.code);
    return 128;
}
/* ------------------------------------------------------ builtins & stages */
typedef int (*builtin_fn)(int argc, const char *const *argv);
typedef struct { const char *name; builtin_fn function; } Builtin;
static const Builtin builtins[] = {
    {"cd", builtin_cd}, {"pwd", builtin_pwd}, {"echo", builtin_echo},
    {"clear", builtin_clear}, {"cls", builtin_clear},
    {"history", builtin_history}, {"help", builtin_help},
    {"ls", builtin_ls}, {"dir", builtin_ls},
    {"cat", builtin_cat}, {"type", builtin_cat},
    {"mkdir", builtin_mkdir}, {"rm", builtin_rm}, {"del", builtin_rm},
    {"rmdir", builtin_rmdir}, {"mv", builtin_mv}, {"rename", builtin_mv},
    {"touch", builtin_touch}, {"env", builtin_env}, {"set", builtin_set},
    {"status", builtin_status}, {"exit", builtin_exit},
};
static const Builtin *find_builtin(const char *name) {
    for (size_t index = 0; index < sizeof(builtins)/sizeof(builtins[0]); ++index)
        if (strcmp(builtins[index].name, name) == 0) return &builtins[index];
    return 0;
}
static int run_command(int argc, const char *const *argv) {
    const Builtin *builtin = find_builtin(argv[0]);
    return builtin ? builtin->function(argc, argv) : run_external(argv);
}

/* Pipes and redirection are recognized as separate unquoted tokens; operators
 * must be surrounded by spaces (`ls | cat`, `echo hi > file`). */
#define MAX_STAGES 4
typedef struct {
    const char *argv[ARG_MAX + 1];
    int argc;
    const char *input_file;
    const char *output_file;
    int append;
} Stage;
static Stage stages[MAX_STAGES];
static int operator_token(int index) {
    if (token_quoted[index]) return 0;
    const char *token = arguments[index];
    return strcmp(token, "|") == 0 || strcmp(token, "<") == 0 ||
           strcmp(token, ">") == 0 || strcmp(token, ">>") == 0;
}
static int build_stages(int argc, int *stage_count) {
    int stage = 0, index = 0;
    while (index < argc) {
        Stage *current = &stages[stage];
        current->argc = 0;
        current->input_file = 0;
        current->output_file = 0;
        current->append = 0;
        while (index < argc) {
            const char *token = arguments[index];
            if (!token_quoted[index] && strcmp(token, "|") == 0) { ++index; break; }
            if (operator_token(index)) {
                if (index + 1 >= argc) return -1;
                if (token[0] == '<') current->input_file = arguments[index + 1];
                else {
                    current->output_file = arguments[index + 1];
                    current->append = token[1] == '>';
                }
                index += 2;
                continue;
            }
            if (current->argc < ARG_MAX) current->argv[current->argc++] = token;
            ++index;
        }
        current->argv[current->argc] = 0;
        if (current->argc == 0) return -1;
        if (index < argc) {
            if (++stage >= MAX_STAGES) return -1;
        }
    }
    *stage_count = stage + 1;
    return 0;
}
static long spawn_stage(const Stage *stage, pid_t pgid) {
    if (find_builtin(stage->argv[0])) {
        const char *arguments_with_mode[ARG_MAX + 3];
        int count = 0;
        arguments_with_mode[count++] = "pollish";
        arguments_with_mode[count++] = "-c";
        for (int index = 0; index < stage->argc; ++index)
            arguments_with_mode[count++] = stage->argv[index];
        arguments_with_mode[count] = 0;
        return pollikos_spawn_group("/bin/pollish", arguments_with_mode, 0, pgid);
    }
    if (strchr(stage->argv[0], '/')) return pollikos_spawn_group(stage->argv[0], stage->argv, 0, pgid);
    return pollikos_spawnp_group(stage->argv[0], stage->argv, 0, pgid);
}
static int wait_code(const pollikos_wait_t *wait) {
    if (wait->kind == POLLIKOS_WAIT_EXITED) return wait->code;
    return 128 + (int)wait->code; /* fault vector or kill reason (Ctrl+C = 130) */
}
static int open_output(const Stage *stage) {
    return open(stage->output_file, O_WRONLY|O_CREAT|O_CLOEXEC|
                (stage->append ? O_APPEND : O_TRUNC));
}
static int run_pipeline(int argc) {
    int stage_count = 0;
    if (build_stages(argc, &stage_count) != 0) {
        line_out("pollish: syntax error near pipe or redirection");
        return 2;
    }
    /* A lone builtin runs in the shell so cd and friends keep their state. */
    if (stage_count == 1 && find_builtin(stages[0].argv[0])) {
        const Stage *stage = &stages[0];
        int in_fd = -1, out_fd = -1, saved_in = -1, saved_out = -1;
        if (stage->input_file) {
            in_fd = open(stage->input_file, O_RDONLY|O_CLOEXEC);
            if (in_fd < 0) { printf("pollish: %s: %s\n", stage->input_file, strerror(errno)); return 1; }
        }
        if (stage->output_file) {
            out_fd = open_output(stage);
            if (out_fd < 0) {
                printf("pollish: %s: %s\n", stage->output_file, strerror(errno));
                if (in_fd >= 0) close(in_fd);
                return 1;
            }
        }
        if (in_fd >= 0) { saved_in = dup(0); dup2(in_fd, 0); }
        if (out_fd >= 0) { saved_out = dup(1); dup2(out_fd, 1); }
        int status = run_command(stage->argc, stage->argv);
        if (saved_out >= 0) { dup2(saved_out, 1); close(saved_out); }
        if (saved_in >= 0) { dup2(saved_in, 0); close(saved_in); }
        if (out_fd >= 0) close(out_fd);
        if (in_fd >= 0) close(in_fd);
        return status;
    }
    long pids[MAX_STAGES] = {0};
    int spawned = 0, failure = 0, status = 127;
    pid_t foreground_group = 0;
    /* spawn clones the descriptor table. Mark raw pipeline and redirection
     * descriptors close-on-spawn; dup2 clears the flag on the intended stdin
     * or stdout descriptor while the raw copies stay private to the shell. */
    int previous_read = -1;
    for (int index = 0; index < stage_count && !failure; ++index) {
        Stage *stage = &stages[index];
        int next_pipe[2] = {-1, -1};
        int in_fd = -1, out_fd = -1, saved_in = -1, saved_out = -1;
        long pid = 0;
        for (;;) {
            if (stage->input_file) {
                in_fd = open(stage->input_file, O_RDONLY|O_CLOEXEC);
                if (in_fd < 0) { printf("pollish: %s: %s\n", stage->input_file, strerror(errno)); failure = 1; break; }
            } else if (previous_read >= 0) {
                in_fd = previous_read;
            }
            if (stage->output_file) {
                out_fd = open_output(stage);
                if (out_fd < 0) { printf("pollish: %s: %s\n", stage->output_file, strerror(errno)); failure = 1; break; }
            } else if (index + 1 < stage_count) {
                if (pipe(next_pipe) != 0) { printf("pollish: pipe: %s\n", strerror(errno)); failure = 1; break; }
                if (pollikos_set_cloexec(next_pipe[0], 1) < 0 ||
                    pollikos_set_cloexec(next_pipe[1], 1) < 0) {
                    printf("pollish: cannot mark pipe close-on-spawn\n");
                    failure = 1;
                    break;
                }
                out_fd = next_pipe[1];
            }
            if (in_fd >= 0) { saved_in = dup(0); if (saved_in < 0 || dup2(in_fd, 0) < 0) { failure = 1; break; } }
            if (out_fd >= 0) { saved_out = dup(1); if (saved_out < 0 || dup2(out_fd, 1) < 0) { failure = 1; break; } }
            pid = spawn_stage(stage, foreground_group);
            if (saved_out >= 0) { dup2(saved_out, 1); close(saved_out); saved_out = -1; }
            if (saved_in >= 0) { dup2(saved_in, 0); close(saved_in); saved_in = -1; }
            break;
        }
        if (stage->output_file && out_fd >= 0) { close(out_fd); out_fd = -1; }
        if (stage->input_file && in_fd >= 0) { close(in_fd); in_fd = -1; }
        if (previous_read >= 0) { close(previous_read); previous_read = -1; }
        if (next_pipe[1] >= 0) { close(next_pipe[1]); next_pipe[1] = -1; }
        previous_read = next_pipe[0];
        next_pipe[0] = -1;
        if (failure) {
            if (saved_in >= 0) close(saved_in);
            if (saved_out >= 0) close(saved_out);
            break;
        }
        if (pid <= 0) {
            if (errno == ENOENT || pid == -(long)USER_ENOENT || pid == -(long)USER_ENOEXEC)
                fprintf(stderr, "'%s' is not recognized as a PollikOS command or executable.\n", stage->argv[0]);
            else
                fprintf(stderr, "%s: cannot execute: rc=%ld errno=%s\n", stage->argv[0], pid, strerror(errno));
            failure = 1;
            break;
        }
        pids[index] = pid;
        ++spawned;
        if (!foreground_group) foreground_group = (pid_t)pid;
    }
    if (previous_read >= 0) close(previous_read);
    if (failure)
        for (int index = 0; index < spawned; ++index)
            if (pids[index] > 0) (void)kill((int)pids[index], 9);
    pollikos_foreground(spawned == stage_count ? foreground_group : 0);
    for (int index = 0; index < spawned; ++index) {
        if (pids[index] <= 0) continue;
        pollikos_wait_t wait;
        int code = 125;
        if (pollikos_waitpid(pids[index], &wait) == pids[index]) code = wait_code(&wait);
        if (index == stage_count - 1) status = code;
    }
    pollikos_foreground(0);
    return failure ? 127 : status;
}

/* ------------------------------------------------------------- line editor */
static char edit_line[LINE_MAX];
static int edit_length, edit_cursor, edit_history;
static void prompt_text(char *buffer, size_t capacity, const char *prefix) {
    char cwd[256];
    if (!getcwd(cwd, sizeof(cwd))) snprintf(cwd, sizeof(cwd), "?");
    snprintf(buffer, capacity, "%sPollikOS:%s> ", prefix ? prefix : "", cwd);
}
static void refresh(void) {
    char buffer[LINE_MAX + 400];
    char prompt[320];
    prompt_text(prompt, sizeof(prompt), "\r");
    int length = snprintf(buffer, sizeof(buffer), "%s%s" ESC "[K", prompt, edit_line);
    int back = edit_length - edit_cursor;
    if (back > 0 && length > 0)
        length += snprintf(buffer + length, sizeof(buffer) - (size_t)length, ESC "[%dD", back);
    if (length > 0) emit(buffer, (size_t)length);
}
static int read_byte(void) {
    unsigned char byte;
    ssize_t got = read(0, &byte, 1);
    return got == 1 ? (int)byte : -1;
}
static void show_history(void) {
    if (edit_history >= history_count) {
        edit_line[0] = 0; edit_length = edit_cursor = 0;
    } else {
        const char *text = history_at(edit_history);
        edit_length = (int)strlen(text);
        if (edit_length >= LINE_MAX) edit_length = LINE_MAX - 1;
        memcpy(edit_line, text, (size_t)edit_length);
        edit_line[edit_length] = 0;
        edit_cursor = edit_length;
    }
    refresh();
}
static void handle_escape(void) {
    if (read_byte() != '[') return;
    int code = read_byte();
    if (code == 'A') { if (edit_history > 0) { --edit_history; show_history(); } return; }
    if (code == 'B') { if (edit_history < history_count) { ++edit_history; show_history(); } return; }
    if (code == 'C') { if (edit_cursor < edit_length) { ++edit_cursor; refresh(); } return; }
    if (code == 'D') { if (edit_cursor > 0) { --edit_cursor; refresh(); } return; }
    if (code == 'H') { edit_cursor = 0; refresh(); return; }
    if (code == 'F') { edit_cursor = edit_length; refresh(); return; }
    if (code >= '1' && code <= '9') {
        int trailer = read_byte();
        if (code == '1' || code == '7') { edit_cursor = 0; refresh(); }
        else if (code == '4' || code == '8') { edit_cursor = edit_length; refresh(); }
        else if (code == '3' && trailer == '~' && edit_cursor < edit_length) {
            memmove(&edit_line[edit_cursor], &edit_line[edit_cursor + 1],
                    (size_t)(edit_length - edit_cursor));
            --edit_length;
            refresh();
        }
    }
}
static void insert_byte(int byte) {
    if (edit_length + 1 >= LINE_MAX) return;
    memmove(&edit_line[edit_cursor + 1], &edit_line[edit_cursor],
            (size_t)(edit_length - edit_cursor) + 1);
    edit_line[edit_cursor++] = (char)byte;
    ++edit_length;
    refresh();
}
static int read_line(void) {
    edit_length = edit_cursor = 0;
    edit_line[0] = 0;
    edit_history = history_count;
    refresh();
    for (;;) {
        int byte = read_byte();
        if (byte < 0) { out("\r\n"); exit(0); }
        if (byte == '\r' || byte == '\n') { out("\r\n"); edit_line[edit_length] = 0; return 1; }
        if (byte == 0x7f || byte == 0x08) {
            if (edit_cursor > 0) {
                memmove(&edit_line[edit_cursor - 1], &edit_line[edit_cursor],
                        (size_t)(edit_length - edit_cursor) + 1);
                --edit_cursor; --edit_length;
                refresh();
            }
            continue;
        }
        if (byte == 0x03) { out("^C\r\n"); edit_line[0] = 0; edit_length = edit_cursor = 0; return 0; }
        if (byte == 0x04) { if (!edit_length) { out("\r\n"); exit(0); } continue; }
        if (byte == 0x1b) { handle_escape(); continue; }
        if (byte >= 0x20 && byte < 0x7f) insert_byte(byte);
    }
}

/* --------------------------------------------------------- native libc prep */
static int native_libc_ready(void) {
    FILE *marker=fopen(LIBC_MARKER,"rb");
    if (!marker) return 0;
    fclose(marker);
    return 1;
}

/* Desktop and serial sessions can start together. Only one may rebuild the
 * shared archive; other shells wait for its marker and recover stale locks. */
static int native_libc_lock(void) {
    long empty_since=-1;
    for (unsigned attempt=0;attempt<12000;attempt++) {
        if (native_libc_ready()) return 0;
        int descriptor=open(LIBC_LOCK,O_WRONLY|O_CREAT|O_EXCL);
        if (descriptor>=0) {
            int32_t owner=(int32_t)getpid();
            int saved_errno=errno;
            ssize_t written=write(descriptor,&owner,sizeof(owner));
            close(descriptor);
            if (written!=(ssize_t)sizeof(owner)) {
                unlink(LIBC_LOCK);
                errno=saved_errno;
                return -1;
            }
            return 1;
        }
        if (errno!=EEXIST) return -1;
        descriptor=open(LIBC_LOCK,O_RDONLY);
        if (descriptor>=0) {
            int32_t owner=0;
            ssize_t got=read(descriptor,&owner,sizeof(owner));
            close(descriptor);
            if (got==(ssize_t)sizeof(owner)&&owner>0) {
                empty_since=-1;
                if (kill((pid_t)owner,0)<0&&errno==ESRCH) unlink(LIBC_LOCK);
            } else {
                long now=pollikos_monotonic_ms();
                if (empty_since<0) empty_since=now;
                else if (now>=empty_since&&now-empty_since>=5000) {
                    unlink(LIBC_LOCK);
                    empty_since=-1;
                }
            }
        }
        (void)pollikos_sleep_ms(20);
    }
    errno=ETIMEDOUT;
    return -1;
}

static int bootstrap_native_libc(void) {
    int locked=native_libc_lock();
    if (locked<=0) return locked==0?0:-1;
    out("[pollish] preparing native libc for /bin/tcc (first run)...\n");
    DIR *directory = opendir(LIBC_SRC_DIR);
    if (!directory) {
        line_out("[pollish] /usr/src/libc is unavailable; linking may fail");
        unlink(LIBC_LOCK);
        return -1;
    }
    static char objects[LIBC_OBJECTS_MAX][64];
    int object_count = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL && object_count < LIBC_OBJECTS_MAX) {
        size_t length = strlen(entry->d_name);
        if (length < 3 || strcmp(entry->d_name + length - 2, ".c") != 0) continue;
        char source[96];
        char base[48];
        size_t base_length = length - 2;
        if (base_length >= sizeof(base)) base_length = sizeof(base) - 1;
        memcpy(base, entry->d_name, base_length);
        base[base_length] = 0;
        snprintf(source, sizeof(source), LIBC_SRC_DIR "/%s", entry->d_name);
        snprintf(objects[object_count], sizeof(objects[0]), "/tmp/libc-%s.o", base);
        const char *compile[] = {"tcc", "-c", source, "-o", objects[object_count], 0};
        int status = spawn_wait("/bin/tcc", compile);
        if (status != 0) {
            closedir(directory);
            printf("[pollish] native libc compile failed (%d)\n", status);
            unlink(LIBC_LOCK);
            return -1;
        }
        ++object_count;
    }
    closedir(directory);
    const char *archive[LIBC_OBJECTS_MAX + 6];
    int index = 0;
    archive[index++] = "tcc";
    archive[index++] = "-ar";
    archive[index++] = "rcs";
    archive[index++] = "/usr/lib/libc.a";
    for (int i = 0; i < object_count; ++i) archive[index++] = objects[i];
    archive[index] = 0;
    int status = object_count ? spawn_wait("/bin/tcc", archive) : -1;
    for (int i = 0; i < object_count; ++i) unlink(objects[i]);
    if (status != 0) {
        printf("[pollish] native libc archive failed (%d)\n", status);
        unlink(LIBC_LOCK);
        return -1;
    }
    FILE *created = fopen(LIBC_MARKER, "wb");
    if (!created) {
        unlink(LIBC_LOCK);
        return -1;
    }
    size_t marker_bytes=fwrite("native\n",1,7,created);
    int marker_close=fclose(created);
    if (marker_bytes!=7||marker_close!=0) {
        unlink(LIBC_LOCK);
        return -1;
    }
    unlink(LIBC_LOCK);
    out("[pollish] native libc ready\n");
    return 0;
}

/* -------------------------------------------------------------------- main */
int main(int argc, char **argv) {
    /* Pipeline stages that are builtins run as children: pollish -c <command>
     * keeps shell state changes (cd) inside the child, like a real pipeline. */
    if (argc > 2 && strcmp(argv[1], "-c") == 0)
        return run_command(argc - 2, (const char *const *)argv + 2);
    history_load();
    line_out("PollikOS shell (pollish). Type 'help' for builtins.");
    if (bootstrap_native_libc() != 0)
        line_out("[pollish] warning: native libc preparation failed");
    for (;;) {
        if (!read_line()) continue;
        int count = parse(edit_line);
        if (count == 0) continue;
        history_add(edit_line);
        history_append(edit_line);
        last_status = run_pipeline(count);
    }
}
