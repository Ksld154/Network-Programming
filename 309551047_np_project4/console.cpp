#include <iostream>
#include <string>
#include <vector>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>

#include "console.hpp"

#define QUERY_ATTR 3

using namespace std;
using boost::asio::ip::tcp;


void print_http_header() {
    cout << "Content-type: text/html\r\n\r\n";
}

vector<query> parse_query_str() {

    vector<query> queryList;
    
    vector<string> args;
    string query_str = getenv("QUERY_STRING");
    boost::split(args, query_str, boost::is_any_of("&"));
    
    string socks_ip   = args.at(args.size()-2).substr(args.at(args.size()-2).find("=") + 1);
    string socks_port = args.at(args.size()-1).substr(args.at(args.size()-1).find("=") + 1);

    for(int i = 0; i < int(args.size()-2); i += QUERY_ATTR) {
        query q;
        q.hostname   = args.at(i).substr(args.at(i).find("=") + 1);
        q.port       = args.at(i+1).substr(args.at(i+1).find("=") + 1);
        q.input_file = args.at(i+2).substr(args.at(i+2).find("=") + 1);
        q.socks_ip   = socks_ip;
        q.socks_port = socks_port;

        if(q.hostname != "" && q.port != ""){
            queryList.push_back(q);
        }
    }

    return queryList;
}

void print_http_payload(vector<query>& queryList) {
    string payload = "";
    payload = payload \
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

    for(int i = 0; i < int(queryList.size()); i++) {    
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

    cout << payload << endl;
}

int main () {

    vector<query> queryList = parse_query_str();
    print_http_header();
    print_http_payload(queryList);

    boost::asio::io_context io_context;
    for(vector<query>::iterator q = queryList.begin(); q != queryList.end(); q++) {

        // the cgi has to connect to SOCKS_SERVER first!!
		std::shared_ptr<client> c = std::make_shared<client>(io_context, q->server_id, q->input_file);
       
        tcp::resolver r(io_context);
        tcp::resolver::results_type socks_endpoints = r.resolve(q->socks_ip, q->socks_port);
        tcp::resolver::results_type dsthost_endpoints = r.resolve(q->hostname, q->port);
                
        c->start(socks_endpoints, dsthost_endpoints);
    }
    io_context.run();
    
    return 0;
}