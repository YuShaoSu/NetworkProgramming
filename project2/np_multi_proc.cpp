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
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/ipc.h>  // share memory
#include <sys/shm.h>

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
int server_fd, new_socket, master_socket, client_socket[30];
int pipe_index = 0;
int id = 0;

int exec(char *command);
int parse(char *str);
int run(char *command, int in_index, int out_index, int redirect, int sender, int receiver, bool ex_mark, bool last_command);
int pipe_handler(int outPipe);
int pipe_index_inc();
void child_handler(int signo);
void pipe_close(int in_index, int out_index);
int passiveTCP(const char *service, int qlen);
int passivesock(const char *service, const char *protocol, int qlen);
void msg_deliver(char *msg, int receiver);
void broadcast(char *msg);
void welcome();
void logout();

void reaper(int signo);
void exit_handler(int signo);
void msg_handler(int signo);

void who();
void tell(int sender, int receiver, char *msg);
void yell(int sender, char *msg);
void name(char *name);

int user_pipe_handler(int sender, int receiver, bool send);
void user_pipe_broadcast(int sender, int receiver, char *command, bool send);

struct shm_user_info {
	int user[MAX_CLIENTS];	
	char name[MAX_CLIENTS][21];
	char ip[MAX_CLIENTS][25];
	int port[MAX_CLIENTS];
	pid_t pid[MAX_CLIENTS];
	char msg[MAX_CLIENTS][1025];
	int pipe[MAX_CLIENTS][MAX_CLIENTS][2];
};

shm_user_info *shm;
int shmid;

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

	for(int i = 0; i < 1001; ++i) {
		pipefd[i][0] = -1;
		pipefd[i][1] = -1;
	}

	if((shmid = shmget(IPC_PRIVATE, sizeof(shm_user_info), 0666 | IPC_CREAT)) < 0) {
		perror("shmget");
	}
	shm = (struct shm_user_info *) shmat(shmid, 0, 0);

	// share memory initial
	for(int i = 0; i < MAX_CLIENTS; ++i) {
		shm->user[i] = -1;
		shm->port[i] = -1;
		shm->pid[i] = -1;
		memset(shm->name[i], '\0', 21);
		strcpy(shm->name[i], "(no name)");
		memset(shm->ip[i], '\0', 25);
		memset(shm->msg[i], '\0', 1025);
		for(int j = 0; j < MAX_CLIENTS; ++j) {
			shm->pipe[i][j][0] = -1;
			shm->pipe[i][j][1] = -1;
		}
	}

	master_socket = passiveTCP(argv[1], MAX_CLIENTS);
	
	signal(SIGINT, exit_handler);
	signal(SIGUSR1, msg_handler);
	signal(SIGCHLD, child_handler);
    
	setenv("PATH", "bin:.", 1);
	
    while (1) {
		addrlen = sizeof(addr);
        char input[15001];
        memset(input, '\0', 15001);

		cout << "start" << endl;

		int sock;
		sock = accept(master_socket, (struct sockaddr *)&addr, (socklen_t*)&addrlen);
		cout << sock << endl;

		if(sock < 0) {
			cout << "err when accept" << endl;
			if(errno == EINTR)	continue;
			else {
				perror("accept");
				exit(EXIT_FAILURE);
			}
		}
		else cout << "connect!" << endl;

		int pid;
		
		while((pid = fork()) < 0);
		
		if(pid == 0) {	
			// initial setting when connection
			for(int i = 1; i < MAX_CLIENTS; ++i) {
				if(shm->user[i] == -1) {
					close(master_socket);
					id = i;
					shm->user[i] = sock;
					strcpy(shm->ip[i], inet_ntoa(addr.sin_addr));
					shm->port[i] = ntohs(addr.sin_port);
					shm->pid[i] = getpid();
					
					//connection success
					welcome();
					write(sock, "% ", strlen("% "));
					break;
				}
				// connection reach limit
				else if(i == MAX_CLIENTS - 1) {
					write(sock, "max user online", 15);
					close(sock);
				}
			}	
			
			while(1) {
				int readlen;
				char* command, *bp;
				readlen = read(shm->user[id], input, 15000);
				
				// user logout
				if(readlen == 0) {
					logout();
					break;
				}
				else {
					if((bp = strchr(input, '\n')) != NULL)
						*bp = '\0';
					if((bp = strchr(input, '\r')) != NULL)
						*bp = '\0';
				}
				
				cout << "user " << id << " " << input << endl;

				if(parse(input) < 0) logout();
				else write(shm->user[id], "% ", strlen("% "));
			}
		}
		else { //parent
			close(sock);
			//while( (pid = waitpid(pid2, &status, 0)) && pid < 0);
		}
    }
    return 0;
}

void welcome() {
	char msg[124] = {"****************************************\n** Welcome to the information server. **\n****************************************\n"};
	write(shm->user[id], msg, strlen(msg));	
	char broadcast_msg[1025];
	memset(broadcast_msg, '\0', 1025);
	sprintf(broadcast_msg, "*** User '%s' entered from %s:%d. ***\n", shm->name[id], shm->ip[id], shm->port[id]);
	broadcast(broadcast_msg);
}

void logout() {
	char *msg = (char *)malloc(sizeof(char) * MSG_INIT);
	sprintf(msg, "*** User '%s' left. ***\n", shm->name[id]);
	broadcast(msg);
	free(msg);	

	close(shm->user[id]);
	shm->user[id] = -1;
	shm->port[id] = -1;
	shm->pid[id] = -1;

	pipe_close(-1, -1);

	memset(shm->name[id], '\0', 21);
	strcpy(shm->name[id], "(no name)");
	memset(shm->ip[id], '\0', 25);
	memset(shm->msg[id], '\0', 1025);
	for(int i = 0; i < MAX_CLIENTS; ++i){ 
		shm->pipe[id][i][0] = -1;
		shm->pipe[id][i][1] = -1;
		shm->pipe[i][id][0] = -1;
		shm->pipe[i][id][1] = -1;
	}
	exit(0);
}

void msg_deliver(char *msg, int receiver) {
	strcpy(shm->msg[receiver], msg);
	kill(shm->pid[receiver], SIGUSR1);	
}

void msg_handler(int signo) {
	write(shm->user[id], shm->msg[id], strlen(shm->msg[id]));
	memset(shm->msg[id], '\0', 1025);
}

void broadcast(char *msg) {
	for(int i = 1; i < MAX_CLIENTS; ++i){
		if(shm->user[i] != -1)	msg_deliver(msg, i);
	}	
}

int exec(char *command) {
	
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
        else if(tmp[0] != '>' && tmp[0] != '<' && tmp[0] != '\n')	command_arg[++index] = tmp;
        tmp = strtok_r(NULL, " ", &pos);
    }

    // 不要fork
    if(!strcmp(command_arg[0], "printenv")) {
		char *str = (char *)malloc(sizeof(char) * 25);
		strcpy(str, getenv(command_arg[1]));
		strcat(str, "\n");
        if(str != NULL)	send(shm->user[id], str, strlen(str), 0);
		free(str);
    }
    else if(!strcmp(command_arg[0], "setenv")) {
        setenv(command_arg[1], command_arg[2], 1);
		cout << getenv(command_arg[1]) << command_arg[2] << endl;
        /*strcat(user[user_index].env, " ");
        strcat(user[user_index].env, command_arg[1]);
        strcat(user[user_index].env, "=");
        strcat(user[user_index].env, command_arg[2]);*/
    }
    else if(!strcmp(command_arg[0], "exit")) {
		return -1;
    }
	else if(!strcmp(command_arg[0], "who"))		who();
	else if(!strcmp(command_arg[0], "tell")) {
		char str[1025];
		memset(str, '\0', 1025);
		strcat(str, command_arg[2]);
		for(int i = 3; i < 1030; ++i) {
			if(command_arg[i] == NULL)	break;
			strcat(str, " ");
			strcat(str, command_arg[i]);
		}
		tell(id, atoi(command_arg[1]), str);
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
		yell(id, str);
	}
	else if(!strcmp(command_arg[0], "name"))	name(command_arg[1]);
    else if(execvp(command_arg[0], command_arg) < 0) {
        fprintf(stderr, "Unknown command: [%s].\n", command);
		//perror("execvp");
        exit(EXIT_FAILURE);
    }
	
	return 1;
}   

int parse(char *str) {
    char *pch, *pos;
    char command[257];
    int out_pipe = 0, out_pipe_index = 0, in_pipe_index = pipe_index, in_user = -1, out_user = -1;
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
            flag = exec(command);
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
            flag = exec(command);
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

            in_pipe_index = pipe_index;

            // check pipe existance, if not, create one and add pipe_inedx
            out_pipe_index = pipe_handler(out_pipe);

            flag = run(command, in_pipe_index, out_pipe_index, -1, in_user, out_user, false, false);
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
            flag = run(command, in_pipe_index, out_pipe_index, -1, in_user, out_user, true, false);

            // clean command
            memset(command, '\0', 257);
        }
        else if(pch[0] == '>' && strlen(pch) == 1) {        // redirect

            command[strlen(command) - 1] = '\0';
            pch = strtok_r(NULL, " ", &pos);                // get the filename

            int out_file_fd = open(pch, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            
            if(out_file_fd < 0) cout << "open file err" << endl;

            in_pipe_index = pipe_index;

            flag = run(command, in_pipe_index, -1, out_file_fd, in_user, out_user, false, false);
            pipe_index_inc();
            close(out_file_fd);
            memset(command, '\0', 257);

        }
		else if(pch[0] == '>') {
			//command[strlen(command) - 1] = '\0';
			int receiver = strtol(pch + 1, NULL, 10);
			out_user = user_pipe_handler(id, receiver, true);
			if(out_user > 0)  {
				char filename[5];
				sprintf(filename, "%d_%d", id, receiver);
				if ( (mkfifo(filename, S_IFIFO | 0666) < 0) && (errno != EEXIST))
					perror("can't create fifo");
				if((shm->pipe[receiver][id][1] = open(filename, O_WRONLY | O_CREAT)) < 0) {
					perror("fifo");
				}
				//flag = run(command, user_index, in_pipe_index, -1, -1, -1, out_user, false, false);	
            	strcat(command, pch);
            	strcat(command, " ");
			}
			else memset(command, '\0', 257);
		}
		else if(pch[0] == '<') {
			//command[strlen(command) - 1] = '\0';
			int sender = strtol(pch + 1, NULL, 10);
			in_user = user_pipe_handler(sender, id, false);
			if(in_user > 0)  {
				//char filename[5];
				//sprintf(filename, "%d_%d", sender, id);
				//if(mkfifo(filename, 0666) < 0 && errno != EEXIST)
				//	perror("fifo create");
				//if((shm->pipe[id][sender][0] = open(filename, O_RDONLY)) < 0)
				//	perror("fifo open");
				//cout << "parse: id " << id << " sender " << sender << " fd " << shm->pipe[id][sender][0] << endl;
				//user[user_index].sender = in_user;
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
		user_pipe_broadcast(in_user, id, pipe_msg, false);
	}
	if(out_user != -1) {
		user_pipe_broadcast(id, out_user, pipe_msg, true);
	}

    in_pipe_index = pipe_index;
    if(command[0] != '\0') {        // the last command of a line

        flag = run(command, in_pipe_index, -1, -1, in_user, out_user, false, true);

        memset(command, '\0', 257);
        pipe_index_inc();
    }

    return flag;
}

int run(char *command, int in_index, int out_index, int redirect, int sender, int receiver, bool ex_mark, bool last_command) {
    if(out_index != -1 || receiver != -1) signal(SIGCHLD, child_handler);
    pid_t pid;
    while((pid = fork()) < 0);
    int flag = 1;

    if(pid == 0) {

		dup2(shm->user[id], 0);
		dup2(shm->user[id], 1);
		dup2(shm->user[id], 2);

        if(pipefd[in_index][0] != -1) {
            dup2(pipefd[in_index][0], 0);
            //close(pipefd[in_index][0]);
            close(pipefd[in_index][1]);
        }

		// fifo
		if(sender != -1) {
			char filename[5];
			sprintf(filename, "%d_%d", sender, id);
			if((shm->pipe[id][sender][0] = open(filename, O_RDONLY)) < 0)
				perror("fifo open");
			dup2(shm->pipe[id][sender][0], 0);
			close(shm->pipe[id][sender][1]);
		}
		
		if(receiver != -1) {
			dup2(shm->pipe[receiver][id][1], 1);
			close(shm->pipe[receiver][id][0]);
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

		if(sender != -1) {
			close(shm->pipe[id][sender][0]);
			close(shm->pipe[id][sender][1]);
			shm->pipe[id][sender][0] = -1;
			shm->pipe[id][sender][1] = -1;
		}

        if(out_index == -1)   waitpid(pid, &status, 0);
        
    }
    return flag;
}

int user_pipe_handler(int sender, int receiver, bool send) {
	char msg[200];
	char *str = msg;

	// user not exists
	if(shm->user[sender] == -1) {
		sprintf(str, "*** Error: user #%d does not exist yet. ***\n", sender);
		msg_deliver(str, receiver);
		return -1;
	}
	else if(shm->user[receiver] == -1) {
		sprintf(str, "*** Error: user #%d does not exist yet. ***\n", receiver);
		msg_deliver(str, sender);
		return -1;
	}

	// out pipe exists
	if(send){
		if(shm->pipe[receiver][sender][0] != -1 ||
			shm->pipe[receiver][sender][1] != -1) {
			sprintf(str, "*** Error: the pipe #%d->#%d already exists. ***\n", sender, receiver);
			write(shm->user[sender], str, strlen(str));
			return -1;
		}
		else{
			//while(pipe(user[receiver].user_pipe[sender]) < 0);
			return receiver;
		}
	}
	// in pipe doesnot exists
	else {
		if(shm->pipe[receiver][sender][1] == -1) {
			sprintf(str, "*** Error: the pipe #%d->#%d does not exist yet. ***\n", sender, receiver);
			write(shm->user[receiver], str, strlen(str));
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
		sprintf(str, "*** %s (#%d) just piped '%s' to %s (#%d) ***\n", shm->name[sender], sender, pipe_msg, shm->name[receiver], receiver);
	}
	else {
		sprintf(str, "*** %s (#%d) just received from %s (#%d) by '%s' ***\n", shm->name[receiver], receiver, shm->name[sender], sender, pipe_msg);	
	}
	broadcast(str);
}

int pipe_handler(int outPipe) {
    int out = (outPipe + pipe_index >= 1001) ? pipe_index + outPipe - 1000 : pipe_index + outPipe;

    pipe_index_inc();

    if(pipefd[out][0] < 0)  {
        while( pipe(pipefd[out]) < 0 );
        //cout << "create pipe: " << user[user_index].own_pipe[out][0] << user[user_index].own_pipe[out][1] << endl;
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

void who(){
	char msg[USER_WHO * MAX_CLIENTS];
	memset(msg, '\0', USER_WHO * MAX_CLIENTS);
	sprintf(msg, "<ID>\t<nickname>\t<IP:port>\t<indicate me>\n");
	char *ptr = strchr(msg, '\n') + 1;
	int length = 0;	

	for(int i = 0; i < MAX_CLIENTS; ++i){
		if(shm->user[i] != -1) {
			
			if(i == id){
				length = sprintf(ptr, "%d\t%s\t%s:%d\t<-me\n", i, shm->name[i], shm->ip[i], shm->port[i]);
				ptr += length;
				continue;
			}		
			

			length = sprintf(ptr, "%d\t%s\t%s:%d\n", i, shm->name[i], shm->ip[i], shm->port[i]);
			ptr += length;
		}
	}

	write(shm->user[id], msg, strlen(msg));
}

void tell(int sender, int receiver, char *msg) {

	char *str;
	if(shm->user[receiver] != -1) {	// exists

		str = (char *)malloc(sizeof(char) * (MSG_INIT + strlen(msg)));
		sprintf(str, "*** %s told you ***: %s\n", shm->name[sender], msg);
		//write(shm->user[receiver], str, strlen(str));
		msg_deliver(str, receiver);
	}	
	else {

		str = (char *)malloc(sizeof(char) * MSG_INIT);
		sprintf(str, "*** Error: user #%d does not exist yet. ***\n", receiver);
		write(shm->user[sender], str, strlen(str));
		//msg_deliver(str, sender);
	}
	free(str);
}

void yell(int sender, char *msg) {
	char *str;
	str = (char *)malloc(sizeof(char) * (MSG_INIT + strlen(msg)));
	sprintf(str, "*** %s yelled ***: %s\n", shm->name[sender], msg);
	broadcast(str);
	free(str);
}

void name(char *name) {
	char *str;
	int i;
	str = (char *)malloc(sizeof(char) * (MSG_INIT + strlen(name)));
	for(i = 1; i < MAX_CLIENTS; ++i) {
		if(!strcmp(name, shm->name[i])) {
			sprintf(str, "*** User '%s' already exists. ***\n", name);
			write(shm->user[id], str, strlen(str));
			break;
		}
	}
	if(i == MAX_CLIENTS) {
		char *str2;
		strcpy(shm->name[id], name);
		str2 = (char *)realloc(str, sizeof(char) * (2 * MSG_INIT));
		if(!str2)	perror("realloc: ");
		else str = str2;
		sprintf(str, "*** User from %s:%d is named '%s'. ***\n", shm->ip[id], shm->port[id], name);
		broadcast(str);		
	}
	free(str);
}

void child_handler(int signo) {
    int status;
    while(waitpid(-1, &status, WNOHANG) > 0);
}

void reaper(int signo) {
	union wait status;
	while(wait3(&status, WNOHANG, (struct rusage *)0) >= 0);
}

void exit_handler(int signo) {
	cout << "Caught signal " << signo << endl;
	shmdt(shm);
	shmctl(shmid, IPC_RMID, NULL);	
	exit(0);
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
