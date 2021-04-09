#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>
#define PANEL_ENTRY_NUM         5
#define SERVER_NUM              12
#define TESTCASE_NUM            10
#define ONE_SECOND              1000000
#define MAX_CONSOLE_RESULT_LEN  15000

using boost::asio::ip::tcp;
using namespace std;


string get_server_list() {
    string server_list = "";
    string domain = ".cs.nctu.edu.tw";
    for(int i = 0; i < SERVER_NUM; i++) {
        string host = "nplinux" + to_string(i+1);
        server_list = server_list + "\"<option value=\"" + host + domain + "\">" + host + "</option>";
    }
    return server_list;
}

string get_testcase_list() {
    string testcase_list = "";
    for(int i = 0; i < TESTCASE_NUM; i++) {
        string testcase_file = "t" + to_string(i+1) + ".txt";
        testcase_list += "<option value=\"" + testcase_file + "\">" + testcase_file + "</option>";
    }
    return testcase_list;
}

string get_panel_html() {
    string panel_http = "";
    panel_http = panel_http \
        + "Content-type: text/html\r\n\r\n"
        + "<!DOCTYPE html>"
        + "<html lang=\"en\">"
        + "  <head>"
        + "    <title>NP Project 3 Panel</title>"
        + "    <link"
        + "      rel=\"stylesheet\""
        + "      href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\""
        + "      integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\""
        + "      crossorigin=\"anonymous\""
        + "    />"
        + "    <link"
        + "      href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\""
        + "      rel=\"stylesheet\""
        + "    />"
        + "    <link"
        + "      rel=\"icon\""
        + "      type=\"image/png\""
        + "      href=\"https://cdn4.iconfinder.com/data/icons/iconsimple-setting-time/512/dashboard-512.png\""
        + "    />"
        + "    <style>"
        + "      * {"
        + "        font-family: \'Source Code Pro\', monospace;"
        + "      }"
        + "    </style>"
        + "  </head>"
        + "  <body class=\"bg-secondary pt-5\">";
    panel_http = panel_http \
        + "    <form action=\"console.cgi\" method=\"GET\">"
        + "      <table class=\"table mx-auto bg-light\" style=\"width: inherit\">"
        + "        <thead class=\"thead-dark\">"
        + "          <tr>"
        + "            <th scope=\"col\">#</th>"
        + "            <th scope=\"col\">Host</th>"
        + "            <th scope=\"col\">Port</th>"
        + "            <th scope=\"col\">Input File</th>"
        + "          </tr>"
        + "        </thead>"
        + "        <tbody>";

    for(int i = 0; i < PANEL_ENTRY_NUM; i++) {
        panel_http = panel_http \
            + "          <tr>"
            + "    <th scope=\"row\" class=\"align-middle\">Session " + to_string(i+1) + "</th>"
            + "    <td>"
            + "      <div class=\"input-group\">"
            + "        <select name=\"h" + to_string(i) + "\" class=\"custom-select\">"
            + "          <option></option>";

        panel_http += get_server_list();

        panel_http = panel_http \
            + " </select>"
            + "        <div class=\"input-group-append\">"
            + "          <span class=\"input-group-text\">.cs.nctu.edu.tw</span>"
            + "        </div>"
            + "      </div>"
            + "    </td>"
            + "    <td>"
            + "<input name=\"p" + to_string(i) + "\" type=\"text\" class=\"form-control\" size=\"5\" />"
            + "</td>"
            + "   <td>"
            + "<select name=\"f" + to_string(i) + "\" class=\"custom-select\">"
            + "<option></option>";
        
        panel_http += get_testcase_list();
    }


    panel_http = panel_http \
        + "        <tr>"
        + "            <td colspan=\"3\"></td>"
        + "            <td>"
        + "              <button type=\"submit\" class=\"btn btn-info btn-block\">Run</button>"
        + "            </td>"
        + "          </tr>"
        + "        </tbody>"
        + "      </table>"
        + "    </form>"
        + "  </body>"
        + "</html>";

    return panel_http;
}


struct query {
    string      server_id;
    
    string      hostname;
    string      port;
    string      input_file;
    string      server_endpoint;
};

vector<query> parse_query_str(string query_str) {
    
    vector<query> queryList;

    cout << query_str << endl;

    vector<string> args;
    boost::split(args, query_str, boost::is_any_of("&"));
    
    for(int i = 0; i < args.size(); i+=3) {
        query q;
        q.hostname   = args.at(i).substr(args.at(i).find("=") + 1);
        q.port       = args.at(i+1).substr(args.at(i+1).find("=") + 1);
        q.input_file = args.at(i+2).substr(args.at(i+2).find("=") + 1);
        // q.server_endpoint = "";

        // cout << q.hostname << endl;
        // cout << q.port << endl;
        // cout << q.input_file << endl;

        if(q.hostname != "" && q.port != ""){
            queryList.push_back(q);
        }
    }

    return queryList;
}

string get_console_html(vector<query>& queryList) {
    string payload = "";
    payload = payload \
        + "Content-type: text/html\r\n\r\n"
        + "<!DOCTYPE html>\n"
        + "<html lang=\"en\">\n"
        + "  <head>\n"
        + "    <meta charset=\"UTF-8\" />\n"
        + "    <title>NP Project 3 Sample Console</title>\n"
        + "    <link\n"
        + "      rel=\"stylesheet\"\n"
        + "      href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\"\n"
        + "      integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\"\n"
        + "      crossorigin=\"anonymous\"\n"
        + "    />\n"
        + "    <link\n"
        + "      href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\"\n"
        + "      rel=\"stylesheet\"\n"
        + "    />\n"
        + "    <link\n"
        + "      rel=\"icon\"\n"
        + "      type=\"image/png\"\n"
        + "      href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\"\n"
        + "    />\n"
        + "    <style>\n"
        + "      * {\n"
        + "        font-family: 'Source Code Pro', monospace;\n"
        + "        font-size: 1rem !important;\n"
        + "      }\n"
        + "      body {\n"
        + "        background-color: #212529;\n"
        + "      }\n"
        + "      pre {\n"
        + "        color: #cccccc;\n"
        + "      }\n"
        + "      b {\n"
        + "        color: #01b468;\n"
        + "      }\n"
        + "    </style>\n"
        + "  </head>\n";
    payload = payload \
        + "<body>\n"
        + "    <table class=\"table table-dark table-bordered\">\n"
        + "      <thead>\n"
        + "        <tr>\n";
    

    for(vector<query>::iterator q = queryList.begin(); q != queryList.end(); q++) {
        string server = q->hostname + ":" + q->port;
        payload = payload + "          <th scope=\"col\">" + server + "</th>\n";
    }
    
    payload = payload \
        + "        </tr>\n"
        + "      </thead>\n"
        + "      <tbody>\n"
        + "        <tr>\n";

    for(int i = 0; i < queryList.size(); i++) {    
        string server_id = "s" + to_string(i);
        queryList[i].server_id = server_id;
        payload = payload + "          <td><pre id=\"" + server_id + "\" class=\"mb-0\"></pre></td>\n";
    }

    payload = payload \
        + "        </tr>\n"
        + "      </tbody>\n"
        + "    </table>\n"
        + "  </body>\n"
        + "</html>\n";

    return payload;
}

class client 
    : public std::enable_shared_from_this<client> {

public:
    client(boost::asio::io_context& io_context, tcp::socket& browser_socket, string server_id, string input_file)
        : socket_(io_context),
          browser_socket_(browser_socket),
          server_id_(server_id),
          input_file_stream_("./test_case/" + input_file),
          stopped_(false)
    {
        memset(recv_data_buf_, '\0', MAX_CONSOLE_RESULT_LEN);
    }

    void start(tcp::resolver::results_type endpoints) {
        endpoints_ = endpoints;
        do_connect(endpoints_.begin());
    }
    void stop() {
        stopped_ = true;
        input_file_stream_.close();
        boost::system::error_code ignore_ec;
        socket_.close(ignore_ec);
    }

private:

    void do_connect(tcp::resolver::results_type::iterator endpoint_iter) {

        auto self(shared_from_this());
        if(endpoint_iter != endpoints_.end()) {
            socket_.async_connect(endpoint_iter->endpoint(),
                [this, self](boost::system::error_code ec) {
                    
                    if(stopped_) return;
                    else if(!ec) {
                        do_read();
                    } else {
                        printf("Coneect error: <script>console.log(\"%s\")</script>", ec.message().c_str());
                        fflush(stdout);
                        socket_.close();

                        // do_connect();
                    }
                }
            );
        }
        else {
            stop();
        }
    }


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

    void do_write() {

        getline(input_file_stream_, cmd_buf_);
        cmd_buf_ += '\n';
        outputCommand(cmd_buf_);
        // usleep(0.2 * ONE_SECOND);

        // write cmd to golden_server
        auto self(shared_from_this()); 
        boost::asio::async_write(socket_, boost::asio::buffer(cmd_buf_, cmd_buf_.length()),
            [this, self](const boost::system::error_code& ec, std::size_t) {
                if(stopped_) return;
                if(!ec) {
                    cmd_buf_.clear();
                    do_read();
                } else {
                    printf("write error: <script>console.log(\"%s\")</script>", ec.message().c_str());
                    fflush(stdout);            
                    stop(); 
                }
            }        
        );
    }

    // print cmd_result/welcome_msg to browser's screen
    void outputShell(string recv_content) {
        
        char html_content[100000];
        string formatted_recv_content = help_format_into_html(recv_content);
       
        sprintf(html_content, "<script>document.getElementById(\'%s\').innerHTML += \'%s\';</script>", server_id_.c_str(), formatted_recv_content.c_str());
        string html_string = html_content;
        help_print_to_browser(html_string);
    }

    // print cmd to browser's screen
    void outputCommand(string cmd) {

        char html_cmd[100000];
        string formatted_cmd = help_format_into_html(cmd);

        sprintf(html_cmd, "<script>document.getElementById(\'%s\').innerHTML += \'<b>%s</b>\';</script>", server_id_.c_str(), formatted_cmd.c_str());
        string html_string = html_cmd;
        help_print_to_browser(html_string);
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

    void help_print_to_browser(string data) {
        boost::asio::async_write(browser_socket_, boost::asio::buffer(data, data.length()), 
            [this](boost::system::error_code ec, std::size_t) {
                if(stopped_) return;
                if(!ec) {
                }
            }
        );
    }


    tcp::resolver::results_type     endpoints_;
    tcp::socket     socket_;
    tcp::socket&    browser_socket_;
    bool            stopped_;
    char            recv_data_buf_[MAX_CONSOLE_RESULT_LEN];
    string          cmd_buf_;
    string          server_id_;
    ifstream        input_file_stream_;
};