#include <iostream>
#include <string>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <cstdlib>          // strtol
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h> 

#define DEBUG 0
#define EXIT 400
#define CMDEXECUTABLE 0
#define PORT 8089

using namespace std;

char argv[257];
pid_t pid[257];
int pipefd[1001][2];
int pipe_index = 0;
int server_fd, new_socket;

int exec(char *command);
int parse(char *str);
int run(char *command, int in_index, int out_index, int redirect, bool ex_mark, bool last_command);
int pipe_handler(int outPipe);
int pipe_index_inc();
void child_handler(int signo);
void pipe_close(int in_index, int out_index);


int main(int argc, const char* argv[]){

	const char new_line = '\n';

    // server
    struct sockaddr_in addr;
    int addrlen = sizeof(addr);
    int opt = 1;
	int port = stoi(argv[1], nullptr);

    // Creating socket file descriptor 
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) { 
        perror("socket failed"); 
        exit(EXIT_FAILURE); 
    } 

    // set reuse
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) { 
        perror("setsockopt"); 
        exit(EXIT_FAILURE); 
    } 
    addr.sin_family = AF_INET; 
    addr.sin_addr.s_addr = INADDR_ANY; 
    addr.sin_port = htons(port); 

    // Forcefully attaching socket to the port 8080 
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { 
        perror("bind failed"); 
        exit(EXIT_FAILURE); 
    } 
    if (listen(server_fd, 1) < 0) { 
        perror("listen"); 
        exit(EXIT_FAILURE); 
    } 
    
    while (1) {
		close(new_socket);
        if ((new_socket = accept(server_fd, (struct sockaddr *)&addr, (socklen_t*)&addrlen))<0) { 
            perror("accept"); 
            exit(EXIT_FAILURE); 
        } 

        dup2(new_socket, 0);
        dup2(new_socket, 1);
        dup2(new_socket, 2);

        char input[15001];
        setenv("PATH", "bin:.", 1);

        for (size_t i = 0; i < 1001; i++) {
            pipefd[i][0] = -1;
            pipefd[i][1] = -1;
        }

        while(1){
			int readlen;
            memset(input, '\0', 15001);
			char* command, *bp;
			send(new_socket, "% ", strlen("% "), 0);
			readlen = read(new_socket, input, 15000);
			if(readlen == 0) {
				break;
			}
			else {
				if((bp = strchr(input, '\n')) != NULL)
					*bp = '\0';
				if((bp = strchr(input, '\r')) != NULL)
					*bp = '\0';
			}
			if(parse(input) < 0) break;
        }
    }
    
    return 0;
}


int exec(char *command) {
    
    char *command_arg[30], *tmp, *pos;
    for(int i = 0; i < 30; ++i) command_arg[i] = NULL;
    int index = -1;
    
    tmp = strtok_r(command, " ", &pos);
    while (tmp != NULL) {
        command_arg[++index] = tmp;
        tmp = strtok_r(NULL, " ", &pos);
    }

    // 不要fork
    if(strcmp(command_arg[0], "printenv") == 0) {
		char *str = (char *)malloc(sizeof(char) * 25);
		strcpy(str, getenv(command_arg[1]));
		strcat(str, "\n");
        if(str != NULL)	send(new_socket, str, strlen(str), 0);
		free(str);
    }
    else if(strcmp(command_arg[0], "setenv") == 0) {
        setenv(command_arg[1], command_arg[2], 1);
    }
    else if(strcmp(command_arg[0], "exit") == 0) {
		return -1;
    }
    else if(execvp(command_arg[0], command_arg) < 0) {
        fprintf(stderr, "Unknown command: [%s].\n", command);
        exit(EXIT_FAILURE);
    }
	
	return 1;
}   

int parse(char *str) {
    char *pch, *pos;
    char command[257];
    int out_pipe = 0, out_pipe_index = 0, in_pipe_index = pipe_index;
    memset(command, '\0', 257);
    int flag = 1;

    pch = strtok_r(str, " ", &pos);
    while(pch != NULL) {
        if(pch[0] == '|') {
            
            command[strlen(command) - 1] = '\0';

            if(strlen(pch) == 1) {      // ordinary pipe
                out_pipe = 1;
            }
            else {                      // number pipe
                out_pipe = strtol(pch + 1, NULL, 10);    // pipe to next N-th
            }

            in_pipe_index = pipe_index;
            // check pipe existance, if not, create one and add pipe_inedx
            out_pipe_index = pipe_handler(out_pipe);

            flag = run(command, in_pipe_index, out_pipe_index, -1, false, false);

            // clean command
            memset(command, '\0', 257);
        }
        else if(pch[0] == '!') {        // only add stderr
            
            command[strlen(command) - 1] = '\0';

            if(strlen(pch) == 1) {      // ordinary pipe
                out_pipe = 1;
            }
            else {                      // number pipe
                out_pipe = strtol(pch + 1, NULL, 10);    // pipe to next N-th
            }

            in_pipe_index = pipe_index;
            // check pipe existance, if not, create one and add pipe_inedx
            out_pipe_index = pipe_handler(out_pipe);

            flag = run(command, in_pipe_index, out_pipe_index, -1, true, false);

            // clean command
            memset(command, '\0', 257);
        }
        else if(pch[0] == '>' && strlen(pch) == 1) {        // redirect

            command[strlen(command) - 1] = '\0';
            pch = strtok_r(NULL, " ", &pos);                // get the filename

            int out_file_fd = open(pch, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            
            if(out_file_fd < 0) cout << "open file err" << endl;

            in_pipe_index = pipe_index;

            flag = run(command, in_pipe_index, -1, out_file_fd, false, false);
            pipe_index_inc();
            close(out_file_fd);
            memset(command, '\0', 257);

        }
        else {
            strcat(command, pch);
            strcat(command, " ");
        }
        pch = strtok_r(NULL, " ", &pos);
    }
    in_pipe_index = pipe_index;
    if(command[0] != '\0') {        // the last command of a line
        char built_in_command[257];
        memset(built_in_command, '\0', 257);
        strcpy(built_in_command, command);
        pch = strtok_r(built_in_command, " ", &pos);
        if(strcmp(pch, "printenv") == 0 || strcmp(pch, "setenv") == 0 || strcmp(pch, "exit") == 0)
            flag = exec(command);
        else    flag = run(command, in_pipe_index, -1, -1, false, true);
        memset(command, '\0', 257);
        pipe_index_inc();
    }
    return flag;
}

int run(char *command, int in_index, int out_index, int redirect, bool ex_mark, bool last_command) {
    if(out_index != -1) signal(SIGCHLD, child_handler);
    pid_t pid;
    while((pid = fork()) < 0);
    int flag = 1;

    if(pid == 0) {
        if(pipefd[in_index][0] != -1) {
            dup2(pipefd[in_index][0], 0);
            //close(pipefd[in_index][0]);
            close(pipefd[in_index][1]);
        }


        //for ！N
        if(ex_mark) {
            dup2(pipefd[out_index][1], 2);
        }

        //for pipe and redirect
        if(out_index != -1) {
            dup2(pipefd[out_index][1], 1);
            //close(pipefd[out_index][1]);
            close(pipefd[out_index][0]);
        }
        else if(redirect != -1) {
            dup2(redirect, 1);
        }
        
		pipe_close(in_index, out_index);

        flag = exec(command);            //unknown command
    }
    else {
        int status;
        if(pipefd[in_index][0] != -1) {
            close(pipefd[in_index][0]);
            close(pipefd[in_index][1]);
            pipefd[in_index][0] = -1;
            pipefd[in_index][1] = -1;
        }

        if(out_index == -1)   waitpid(pid, &status, 0);
        
    }
    return flag;
}

int pipe_handler(int outPipe) {
    int out = (outPipe + pipe_index >= 1001) ? pipe_index + outPipe - 1000 : pipe_index + outPipe;

    pipe_index_inc();

    if(pipefd[out][0] < 0)  {
        while( pipe(pipefd[out]) < 0 );
        //cout << "create pipe: " << pipefd[out][0] << pipefd[out][1] << endl;
    }
    return out;

}

int pipe_index_inc() {
    return pipe_index = (pipe_index == 1000) ? 1 : pipe_index + 1;
}

void pipe_close(int in_index, int out_index) {
    for(int i = 0; i <= 1000; ++i) {
	if(i != in_index && i != out_index) {
		if(pipefd[i][0] != -1) close(pipefd[i][0]);
		if(pipefd[i][1] != -1) close(pipefd[i][1]);
	}
    }
}

void child_handler(int signo) {
    int status;
    while(waitpid(-1, &status, WNOHANG) > 0);
}
