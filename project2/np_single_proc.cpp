#include <iostream>
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
#include <sys/time.h>		//FD_*
#include <arpa/inet.h>
#include <netdb.h>

#define DEBUG 0
#define EXIT 400
#define CMDEXECUTABLE 0
#define MAX_CLIENTS 31
#define QLEN 5
#define USER_WHO 60
#define MSG_INIT 45

using namespace std;

char argv[257];
char pipe_msg[1025];
pid_t pid[257];
int pipefd[1001][2];
//int pipe_index = 0;
int server_fd, new_socket, master_socket, client_socket[30];

// socket descriptors
fd_set readfds;
fd_set activefds;
int numfd;

int exec(char *command, int user_index);
int parse(char *str, int user_index);
int run(char *command, int user_index, int in_index, int out_index, int redirect, int sender, int receiver, bool ex_mark, bool last_command);
int pipe_handler(int user_index, int outPipe);
int pipe_index_inc(int user_index);
void child_handler(int signo);
void pipe_close(int user_index, int in_index, int out_index);
void user_pipe_close(int user_ndex, int sender, int receiver);
int passiveTCP(const char *service, int qlen);
int passivesock(const char *service, const char *protocol, int qlen);
void broadcast(char *msg);
void welcome(int user_index);
void env(int user_index, bool set);
void logout(int user_index);

void who(int user_index);
void tell(int sender, int receiver, char *msg);
void yell(int sender, char *msg);
void name(int user_index, char *name);

int user_pipe_handler(int sender, int receiver, bool send);
void user_pipe_broadcast(int sender, int receiver, char *command, bool send);

struct user_info {
	int fd;
	char name[21];
	char ip[25];
	int port;
	int own_pipe[1001][2];
	int user_pipe[MAX_CLIENTS][2];
	int pipe_index;
	int sender;
	int receiver;
	char env[200];
} user[MAX_CLIENTS];

int main(int argc, const char* argv[]){

    // server
    struct sockaddr_in addr;
    int addrlen = sizeof(addr);
    int opt = 1;
	int port = stoi(argv[1], nullptr);

	switch(argc) {
		case 1:
				break;
		case 2:
				break;
		default:
				perror("no service name");
				exit(EXIT_FAILURE);
	}

	// user init
	for(int i = 0; i < MAX_CLIENTS; ++i) {
		user[i].fd = -1;
		user[i].port = -1;
		user[i].pipe_index = 0;
		user[i].sender = -1;
		user[i].receiver = -1;
		memset(user[i].name, '\0', 21);
		strcpy(user[i].name, "(no name)");
		memset(user[i].ip, '\0', 25);
		memset(user[i].env, '\0', 200);
		strcpy(user[i].env, "PATH=bin:.");
        for (size_t j = 0; j < 1001; j++) {
			user[i].own_pipe[j][0] = -1;
			user[i].own_pipe[j][1] = -1;
		}	
        for (size_t j = 0; j < MAX_CLIENTS; j++) {
			user[i].user_pipe[j][0] = -1;
			user[i].user_pipe[j][1] = -1;
		}	
	}

	master_socket = passiveTCP(argv[1], QLEN);
	
	numfd = master_socket + 1;
	// clear the socket set
	FD_ZERO(&activefds);
	FD_SET(master_socket, &activefds);
    
    while (1) {
        char input[15001];
        setenv("PATH", "bin:.", 1);

		memcpy(&readfds, &activefds, sizeof(readfds));
		
		while(select(numfd, &readfds, (fd_set *)0, (fd_set *)0, (struct timeval*)0) < 0 && errno == EINTR);
		
		// set user and client
		if(FD_ISSET(master_socket, &readfds)) {
			int sock;
			sock = accept(master_socket, (struct sockaddr *)&addr, (socklen_t*)&addrlen);
			if(sock < 0) {
				perror("accept");
				exit(EXIT_FAILURE);
			}
			
			for(int i = 1; i < MAX_CLIENTS; ++i) {
				if(user[i].fd == -1) {
					user[i].fd = sock;
					strcpy(user[i].ip, inet_ntoa(addr.sin_addr));
					user[i].port = ntohs(addr.sin_port);

					cout << "connect" << endl;
					
					//connection success
					numfd = (sock >= numfd) ? sock + 1: numfd;	
					FD_SET(sock, &activefds);
					welcome(i);
					write(sock, "% ", strlen("% "));
					break;
				}
				else if(i == MAX_CLIENTS - 1) {
					write(sock, "max user online", 15);
					close(sock);
				}
			}							
		}

		for(int i = 1; i < MAX_CLIENTS; ++i) {
			if(user[i].fd != -1 && FD_ISSET(user[i].fd, &readfds)) {
				int readlen;
            	memset(input, '\0', 15001);
				char* command, *bp;
				readlen = read(user[i].fd, input, 15000);
				
				// user logout
				if(readlen == 0) {
					logout(i);
				}
				else {
					if((bp = strchr(input, '\n')) != NULL)
						*bp = '\0';
					if((bp = strchr(input, '\r')) != NULL)
						*bp = '\0';
				}

				env(i, true);
				if(parse(input, i) < 0) logout(i);
				else write(user[i].fd, "% ", strlen("% "));
				env(i, false);
			}
		}
    }
    return 0;
}

void welcome(int user_index) {
	char msg[124] = {"****************************************\n** Welcome to the information server. **\n****************************************\n"};
	write(user[user_index].fd, msg, strlen(msg));	
	char broadcast_msg[1025];
	memset(broadcast_msg, '\0', 1025);
	sprintf(broadcast_msg, "*** User '%s' entered from %s:%d. ***\n", user[user_index].name, user[user_index].ip, user[user_index].port);
	broadcast(broadcast_msg);
}

void env(int user_index, bool set) {
	// set env for this user
	char env[200];
	strcpy(env, user[user_index].env);
	char *pch, *pos;
	pch = strtok_r(env, " ", &pos);				
	if(!set) {
		while(pch != NULL) {
			char tmp[200];
			char *pch2, *pos2, *set[2];
			strcpy(tmp, pch);
			pch2 = strtok_r(tmp, "=", &pos2);
			if(!strcmp(pch2, "PATH")) {
				pch = strtok_r(NULL, " ", &pos);
				continue;
			}
			set[0] = pch2;
			pch2 = strtok_r(NULL, "=", &pos2);
			set[1] = pch2;
			unsetenv(set[0]);
			pch = strtok_r(NULL, " ", &pos);
		}
	}
	else {
		while(pch != NULL) {
			char tmp[200];
			char *pch2, *pos2, *set[2];
			strcpy(tmp, pch);
			pch2 = strtok_r(tmp, "=", &pos2);
			set[0] = pch2;
			pch2 = strtok_r(NULL, "=", &pos2);
			set[1] = pch2;
			setenv(set[0], set[1], 1);
			pch = strtok_r(NULL, " ", &pos);
		}
	}
} 

void logout(int user_index) {

	char *msg = (char *)malloc(sizeof(char) * MSG_INIT);
	sprintf(msg, "*** User '%s' left. ***\n", user[user_index].name);
	broadcast(msg);
	free(msg);	

	close(user[user_index].fd);
	FD_CLR(user[user_index].fd, &activefds);
	user[user_index].fd = -1;
	user[user_index].port = -1;
	user[user_index].sender = -1;
	user[user_index].receiver = -1;

	memset(user[user_index].name, '\0', 21);
	strcpy(user[user_index].name, "(no name)");
	memset(user[user_index].ip, '\0', 25);
	memset(user[user_index].env, '\0', 200);
	strcpy(user[user_index].env, "PATH=bin:.");

	user[user_index].pipe_index = 0;
    for (size_t j = 0; j < 1001; j++) {
		user[user_index].own_pipe[j][0] = -1;
		user[user_index].own_pipe[j][1] = -1;
	}	
    for (size_t j = 0; j < MAX_CLIENTS; j++) {
		user[user_index].user_pipe[j][0] = -1;
		user[user_index].user_pipe[j][1] = -1;
	}		
}

void broadcast(char *msg) {
	for(int i = 1; i < MAX_CLIENTS; ++i){
		if(user[i].fd != -1)	write(user[i].fd, msg, strlen(msg));
	}	
}

int exec(char *command, int user_index) {
	
    char *command_arg[1030], *tmp, *pos;
    for(int i = 0; i < 1030; ++i) command_arg[i] = NULL;
    int index = -1;
	bool msg = false; 	
   
    tmp = strtok_r(command, " ", &pos);
	if(!strcmp(tmp, "tell") || !strcmp(tmp, "yell"))	msg = true;
    while (tmp != NULL) {
		if(msg) {
			command_arg[++index] = tmp;
		}
        else if(tmp[0] != '>' && tmp[0] != '<')	command_arg[++index] = tmp;
        tmp = strtok_r(NULL, " ", &pos);
    }

    // 不要fork
    if(!strcmp(command_arg[0], "printenv")) {
		char *str = (char *)malloc(sizeof(char) * 25);
		strcpy(str, getenv(command_arg[1]));
		strcat(str, "\n");
        if(str != NULL)	send(user[user_index].fd, str, strlen(str), 0);
		free(str);
    }
    else if(!strcmp(command_arg[0], "setenv")) {
        //setenv(command_arg[1], command_arg[2], 1);
        strcat(user[user_index].env, " ");
        strcat(user[user_index].env, command_arg[1]);
        strcat(user[user_index].env, "=");
        strcat(user[user_index].env, command_arg[2]);
    }
    else if(!strcmp(command_arg[0], "exit")) {
		return -1;
    }
	else if(!strcmp(command_arg[0], "who"))		who(user_index);
	else if(!strcmp(command_arg[0], "tell")) {
		char str[1025];
		memset(str, '\0', 1025);
		strcat(str, command_arg[2]);
		for(int i = 3; i < 1030; ++i) {
			if(command_arg[i] == NULL)	break;
			strcat(str, " ");
			strcat(str, command_arg[i]);
		}
		tell(user_index, atoi(command_arg[1]), str);
	}
	else if(!strcmp(command_arg[0], "yell")) {
		char str[1025];
		memset(str, '\0', 1025);
		strcat(str, command_arg[1]);
		for(int i = 2; i < 1030; ++i) {
			if(command_arg[i] == NULL)	break;
			strcat(str, " ");
			strcat(str, command_arg[i]);
		}
		yell(user_index, str);
	}
	else if(!strcmp(command_arg[0], "name"))	name(user_index, command_arg[1]);
    else if(execvp(command_arg[0], command_arg) < 0) {
        fprintf(stderr, "Unknown command: [%s].\n", command);
        exit(EXIT_FAILURE);
    }
	
	return 1;
}   

int parse(char *str, int user_index) {
    char *pch, *pos;
    char command[257];
    int out_pipe = 0, out_pipe_index = 0, in_pipe_index = user[user_index].pipe_index, in_user = -1, out_user = -1;
    memset(command, '\0', 257);
	memset(pipe_msg, '\0', 1025);
    int flag = 1;

    pch = strtok_r(str, " ", &pos);
	if(pch != NULL)	strcat(pipe_msg, pch);
    while(pch != NULL) {
        if(!strcmp(pch, "printenv") || !strcmp(pch, "setenv") || !strcmp(pch, "exit")) {
			strcpy(command, pch);
    		pch = strtok_r(NULL, " ", &pos);
			while(pch != NULL) {
				strcat(command, " ");	
				strcat(command, pch);
    			pch = strtok_r(NULL, " ", &pos);
			}
            flag = exec(command, user_index);
			memset(command, '\0', 257);
			break;
		}
        else if(!strcmp(pch, "who") || !strcmp(pch, "tell") || !strcmp(pch, "yell") || !strcmp(pch, "name")) {
			strcpy(command, pch);
    		pch = strtok_r(NULL, " ", &pos);
			while(pch != NULL) {
				strcat(command, " ");	
				strcat(command, pch);
    			pch = strtok_r(NULL, " ", &pos);
			}
            flag = exec(command, user_index);
			memset(command, '\0', 257);
			break;
		}
        if(pch[0] == '|') {
            
            command[strlen(command) - 1] = '\0';

            if(strlen(pch) == 1) {      // ordinary pipe
                out_pipe = 1;
            }
            else {                      // number pipe
                out_pipe = strtol(pch + 1, NULL, 10);    // pipe to next N-th
            }

            in_pipe_index = user[user_index].pipe_index;

            // check pipe existance, if not, create one and add pipe_inedx
            out_pipe_index = pipe_handler(user_index, out_pipe);

            flag = run(command, user_index, in_pipe_index, out_pipe_index, -1, in_user, out_user, false, false);
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

            in_pipe_index = user[user_index].pipe_index;
            // check pipe existance, if not, create one and add pipe_inedx
            out_pipe_index = pipe_handler(user_index, out_pipe);
            flag = run(command, user_index, in_pipe_index, out_pipe_index, -1, in_user, out_user, true, false);

            // clean command
            memset(command, '\0', 257);
        }
        else if(pch[0] == '>' && strlen(pch) == 1) {        // redirect

            command[strlen(command) - 1] = '\0';
            pch = strtok_r(NULL, " ", &pos);                // get the filename

            int out_file_fd = open(pch, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            
            if(out_file_fd < 0) cout << "open file err" << endl;

            in_pipe_index = user[user_index].pipe_index;

            flag = run(command, user_index, in_pipe_index, -1, out_file_fd, in_user, out_user, false, false);
            pipe_index_inc(user_index);
            close(out_file_fd);
            memset(command, '\0', 257);

        }
		else if(pch[0] == '>') {
			//command[strlen(command) - 1] = '\0';
			int receiver = strtol(pch + 1, NULL, 10);
			out_user = user_pipe_handler(user_index, receiver, true);
			if(out_user > 0)  { 
				user[user_index].receiver = out_user;
				//flag = run(command, user_index, in_pipe_index, -1, -1, -1, out_user, false, false);	
            	strcat(command, pch);
            	strcat(command, " ");
			}
			else memset(command, '\0', 257);
		}
		else if(pch[0] == '<') {
			//command[strlen(command) - 1] = '\0';
			int sender = strtol(pch + 1, NULL, 10);
			in_user = user_pipe_handler(sender, user_index, false);
			if(in_user > 0)  {
				user[user_index].sender = in_user;
				//flag = run(command, user_index, in_pipe_index, -1, -1, in_user, -1, false, false);
            	strcat(command, pch);
            	strcat(command, " ");
			}
			else memset(command, '\0', 257);
		}
        else {
            strcat(command, pch);
            strcat(command, " ");
        }
        pch = strtok_r(NULL, " ", &pos);
		if(pch != NULL) {
			strcat(pipe_msg, " ");
			strcat(pipe_msg, pch);
		}
    }

	if(in_user != -1) {
		user_pipe_broadcast(in_user, user_index, pipe_msg, false);
	}
	if(out_user != -1) {
		user_pipe_broadcast(user_index, out_user, pipe_msg, true);
	}

    in_pipe_index = user[user_index].pipe_index;
    if(command[0] != '\0') {        // the last command of a line
		/*command[strlen(command) - 1] = '\0';
        char built_in_command[257];
        memset(built_in_command, '\0', 257);
        strcpy(built_in_command, command);
        pch = strtok_r(built_in_command, " ", &pos);*/

        flag = run(command, user_index, in_pipe_index, -1, -1, in_user, out_user, false, true);

        memset(command, '\0', 257);
        pipe_index_inc(user_index);
    }

    return flag;
}

int run(char *command, int user_index, int in_index, int out_index, int redirect, int sender, int receiver, bool ex_mark, bool last_command) {
    if(out_index != -1 || receiver != -1) signal(SIGCHLD, child_handler);
    pid_t pid;
    while((pid = fork()) < 0);
    int flag = 1;

    if(pid == 0) {

		dup2(user[user_index].fd, 0);
		dup2(user[user_index].fd, 1);
		dup2(user[user_index].fd, 2);

        if(user[user_index].own_pipe[in_index][0] != -1) {
            dup2(user[user_index].own_pipe[in_index][0], 0);
            //close(pipefd[in_index][0]);
            //close(user[user_index].own_pipe[in_index][1]);
        }

		if(sender != -1) {
			dup2(user[user_index].user_pipe[sender][0], 0);
		}
		
		if(receiver != -1) {
			dup2(user[receiver].user_pipe[user_index][1], 1);
		}

        //for ！N
        if(ex_mark) {
            dup2(user[user_index].own_pipe[out_index][1], 2);
        }

        //for pipe and redirect
        if(out_index != -1) {
            dup2(user[user_index].own_pipe[out_index][1], 1);
            //close(pipefd[out_index][1]);
            //close(user[user_index].own_pipe[out_index][0]);
        }
        else if(redirect != -1) {
            dup2(redirect, 1);
        }
        
		pipe_close(user_index, in_index, out_index);
		user_pipe_close(user_index, sender, receiver);

        flag = exec(command, user_index);            //unknown command
    }
    else {
        int status;
        if(user[user_index].own_pipe[in_index][0] != -1) {
            close(user[user_index].own_pipe[in_index][0]);
            close(user[user_index].own_pipe[in_index][1]);
            user[user_index].own_pipe[in_index][0] = -1;
            user[user_index].own_pipe[in_index][1] = -1;
        }

		if(sender != -1) {
			close(user[user_index].user_pipe[sender][0]);
			close(user[user_index].user_pipe[sender][1]);
			user[user_index].user_pipe[sender][0] = -1;
			user[user_index].user_pipe[sender][1] = -1;
		}		

        if(out_index == -1 && receiver == -1)   waitpid(pid, &status, 0);
        
    }
    return flag;
}

int user_pipe_handler(int sender, int receiver, bool send) {
	char msg[200];
	char *str = msg;

	// user not exists
	if(user[sender].fd == -1) {
		sprintf(str, "*** Error: user #%d does not exist yet. ***\n", sender);
		write(user[receiver].fd, str, strlen(str));
		return -1;
	}
	else if(user[receiver].fd == -1) {
		sprintf(str, "*** Error: user #%d does not exist yet. ***\n", receiver);
		write(user[sender].fd, str, strlen(str));
		return -1;
	}

	// out pipe exists
	if(send){
		if(user[receiver].user_pipe[sender][0] != -1 ||
			user[receiver].user_pipe[sender][1] != -1) {
			sprintf(str, "*** Error: the pipe #%d->#%d already exists. ***\n", sender, receiver);
			write(user[sender].fd, str, strlen(str));
			return -1;
		}
		else{
			while(pipe(user[receiver].user_pipe[sender]) < 0);
			return receiver;
		}
	}
	// in pipe doesnot exists
	else {
		if(user[receiver].user_pipe[sender][0] == -1 ||
			user[receiver].user_pipe[sender][1] == -1) {
			sprintf(str, "*** Error: the pipe #%d->#%d does not exist yet. ***\n", sender, receiver);
			write(user[receiver].fd, str, strlen(str));
			return -1;
		}
		return sender;
	}
	return -1;
}

void user_pipe_broadcast(int sender, int receiver, char *command, bool send) {
	char msg[2000];
	char *str = msg;
	if(send) {
		sprintf(str, "*** %s (#%d) just piped '%s' to %s (#%d) ***\n", user[sender].name, sender, pipe_msg, user[receiver].name, receiver);
	}
	else {
		sprintf(str, "*** %s (#%d) just received from %s (#%d) by '%s' ***\n", user[receiver].name, receiver, user[sender].name, sender, pipe_msg);	
	}
	broadcast(str);
}

void user_pipe_close(int user_index, int sender, int receiver) {
    for(int i = 0; i < MAX_CLIENTS; ++i) {
		for(int j = 0; j < MAX_CLIENTS; ++j){
			if(user[i].user_pipe[j][0] != -1) close(user[i].user_pipe[j][0]);
			if(user[i].user_pipe[j][1] != -1) close(user[i].user_pipe[j][1]);
		}
    }
}

int pipe_handler(int user_index, int outPipe) {
    int out = (outPipe + user[user_index].pipe_index >= 1001) ? user[user_index].pipe_index + outPipe - 1000 : user[user_index].pipe_index + outPipe;

    pipe_index_inc(user_index);

    if(user[user_index].own_pipe[out][0] < 0)  {
        while( pipe(user[user_index].own_pipe[out]) < 0 );
        //cout << "create pipe: " << user[user_index].own_pipe[out][0] << user[user_index].own_pipe[out][1] << endl;
    }
    return out;

}

int pipe_index_inc(int user_index) {
    return user[user_index].pipe_index = (user[user_index].pipe_index == 1000) ? 1 : user[user_index].pipe_index + 1;
}

void pipe_close(int user_index, int in_index, int out_index) {
    for(int i = 0; i <= 1000; ++i) {
		//if(i != in_index && i != out_index) {
			if(user[user_index].own_pipe[i][0] != -1) close(user[user_index].own_pipe[i][0]);
			if(user[user_index].own_pipe[i][1] != -1) close(user[user_index].own_pipe[i][1]);
		//}
    }
}

void who(int user_index){
	char msg[USER_WHO * MAX_CLIENTS];
	memset(msg, '\0', USER_WHO * MAX_CLIENTS);
	sprintf(msg, "<ID>\t<nickname>\t<IP:port>\t<indicate me>\n");
	char *ptr = strchr(msg, '\n') + 1;
	int length = 0;	

	for(int i = 0; i < MAX_CLIENTS; ++i){
		if(user[i].fd != -1) {
			
			if(i == user_index){
				length = sprintf(ptr, "%d\t%s\t%s:%d\t<-me\n", i, user[i].name, user[i].ip, user[i].port);
				ptr += length;
				continue;
			}		
			

			length = sprintf(ptr, "%d\t%s\t%s:%d\n", i, user[i].name, user[i].ip, user[i].port);
			ptr += length;
		}
	}

	cout << msg << endl;

	write(user[user_index].fd, msg, strlen(msg));
}

void tell(int sender, int receiver, char *msg) {

	char *str;
	if(user[receiver].fd != -1) {	// exists

		str = (char *)malloc(sizeof(char) * (MSG_INIT + strlen(msg)));
		sprintf(str, "*** %s told you ***: %s\n", user[sender].name, msg);
		write(user[receiver].fd, str, strlen(str));
	}	
	else {

		str = (char *)malloc(sizeof(char) * MSG_INIT);
		sprintf(str, "*** Error: user #%d does not exist yet. ***\n", receiver);
		write(user[sender].fd, str, strlen(str));
	}
	free(str);
}

void yell(int sender, char *msg) {
	char *str;
	str = (char *)malloc(sizeof(char) * (MSG_INIT + strlen(msg)));
	sprintf(str, "*** %s yelled ***: %s\n", user[sender].name, msg);
	broadcast(str);
	free(str);
}

void name(int user_index, char *name) {
	char *str;
	int i;
	str = (char *)malloc(sizeof(char) * (MSG_INIT + strlen(name)));
	for(i = 1; i < MAX_CLIENTS; ++i) {
		if(!strcmp(name, user[i].name)) {
			sprintf(str, "*** User '%s' already exists. ***\n", name);
			write(user[user_index].fd, str, strlen(str));
			break;
		}
	}
	if(i == MAX_CLIENTS) {
		char *str2;
		strcpy(user[user_index].name, name);
		str2 = (char *)realloc(str, sizeof(char) * (2 * MSG_INIT));
		if(!str2)	perror("realloc: ");
		else str = str2;
		sprintf(str, "*** User from %s:%d is named '%s'. ***\n", user[user_index].ip, user[user_index].port, name);
		broadcast(str);		
	}
	free(str);
}

void child_handler(int signo) {
    int status;
    while(waitpid(-1, &status, WNOHANG) > 0);
}

int passiveTCP(const char* service, int qlen) {
	return passivesock(service, "tcp", qlen);
}

int passivesock(const char *service, const char *protocol, int qlen) {
	struct servent *pse;			// pointer to service info entry
	struct protoent *ppe;			// pointer to protocol info entry
	struct sockaddr_in sin;			// an endpoint addr
	int sock, type, opt = 1;		// socketfd & socket type

	bzero((char *)&sin, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	
	//map service name to port number
	if(pse = getservbyname(service, protocol))
		sin.sin_port = htons(ntohs((u_short) pse -> s_port));
	else if((sin.sin_port = htons((u_short)atoi(service))) == 0) {
		perror("service entry");
		exit(EXIT_FAILURE);
	}

	//map protocol name to protocol number
	if((ppe = getprotobyname(protocol)) == 0) {
		perror("protocol entry");
		exit(EXIT_FAILURE);
	}
	
	//use protocol to choose a socket type
	if(strcmp(protocol, "tcp") == 0)
		type = SOCK_STREAM;
	else
		type = SOCK_DGRAM;

	// allocate a socket
	sock = socket(PF_INET, type, ppe->p_proto);
	if(sock < 0) {
		perror("socket create");
		exit(EXIT_FAILURE);
	}
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

	//bind the socket
	if(bind(sock, (struct sockaddr*) &sin, sizeof(sin)) < 0) {
		perror("bind");
		exit(EXIT_FAILURE);
	}
	if(type == SOCK_STREAM && listen(sock, qlen) < 0) {
		perror("listen");
		exit(EXIT_FAILURE);
	}
	return sock;
}
