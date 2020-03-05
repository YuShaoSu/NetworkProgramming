#include <array>
#include <boost/asio.hpp>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sstream>
#include <regex>

using namespace std;
using namespace boost::asio;

io_service global_io_service;

class HttpSession : public enable_shared_from_this<HttpSession> {
 private:
  enum { max_length = 1024 };
  ip::tcp::socket _socket;
  array<char, max_length> _data;
  bool is_cgi = true;
  string query, cgi, method = {"GET"}, uri, protocol, host;
  string s_addr, s_port, r_addr, r_port;
  stringstream ss;
  string header;

 public:
  HttpSession(ip::tcp::socket socket) : _socket(move(socket)) {}

  void start() { do_read(); }

 private:
  void do_read() {
    auto self(shared_from_this());
    _socket.async_read_some(
        buffer(_data, max_length),
        [this, self](boost::system::error_code ec, size_t length) {
          if (!ec) { 
				for(auto &s : _data){
					ss << s;
				}
				header = ss.str();
				cout << header;
				parse();
				run();
			}
        });
  }

	void parse() {
		std::regex reg_first("/.*");
  		std::smatch m;
		std::ssub_match sm;
		if(regex_search(header, m, reg_first)){
			string first = m.str();
			//cout << first << endl;

			std::regex reg_uri("/[^ ]+");
			if(regex_search(first, m, reg_uri))	uri = m.str();
				
			string tmp;
			std::regex reg_query_tmp("[?][^ ]*");
			if(regex_search(first, m, reg_query_tmp))	tmp = m.str();
		
			std::regex reg_query("[^?].*");
			if(regex_search(tmp, m, reg_query))	query = m.str();

			std::regex reg_cgi("[a-zA-Z0-9_]*\\.cgi");
			if(regex_search(first, m, reg_cgi))	{ cgi = m.str(); is_cgi = true; }
			else is_cgi = false;

			std::regex reg_protocol("HTTP.*");
			if(regex_search(first, m, reg_protocol))	protocol = m.str();
			
			
		}

		std::regex reg_second("Host:.*");
		if(regex_search(header, m, reg_second)){
			string second = m.str();

			std::regex reg_host("[^ ]+\\.[^:]*");
			if(regex_search(second, m ,reg_host)){
				host = m.str();
			}
		}	

		cout << "uri " << uri << endl;
		cout << "query " << query << endl;
		cout << "cgi " << cgi << endl;
		cout << "method " << method << endl;
		cout << "host " << host << endl;
		cout << endl;
		s_addr = _socket.local_endpoint().address().to_string();
		s_port = to_string(_socket.local_endpoint().port());
		r_addr = _socket.remote_endpoint().address().to_string();
		r_port = to_string(_socket.remote_endpoint().port());

	}
  
	void run() {
		signal(SIGCHLD, [](int signo){
	    	int status;
			while(waitpid(-1, &status, WNOHANG) > 0);
		});
		pid_t pid;
		global_io_service.notify_fork(boost::asio::io_service::fork_prepare);

		while((pid = fork()) < 0);

		if(pid == 0) {
			global_io_service.notify_fork(boost::asio::io_service::fork_child);
			set_env();
			char* const arg[2] = {const_cast<char*>(cgi.c_str()), NULL};
			if(is_cgi)	do_write();
			dup2(_socket.native_handle(), 0);
			dup2(_socket.native_handle(), 1);
			dup2(_socket.native_handle(), 2);
			if( is_cgi && (execvp(arg[0], arg) < 0)){
				exit(EXIT_FAILURE);
			}
		}
		else {
			global_io_service.notify_fork(boost::asio::io_service::fork_parent);
		}
	}

	void set_env() {
		setenv("REQUEST_METHOD", method.c_str(), 1);
		setenv("REQUEST_URI", uri.c_str(), 1);
		setenv("QUERY_STRING", query.c_str(), 1);
		setenv("SERVER_PROTOCOL", protocol.c_str(), 1);
		setenv("HTTP_HOST", host.c_str(), 1);
		setenv("SERVER_ADDR", s_addr.c_str(), 1);
		setenv("SERVER_PORT", s_port.c_str(), 1);
		setenv("REMOTE_ADDR", r_addr.c_str(), 1);
		setenv("REMOTE_PORT", r_port.c_str(), 1);
	}

  void do_write() {
    auto self(shared_from_this());
    _socket.async_send(
        buffer("HTTP/1.1 200 OK\r\n", strlen("HTTP/1.1 200 OK\r\n")),
        [this, self](boost::system::error_code ec, size_t /* length */) {
          if (!ec);
        });
  }

};

class HttpServer {
 private:
  ip::tcp::acceptor _acceptor;
  ip::tcp::socket _socket;

 public:
  HttpServer(short port)
      : _acceptor(global_io_service, ip::tcp::endpoint(ip::tcp::v4(), port)),
        _socket(global_io_service) {
    do_accept();
  }

 private:
  void do_accept() {
    _acceptor.async_accept(_socket, [this](boost::system::error_code ec) {
      if (!ec) make_shared<HttpSession>(move(_socket))->start();

      do_accept();
    });
  }
};


int main(int argc, char* const argv[]) {
  if (argc != 2) {
    cerr << "Usage:" << argv[0] << " [port]" << endl;
    return 1;
  }

 setenv("PATH", "/bin:./bin:.", 1);

  try {
    unsigned short port = atoi(argv[1]);
    HttpServer server(port);
    global_io_service.run();
  } catch (exception& e) {
    cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}
