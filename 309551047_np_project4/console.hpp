#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <fstream>
#include <cstdio>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/bind.hpp>
#define MAX_CONSOLE_RESULT_LEN 30000
#define SOCKS_REQUEST_LEN 9
#define SOCKS_REPLY_LEN 8


using boost::asio::ip::tcp;
using namespace std;

struct query {
    string      server_id;
    
    string      hostname;
    string      port;
    string      input_file;

    string      socks_ip;
    string      socks_port;
};

class client 
    : public std::enable_shared_from_this<client>

{
public:
    client(boost::asio::io_context& io_context, string server_id, string input_file)
    : socket_(io_context),
      server_id_(server_id), 
      input_file_stream_("./test_case/" + input_file),
      stopped_(false)
    {
        memset(recv_data_buf_, '\0', MAX_CONSOLE_RESULT_LEN);
		memset(socks_reply_buf_, 0, SOCKS_REPLY_LEN);
    }
    
    void start(tcp::resolver::results_type socks_endpoints, tcp::resolver::results_type dsthost_endpoints) {
        endpoints_ = socks_endpoints;
        endpoint_iter_ = endpoints_.begin();
        dst_endpoints_ = dsthost_endpoints;
        do_connect();
    }
    void stop() {
        stopped_ = true;
        boost::system::error_code ignored_ec;
        socket_.close(ignored_ec);
    }


    void do_connect() {

        if(endpoint_iter_ != endpoints_.end()) {

		    auto self(shared_from_this());
            socket_.async_connect(endpoint_iter_->endpoint(),
                [this, self](const boost::system::error_code& ec) {
                    
                    if(stopped_) return;
                    else if(!ec) {
                        send_socks_request();
                    }
                    else {
                        printf("<script>console.log(\"%s\")</script>", ec.message().c_str());
                        fflush(stdout);
                        socket_.close();

                        endpoint_iter_++;
                        do_connect();
                    }
                }   
            );
        } else {
            stop();
        }
    }

    // Handle how to receive command_result/welcome msg. from remote np_server, and then print to screen
    void do_read() {

		auto self(shared_from_this());
        socket_.async_read_some(boost::asio::buffer(recv_data_buf_, MAX_CONSOLE_RESULT_LEN), 
            [this, self](boost::system::error_code ec, std::size_t length) {
                if(stopped_) return;
                if(!ec) {                    
                
                    string recv_data = recv_data_buf_;
                    outputShell(recv_data);
                    memset(recv_data_buf_, '\0', MAX_CONSOLE_RESULT_LEN);
                    
                    // If recv_data contains the prompt, then we can start sending next cmd to golden_server
                    if(recv_data.find("% ") != recv_data.npos) {  
                        do_write();
                    } else {  // otherwise we continue to read server output from current cmd
                        do_read();
                    }
                
                } else {                    
                    printf("<script>console.log(\"%s\")</script>\n", ec.message().c_str());
                    fflush(stdout);
                    stop(); 
                }
			}
        );
    }

    // Handle how to send command to remote np_server, and then print to screen
    void do_write() {

        getline(input_file_stream_, cmd_buf_);
        cmd_buf_ += '\n';
        outputCommand(cmd_buf_);

		auto self(shared_from_this());
        boost::asio::async_write(socket_, boost::asio::buffer(cmd_buf_, cmd_buf_.length()),
            [this, self](const boost::system::error_code& ec, std::size_t) {
                if(stopped_) return;
                if(!ec) {
                    cmd_buf_.clear();
                    do_read();
                } else {
                    printf("<script>console.log(\"%s\")</script>\n", ec.message().c_str());
                    fflush(stdout);            
                    stop(); 
                }
            }
        );
    }

    // print cmd_result/welcome_msg to client's screen
    void outputShell(string recv_content) {
        string formatted_recv_content = help_format_into_html(recv_content);
    
        printf("<script>document.getElementById(\'%s\').innerHTML += \'%s\';</script>", server_id_.c_str(), formatted_recv_content.c_str());
        fflush(stdout);
    }

    // print cmd to client's screen
    void outputCommand(string cmd) {
        string formatted_cmd = help_format_into_html(cmd);

        printf("<script>document.getElementById(\'%s\').innerHTML += \'<b>%s</b>\';</script>", server_id_.c_str(), formatted_cmd.c_str());
        fflush(stdout);
    }


    string help_format_into_html(string raw_str) {
        
        string formatted_str = raw_str;
        boost::replace_all(formatted_str, "\r", "");
        boost::replace_all(formatted_str, "&", "&amp;");
        boost::replace_all(formatted_str, "\n", "&NewLine;");
        boost::replace_all(formatted_str, "<", "&lt;");
        boost::replace_all(formatted_str, ">", "&gt;");
        boost::replace_all(formatted_str, "\"", "&quot;");
        boost::replace_all(formatted_str, "\'", "&apos;");

        return formatted_str;
    }


    void send_socks_request() {
        unsigned char socks_req_buf[SOCKS_REQUEST_LEN];
		memset(socks_req_buf, 0, SOCKS_REQUEST_LEN);

        socks_req_buf[0] = 4;
        socks_req_buf[1] = 1;

        // fill in DST_PORT
        short dst_port = dst_endpoints_.begin()->endpoint().port();
        // help_print_debug_msg(to_string(dst_port));

        socks_req_buf[2] = (dst_port >> 8) & 0xFF;
        socks_req_buf[3] = dst_port & 0xFF;


        // fill in DST_IP
        string dst_ip = dst_endpoints_.begin()->endpoint().address().to_string();
        // help_print_debug_msg("dst ip: " + dst_ip);

        vector<string> ip_frags;
        boost::split(ip_frags, dst_ip, boost::is_any_of("."));
        socks_req_buf[4] = stoi(ip_frags.at(0)) & 0xFF;
        socks_req_buf[5] = stoi(ip_frags.at(1)) & 0xFF;
        socks_req_buf[6] = stoi(ip_frags.at(2)) & 0xFF;
        socks_req_buf[7] = stoi(ip_frags.at(3)) & 0xFF;

        
        auto self(shared_from_this());
        boost::asio::async_write(socket_, boost::asio::buffer(socks_req_buf, SOCKS_REQUEST_LEN),
            [this, self](const boost::system::error_code& ec, std::size_t) {
                if(!ec) {
                    get_socks_reply();
                }
            }
        );
    }

    void get_socks_reply() {


        auto self(shared_from_this());
        socket_.async_read_some(boost::asio::buffer(socks_reply_buf_, SOCKS_REPLY_LEN), 
            [this, self](boost::system::error_code ec, std::size_t length) {
                if(stopped_) return;
                if(!ec) {                                        
                    
                    if(socks_reply_buf_[1] == 90) {
                        do_read();
                    } else {
                        printf("<script>console.log(\"%s\")</script>\n", "FIREWALL REJECT!!");
                        fflush(stdout);
                        stop(); 
                    }
                } else {                    
                    printf("<script>console.log(\"%s\")</script>\n", ec.message().c_str());
                    fflush(stdout);
                    stop(); 
                }
			}
        );
    }

    void help_print_debug_msg(string attr) {
        printf("<script>console.log(\"[DEBUG] %s\")</script>\n", attr.c_str());
    }

private:
    tcp::resolver::results_type             endpoints_;
    tcp::resolver::results_type::iterator   endpoint_iter_;
    tcp::resolver::results_type             dst_endpoints_;
    tcp::socket socket_;
    char        recv_data_buf_[MAX_CONSOLE_RESULT_LEN];
    string      cmd_buf_;
    string      server_id_;
    ifstream    input_file_stream_;
    bool        stopped_;


    unsigned char socks_reply_buf_[SOCKS_REPLY_LEN];
};