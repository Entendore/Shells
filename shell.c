#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <termios.h>
#include <glob.h>

// --- Global State ---
volatile pid_t foreground_pgid = 0;
int last_exit_status = 0;

// --- Job Control Linked List ---
typedef struct bg_proc {
    int job_id;
    pid_t pid;
    char *cmd;
    struct bg_proc *next;
} bg_proc_t;

bg_proc_t *bg_list = NULL;
int next_job_id = 1;

void add_bg_proc(pid_t pid, char *cmd) {
    bg_proc_t *p = malloc(sizeof(bg_proc_t));
    p->job_id = next_job_id++;
    p->pid = pid;
    p->cmd = strdup(cmd);
    p->next = bg_list;
    bg_list = p;
}

void cleanup_background_zombies() {
    int status;
    bg_proc_t **curr = &bg_list;
    while (*curr) {
        pid_t result = waitpid((*curr)->pid, &status, WNOHANG);
        if (result > 0) {
            printf("[%d]+ Done\t\t%s\n", (*curr)->job_id, (*curr)->cmd);
            bg_proc_t *tmp = *curr;
            *curr = (*curr)->next;
            free(tmp->cmd);
            free(tmp);
        } else {
            curr = &(*curr)->next;
        }
    }
}

char* args_to_string(char **args) {
    int len = 0;
    for(int i=0; args[i]!=NULL; i++) len += strlen(args[i]) + 1;
    char *str = malloc(len + 1);
    str[0] = '\0';
    for(int i=0; args[i]!=NULL; i++) {
        strcat(str, args[i]);
        if(args[i+1]!=NULL) strcat(str, " ");
    }
    return str;
}

// --- Custom Variable Dictionary ---
typedef struct var {
    char *name;
    char *value;
    struct var *next;
} var_t;

var_t *var_list = NULL;

void set_var(const char *name, const char *value) {
    var_t *curr = var_list;
    while (curr != NULL) {
        if (strcmp(curr->name, name) == 0) { free(curr->value); curr->value = strdup(value); return; }
        curr = curr->next;
    }
    var_t *new_var = malloc(sizeof(var_t));
    new_var->name = strdup(name);
    new_var->value = strdup(value);
    new_var->next = var_list;
    var_list = new_var;
}

char *get_var(const char *name) {
    var_t *curr = var_list;
    while (curr != NULL) { if (strcmp(curr->name, name) == 0) return curr->value; curr = curr->next; }
    char *env_val = getenv(name);
    return env_val ? env_val : "";
}

// --- Signal Handling ---
void handle_sigint(int sig) {
    if (foreground_pgid > 0) kill(-foreground_pgid, SIGINT);
    printf("\nmysh> ");
    fflush(stdout);
}

void setup_signals() {
    struct sigaction sa_int;
    sa_int.sa_handler = handle_sigint;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa_int, NULL);
    signal(SIGTTOU, SIG_IGN);
}

// --- Built-in Commands ---
int shell_cd(char **args) {
    if (args[1] == NULL) fprintf(stderr, "myshell: expected argument to \"cd\"\n");
    else if (chdir(args[1]) != 0) perror("myshell");
    return 1;
}
int shell_pwd(char **args) { char cwd[1024]; if (getcwd(cwd, sizeof(cwd)) != NULL) printf("%s\n", cwd); return 1; }
int shell_echo(char **args) { for (int i = 1; args[i] != NULL; i++) { printf("%s%s", args[i], args[i+1] ? " " : ""); } printf("\n"); return 1; }
int shell_export(char **args) {
    if (args[1] == NULL) { var_t *curr = var_list; while(curr) { printf("declare -x %s=\"%s\"\n", curr->name, curr->value); curr = curr->next; } return 1; }
    char *eq = strchr(args[1], '=');
    if (eq != NULL) { *eq = '\0'; set_var(args[1], eq + 1); setenv(args[1], eq + 1, 1); *eq = '='; }
    else setenv(args[1], get_var(args[1]), 1);
    return 1;
}
int shell_exit(char **args) { return 0; }

int shell_jobs(char **args) {
    bg_proc_t *curr = bg_list;
    while (curr) {
        printf("[%d]+ Running\t\t%s\n", curr->job_id, curr->cmd);
        curr = curr->next;
    }
    return 1;
}

int shell_fg(char **args) {
    if (args[1] == NULL) { fprintf(stderr, "myshell: fg: current: no such job\n"); return 1; }
    int job_id = atoi(args[1] + (*args[1] == '%' ? 1 : 0)); // Handle %1 or 1
    
    bg_proc_t *curr = bg_list;
    while (curr) {
        if (curr->job_id == job_id) {
            printf("%s\n", curr->cmd);
            if (isatty(STDIN_FILENO)) tcsetpgrp(STDIN_FILENO, curr->pid);
            foreground_pgid = curr->pid;
            
            int status;
            waitpid(curr->pid, &status, WUNTRACED);
            last_exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
            foreground_pgid = 0;
            if (isatty(STDIN_FILENO)) tcsetpgrp(STDIN_FILENO, getpgrp());
            
            // Remove from bg list
            bg_proc_t **c = &bg_list;
            while (*c) {
                if ((*c)->job_id == job_id) { bg_proc_t *tmp = *c; *c = (*c)->next; free(tmp->cmd); free(tmp); break; }
                c = &(*c)->next;
            }
            return 1;
        }
        curr = curr->next;
    }
    fprintf(stderr, "myshell: fg: %%%d: no such job\n", job_id);
    return 1;
}

// --- Redirection ---
void setup_redirection(char **args) {
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], ">") == 0 || strcmp(args[i], ">>") == 0 || strcmp(args[i], "<") == 0) {
            if (args[i+1] == NULL) { fprintf(stderr, "myshell: syntax error near `%s'\n", args[i]); exit(1); }
            int fd;
            if (strcmp(args[i], ">") == 0) fd = open(args[i+1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            else if (strcmp(args[i], ">>") == 0) fd = open(args[i+1], O_WRONLY | O_CREAT | O_APPEND, 0644);
            else fd = open(args[i+1], O_RDONLY);
            if (fd < 0) { perror("myshell"); exit(1); }
            if (strcmp(args[i], "<") == 0) dup2(fd, STDIN_FILENO); else dup2(fd, STDOUT_FILENO);
            close(fd); args[i] = NULL; args[i+1] = NULL; return;
        }
    }
}

// --- Process Launching ---
int launch_process(char **args, int is_background, char *cmd_str) {
    pid_t pid = fork();
    if (pid == 0) {
        setpgid(0, 0); setup_redirection(args);
        if (execvp(args[0], args) == -1) perror("myshell");
        exit(EXIT_FAILURE);
    } else if (pid < 0) { perror("myshell");
    } else {
        setpgid(pid, pid);
        if (!is_background) {
            if (isatty(STDIN_FILENO)) tcsetpgrp(STDIN_FILENO, pid);
            foreground_pgid = pid; int status; waitpid(pid, &status, WUNTRACED);
            last_exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
            foreground_pgid = 0;
            if (isatty(STDIN_FILENO)) tcsetpgrp(STDIN_FILENO, getpgrp());
        } else { add_bg_proc(pid, cmd_str); printf("[%d]\n", next_job_id - 1); }
    }
    return 1;
}

int execute_pipeline(char **args, int is_background, char *cmd_str) {
    int num_commands = 1;
    for (int i = 0; args[i] != NULL; i++) if (strcmp(args[i], "|") == 0) num_commands++;
    if (strcmp(args[0], "|") == 0 || strcmp(args[strlen(args[0])-1 == 0 ? 0 : strlen(args[0])-1], "|") == 0) {
        // Simple syntax check (Note: arg[0] is the first word, we need to check the actual last word)
        int last=0; while(args[last]!=NULL) last++; last--;
        if(strcmp(args[last], "|")==0) { fprintf(stderr, "myshell: syntax error near unexpected token `|'\n"); last_exit_status = 2; return 1; }
    }

    char **cmd_arrays[num_commands]; int cmd_idx = 0; cmd_arrays[cmd_idx] = &args[0];
    for (int i = 0; args[i] != NULL; i++) { if (strcmp(args[i], "|") == 0) { args[i] = NULL; cmd_idx++; cmd_arrays[cmd_idx] = &args[i + 1]; } }

    int pipefd[2], prev_fd = -1; pid_t pids[num_commands], pgid = 0;
    for (int i = 0; i < num_commands; i++) {
        if (i < num_commands - 1) pipe(pipefd);
        pids[i] = fork();
        if (pids[i] == 0) {
            if (pgid == 0) pgid = getpid(); setpgid(0, pgid);
            if (prev_fd != -1) dup2(prev_fd, STDIN_FILENO);
            if (i < num_commands - 1) dup2(pipefd[1], STDOUT_FILENO);
            if (prev_fd != -1) close(prev_fd);
            if (i < num_commands - 1) { close(pipefd[0]); close(pipefd[1]); }
            setup_redirection(cmd_arrays[i]);
            if (execvp(cmd_arrays[i][0], cmd_arrays[i]) == -1) perror("myshell"); exit(1);
        } else if (pids[i] < 0) { perror("fork");
        } else {
            if (pgid == 0) pgid = pids[i]; setpgid(pids[i], pgid);
            if (prev_fd != -1) close(prev_fd);
            if (i < num_commands - 1) { prev_fd = pipefd[0]; close(pipefd[1]); }
        }
    }
    if (!is_background) {
        if (isatty(STDIN_FILENO)) tcsetpgrp(STDIN_FILENO, pgid);
        foreground_pgid = pgid; int status;
        for (int i = 0; i < num_commands; i++) waitpid(pids[i], &status, 0);
        last_exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        foreground_pgid = 0;
        if (isatty(STDIN_FILENO)) tcsetpgrp(STDIN_FILENO, getpgrp());
    } else { add_bg_proc(pgid, cmd_str); printf("[%d]\n", next_job_id - 1); }
    return 1;
}

// --- Execution Router ---
int execute(char **args) {
    if (args[0] == NULL) return 1;
    
    // 1. Sequential Execution (;)
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], ";") == 0) { args[i] = NULL; execute(args); execute(&args[i + 1]); return 1; }
    }

    if (strcmp(args[0], "cd") == 0) return shell_cd(args);
    if (strcmp(args[0], "pwd") == 0) return shell_pwd(args);
    if (strcmp(args[0], "echo") == 0) return shell_echo(args);
    if (strcmp(args[0], "export") == 0) return shell_export(args);
    if (strcmp(args[0], "exit") == 0) return shell_exit(args);
    if (strcmp(args[0], "jobs") == 0) return shell_jobs(args);
    if (strcmp(args[0], "fg") == 0) return shell_fg(args);

    // 2. Logical Execution (&& ||)
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "&&") == 0 || strcmp(args[i], "||") == 0) {
            int j = 0; char *cmd_start = args[0];
            while (args[j] != NULL) {
                if (strcmp(args[j], "&&") == 0 || strcmp(args[j], "||") == 0) {
                    int is_and = (strcmp(args[j], "&&") == 0);
                    args[j] = NULL;
                    execute(cmd_start);
                    int skip = (is_and && last_exit_status != 0) || (!is_and && last_exit_status == 0);
                    if (skip) { while (args[j] != NULL && strcmp(args[j], "&&") != 0 && strcmp(args[j], "||") != 0) j++; }
                    cmd_start = args[j] ? args[j + 1] : NULL;
                }
                j++;
            }
            if (cmd_start != NULL) execute(cmd_start);
            return 1;
        }
    }

    char *cmd_str = args_to_string(args);
    int is_background = 0; int len = 0; while (args[len] != NULL) len++;
    if (len > 0 && strcmp(args[len - 1], "&") == 0) { is_background = 1; args[len - 1] = NULL; }

    int has_pipe = 0; for (int i = 0; args[i] != NULL; i++) if (strcmp(args[i], "|") == 0) has_pipe = 1;

    if (has_pipe) { int res = execute_pipeline(args, is_background, cmd_str); free(cmd_str); return res; }
    else { int res = launch_process(args, is_background, cmd_str); free(cmd_str); return res; }
}

// --- Tokenizer ---
#define TOK_BUFSIZE 64
char **split_line(char *line) {
    int bufsize = TOK_BUFSIZE, position = 0;
    char **tokens = malloc(bufsize * sizeof(char*));
    char *token = malloc(8192 * sizeof(char)); int t_pos = 0;
    int in_single_quote = 0, in_double_quote = 0;

    if (!tokens || !token) { fprintf(stderr, "Allocation error\n"); exit(EXIT_FAILURE); }

    for (int i = 0; line[i] != '\0'; i++) {
        char c = line[i];
        if (t_pos >= 8190) { fprintf(stderr, "Token too long\n"); exit(1); }
        if (c == '\'' && !in_double_quote) { in_single_quote = !in_single_quote; continue; }
        if (c == '"' && !in_single_quote) { in_double_quote = !in_double_quote; continue; }
        if (c == '$' && !in_single_quote) {
            char var_name[256]; int v_pos = 0; i++;
            if (line[i] == '?') { snprintf(var_name, sizeof(var_name), "%d", last_exit_status); }
            else { while (line[i] != '\0' && (isalnum(line[i]) || line[i] == '_')) var_name[v_pos++] = line[i++]; var_name[v_pos] = '\0'; i--; }
            char *val = get_var(var_name); while (*val && t_pos < 8190) token[t_pos++] = *val++; continue;
        }
        if ((c == ' ' || c == '\t' || c == '\n' || c == '\r') && !in_single_quote && !in_double_quote) {
            if (t_pos > 0) { token[t_pos] = '\0'; tokens[position++] = strdup(token); t_pos = 0; if (position >= bufsize) { bufsize += TOK_BUFSIZE; tokens = realloc(tokens, bufsize * sizeof(char*)); } } continue;
        }
        if ((c == '|' || c == '<' || c == '>' || c == '&' || c == ';') && !in_single_quote && !in_double_quote) {
            if (t_pos > 0) { token[t_pos] = '\0'; tokens[position++] = strdup(token); t_pos = 0; if (position >= bufsize) { bufsize += TOK_BUFSIZE; tokens = realloc(tokens, bufsize * sizeof(char*)); } }
            if ((c == '&' && line[i+1] == '&') || (c == '|' && line[i+1] == '|')) {
                char op[3] = {c, c, '\0'}; tokens[position++] = strdup(op); i++;
            } else if (c == '>' && line[i+1] == '>') {
                tokens[position++] = strdup(">>"); i++;
            } else { char op[2] = {c, '\0'}; tokens[position++] = strdup(op); }
            if (position >= bufsize) { bufsize += TOK_BUFSIZE; tokens = realloc(tokens, bufsize * sizeof(char*)); } continue;
        }
        token[t_pos++] = c;
    }
    if (t_pos > 0) { token[t_pos] = '\0'; tokens[position++] = strdup(token); }
    tokens[position] = NULL; free(token); return tokens;
}

// --- File Globbing & Tilde Expansion ---
char **expand_globs(char **args) {
    int new_argc = 0, buf_size = 64;
    char **new_args = malloc(buf_size * sizeof(char*));
    
    for (int i = 0; args[i] != NULL; i++) {
        // Skip globbing for redirection targets
        int skip_glob = 0;
        if (i > 0 && (strcmp(args[i-1], ">") == 0 || strcmp(args[i-1], ">>") == 0 || strcmp(args[i-1], "<") == 0)) {
            skip_glob = 1;
        }

        if (!skip_glob && strpbrk(args[i], "*?[") != NULL) {
            glob_t globbuf;
            // GLOB_TILDE handles ~ automatically. GLOB_NOCHECK leaves pattern as-is if no match.
            int ret = glob(args[i], GLOB_NOCHECK | GLOB_TILDE, NULL, &globbuf);
            if (ret == 0) {
                for (size_t j = 0; j < globbuf.gl_pathc; j++) {
                    new_args[new_argc++] = strdup(globbuf.gl_pathv[j]);
                    if (new_argc >= buf_size) { buf_size *= 2; new_args = realloc(new_args, buf_size * sizeof(char*)); }
                }
                globfree(&globbuf);
            } else { new_args[new_argc++] = strdup(args[i]); }
        } else {
            // Even if no wildcards, use glob() just for the GLOB_TILDE (~) expansion
            glob_t globbuf;
            int ret = glob(args[i], GLOB_NOCHECK | GLOB_TILDE, NULL, &globbuf);
            if (ret == 0 && globbuf.gl_pathc > 0) {
                new_args[new_argc++] = strdup(globbuf.gl_pathv[0]);
                globfree(&globbuf);
            } else { new_args[new_argc++] = strdup(args[i]); }
        }
        if (new_argc >= buf_size) { buf_size *= 2; new_args = realloc(new_args, buf_size * sizeof(char*)); }
    }
    new_args[new_argc] = NULL;
    return new_args;
}

// --- REPL ---
char *read_line(FILE *stream) {
    char *line = NULL; size_t bufsize = 0;
    if (getline(&line, &bufsize, stream) == -1) { if (feof(stream)) return NULL; else { perror("readline"); exit(EXIT_FAILURE); } }
    return line;
}

void shell_loop(FILE *stream) {
    char *line; char **args, **expanded_args;
    int is_interactive = isatty(STDIN_FILENO);
    while (1) {
        if (is_interactive) printf("mysh> ");
        line = read_line(stream);
        if (line == NULL) break;
        args = split_line(line);
        expanded_args = expand_globs(args);
        execute(expanded_args);
        for (int i = 0; expanded_args[i] != NULL; i++) free(expanded_args[i]);
        free(expanded_args);
        for (int i = 0; args[i] != NULL; i++) free(args[i]);
        free(args);
        free(line);
        cleanup_background_zombies();
    }
}

int main(int argc, char **argv) {
    setup_signals();
    if (argc > 1) { FILE *fp = fopen(argv[1], "r"); if (!fp) { perror("myshell"); return EXIT_FAILURE; } shell_loop(fp); fclose(fp); }
    else shell_loop(stdin);
    return EXIT_SUCCESS;
}