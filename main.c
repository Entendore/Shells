#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

#define MAX_CMD_LENGTH 1024
#define MAX_ARGS 100
#define HISTORY_SIZE 10
#define MAX_ALIASES 10

// History and Alias data structures
char *history[HISTORY_SIZE];
int history_index = 0;
char *aliases[MAX_ALIASES][2];  // Alias name and corresponding command

// Function to handle Ctrl+C (SIGINT) gracefully
void sigint_handler(int sig) {
    // Print a new prompt instead of terminating the shell
    printf("\nmyshell> ");
    fflush(stdout);
}

// Function to execute a command with arguments
void execute_command(char *cmd) {
    char *args[MAX_ARGS];
    char *token;
    int i = 0;
    int background = 0;

    // Check for background execution
    if (cmd[strlen(cmd) - 2] == '&') {
        background = 1;
        cmd[strlen(cmd) - 2] = '\0'; // Remove '&'
    }

    // Tokenize the command string into arguments
    token = strtok(cmd, " \n");
    while (token != NULL) {
        args[i++] = token;
        token = strtok(NULL, " \n");
    }
    args[i] = NULL;  // NULL-terminate the argument list

    // Handle built-in commands (exit, cd)
    if (strcmp(args[0], "exit") == 0) {
        exit(0);
    }

    if (strcmp(args[0], "cd") == 0) {
        if (i < 2) {
            fprintf(stderr, "cd: missing argument\n");
        } else {
            if (chdir(args[1]) != 0) {
                perror("cd");
            }
        }
        return;
    }

    // Check for aliases and replace command
    for (int j = 0; j < MAX_ALIASES; j++) {
        if (aliases[j][0] != NULL && strcmp(args[0], aliases[j][0]) == 0) {
            args[0] = aliases[j][1];
            break;
        }
    }

    // Handle normal commands
    if (fork() == 0) {  // Child process
        execvp(args[0], args);
        perror("execvp failed");
        exit(1);
    } else {  // Parent process
        if (!background) {
            wait(NULL);  // Wait for the child process to complete
        }
    }
}

// Function to handle command history
void add_to_history(char *cmd) {
    if (history_index < HISTORY_SIZE) {
        history[history_index] = strdup(cmd);
        history_index++;
    } else {
        free(history[0]);  // Free the oldest command
        for (int i = 1; i < HISTORY_SIZE; i++) {
            history[i - 1] = history[i];
        }
        history[HISTORY_SIZE - 1] = strdup(cmd);  // Add the new command
    }
}

// Function to print command history
void print_history() {
    for (int i = 0; i < history_index; i++) {
        printf("%d  %s", i + 1, history[i]);
    }
}

// Function to add an alias
void add_alias(char *alias, char *command) {
    for (int i = 0; i < MAX_ALIASES; i++) {
        if (aliases[i][0] == NULL) {
            aliases[i][0] = strdup(alias);
            aliases[i][1] = strdup(command);
            printf("Alias '%s' added for command '%s'\n", alias, command);
            return;
        }
    }
    printf("Alias limit reached.\n");
}

// Function to handle aliases
void list_aliases() {
    for (int i = 0; i < MAX_ALIASES; i++) {
        if (aliases[i][0] != NULL) {
            printf("alias %s='%s'\n", aliases[i][0], aliases[i][1]);
        }
    }
}

int main() {
    char cmd[MAX_CMD_LENGTH];

    // Setup signal handler for Ctrl+C (SIGINT)
    signal(SIGINT, sigint_handler);

    // Shell loop
    while (1) {
        printf("myshell> ");
        if (fgets(cmd, MAX_CMD_LENGTH, stdin) == NULL) {
            // Handle EOF (Ctrl+D)
            printf("\nExiting shell\n");
            break;
        }

        // If command is "exit", break the loop
        if (strncmp(cmd, "exit", 4) == 0) {
            break;
        }

        // Command History: Add command to history
        add_to_history(cmd);

        // If command is "history", print command history
        if (strncmp(cmd, "history", 7) == 0) {
            print_history();
        } else if (strncmp(cmd, "alias", 5) == 0) {
            // If command is "alias", add a new alias
            char *alias = strtok(cmd + 6, "=");
            char *command = strtok(NULL, "\n");
            if (alias != NULL && command != NULL) {
                add_alias(alias, command);
            } else {
                printf("Usage: alias <alias_name>=<command>\n");
            }
        } else if (strncmp(cmd, "unalias", 7) == 0) {
            // If command is "unalias", remove an alias (not implemented yet)
            // This could be implemented similarly to adding an alias.
            // For now, we just display the aliases.
            list_aliases();
        } else {
            execute_command(cmd);  // Execute the command
        }
    }

    // Free command history memory
    for (int i = 0; i < history_index; i++) {
        free(history[i]);
    }

    // Free alias memory
    for (int i = 0; i < MAX_ALIASES; i++) {
        free(aliases[i][0]);
        free(aliases[i][1]);
    }

    return 0;
}
