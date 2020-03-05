#include <array>
#include <boost/asio.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sstream>
#include <regex>
#include <fstream>
#include <boost/bind.hpp>


using namespace std;
using namespace boost::asio;

string host[5], port[5], file[5];

io_service global_io_service;

class HttpSession : public enable_shared_from_this<HttpSession> {
 private:
  enum { max_length = 1024 };
  ip::tcp::socket _socket;
  array<char, max_length> _data;
  bool is_cgi = true;
  string query, cgi, method = {"GET"}, uri;
  stringstream ss;
  string header;
  int connect_num = 0;

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
                request_handler();
			}
        });
  }

  void request_handler() {
      auto self(shared_from_this());
      do_write();
      parse();
      if(!is_cgi)   return;

      if(cgi == "panel.cgi")    show_panel();
      else if(cgi == "console.cgi") show_console();
  }

  void show_console() {
      auto self(shared_from_this());
      console_init();
      try {
		for(auto i = 0; i < 5; ++i){
			//cout << "host" << i << ": " << host[i] << endl;
			if(host[i] != "") {
				//cout << "shellserver: " << i << endl;
				make_shared<ShellServer>(i, self)->start();			
			}
		}
	} catch(exception &e) {
		cerr << "Exception: " << e.what() << endl;
	}
  }

  void show_panel() {
    auto self(shared_from_this());
    
    string content = "Content-type: text/html\r\n\r\n";
    _socket.async_send(
        buffer(content),
        [this, self](boost::system::error_code ec, size_t) {}
    );

    content = "<!DOCTYPE html> <html lang=\"en\"><head><title>NP Project 3 Panel</title><linkrel=\"stylesheet\"href=\"https://stackpath.bootstrapcdn.com/bootstrap/4.1.3/css/bootstrap.min.css\"integrity=\"sha384-MCw98/SFnGE8fJT3GXwEOngsV7Zt27NXFoaoApmYm81iuXoPkFOJwJ8ERdknLPMO\"crossorigin=\"anonymous\"/><linkhref=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\"rel=\"stylesheet\"/><link rel=\"icon\" type=\"image/png\" href=\"https://cdn4.iconfinder.com/data/icons/iconsimple-setting-time/512/dashboard-512.png\" /> <style> * { font-family: 'Source Code Pro', monospace; } </style> </head> <body class=\"bg-secondary pt-5\"> <form action=\"console.cgi\" method=\"GET\"> <table class=\"table mx-auto bg-light\" style=\"width: inherit\"> <thead class=\"thead-dark\"> <tr> <th scope=\"col\">#</th> <th scope=\"col\">Host</th> <th scope=\"col\">Port</th> <th scope=\"col\">Input File</th> </tr> </thead> <tbody>";

    for (int i = 0; i < 5; i++) {
        content += "<tr><th scope=\"row\" class=\"align-middle\">Session " + to_string(i + 1) + "</th><td><div class=\"input-group\"><select name=\"h" + to_string(i) + "\" class=\"custom-select\"><option></option>";
        
        for (int j = 0; j < 12; j++) {
            content += "<option value=\"nplinux" + to_string(j + 1) + ".cs.nctu.edu.tw\">nplinux" + to_string(j + 1) + "</option>";
        }

        content += "</select><div class=\"input-group-append\"><span class=\"input-group-text\">.cs.nctu.edu.tw</span></div></div></td><td><input name=\"p" + to_string(i) + "\" type=\"text\" class=\"form-control\" size=\"5\" /></td><td><select name=\"f" + to_string(i) + "\" class=\"custom-select\"><option></option>";

        for (int j = 1; j <= 10; j++) {
            content += "<option value=\"t" + to_string(j) + ".txt\">t" + to_string(j) + ".txt</option>";
        }

        content += "</select></td></tr>";
    }

    content += "<tr><td colspan=\"3\"></td><td><button type=\"submit\" class=\"btn btn-info btn-block\">Run</button></td></tr></tbody></table></form></body></html>";

    _socket.async_send(
        buffer(content),
        [this, self](boost::system::error_code ec, size_t) {}
    );
  }

  void console_init() {
    auto self(shared_from_this());
    
    string s = "Content-type: text/html\r\n\r\n";
    _socket.async_send(
        buffer(s),
        [this, self](boost::system::error_code ec, size_t) {}
    );

    string init = "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\" /><title>NP Project 3 Console</title>";
	init += "<link rel=\"stylesheet\" href=\"https://stackpath.bootstrapcdn.com/bootstrap/4.1.3/css/bootstrap.min.css\" integrity=\"sha384-MCw98/SFnGE8fJT3GXwEOngsV7Zt27NXFoaoApmYm81iuXoPkFOJwJ8ERdknLPMO\" crossorigin=\"anonymous\"/><link href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\" \rel=\"stylesheet\" /> <link rel=\"icon\" type=\"image/png\" href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\" /> <style> * { font-family: 'Source Code Pro', monospace; font-size: 1rem !important;} body {background-color: #212529;} pre {color: #cccccc;} b {color: #ffffff;} </style> </head> <body> <table class=\"table table-dark table-bordered\"> <thead> <tr>";
	for(auto i = 0; i < connect_num; ++i) {
		init += "<th scope=\"col\">" + host[i] + ":" + port[i] + "</th>";
	}
    init += "</tr></thead><tbody><tr>";
	for(auto i = 0; i < connect_num; ++i) {
    	init += "<td><pre id=\"s" + to_string(i) + "\" class=\"mb-0\"></pre></td>";
	}
    init += "</tr></tbody></table></body></html>";
	cout << init << endl;

    _socket.async_send(
        buffer(init),
        [this, self](boost::system::error_code ec, size_t) {}
    );
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
        
    }

    cout << "uri " << uri << endl;
    cout << "query " << query << endl;
    cout << "cgi " << cgi << endl;
    cout << "method " << method << endl;
    cout << endl;

    //

    cout << "Content-type: text/html\r\n\r\n";
    string query_string = query;
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

    for(int j = 0; j < 5; j++){
        if(host[j] != "")	connect_num++;
    }
  }

  void do_write() {
    auto self(shared_from_this());
    _socket.async_send(
        buffer("HTTP/1.1 200 OK\r\n", strlen("HTTP/1.1 200 OK\r\n")),
        [this, self](boost::system::error_code ec, size_t /* length */) {
          if (!ec);
        });
  }

  private:
    class ShellServer : public enable_shared_from_this<ShellServer>{
        private:
		enum { max_length = 1024 };
		ip::tcp::socket _socket;
		ip::tcp::resolver _resolver;
        shared_ptr<HttpSession> browser;
		int index;
		array<char, max_length> _data;
		stringstream ss;
		fstream txt;
		bool finish, logout;

	public:
		ShellServer(int i, shared_ptr<HttpSession> &browser)
			: browser(browser),
              _socket(global_io_service), 
			  _resolver(global_io_service), 
			  index(i), 
			  finish(false),
			  logout(false) { 
				txt.open("./test_case/" + file[index], ios::in);
			}
		
		void start() { do_connect(); }

	private:
		void do_connect() {
			auto self(shared_from_this());
			ip::tcp::resolver::query query(host[index], port[index]);
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
      			cerr << "Error: " << ec.message() << endl;
    		}
		}

		// read from file and write to np_golden_shell
		void connect_handler(const boost::system::error_code& ec,
			ip::tcp::resolver::iterator endpoint_iterator) {
			auto self(shared_from_this());
			if(!ec) {
				do_read();
			}
			else {
				cerr << "Error: " << ec.message() << endl;
	    	}
		}

		void do_write() {
			auto self(shared_from_this());
			string input;
			if(!getline(txt, input)) return;
			input += "\n";
			string content = content_handler(input);
			string body = string("<script>document.getElementById(\"") + string("s") + to_string(index) + "\").innerHTML += \"<b>" + content + "</b>\";</script>";
			browser->_socket.async_send(
                buffer(body),
                [this, self](boost::system::error_code ec, size_t /* length */) {
            });
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
							// cout << body << endl;

                            browser->_socket.async_send(
                                buffer(body),
                                [this, self](boost::system::error_code ec, size_t length) {
                                if(!ec) {
                                    if(logout) {
                                        _socket.close();
                                        txt.close();
                                    }							

                                    if(finish)	do_write();
                                    else do_read();
                                }
                            });
						}
					});
		}


		string content_handler(string s){
			boost::algorithm::replace_all(s, ">", "&gt;");
			boost::algorithm::replace_all(s, "<", "&lt;");
			boost::algorithm::replace_all(s, "|", "&#124;");
			boost::algorithm::replace_all(s, "\n", "<br>");
			boost::algorithm::replace_all(s, "\r", "");
			boost::algorithm::replace_all(s, "\"", "&quot;");

			// server response finish
			if(s.find("% ") != string::npos)	finish = true;
			else finish = false;

			return s;
		}

    };

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

  try {
    unsigned short port = atoi(argv[1]);
    HttpServer server(port);
    global_io_service.run();
  } catch (exception& e) {
    cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}
