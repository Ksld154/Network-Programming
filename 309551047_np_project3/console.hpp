#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <fstream>
#include <cstdio>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/bind.hpp>
#define ONE_SECOND 1000000
#define MAX_CONSOLE_RESULT_LEN 15000


using boost::asio::ip::tcp;
using namespace std;

struct query {
    string      server_id;
    
    string      hostname;
    string      port;
    string      input_file;
    string      server_endpoint;
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
    }
    
    void start(tcp::resolver::results_type endpoints) {
        endpoints_ = endpoints;
        endpoint_iter_ = endpoints_.begin();
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
                    else if(ec) {
                        printf("<script>console.log(\"%s\")</script>", ec.message().c_str());
                        fflush(stdout);
                        socket_.close();

                        endpoint_iter_++;
                        do_connect();
                    }
                    else {
                        // cout << "Connected to: " << endpoint_iter->endpoint() << endl;
                        do_read();
                    }
                }   
            );
        }

        else {
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
                    printf("<script>console.log(\"%s\")</script>", ec.message().c_str());
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
        // usleep(0.2 * ONE_SECOND);

		auto self(shared_from_this());
        boost::asio::async_write(socket_, boost::asio::buffer(cmd_buf_, cmd_buf_.length()),
            [this, self](const boost::system::error_code& ec, std::size_t) {
                if(stopped_) return;
                if(!ec) {
                    cmd_buf_.clear();
                    do_read();
                } else {
                    printf("<script>console.log(\"%s\")</script>", ec.message().c_str());
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


private:
    tcp::resolver::results_type             endpoints_;
    tcp::resolver::results_type::iterator   endpoint_iter_;
    tcp::socket socket_;
    bool        stopped_;
    char        recv_data_buf_[MAX_CONSOLE_RESULT_LEN];
    string      cmd_buf_;
    string      server_id_;
    ifstream    input_file_stream_;
};