#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdbool.h>

#define DEBUG 0
#define EXIT 400
#define CMDEXECUTABLE 0

char argv[257];
pid_t pid[257];
int pipefd[1001][2];
int pipe_index = 0;

void exec(char *command);
void parse(char *str);
void run(char *command, int in_index, int out_index, int redirect, bool ex_mark, bool last_command);
int pipe_handler(int outPipe);
int pipe_index_inc();
void child_handler(int signo);
void pipe_close(int in_index, int out_index);

int main()
{
    char *input;
    int flag = 0, len;
    size_t input_size = 0;
    setenv("PATH", "bin:.", 1);

    for (size_t i = 0; i < 1001; i++)
    {
        pipefd[i][0] = -1;
        pipefd[i][1] = -1;
    }

    while (1)
    {
        fprintf(stdout, "%% ");
        if ((len = getline(&input, &input_size, stdin)) != -1)
        {
            input[len - 1] = '\0';          // eliminate the newline char
            parse(input);
        }
        else
            break;
    }
    free(input);

    return 0;
}

void parse(char *str)
{
    char *pch, *pos;
    char command[257];
    int out_pipe = 0, out_pipe_index = 0, in_pipe_index = pipe_index;
    memset(command, '\0', 257);

    pch = strtok_r(str, " ", &pos);
    while (pch != NULL)
    {
        if (pch[0] == '|')
        {

            command[strlen(command) - 1] = '\0';

            if (strlen(pch) == 1)
            { // ordinary pipe
                out_pipe = 1;
            }
            else
            {                                         // number pipe
                out_pipe = strtol(pch + 1, NULL, 10); // pipe to next N-th
            }

            in_pipe_index = pipe_index;
            // check pipe existance, if not, create one and add pipe_inedx
            out_pipe_index = pipe_handler(out_pipe);

            run(command, in_pipe_index, out_pipe_index, -1, false, false);

            // clean command
            memset(command, '\0', 257);
        }
        else if (pch[0] == '!')
        { // only add stderr

            command[strlen(command) - 1] = '\0';

            if (strlen(pch) == 1)
            { // ordinary pipe
                out_pipe = 1;
            }
            else
            {                                         // number pipe
                out_pipe = strtol(pch + 1, NULL, 10); // pipe to next N-th
            }

            in_pipe_index = pipe_index;
            // check pipe existance, if not, create one and add pipe_inedx
            out_pipe_index = pipe_handler(out_pipe);

            run(command, in_pipe_index, out_pipe_index, -1, true, false);

            // clean command
            memset(command, '\0', 257);
        }
        else if (pch[0] == '>' && strlen(pch) == 1)
        { // redirect

            command[strlen(command) - 1] = '\0';
            pch = strtok_r(NULL, " ", &pos); // get the filename

            int out_file_fd = open(pch, O_WRONLY | O_CREAT | O_TRUNC, 0644);

            if (out_file_fd < 0)
                fprintf(stderr, "open file err\n");

            in_pipe_index = pipe_index;

            run(command, in_pipe_index, -1, out_file_fd, false, false);
            pipe_index_inc();
            close(out_file_fd);
            memset(command, '\0', 257);
        }
        else
        {
            strcat(command, pch);
            strcat(command, " ");
        }
        pch = strtok_r(NULL, " ", &pos);
    }

    in_pipe_index = pipe_index;
    if (command[0] != '\0')
    { // the last command of a line
        char built_in_command[257];
        memset(built_in_command, '\0', 257);
        strcpy(built_in_command, command);
        pch = strtok_r(built_in_command, " ", &pos);
        if (strcmp(pch, "printenv") == 0 || strcmp(pch, "setenv") == 0 || strcmp(pch, "exit") == 0 || strcmp(pch, "unsetenv") == 0)
            exec(command);
        else
            run(command, in_pipe_index, -1, -1, false, true);
        memset(command, '\0', 257);
        pipe_index_inc();
    }
}

void exec(char *command)
{
    char *command_arg[30], *tmp, *pos;
    for (int i = 0; i < 30; ++i)
        command_arg[i] = NULL;
    int index = -1;

    tmp = strtok_r(command, " ", &pos);
    while (tmp != NULL)
    {
        command_arg[++index] = tmp;
        tmp = strtok_r(NULL, " ", &pos);
    }

    if (strcmp(command_arg[0], "printenv") == 0)
    {
        char *str = getenv(command_arg[1]);
        if (str != NULL)
            fprintf(stdout, "%s\n", str);
    }
    else if (strcmp(command_arg[0], "setenv") == 0)
    {
        setenv(command_arg[1], command_arg[2], 1);
    }
    else if (strcmp(command_arg[0], "unsetenv") == 0)
    {
        unsetenv(command_arg[1]);
    }
    else if (strcmp(command_arg[0], "exit") == 0)
    {
        exit(0);
    }
    else if (execvp(command_arg[0], command_arg) < 0)
    {
        fprintf(stderr, "Unknown command: [%s].\n", command);
        exit(EXIT_FAILURE);
    }
}

void run(char *command, int in_index, int out_index, int redirect, bool ex_mark, bool last_command)
{
    if (out_index != -1)
        signal(SIGCHLD, child_handler);
    pid_t pid;
    while ((pid = fork()) < 0)
        ;

    if (pid == 0)
    {

        //pipe and fd
        if (pipefd[in_index][0] != -1)
        {
            dup2(pipefd[in_index][0], 0);
            //close(pipefd[in_index][0]);
            close(pipefd[in_index][1]);
        }

        //for ！N
        if (ex_mark)
        {
            dup2(pipefd[out_index][1], 2);
        }

        //for pipe and redirect
        if (out_index != -1)
        {
            dup2(pipefd[out_index][1], 1);
            close(pipefd[out_index][0]);
        }
        else if (redirect != -1)
        {
            dup2(redirect, 1);
        }

        pipe_close(in_index, out_index);

        exec(command);
    }
    else
    {
        int status;
        if (pipefd[in_index][0] != -1)
        {
            close(pipefd[in_index][0]);
            close(pipefd[in_index][1]);
            pipefd[in_index][0] = -1;
            pipefd[in_index][1] = -1;
        }

        if (out_index == -1)
            waitpid(pid, &status, 0);
    }
}

int pipe_handler(int outPipe)
{
    int out = (outPipe + pipe_index >= 1001) ? pipe_index + outPipe - 1000 : pipe_index + outPipe;

    pipe_index_inc();

    if (pipefd[out][0] < 0)
    {
        while (pipe(pipefd[out]) < 0)
            ;
    }
    return out;
}

int pipe_index_inc()
{
    return pipe_index = (pipe_index == 1000) ? 1 : pipe_index + 1;
}

void pipe_close(int in_index, int out_index)
{
    for (int i = 0; i <= 1000; ++i)
    {
        if (i != in_index && i != out_index)
        {
            if (pipefd[i][0] != -1)
                close(pipefd[i][0]);
            if (pipefd[i][1] != -1)
                close(pipefd[i][1]);
        }
    }
}

void child_handler(int signo)
{
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0)
        ;
}
