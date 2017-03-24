/**
 *  shell.c - first assignment in OS class
 *  
 *  @author Sangchul, Lee
 *  @since  2017-03-23
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAXLINESIZE 1000     /* max length of the command line */
#define MAXCHILD    100      /* max number of child */
#define MAXCMDS     100      /* max number of commands */
#define MAXARGS     100      /* max arguments of command */

typedef enum { INTERACTIVE, BATCH } Mode;
typedef enum { PARENT, CHILD } Process;

char *PromptFgets(Mode mode, char *line, int size, FILE *input);
void Split(char *arr[], char *str, const char *delimiter);
void RemoveReturn(char *str);

int main(int argc, char *argv[])
{
    Mode mode;          /* input mode */
    Process process;    /* process type */
    FILE *input;        /* input stream */

    /* configure interactive mode */
    if (argc == 1) {
        input = stdin;
        mode = INTERACTIVE;
    }
    /* configure batch mode */
    else if (argc == 2) {
        input = fopen(argv[1], "r");
        if (input == NULL) {
            printf("error: can't open file \"%s\"\n", argv[1]);
            return 1;
        }
        mode = BATCH;
    } else {
        printf("usage: ./shell [file]\n");
        return 1;
    }

    char line[MAXLINESIZE]; /* current input line */
    char *cmd;              /* current command */
    
    while (PromptFgets(mode, line, MAXLINESIZE, input) != NULL 
                                && strstr(line, "quit") == NULL) {

        const char *delimiter = ";";
        int child_pids[MAXCHILD], child_count = 0;
        char *cmds[MAXCMDS];
        int i;

        Split(cmds, line, delimiter);

        /* make child process */
        for (i = 0; cmds[i] != NULL; i++) {
            cmd = cmds[i];
            int current_pid = child_pids[child_count++] = fork();
            if (current_pid < 0) {
                fprintf(stderr, "fork failed\n");
                exit(1);
            } else if (current_pid == 0) {
                process = CHILD;
                goto child;
            } else {
                process = PARENT;
            }
        }

        /* wait for child process */
        int status;
        for(i = 0; i < child_count; i++) {
            waitpid(child_pids[i], &status, 0);
            // TODO: error handler needed
        }
    }

    child: if (process == CHILD) {
        const char *delimiter = " ";
        char *args[MAXARGS];

        Split(args, cmd, delimiter);

        execvp(args[0], args);
        // TODO: error handler needed
    }

    fclose(input);
    return 0;
}

/**
 *  PromptFgets: fgets with "prompt>" print 
 *      @param[in]      mode -> mode of the program (interactive:batch)
 *      @param[out]     line -> save data from input
 *      @param[in]      size -> max line size
 *      @param[in]      input -> input stream
 *      @return         return NULL when fgets get EOF or no characters
 */
char *PromptFgets(Mode mode, char *line, int size, FILE *input)
{
    char *result;
    if (mode == INTERACTIVE) {
        printf("prompt> ");
        result = fgets(line, size, input);
    }
    if (mode == BATCH) {
        result = fgets(line, size, input);
        if (result != NULL) {
            printf("%s", result);
        }
    }
    RemoveReturn(line);
    return result;
}

/**
 *  Split: Split the string by delimiter and save them to array 
 *      @param[out]     arr -> save splitted strings in here
 *      @param[in]      str -> input string to spilt
 *      @param[in]      delimiter -> delimiter to divide str
 */
void Split(char *arr[], char *str, const char *delimiter)
{
    int i;
    char *token = strtok(str, delimiter);
    for (i = 0; token != NULL; i++) {
        arr[i] = token;
        token = strtok(NULL, delimiter);
    }
    arr[i] = NULL;
}

/**
 *  RemoveReturn: remove '\n' from the string 
 *      @param[in, out]     str -> str will be manipulated
 */
void RemoveReturn(char *str)
{
    if (str[strlen(str)-1] == '\n') {
        str[strlen(str)-1] = '\0';
    }
}
