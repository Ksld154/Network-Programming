#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <algorithm>
#include <sys/stat.h>
#include <sys/wait.h>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>
#define MAX_REQUEST_LEN 15000


using boost::asio::io_context;
using boost::asio::ip::tcp;

io_context global_io_context;

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
	// std::map<std::string, std::string> 	http_headers;
};

class session 
	: public std::enable_shared_from_this<session> {

public:
	session(tcp::socket socket, boost::asio::signal_set& signal) 
		: socket_(std::move(socket)), 
		  signals_(signal)
	{
	}
	void start() {
		do_read();
	}

private:
	void do_read() {
		auto self(shared_from_this());
		socket_.async_read_some(boost::asio::buffer(payload_, MAX_REQUEST_LEN),
			[this, self](boost::system::error_code ec, std::size_t length) {
				if (!ec) {

					do_parse(payload_);
					do_write();
					do_fork();
				}
			}
		);
	}

	// Generate HTTP response
	void do_write() {
		resp_ = "";
		
		// check uri_file exists
		std::string uri_file = "." + request_.uri_file;
		struct stat buf;
		if(stat(uri_file.c_str(), &buf) != -1 && S_ISREG(buf.st_mode) == true) {
			resp_ += "HTTP/1.1 200 OK\r\n";
			request_.uri_file = uri_file;
			request_.uri_file_exists = true;
			help_print(request_.uri_file); 
		}
		else {
			resp_ += "HTTP/1.1 404 NOT FOUND\r\n\r\n";
			request_.uri_file_exists = false;
		}

		auto self(shared_from_this());
		boost::asio::async_write(socket_, boost::asio::buffer(resp_, resp_.length()), 
			[this, self](boost::system::error_code ec, std::size_t) {
				if (!ec) {  // no write error
				}
		});
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

	void do_setenv() {
		setenv("REQUEST_METHOD", request_.method.c_str(), true);
		setenv("REQUEST_URI", request_.uri.c_str(), true);
		setenv("QUERY_STRING", request_.query_str.c_str(), true);
		setenv("SERVER_PROTOCOL", request_.server_protocol.c_str(), true);
		setenv("HTTP_HOST", request_.host.c_str(), true);
		setenv("SERVER_ADDR", request_.server_ip_addr.c_str(), true);
		setenv("SERVER_PORT", request_.server_port.c_str(), true);
		setenv("REMOTE_ADDR", request_.client_ip_addr.c_str(), true);
		setenv("REMOTE_PORT", request_.client_port.c_str(), true);
	}

	void do_fork() {
		global_io_context.notify_fork(io_context::fork_prepare);

		pid_t pid;
		pid = fork();
		if(pid != 0) {
			global_io_context.notify_fork(io_context::fork_parent);
			socket_.close();
		}
		else {
			global_io_context.notify_fork(io_context::fork_child);

			do_setenv();
			if(request_.uri_file_exists) {
				int sock = socket_.native_handle();
				dup2(sock, STDIN_FILENO);
				dup2(sock, STDOUT_FILENO);
				dup2(sock, STDERR_FILENO);
				socket_.close();
				
				if(execlp(request_.uri_file.c_str(), request_.uri_file.c_str(), NULL) < 0) {
					help_print("Content-type:text/html\r\n\r\n<h1>FAIL</h1>");
				}
			}
			else {
				socket_.close();
				exit(EXIT_SUCCESS);
			}
		}

	}

	void help_print(std::string str) {
		std::cout << str << std::endl;
	}

	char payload_[MAX_REQUEST_LEN];
	std::string resp_;
	tcp::socket socket_;
	struct request_paras request_;
	boost::asio::signal_set& signals_;
};


class server {
public:
	server(boost::asio::io_context& global_io_context, short port)
		: acceptor_(global_io_context, tcp::endpoint(tcp::v4(), port)), 
		  signals_(global_io_context, SIGCHLD)
	{
		acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
		child_reaper();
		do_accept();
	}

private:
	void child_reaper() {
		signals_.async_wait(
			[this](boost::system::error_code /*ec*/, int /*signo*/) {
				// Only the parent process should check for this signal. We can
				// determine whether we are in the parent by checking if the acceptor
				// is still open.
				if (acceptor_.is_open()) {
					// Reap completed child processes so that we don't end up with zombies.
					int status = 0;
					while (waitpid(-1, &status, WNOHANG) > 0) {}

					child_reaper();
				}
			});
	}


	void do_accept() {
		acceptor_.async_accept(
			[this](boost::system::error_code ec, tcp::socket socket) {
				if (!ec) {
					std::make_shared<session>(std::move(socket), signals_)->start();
				}
				do_accept();
			}
		);
	}

	tcp::acceptor acceptor_;
	boost::asio::signal_set signals_;
};


int main(int argc, char* argv[]) {
	try {
		if (argc != 2) {
			std::cerr << "Usage: http_server <port>\n";
			return 1;
		}

		server s(global_io_context, std::atoi(argv[1]));
		std::cout << "*** Server start! ***" << std::endl;
		global_io_context.run();
	}
	
	catch (std::exception& e) {
		std::cerr << "Exception: " << e.what() << "\n";
	}

	return 0;
}