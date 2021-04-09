#include <iostream>
#include <unistd.h>
#include <cstring>
#include <boost/asio.hpp>
// #include <boost/asio/ssl.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/bind.hpp>

#define MAX_RECV_LEN 100000

using boost::asio::ip::tcp;
using namespace std;

struct request_paras {
    int     vn;
    int     cd;
    int     dst_port;
    string  dst_ip;
    string  dst_domain;

    string 	client_ip_addr;
	string 	client_port;
	string 	server_ip_addr;
	string 	server_port;

    string  socks_type;
    string  socks_reply;
};

class client
    : public enable_shared_from_this<client> {

public:
    client(boost::asio::io_context& io_context, tcp::socket browser_socket, struct request_paras request, char* socks_resp)
        : socket_(io_context),
          browser_socket_(move(browser_socket)),
          request_(request),
          socks_reply_(socks_resp)
    {
        memset(src_req_data_buf_, 0, sizeof(src_req_data_buf_));
        memset(dst_resp_data_buf_, 0, sizeof(dst_resp_data_buf_));
    }

    void start(tcp::resolver::results_type endpoints) {
        // socket_.set_verify_mode(boost::asio::ssl::verify_peer);
        endpoints_ = endpoints;
        do_connect(endpoints_.begin());
    }

    void stop_success() {
        socket_.close();
        browser_socket_.close();
        exit(EXIT_SUCCESS);
    }
    void stop() {
        socket_.close();
        browser_socket_.close();
        exit(EXIT_FAILURE);
    }

private:
    void do_connect(tcp::resolver::results_type::iterator endpoint_iter) {
        
        auto self(shared_from_this());
        if(endpoint_iter != endpoints_.end()) {
            
            socket_.async_connect(endpoint_iter->endpoint(),
                [this, self](boost::system::error_code ec) {
                    
                    if(!ec) {
                        do_read_dest_resp();
                        do_read_src_req();
                    } else {
                        // printf("Connect error: <script>console.log(\"%s\")</script>", ec.message().c_str());
                        fflush(stdout);
                        stop();
                    }
                }
            );
        } else {
            stop();
        }
    }

    // Read HTTP request from src_client
    void do_read_src_req() {

        auto self(shared_from_this());
        browser_socket_.async_read_some(boost::asio::buffer(src_req_data_buf_, MAX_RECV_LEN),
            [this, self](boost::system::error_code ec, size_t recv_len) {
                if(!ec) {
                    string src_data((char*)src_req_data_buf_, recv_len);
                    memset(src_req_data_buf_, 0, sizeof(src_req_data_buf_));
                    // cout << recv_len << endl;
                    // printf("(%d): [CGI->SOCKS] %d %s\n", getpid(), int(recv_len), src_data.c_str());

                    write_to_remote(src_data);
                } 
                else if(ec == boost::asio::error::eof) {
                    stop_success();
                    // stop();
                }
                else { 
                    // printf("do_read_src_req() error: %s\n", ec.message().c_str());
                    stop(); 
                }
            }
        );
    }
    
    void write_to_remote(string data) {
        boost::asio::async_write(socket_, boost::asio::buffer(data, data.length()), 
            [this](boost::system::error_code ec, std::size_t len) {
                // cout << len << endl;
                // printf("(%d): [SOCKS->Golden] %d %s\n", getpid(), int(len), data.c_str());
                // printf("(%d): %d [SOCKS->Golden] success\n", getpid(), int(len));
                
                if(!ec) {
                    do_read_src_req();
                } else {
                    stop();
                }
            }
        );
    }

    // Read HTTP resp from dst_host
    void do_read_dest_resp() {
        auto self(shared_from_this());
        socket_.async_read_some(boost::asio::buffer(dst_resp_data_buf_, MAX_RECV_LEN),
            [this, self](boost::system::error_code ec, size_t recv_len) {
                if(!ec) {
                    string dst_data((char*)dst_resp_data_buf_, recv_len);
                    memset(dst_resp_data_buf_, 0, sizeof(dst_resp_data_buf_));
                    // printf("(%d): %d %s\n", getpid(), int(recv_len), dst_data.c_str());
                    
                    // cout << getpid() << << dst_data << endl;
                    // cout << recv_len << endl;

                    write_to_src(dst_data);

                } else {                    
                    // printf("<script>console.log(\"%s\")</script>", ec.message().c_str());
                    fflush(stdout);
                    stop(); 
                }
            }
        );

    }

    void write_to_src(string data) {
        boost::asio::async_write(browser_socket_, boost::asio::buffer(data, data.length()), 
            [this](boost::system::error_code ec, std::size_t len) {
                // cout << len << endl;
                if(!ec) {
                    // printf("(%d): %d Proxy_write_to_src success\n", getpid(), int(len));
                    do_read_dest_resp();
                } else {
                    stop();
                }
            }
        );
    }


    tcp::resolver::results_type endpoints_;
    tcp::socket     socket_;
    tcp::socket     browser_socket_;
    unsigned char   src_req_data_buf_[MAX_RECV_LEN];
    unsigned char   dst_resp_data_buf_[MAX_RECV_LEN];
    struct request_paras request_;
    char*            socks_reply_;
};


class relay_point
    : public enable_shared_from_this<relay_point> {

public:
    relay_point(boost::asio::io_context& io_context, tcp::socket dst_socket, tcp::socket browser_socket, struct request_paras request)
        : socket_(move(dst_socket)),
          browser_socket_(move(browser_socket)),
          request_(request)
    {
        memset(src_data_buf_, 0, sizeof(src_data_buf_));
        memset(dst_data_buf_, 0, sizeof(dst_data_buf_));
    }

    void start() {
        do_read_dst();
        do_read_src();
    }

    void stop_success() {
        socket_.close();
        browser_socket_.close();
        exit(EXIT_SUCCESS);
    }

    void stop() {
        socket_.close();
        browser_socket_.close();
        exit(EXIT_FAILURE);
    }

private:

    // Read FTP payload from src_client
    void do_read_src() {

        auto self(shared_from_this());
        browser_socket_.async_read_some(boost::asio::buffer(src_data_buf_, MAX_RECV_LEN),
            [this, self](boost::system::error_code ec, size_t recv_len) {
                if(!ec) {
                    string src_data((char*)src_data_buf_, recv_len);
                    memset(src_data_buf_, 0, sizeof(src_data_buf_));
                    // cout << recv_len << endl;
                    write_to_dst(src_data);
                } 
                else if(ec == boost::asio::error::eof) {
                    stop_success();
                }
                else { 
                    // printf("do_read_src() error: %s\n", ec.message().c_str());
                    stop(); 
                }
            }
        );
    }
    
    void write_to_dst(string data) {
        boost::asio::async_write(socket_, boost::asio::buffer(data, data.length()), 
            [this](boost::system::error_code ec, std::size_t len) {
                if(!ec) {
                    // cout << len << endl;
                    do_read_src();
                }
            }
        );
    }


    // Read FTP payload from dst_host
    void do_read_dst() {
        auto self(shared_from_this());
        socket_.async_read_some(boost::asio::buffer(dst_data_buf_, MAX_RECV_LEN),
            [this, self](boost::system::error_code ec, size_t recv_len) {
                if(!ec) {
                    string dst_data((char*)dst_data_buf_, recv_len);
                    memset(dst_data_buf_, 0, sizeof(dst_data_buf_));
                    // cout << recv_len << endl;
                    
                    write_to_src(dst_data);

                } else {                    
                    // printf("do_read_dst() error: %s\n", ec.message().c_str());
                    stop(); 
                }
            }
        );

    }

    void write_to_src(string data) {
        boost::asio::async_write(browser_socket_, boost::asio::buffer(data, data.length()), 
            [this](boost::system::error_code ec, std::size_t len) {
                if(!ec) {
                    // cout << len << endl;
                    do_read_dst();
                }
            }
        );
    }

    tcp::socket     socket_;
    tcp::socket     browser_socket_;
    struct request_paras request_;
    unsigned char   src_data_buf_[MAX_RECV_LEN];
    unsigned char   dst_data_buf_[MAX_RECV_LEN];
};