#include <unistd.h>
#include <sys/wait.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <boost/asio.hpp>
#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>

#include "socks_server.hpp"

using namespace std;
using boost::asio::ip::tcp;

#define MAX_REQUEST_LEN 100000
#define SOCKS_REPLY_LEN 8
#define FIREWALL_ENTRY_LEN 1000

enum Socks4ConnType {
    SOCKS4_CONNECT = 1,
    SOCKS4_BIND = 2,
};

enum SocksReplyType {
    SOCKS_REPLY_OK = 90,
    SOCKS_REPLY_REJECTED = 91,
};

struct firewall_rule {
    string  action;
    string  socks_type;
    string  dst_ip;
};


class session 
    : public enable_shared_from_this<session> {
public:
    session(tcp::socket socket, boost::asio::io_context& io_context) 
        : io_context_(io_context),
          socket_(move(socket)),
          bind_mode_socket_(io_context),
          bind_mode_acceptor_(io_context)
    {
        memset(request_payload_, 0, sizeof(request_payload_));
        memset(resp_, 0, sizeof(resp_));
    }

    void start() {
        do_read_socks_req();
        // do_fork();
    }

    void stop() {
        socket_.close();
        // exit(EXIT_SUCCESS);
    }

private:

    // Read SOCKS request from browser
    void do_read_socks_req() {
        auto self(shared_from_this());
        socket_.async_read_some(boost::asio::buffer(request_payload_, MAX_REQUEST_LEN),
            [this, self](boost::system::error_code ec, size_t size) {
                if(!ec) {

                    do_parse_req(size);
                    do_resolve_dst();
                    do_check_firewall();
                    
                    if(request_.cd == SOCKS4_CONNECT) {  // Type1: CONNECT
                        send_socks_reply();
                        shared_ptr<client> c = make_shared<client>(io_context_, move(socket_), request_, (char *)resp_);
                        c->start(endpoints_); 
                    } else if(request_.cd == SOCKS4_BIND) {
                        // send_socks_reply();
                        do_bind_mode();
                        shared_ptr<relay_point> rp = make_shared<relay_point>(io_context_, move(bind_mode_socket_), move(socket_), request_);
                        rp->start();
                    }
                    stop();
                } else {
                    printf("do_read_socks_req() error: %s\n", ec.message().c_str());
                }
            }
        );
    }

	// Parse SOCKS request
    void do_parse_req(size_t read_buf_size) {

        int vn = request_payload_[0];
        int cd = request_payload_[1];
        short dst_port = request_payload_[2]<<8 | request_payload_[3];
        
        char dst_ip[20];
        snprintf(dst_ip, 20, "%d.%d.%d.%d", \
                request_payload_[4], request_payload_[5], \
                request_payload_[6], request_payload_[7]
        );
        
        int domain_idx = 0;
        for(int i = 8; i < MAX_REQUEST_LEN; i++) {
            if(request_payload_[i] == 0) {
                domain_idx = i+1;
                break;
            }
        }
        string dst_domain = "";
        for(int i = domain_idx; i < int(read_buf_size); i++) {
            dst_domain += request_payload_[i];
        }
        request_.server_ip_addr = socket_.local_endpoint().address().to_string();
		request_.server_port = std::to_string(socket_.local_endpoint().port());

		request_.client_ip_addr = socket_.remote_endpoint().address().to_string();
		request_.client_port = std::to_string(socket_.remote_endpoint().port());

        request_.vn = vn;
        request_.cd = cd;
        request_.dst_port = dst_port;
        request_.dst_ip = dst_ip;
        request_.dst_domain = dst_domain;

        if(request_.cd == SOCKS4_CONNECT) {
            request_.socks_type = "CONNECT";
        } else if(request_.cd == SOCKS4_BIND) {
            request_.socks_type = "BIND";
        }
    }

    void do_resolve_dst() {
        tcp::resolver r(io_context_);
        tcp::resolver::results_type endpoints;
        
        if(request_.dst_domain != "")
            endpoints_ = r.resolve(request_.dst_domain, to_string(request_.dst_port));
        else 
            endpoints_ = r.resolve(request_.dst_ip, to_string(request_.dst_port));
        tcp::resolver::results_type::iterator endpoint_iter = endpoints_.begin();

        if(endpoint_iter != endpoints_.end()) {
            request_.dst_ip = endpoint_iter->endpoint().address().to_string();
            request_.dst_port = endpoint_iter->endpoint().port();
        }
    }

    // check firewall and build SOCKS reply
    void do_check_firewall() {
        
        // 1. check firewall, and send SOCKS4 REPLY to the SOCKS client if rejected
        ifstream fin("./socks.conf");
        bool matched_flag = false; 
        string firewall_entry;
        while(getline(fin, firewall_entry)) {
            
            istringstream iss(firewall_entry);
            vector<string> all_attrs;
            string attr;
            while (iss >> attr) {
                all_attrs.push_back(attr);
            }

            struct firewall_rule rule;

            rule.action = all_attrs[0];
            rule.socks_type = all_attrs[1];
            rule.dst_ip = all_attrs[2];
            
            // cout << firewall_entry << endl;
            string ip_rule = rule.dst_ip;
            // cout << ip_rule << endl;
		    boost::replace_all(ip_rule, "*", "([0-9]{1,3})");
		    boost::replace_all(ip_rule, ".", "\\.");
            // cout << ip_rule << endl;

            boost::regex ip_expr(ip_rule);
            if((rule.socks_type == "c" && request_.cd == SOCKS4_CONNECT) || (rule.socks_type == "b" && request_.cd == SOCKS4_BIND)) {
                if(boost::regex_match(request_.dst_ip, ip_expr)) {
                    // printf("%s %d %s\n", rule.socks_type.c_str(), request_.cd, request_.dst_ip.c_str());
                    matched_flag = true;
                    break;
                }
            } 
            // std::cout << std::boolalpha << boost::regex_match(request_.dst_ip, ip_expr) << '\n';
            // cout << firewall_entry << endl;
        }

        // 2. Assume firewall is passed, then Check CD value and choose either CONNECT/BIND
        memset(resp_, 0, sizeof(resp_));
        if(matched_flag) {
            resp_[1] = SOCKS_REPLY_OK;
            request_.socks_reply = "Accept";
            help_print_conn_info();
        } else {
            resp_[1] = SOCKS_REPLY_REJECTED;
            request_.socks_reply = "Reject";
            help_print_conn_info();
            send_socks_reply_reject();
            // stop();
        }
    }


    void do_bind_mode() {

        // bind an idle port for client/dst to send traffic
        bind_mode_acceptor_.open(tcp::v4());
        bind_mode_acceptor_.bind(tcp::endpoint(tcp::v4(), 0));
        bind_mode_acceptor_.set_option(tcp::acceptor::reuse_address(true));
        bind_mode_acceptor_.listen();
        
        unsigned short port =  bind_mode_acceptor_.local_endpoint().port();
        // help_print( "Bind port: " + to_string(port));

        resp_[2] = (port >> 8) & 0xFF;
        resp_[3] = port & 0xFF;


        string reply_copy((char*)resp_, SOCKS_REPLY_LEN);
        send_socks_reply_EX(reply_copy);   
        // help_print("1st SOCKS reply sent!");

        bind_mode_acceptor_.accept(bind_mode_socket_);
        
        send_socks_reply_EX(reply_copy);
        // help_print("2nd SOCKS reply sent!");
    }

    void help_print(std::string str) {
		std::cout << "[DEBUG]\t" << str << std::endl;
	}

    void help_print_conn_info() {
        printf("<S_IP>: %s\n", request_.client_ip_addr.c_str());
        printf("<S_PORT>: %s\n", request_.client_port.c_str());
        printf("<D_IP>: %s\n", request_.dst_ip.c_str());
        printf("<D_PORT>: %s\n", to_string(request_.dst_port).c_str());
        printf("<Command>: %s\n", request_.socks_type.c_str());
        printf("<Reply>: %s\n", request_.socks_reply.c_str());
    }

    void send_socks_reply_reject() {
        auto self(shared_from_this());
		boost::asio::async_write(socket_, boost::asio::buffer(resp_, sizeof(resp_)), 
			[this, self](boost::system::error_code ec, std::size_t) {
				
                if (!ec) {  // no write error
                    stop();
				} else {
                    printf("send_socks_reply() error: %s\n", ec.message().c_str());
                    stop();
                }
		});
    }
    
    void send_socks_reply() {

        auto self(shared_from_this());
		boost::asio::async_write(socket_, boost::asio::buffer(resp_, sizeof(resp_)), 
			[this, self](boost::system::error_code ec, std::size_t) {
				
                if (!ec) {  // no write error
                    // cout << "socks_reply\n";
				} else {
                    printf("send_socks_reply() error: %s\n", ec.message().c_str());
                }
		});
    }

    void send_socks_reply_EX(string data) {

        auto self(shared_from_this());
		boost::asio::async_write(socket_, boost::asio::buffer(data, data.length()), 
			[this, self](boost::system::error_code ec, std::size_t) {
				
                if (!ec) {  // no write error
                    // cout << "socks_reply\n";
				} else {
                    printf("send_socks_reply() error: %s\n", ec.message().c_str());
                }
		});
    }

    boost::asio::io_context&    io_context_;
    tcp::socket     socket_;
    tcp::socket     bind_mode_socket_;
    unsigned char   resp_[SOCKS_REPLY_LEN];
    unsigned char   request_payload_[MAX_REQUEST_LEN];
    struct request_paras        request_;
    tcp::resolver::results_type endpoints_;
    tcp::acceptor               bind_mode_acceptor_;
};

class server {
public:
    server(boost::asio::io_context& io_context, short port) 
        : io_context_(io_context),
          acceptor_(io_context, tcp::endpoint(tcp::v4(), port)),
          signals_(io_context, SIGCHLD)
    {
        acceptor_.set_option(tcp::acceptor::reuse_address(true));
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
                if(!ec) {
                    do_fork(move(socket));
                }
                do_accept();
            }
        );
    }

    // fork a child process for handling client's connection to dst_host
    void do_fork(tcp::socket socket) {
        io_context_.notify_fork(boost::asio::io_context::fork_prepare);
        
        pid_t pid = fork();
        if(pid != 0) {
            io_context_.notify_fork(boost::asio::io_context::fork_parent);
            socket.close();
        }
        else {
            io_context_.notify_fork(boost::asio::io_context::fork_child); 
            acceptor_.close();
            // printf("[INFO] (%d): Connection established from: %s:%s\n", getpid(), 
                // socket.remote_endpoint().address().to_string().c_str(), to_string(socket.remote_endpoint().port()).c_str()
            // );

            shared_ptr<session> s = make_shared<session>(move(socket), io_context_);
            s->start();
        }
    }

    boost::asio::io_context&    io_context_;
    tcp::acceptor               acceptor_;
    boost::asio::signal_set     signals_;
};


int main(int argc, char* argv[]) {

    try {
        if(argc != 2) {
            cerr << "Usage: socks_server [port]\n";
            return 1;
        }

        boost::asio::io_context io_context;
        int port = atoi(argv[1]);

        server s(io_context, port);
        // cout << "*** Server start! ***" << endl;
        io_context.run();
    }
    catch(const std::exception& e) {
        std::cerr << e.what() << '\n';
    }
    return 0;
}