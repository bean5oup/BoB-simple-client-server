#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vector>
#ifdef __linux__
#include <arpa/inet.h>
#include <sys/socket.h>
#endif // __linux
#ifdef WIN32
#include <winsock2.h>
#include "../mingw_net.h"
#endif // WIN32
#include <thread>
#include <mutex>

#ifdef WIN32
void perror(const char* msg) { fprintf(stderr, "%s %ld\n", msg, GetLastError()); }
#endif // WIN32

std::mutex m;

void usage() {
	printf("syntax: ts [-e] <port>\n");
	printf("  -e : echo\n");
	printf("sample: ts 1234\n");
}

struct Param {
	bool echo{false};
	bool broadcast{false};
	uint16_t port{0};

	bool parse(int argc, char* argv[]) {
		for (int i = 1; i < argc; i++) {
			if (strcmp(argv[i], "-e") == 0) {
				echo = true;
				continue;
			}
			else if(strcmp(argv[i], "-b") == 0){
				broadcast = true;
				continue;
			}
			port = atoi(argv[i]);
		}
		return port != 0;
	}
} param;

struct Cli {
	std::vector<int> cli;
	int cnt = cli.size();

	void append(int sd) {
		if(cnt == 100)
			printf("[*] full\n");
		cli.push_back(sd);
		cnt = cli.size();
	}

	void erase(int sd) {
		std::vector<int>::iterator iter;
    		for(iter = cli.begin(); iter != cli.end(); iter++){
			if (*iter == sd)
				break;
    		}
		cli.erase(iter);
		cnt = cli.size();
	}
} clients;

void recvThread(int sd) {
	printf("connected\n");
	m.lock();
	clients.append(sd);
	m.unlock();
	static const int BUFSIZE = 65536;
	char buf[BUFSIZE];
	while (true) {
		ssize_t res = ::recv(sd, buf, BUFSIZE - 1, 0);
		if (res == 0 || res == -1) {
			fprintf(stderr, "recv return %ld", res);
			perror(" ");
			break;
		}
		buf[res] = '\0';
		printf("[*]%s", buf);
		fflush(stdout);
		if (param.echo) {
			res = ::send(sd, buf, res, 0);
			if (res == 0 || res == -1) {
				fprintf(stderr, "send return %ld", res);
				perror(" ");
				break;
			}
		}
		if(param.broadcast) {
			m.lock();
			for (int client_sd : clients.cli){
				if (sd == client_sd)
					continue;
				res = ::send(client_sd, buf, res, 0);
				if (res == 0 || res == -1) {
					fprintf(stderr, "send return %ld", res);
					perror(" ");
					break;
				}
			}
			m.unlock();	
		}
	}
	printf("disconnected\n");
	m.lock();
	clients.erase(sd);
	m.unlock();
	::close(sd);
}

void sendThread(int sd){
	while (true) {
		static const int BUFSIZE = 65536;
		char buf[BUFSIZE];
		ssize_t res;
		scanf("%s", buf);
		strcat(buf, "\r\n");
		m.lock();
		for (int client_sd : clients.cli){
			if (sd == client_sd)
				continue;
			res = ::send(client_sd, buf, strlen(buf), 0);
			if (res == 0 || res == -1) {
				fprintf(stderr, "send return %ld", res);
				perror(" ");
				break;
			}
		}
		m.unlock();	
	}
}

int main(int argc, char* argv[]) {
	if (!param.parse(argc, argv)) {
		usage();
		return -1;
	}

#ifdef WIN32
	WSAData wsaData;
	WSAStartup(0x0202, &wsaData);
#endif // WIN32

	int sd = ::socket(AF_INET, SOCK_STREAM, 0);
	if (sd == -1) {
		perror("socket");
		return -1;
	}

	int res;
#ifdef __linux__
	int optval = 1;
	res = ::setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
	if (res == -1) {
		perror("setsockopt");
		return -1;
	}
#endif // __linux

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(param.port);

	ssize_t res2 = ::bind(sd, (struct sockaddr *)&addr, sizeof(addr));
	if (res2 == -1) {
		perror("bind");
		return -1;
	}

	res = listen(sd, 5);
	if (res == -1) {
		perror("listen");
		return -1;
	}

	std::thread t(sendThread, sd);
	t.detach();

	while (true) {
		struct sockaddr_in cli_addr;
		socklen_t len = sizeof(cli_addr);
		int cli_sd = ::accept(sd, (struct sockaddr *)&cli_addr, &len);
		if (cli_sd == -1) {
			perror("accept");
			break;
		}
		std::thread* t = new std::thread(recvThread, cli_sd);
		t->detach();
	}
	::close(sd);
}
