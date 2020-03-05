#include <iostream>
#include <fstream>
#include <string.h>
#include <string>
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
#include <regex>
#include <array>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <iostream>
#include <memory>
#include <utility>

void parse();
void html_init(int num);
using namespace std;
using namespace boost::asio;

string host[5], port[5], file[5];
string sh, sp;

io_service _io_service;

class ShellServer : public enable_shared_from_this<ShellServer> {
	private:
		enum { max_length = 1024 };
		ip::tcp::socket _socket;
		ip::tcp::resolver _resolver;
		int index;
		array<char, max_length> _data;
		stringstream ss;
		fstream txt;
		bool finish, logout;
		bool socks;
		unsigned char socks_rep[8];

	public:
		ShellServer(int i)
			: _socket(_io_service), 
			  _resolver(_io_service), 
			  index(i), 
			  finish(false),
			  logout(false) { 
				txt.open("./test_case/" + file[index], ios::in);
				socks = (sh != "" && sp != "");
			}
		
		void start() { do_connect(); }

	private:
		void do_connect() {
			auto self(shared_from_this());
			string q_host, q_port;
			q_host = (socks) ? sh : host[index];
			q_port = (socks) ? sp : port[index];
			ip::tcp::resolver::query query(q_host, q_port);
			_resolver.async_resolve(query,
					boost::bind(&ShellServer::resolve_handler, self,
					boost::asio::placeholders::error,
					boost::asio::placeholders::iterator));
		}

		void resolve_handler(const boost::system::error_code& ec,
			ip::tcp::resolver::iterator endpoint_iterator) {
			auto self(shared_from_this());
			if(!ec) {
				ip::tcp::endpoint endpoint = *endpoint_iterator;
				_socket.async_connect(endpoint,
					boost::bind(&ShellServer::connect_handler, self,
            		boost::asio::placeholders::error, ++endpoint_iterator));				
			}
			else {
      			cout << "Error: " << ec.message() << endl;
    		}
		}

		// read from file and write to np_golden_shell
		void connect_handler(const boost::system::error_code& ec,
			ip::tcp::resolver::iterator endpoint_iterator) {
			auto self(shared_from_this());
			if(!ec) {
				if(socks)	socks_resolve();
				else do_read();
			}
			else {
				cout << "Error: " << ec.message() << endl;
	    	}
		}

		void socks_resolve() {
			auto self(shared_from_this());
			ip::tcp::resolver::query query(host[index], port[index]);
			_resolver.async_resolve(query,
					boost::bind(&ShellServer::socks_resolve_handler, self,
					boost::asio::placeholders::error,
					boost::asio::placeholders::iterator));
		}

		void socks_resolve_handler(const boost::system::error_code& ec,
			ip::tcp::resolver::iterator endpoint_iterator) {
			auto self(shared_from_this());
			if(!ec) {
				socks_request(endpoint_iterator->endpoint().address().to_string(),
								endpoint_iterator->endpoint().port());
			}
			else {
				cout << "Error: " << ec.message() << endl;
			}
		}

		void socks_request(string addr, unsigned short p) {
			auto self(shared_from_this());
			unsigned char socks_req[8];
			stringstream des(addr);
			memset(socks_req, 0, sizeof(socks_req));

			socks_req[0] = 4;
			socks_req[1] = 1;
			socks_req[2] = p / 256;
			socks_req[3] = p % 256;
			for(int i = 4; i < 8; ++i){
				string s;
				getline(des, s, '.');
				socks_req[i] = (unsigned char)stoi(s);
			}

			_socket.async_send(
				buffer(socks_req, 8),
				[this, self](boost::system::error_code ec, size_t ) {
					if(!ec){
						socks_reply();
					}
				}
			);
		}

		void socks_reply() {
			auto self(shared_from_this());
			memset(socks_rep, 0, sizeof(socks_rep));

			_socket.async_read_some(
				buffer(socks_rep, 8),
				[this, self](boost::system::error_code ec, size_t length) {
					if(!ec) {
						if(socks_rep[1] == 90){		// succeed
							do_read();
						}
						else	_socket.close();
					}
				}
			);
		}

		void do_write() {
			auto self(shared_from_this());
			string input;
			if(!getline(txt, input)) return;
			input += "\n";
			string content = content_handler(input);
			string body = string("<script>document.getElementById(\"") + string("s") + to_string(index) + "\").innerHTML += \"<b>" + content + "</b>\";</script>";
			cout << body << endl;
			if(input == "exit")	logout = true;	
			_socket.async_send(
				buffer(input),
				[this, self](boost::system::error_code ec, size_t /* length */) {
				if (!ec) { 
					finish = false;
					do_read();
				}
	        });
		}

		void do_read(){
			auto self(shared_from_this());
				_data.fill('\0');
				_socket.async_read_some(
					buffer(_data, max_length),
					[this, self](boost::system::error_code ec, size_t length) {
					if (!ec) { 
							string content = content_handler(string(_data.data()));
							string body = string("<script>document.getElementById(\"") + string("s") + to_string(index) + "\").innerHTML += \"" + content + "\";</script>";
							cout << body << endl;
			
							if(logout) {
								_socket.close();
								txt.close();
							}							

							if(finish)	do_write();
							else do_read();
						}
					});
		}

		string content_handler(string s){
			boost::algorithm::replace_all(s, ">", "&gt;");
			boost::algorithm::replace_all(s, "<", "&lt;");
			boost::algorithm::replace_all(s, "|", "&#124;");
			boost::algorithm::replace_all(s, "\n", "&NewLine;");
			boost::algorithm::replace_all(s, "\r", "");
			boost::algorithm::replace_all(s, "\"", "&quot;");

			// server response finish
			if(s.find("% ") != string::npos)	finish = true;
			else finish = false;

			return s;
		}

};

int main() {
	parse();
	try {
		for(auto i = 0; i < 5; ++i){
			//cout << "host" << i << ": " << host[i] << endl;
			if(host[i] != "") {
				//cout << "shellserver: " << i << endl;
				make_shared<ShellServer>(i)->start();			
			}
		}
		_io_service.run();
	} catch(exception &e) {
		cerr << "Exception: " << e.what() << endl;
	}
	return 0;	
}

void parse() {
	cout << "Content-type: text/html\r\n\r\n";
	string query_string = getenv("QUERY_STRING");

	std::smatch m;
	std::ssub_match sm;
	string seg[5], seg_[5];
	string segs, segs_;

	regex reg_h("h[0-9][^&]*");
	regex reg_p("p[0-9][^&]*");
	regex reg_f("f[0-9][^&]*");
	regex reg_e("=[^&]+");
	regex reg_v("[^=]+");
	regex reg_sh("sh[^&]*");
	regex reg_sp("sp[^&]*");


	auto result_s = std::sregex_iterator(query_string.begin(), query_string.end(), reg_h);
	auto result_e = std::sregex_iterator();
	int i = 0;
	for(auto it = result_s; it!= result_e; ++it) {
		m = *it;
		seg[i++] = m.str();
    }
	i = 0;
	for(auto &s: seg){
		if(regex_search(s, m, reg_e)){
			seg_[i++] = m.str();
		}
	}
	i = 0;
	for(auto &s: seg_){
		if(regex_search(s, m, reg_v)){
			host[i++] = m.str();
		}
	}
	
	result_s = std::sregex_iterator(query_string.begin(), query_string.end(), reg_p);
	result_e = std::sregex_iterator();
	i = 0;
	for(auto it = result_s; it!= result_e; ++it) {
		m = *it;
		seg[i++] = m.str();
    }
	i = 0;
	for(auto &s: seg){
		if(regex_search(s, m, reg_e)){
			seg_[i++] = m.str();
		}
	}
	i = 0;
	for(auto &s: seg_){
		if(regex_search(s, m, reg_v)){
			port[i++] = m.str();
		}
	}

	result_s = std::sregex_iterator(query_string.begin(), query_string.end(), reg_f);
	result_e = std::sregex_iterator();
	i = 0;
	for(auto it = result_s; it!= result_e; ++it) {
		m = *it;
		seg[i++] = m.str();
    }
	i = 0;
	for(auto &s: seg){
		if(regex_search(s, m, reg_e)){
			seg_[i++] = m.str();
		}
	}
	i = 0;
	for(auto &s: seg_){
		if(regex_search(s, m, reg_v)){
			file[i++] = m.str();
		}
	}

	if(regex_search(query_string, m, reg_sh)){
		segs = m.str();
	}
	if(regex_search(segs, m, reg_e)){
		segs_ = m.str();
	}
	if(regex_search(segs_, m, reg_v)){
		sh = m.str();
	}

	if(regex_search(query_string, m, reg_sp)){
		segs = m.str();
	}
	if(regex_search(segs, m, reg_e)){
		segs_ = m.str();
	}
	if(regex_search(segs_, m, reg_v)){
		sp = m.str();
	}

	auto connect_num = 0;
	for(int j = 0; j < 5; j++){
		if(host[j] != "")	connect_num++;
	}
	html_init(connect_num);

}

void html_init(int num) {
	string init = "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\" /><title>NP Project 3 Console</title>";
	init += "<link rel=\"stylesheet\" href=\"https://stackpath.bootstrapcdn.com/bootstrap/4.1.3/css/bootstrap.min.css\" integrity=\"sha384-MCw98/SFnGE8fJT3GXwEOngsV7Zt27NXFoaoApmYm81iuXoPkFOJwJ8ERdknLPMO\" crossorigin=\"anonymous\"/><link href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\" \rel=\"stylesheet\" /> <link rel=\"icon\" type=\"image/png\" href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\" /> <style> * { font-family: 'Source Code Pro', monospace; font-size: 1rem !important;} body {background-color: #212529;} pre {color: #cccccc;} b {color: #ffffff;} </style> </head> <body> <table class=\"table table-dark table-bordered\"> <thead> <tr>";
	for(auto i = 0; i < num; ++i) {
		init += "<th scope=\"col\">" + host[i] + ":" + port[i] + "</th>";
	}
    init += "</tr></thead><tbody><tr>";
	for(auto i = 0; i < num; ++i) {
    	init += "<td><pre id=\"s" + to_string(i) + "\" class=\"mb-0\"></pre></td>";
	}
    init += "</tr></tbody></table></body></html>";
	cout << init << endl;
}
