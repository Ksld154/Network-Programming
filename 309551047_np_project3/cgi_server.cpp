#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <signal.h>
#include <unistd.h>
#include <vector>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>

#include "cgi_server.hpp"

#define MAX_REQUEST_LEN 15000

using namespace std;
using boost::asio::ip::tcp;


struct request_paras {
	std::string 	method;
	std::string 	uri;
	std::string 	query_str;
	std::string 	server_protocol;
	std::string 	host;

	std::string 	client_ip_addr;
	std::string 	client_port;
	std::string 	server_ip_addr;
	std::string 	server_port;

	std::string 	uri_file;
	bool 			uri_file_exists;
};


class session 
    : public enable_shared_from_this<session> {

public:
    session(tcp::socket socket)
        : socket_(move(socket))
    {
    }

    void start() {
        do_read();
    }

    void stop() {
        resp_.clear();
        socket_.close();
    }

private:

    // read HTTP request from browser
	void do_read() {
		auto self(shared_from_this());
		socket_.async_read_some(boost::asio::buffer(payload_, MAX_REQUEST_LEN),
			[this, self](boost::system::error_code ec, std::size_t length) {
				if (!ec) {
                    // cout << payload_ << endl;
					do_parse(payload_);
					do_write_and_render();
                    stop();
				}
			}
		);
	}

	// Parse HTTP request
	void do_parse(std::string payload) {
		boost::replace_all(payload, "\r", "");

		std::vector<std::string> request_headers;
		boost::split(request_headers, payload, boost::is_any_of("\n"));

		int header_idx = 0;
		for(std::vector<std::string>::iterator it = request_headers.begin(); it != request_headers.end(); it++){
		
			std::vector<std::string> args;
			boost::split(args, *it, boost::is_any_of(" "));
			
			if(header_idx == 0) {
				request_.method = args.at(0);
				request_.uri = args.at(1);
				request_.server_protocol = args.at(2);
				
				// spilt uri_file and query_string
				std::vector<std::string> uri_seg;
				boost::split(uri_seg, request_.uri, boost::is_any_of("?"));
				request_.uri_file = uri_seg.at(0);
				if(uri_seg.size() > 1) {
					request_.query_str = uri_seg.at(1);
				} else {
					request_.query_str = "";
				}
			}
			else if(args.at(0) == "Host:") {
				request_.host = args.at(1);
			}
			header_idx++;
		}
		
		request_.server_ip_addr = socket_.local_endpoint().address().to_string();
		request_.server_port = std::to_string(socket_.local_endpoint().port());

		request_.client_ip_addr = socket_.remote_endpoint().address().to_string();
		request_.client_port = std::to_string(socket_.remote_endpoint().port());

		help_print(request_.method);
		help_print(request_.uri);
		help_print(request_.query_str);
		help_print(request_.server_protocol);
		help_print(request_.host);

		help_print(request_.server_ip_addr);
		help_print(request_.server_port);
		help_print(request_.client_ip_addr);
		help_print(request_.client_port);
	}

    // Generate HTTP response and send to browser
	void do_write_and_render() {
		resp_ = "";
		
		// check uri_file exists
		std::string uri_file = "." + request_.uri_file;
        help_print(uri_file); 

		if(uri_file == "./panel.cgi") {
			resp_ += "HTTP/1.1 200 OK\r\n";
			request_.uri_file = uri_file;
			request_.uri_file_exists = true;
            
            render_panel();
		}
        else if (uri_file == "./console.cgi") {
			resp_ += "HTTP/1.1 200 OK\r\n";
			request_.uri_file = uri_file;
			request_.uri_file_exists = true;

            // do console.cpp main()
            render_console();
        }
		else {
			resp_ += "HTTP/1.1 404 NOT FOUND\r\n\r\n";
			request_.uri_file_exists = false;
            help_print_to_browser();
		}
	}


    void render_panel() {
        resp_ += get_panel_html();
        help_print_to_browser();
    }

    void render_console() {
        vector<query> queryList = parse_query_str(request_.query_str);
        resp_ += get_console_html(queryList);
        
        help_print_to_browser();

        // TODO: client IO bug
        boost::asio::io_context io_context2;
        for(vector<query>::iterator q = queryList.begin(); q != queryList.end(); q++) {
            tcp::resolver r(io_context2);
            std::shared_ptr<client> c = std::make_shared<client>(io_context2, socket_, q->server_id, q->input_file);
            c->start(r.resolve(q->hostname, q->port));
        }
        io_context2.run();

    }


    void help_print_to_browser() {
        boost::asio::async_write(socket_, boost::asio::buffer(resp_, resp_.length()), 
            [this](boost::system::error_code ec, std::size_t) {
                if (ec) {  // no write error
                    printf("print to browser error: <script>console.log(\"%s\")</script>", ec.message().c_str());
                    fflush(stdout); 
                }
        });
    }
    
    void help_print(std::string str) {
		std::cout << str << std::endl;
	}

    tcp::socket socket_;
    char payload_[MAX_REQUEST_LEN];
    string resp_;
    struct request_paras request_;
};


class server {
public:
	server(boost::asio::io_context& io_context, short port)
		: acceptor_(io_context, tcp::endpoint(tcp::v4(), port))
	{
		acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
		do_accept();
	}

private:
	void do_accept() {
		acceptor_.async_accept(
			[this](boost::system::error_code ec, tcp::socket socket) {
				if (!ec) {
					std::make_shared<session>(std::move(socket))->start();
				}
				do_accept();
			}
		);
	}

	tcp::acceptor acceptor_;
};


int main(int argc, char* argv[]) {
    try {
        if(argc != 2) {
            std::cerr << "Usage: cgi_server.exe [port]" << endl;
            return 1;
        }

        boost::asio::io_context io_context;
        int port = std::atoi(argv[1]);

        server s(io_context, port);
        std::cout << "*** Server start! ***" << std::endl;
		io_context.run();

    } catch(const std::exception& e) {
        std::cerr << e.what() << '\n';
    }
    
    return 0;
}