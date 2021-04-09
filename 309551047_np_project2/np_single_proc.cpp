#include<stdlib.h>
#include<iostream>
#include<sstream>
#include<ostream>
#include<string>
#include<vector>
#include<unistd.h>
#include<fcntl.h>
#include<signal.h>
#include<string.h>
#include<sys/wait.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<map>
#include<list>
#include<arpa/inet.h>
#include<errno.h>
using namespace std;

#define INPUT_LEN 15100
#define CMD_LEN 257
#define SOCK_LIMIT 1024

struct pipeInfo {
    int     fdIn;                // for process to READ
    int     fdOut;
    int     line_distance;       // d line left to NUMBER_PIPE's read end
    int     cmd_distance;        // c cmd left to  NORMAL_PIPE's read end

    int     type;

    int     cmd_idx;             // behind i-th cmd of current line
    bool    visited;             // TRUE means this pipe is processed, therefore it's idx is no longer needed
};

struct userInfo {
    int     uid;
    int     socket_id;
    string  name;
    string  ip;
    string  port;
    map<string, string>             envList;
    vector<struct pipeInfo>         pipeList;
    vector<struct userPipeInfo>     userPipeList;
};

struct userPipeInfo {
    int     src_uid;
    int     src_ssock;
    int     dst_uid;
    int     dst_ssock;

    int     fdIn;
    int     fdOut;
    int     type;          // 0: SEND, 1: RECV
    
    int     cmd_idx;       // behind i-th cmd of current line
    int     visited; 
};

enum userPipeType {
    USER_PIPE_SEND = 0,
    USER_PIPE_RECV = 1,
    USER_PIPE_SEND_ERR = 2,
    USER_PIPE_RECV_ERR = 3
};

vector<userInfo> userList(SOCK_LIMIT);


int updatePipeInfo(int, int, int, int, int);
void broadcastMsg(int, string);

void childReaper(int signo) {
    pid_t   child_pid;
    int     status;

    // Monitor ALL subprocesses. 
    // If 1 subprocess is dead, then waitpid() returns that process's pid (> 0).
    // Since WHOHANG is set, so if no more process is dead, waitpid() will return 0.
    while( (child_pid = waitpid(-1, &status, WNOHANG)) > 0 ) {
        // Enter Here means that 1 subprocess is dead.
        // waitpid() will remove this subprocess from somekind of subprocess-table that it is monitoring.
    }

    return;
}

void printWelcomeMsg() {
    cout << "****************************************" << endl;
    cout << "** Welcome to the information server. **" << endl;
    cout << "****************************************" << endl;
}

int minAvailableUID(int msock) {

    bool uidList[SOCK_LIMIT] = {false};

    for(vector<userInfo>::iterator it = userList.begin(); it != userList.end(); it++) {
        if(it->uid > 0) { 
            uidList[it->uid] = true;
        }
    }

    for(int i = 1; i <= SOCK_LIMIT; i++) {
        if(uidList[i] == false) {
            return i;
        }
    }

    return -1;
}


int userLogin(int ssock, sockaddr_in client_addr, int msock) {

    string ip_addr = inet_ntoa(client_addr.sin_addr);
    int real_port = ntohs(client_addr.sin_port);
    string port = to_string(real_port);

    int uid = minAvailableUID(msock);
    userList[ssock].uid = uid;
    userList[ssock].socket_id = ssock;
    userList[ssock].name = "(no name)";
    userList[ssock].envList["PATH"] = "bin:.";
    userList[ssock].pipeList.clear();
    userList[ssock].ip = ip_addr;
    userList[ssock].port = port;

    dup2(ssock, STDIN_FILENO);
    dup2(ssock, STDOUT_FILENO);
    dup2(ssock, STDERR_FILENO);

    printWelcomeMsg();

    // broadcast login msg. to all users
    string addr = userList[ssock].ip + ":" + userList[ssock].port;
    string loginMsg = "*** User '" + userList[ssock].name + "' entered from " + addr + ". ***";
    broadcastMsg(ssock, loginMsg);

    return 0;
}

int userLogout(int ssock, int msock) {
    
    string logoutMsg = "*** User '" + userList[ssock].name + "' left. ***";
    broadcastMsg(ssock, logoutMsg);

    // TODO: clean-up other user's user_pipe list
    for(vector<userInfo>::iterator user = userList.begin(); user != userList.end(); user++){
        for(vector<userPipeInfo>:: iterator loggedout_user_pipe = user->userPipeList.begin() ; 
            loggedout_user_pipe != user->userPipeList.end(); ){

            if(loggedout_user_pipe->src_ssock == ssock || loggedout_user_pipe->dst_ssock == ssock) {
                loggedout_user_pipe = user->userPipeList.erase(loggedout_user_pipe);
            }
            else {
                loggedout_user_pipe++;
            }        
        }
    } 

    // mark the user as inactive
    userList[ssock].uid = 0;
    userList[ssock].socket_id = -1;
    userList[ssock].name = "(no name)";
    userList[ssock].envList.clear();
    userList[ssock].pipeList.clear();
    userList[ssock].userPipeList.clear();
    userList[ssock].ip = "";
    userList[ssock].port = "";
    
    dup2(msock, STDIN_FILENO);
    dup2(msock, STDOUT_FILENO);
    dup2(msock, STDERR_FILENO);
    close(ssock);

    return 0; 
}

int userChangeName(int ssock, string new_name) {
    // check new_name cannot be the same as other user's name
    vector<userInfo>::iterator it;
    for(it = userList.begin(); it != userList.end(); it++) {
        
        // existed name
        if(it->name == new_name) {
            cerr << "*** User '" <<  new_name << "' already exists. ***" << endl;
            return 1;
        }
    }

    // new_name is available
    string addr = userList[ssock].ip + ":" + userList[ssock].port;
    string errMsg = "*** User from " + addr + " is named '" + new_name + "'. ***";
    broadcastMsg(ssock, errMsg);
    userList[ssock].name = new_name;

    return 0;
}


void initEnv() {
    clearenv();
    setenv("PATH", "bin:.", true);
}

void printEnv(int client_fd, string envKey) {

    string envValue = userList[client_fd].envList[envKey];
    cout << envValue << endl;
}

void setEnv(int client_fd, string envKey, string envValue) {
    userList[client_fd].envList[envKey] = envValue;
}

void recoverEnv(int client_fd) {

    map<string, string> envList = userList[client_fd].envList;
    for(map<string, string>::iterator it = envList.begin(); it != envList.end(); it++) {
        setenv(it->first.c_str(), it->second.c_str(), true);
    }

    return;
}


void who(int client_fd) {
    
    cout << "<ID>" << "\t" << "<nickname>" << "\t" << "<IP:port>" << "\t" << "<indicate me>" << endl;
    for(vector<userInfo>::iterator it = userList.begin(); it != userList.end(); it++) {
        
        string addr = it->ip + ":" + it->port;
        string me = "";
        if(client_fd == it->socket_id) {
            me = "<-me";
        }
        
        if(it->uid > 0) {
            cout << it->uid <<  "\t" << it->name << "\t" << addr << "\t" << me << endl;
        }
    }
}

void broadcastMsg(int client_fd, string msg) {
    vector<userInfo>::iterator it;
    for(it = userList.begin(); it != userList.end(); it++) {
        if(it->uid > 0) {
            dup2(it->socket_id, STDIN_FILENO);
            dup2(it->socket_id, STDOUT_FILENO);
            dup2(it->socket_id, STDERR_FILENO);
            cout << msg << endl;
        }
    }
    
    // dup back original fd
    dup2(client_fd, STDIN_FILENO);
    dup2(client_fd, STDOUT_FILENO);
    dup2(client_fd, STDERR_FILENO);
}

void yell(int client_fd, vector<string> whole_cmd) {

    string yelled_msg = "";
    for(int i = 1; i < whole_cmd.size(); i++) {
        yelled_msg = yelled_msg + whole_cmd[i] + " ";
    }
    if(yelled_msg.back() == ' '){
        yelled_msg.pop_back();
    }

    string yellLine = "*** " + userList[client_fd].name + " yelled ***: " + yelled_msg; 
    broadcastMsg(client_fd, yellLine);
}

void tell(int client_fd, int receiver_uid, vector<string> whole_cmd) {
    string tell_msg = "";
    for(int i = 2; i < whole_cmd.size(); i++) {
        tell_msg = tell_msg + whole_cmd[i] + " ";
    }
    if(tell_msg.back() == ' '){
        tell_msg.pop_back();
    }
    string tellLine = "*** " + userList[client_fd].name + " told you ***: " + tell_msg; 

    if(receiver_uid < 1 || receiver_uid > 30) {
        cerr << "*** Error: user #" << receiver_uid << " does not exist yet. ***" << endl;
        return;
    }

    // search if receiver_uid exists
    bool receiver_exists = false;
    vector<userInfo>::iterator it;
    for(it = userList.begin(); it != userList.end(); it++) {
        if(it->uid == receiver_uid) { //receiver exists!

            // send msg. to receiver
            dup2(it->socket_id, STDIN_FILENO);
            dup2(it->socket_id, STDOUT_FILENO);
            dup2(it->socket_id, STDERR_FILENO);
            cout << tellLine << endl;

            dup2(client_fd, STDIN_FILENO);
            dup2(client_fd, STDOUT_FILENO);
            dup2(client_fd, STDERR_FILENO);
            return;
        }
    }
    
    cerr << "*** Error: user #" << receiver_uid << " does not exist yet. ***" << endl;
    return;
}


int builtinCmd(int client_fd, vector<string> cmd, vector<string> split_line) {
    
    string cmd_type = cmd.at(0);
    if(cmd_type == "printenv") {
        if(cmd.size() != 2) {
            cerr << "Usage: ​printenv [variable name]" << endl;
            return 1;
        }
        printEnv(client_fd, cmd.at(1));
    }
    else if(cmd_type == "setenv") {
        if(cmd.size() != 3) {
            cerr << "Usage: setenv [variable name] [value to assign]" << endl;
            return 1;
        }
        setEnv(client_fd, cmd.at(1), cmd.at(2));
    }
    else if(cmd_type == "who") {
        who(client_fd);
    }
    else if(cmd_type == "name") {
        if(cmd.size() != 2) {
            cerr << "Usage: name [new username] " << endl;
            return 1;
        }
        userChangeName(client_fd, cmd.at(1));
    }
    else if(cmd_type == "yell") {
        if(cmd.size() < 2) {
            cerr << "Usage: yell [message] " << endl;
            return 1;
        }
        yell(client_fd, split_line);
    }
    else if(cmd_type == "tell") {
        if(cmd.size() < 3) {
            cerr << "Usage: yell <user id> <message> " << endl;
            return 1;
        }

        // TODO: arg2 may not be integer
        int receiver_uid = stoi(cmd.at(1));
        tell(client_fd, receiver_uid, split_line);
    }
    return 0;
}

string readInputLine(int client_fd) {

    char buf[INPUT_LEN] = {0};

    recv(client_fd, buf, sizeof(buf), 0);

    string inputLine;
    inputLine = buf;

    return inputLine;
}

vector<string> splitLine(string rawInput) {
    
    vector<string> args;
    std::istringstream iss(rawInput);
    string token;
    while(getline(iss, token, ' ')) {
        if(token.length() != 0) {
            args.push_back(token);
        }
    }
    return args;
}

vector<vector<string>> parseLine(int client_fd, vector<string> &line_args, bool &line_end_with_pipeN) {
    vector< vector<string> > allCmds;    // all cmds of current line
    vector<string> singleCmd;
    bool add_cmd_to_table = false;


    int cmd_cnt = 0;
    for(vector<string>::iterator it = line_args.begin(); it != line_args.end(); it++) {

        string single_arg = *it;
        
        int pipe_type = -1;
        int pipe_distance = 0;
        int pipe_cmd_distance = 0;
        add_cmd_to_table = false;

        bool user_pipe = false;
        struct userPipeInfo userPipeObj;

        // file redirection
        if(single_arg == ">") {
            pipe_type = 4;
            pipe_distance = -1;
            pipe_cmd_distance = 1;
        }

        // normal pipe
        else if(single_arg == "|") {
            pipe_type = 1;
            pipe_distance = -1;
            pipe_cmd_distance = 1;
        }

        // NUMBER PIPE
        else if((single_arg.at(0) == '|' || single_arg.at(0) == '!') && (single_arg.at(1) >= '1' && single_arg.at(1) <= '9')) {
            pipe_cmd_distance = -1;

            // calculate pipe line_distance 
            for(string::iterator it2 = single_arg.begin(); it2 != single_arg.end(); it2++) {
                if(isdigit(*it2)) {
                    int current_digit = *it2 - '0';
                    pipe_distance = pipe_distance*10 + current_digit;
                }
            }

            if(single_arg.at(0) == '|') {
                pipe_type = 2;
            }
            else if(single_arg.at(0) == '!') {
                pipe_type = 3;
            }
        }
        

        // parse user_pipe info
        else if((single_arg.at(0) == '>' || single_arg.at(0) == '<') && (single_arg.at(1) >= '1' && single_arg.at(1) <= '9')) {
            user_pipe = true;

            int uid = 0;
            for(string::iterator it2 = single_arg.begin(); it2 != single_arg.end(); it2++) {
                if(isdigit(*it2)) {
                    int current_digit = *it2 - '0';
                    uid = uid*10 + current_digit;
                }
            }
            
            // check if uid exists
            bool uid_exists = false;
            int  uid_ssock = -1;
            for(vector<userInfo>::iterator it = userList.begin(); it != userList.end(); it++) {
                if(it->uid == uid) {
                    uid_ssock = it->socket_id;
                    uid_exists = true;
                    break;
                }
            }

            userPipeObj.cmd_idx = cmd_cnt;
            userPipeObj.visited = false;

            if(single_arg.at(0) == '>') {
                userPipeObj.type = USER_PIPE_SEND;
                userPipeObj.src_uid = userList[client_fd].uid;
                userPipeObj.src_ssock = userList[client_fd].socket_id;
                userPipeObj.dst_uid = uid;
                userPipeObj.dst_ssock = uid_ssock;
                userPipeObj.fdIn = -1;
                userPipeObj.fdOut = -1;
                
                if(!uid_exists) {
                    userPipeObj.type = USER_PIPE_SEND_ERR;
                    cerr << "*** Error: user #" << uid << " does not exist yet. ***\n" << flush;
                }
                else {
                    for(vector<userPipeInfo>::iterator it = userList[client_fd].userPipeList.begin(); it !=userList[client_fd].userPipeList.end(); it++) {
                        if(it->dst_uid == uid && it->type == USER_PIPE_SEND) {
                            // the same user pipe is already existed! (cannot send)
                            userPipeObj.type = USER_PIPE_SEND_ERR;
                            cerr << "*** Error: the pipe #" << userList[client_fd].uid << "->#" << uid <<" already exists. ***\n" << flush;
                        }
                    }
                }
            }
            else if(single_arg.at(0) == '<') {
                userPipeObj.type = USER_PIPE_RECV;
                userPipeObj.src_uid = uid;
                userPipeObj.src_ssock = uid_ssock;
                userPipeObj.dst_uid = userList[client_fd].uid;
                userPipeObj.dst_ssock = userList[client_fd].socket_id;
                userPipeObj.fdIn = -1;
                userPipeObj.fdOut = -1;

                if(!uid_exists) {
                    userPipeObj.type = USER_PIPE_RECV_ERR;
                    cerr << "*** Error: user #" << uid << " does not exist yet. ***\n" << flush;
                }
                else {
                    // check the pipe does not exist(cannot recv) => check from sender's side!!
                    bool send_pipe_exist = false;
                    for(vector<userPipeInfo>::iterator send_pipe = userList[uid_ssock].userPipeList.begin(); send_pipe !=userList[uid_ssock].userPipeList.end(); send_pipe++) {
                        if(send_pipe->dst_uid == userList[client_fd].uid && send_pipe->type == USER_PIPE_SEND) {
                            send_pipe_exist = true;
                            userPipeObj.fdIn  = send_pipe->fdIn;
                            userPipeObj.fdOut = send_pipe->fdOut;
                            break;
                        }
                    }
                    if(!send_pipe_exist){
                        userPipeObj.type = USER_PIPE_RECV_ERR;
                        cerr << "*** Error: the pipe #" << uid << "->#" << userList[client_fd].uid <<" does not exist yet. ***\n" << flush;
                    }
                }
            }            
        }


        // normal cmd_arg, just push to singleCmd table
        else {
            singleCmd.push_back(single_arg);
        }
        
        // update current pipe info to pipeList && push current cmd and its args
        if(pipe_type != -1 || user_pipe) {
            
            // it's a user_pipe
            if(user_pipe) {   
                // TODO: handle user_pipe order
                if(userList[client_fd].userPipeList.size() > 0) {
                    if(userList[client_fd].userPipeList.back().type == USER_PIPE_SEND 
                        || userList[client_fd].userPipeList.back().type == USER_PIPE_SEND_ERR) {
                        
                        // Show the message of '<n' first, then show the message of '>n'
                        if(userPipeObj.type == USER_PIPE_RECV || userPipeObj.type == USER_PIPE_RECV_ERR) {
                            struct userPipeInfo send_pipe = userList[client_fd].userPipeList.back();
                            userList[client_fd].userPipeList.pop_back();  
                            userList[client_fd].userPipeList.push_back(userPipeObj); 
                            userList[client_fd].userPipeList.push_back(send_pipe); 
                        }
                        else {
                            userList[client_fd].userPipeList.push_back(userPipeObj);
                        } 
                    }
                    else{
                        userList[client_fd].userPipeList.push_back(userPipeObj);
                    }
                }
                else {
                    userList[client_fd].userPipeList.push_back(userPipeObj);
                }
                if(userPipeObj.type == USER_PIPE_RECV){
                    line_end_with_pipeN = false;
                }
            }
            else {  // number_pipe or normal_pipe 
                updatePipeInfo(client_fd, pipe_type, pipe_distance, cmd_cnt, pipe_cmd_distance);
            }
            
            if(singleCmd.size() != 0) {
                add_cmd_to_table = true;
                allCmds.push_back(singleCmd);
                singleCmd.clear();                
            }

            // These kind of pipe MUST be right in front of other cmd, 
            // (i.e. there won't be other pipes between this pipe and new cmd)
            // therefore it's a good timing to increase cmd_cnt
            if(single_arg == "|" || single_arg == ">") {
                cmd_cnt++;
            }
            
        }
    }

    //  this line is end with a COMMAND, not a number pipe
    if(!add_cmd_to_table) {
        if(!singleCmd.empty()){
            allCmds.push_back(singleCmd);
            singleCmd.clear();
        }
        line_end_with_pipeN = false;
    }
    
    return allCmds;
}

int updatePipeInfo(int client_fd, int pipe_type, int pipe_distance, int cmd_idx, int pipe_cmd_distance) {

    // number pipe or normal pipe
    struct pipeInfo pipe_info;
    if(pipe_type != 4) {
        pipe_info.fdIn = -1;
        pipe_info.fdOut = -1;
    }

    pipe_info.type = pipe_type;
    pipe_info.line_distance = pipe_distance;
    pipe_info.cmd_idx = cmd_idx;
    pipe_info.visited = false;
    pipe_info.cmd_distance = pipe_cmd_distance;

    userList[client_fd].pipeList.push_back(pipe_info);

    return 0;
}

int redirectPipe(int client_fd, int cmd_table_idx, const char* filename, bool &next_cmd_executable) {

    for(vector<pipeInfo>::iterator it = userList[client_fd].pipeList.begin(); it != userList[client_fd].pipeList.end(); it++) {
        
        pipeInfo current_pipe = *it;
        if(current_pipe.cmd_idx == cmd_table_idx && !current_pipe.visited) {
            
            // these are current_pipe's write side
            if(current_pipe.type == 1 || current_pipe.type == 2) {
                dup2(current_pipe.fdOut, STDOUT_FILENO);
            }
            else if (current_pipe.type == 3) {
                dup2(current_pipe.fdOut, STDOUT_FILENO);
                dup2(current_pipe.fdOut, STDERR_FILENO);
            }
            else if(current_pipe.type == 4) {
                int opened_file_fd = open(filename,  O_RDWR|O_CREAT|O_TRUNC, S_IRWXU|S_IRWXG|S_IRWXO);
                dup2(opened_file_fd, STDOUT_FILENO);
            }
        }    
        
        // these are current_pipe's read side
        if(current_pipe.type == 1 && current_pipe.cmd_distance == 0) {
            dup2(current_pipe.fdIn, STDIN_FILENO);
        }
        if(current_pipe.type == 2 || current_pipe.type == 3) {
            if(cmd_table_idx == 0 && current_pipe.line_distance == 0) {
                dup2(current_pipe.fdIn, STDIN_FILENO);
            }
        }
        else if(current_pipe.type == 4 && current_pipe.cmd_distance == 0) {
            next_cmd_executable = false;
        }
    }

    // TODO: redirect user_pipe
    for(vector<userPipeInfo>::iterator usr_pipe = userList[client_fd].userPipeList.begin();
        usr_pipe != userList[client_fd].userPipeList.end(); usr_pipe++) {
        
        if(usr_pipe->cmd_idx == cmd_table_idx && !(usr_pipe->visited)) {
            // sender's user_pipe
            if(usr_pipe->type == USER_PIPE_SEND) {
                dup2(usr_pipe->fdOut, STDOUT_FILENO);
            }
            else if(usr_pipe->type == USER_PIPE_SEND_ERR) {
                int blackhole = open("/dev/null", O_RDWR);
                dup2(blackhole, STDOUT_FILENO);
                close(blackhole);
            }

            // receiver's user_pipe
            if(usr_pipe->type == USER_PIPE_RECV) {
                dup2(usr_pipe->fdIn, STDIN_FILENO);
            }
            else if(usr_pipe->type == USER_PIPE_RECV_ERR) {
                int blackhole = open("/dev/null", O_RDWR);
                dup2(blackhole, STDIN_FILENO);
                close(blackhole);
            }
        }
    }

    // clean up all useless fd-entry
    for(vector<pipeInfo>::iterator it = userList[client_fd].pipeList.begin(); it != userList[client_fd].pipeList.end(); it++) {    
        
        pipeInfo cur_pipe = *it;
        if(cur_pipe.type >= 1) {
            close(cur_pipe.fdIn);
            close(cur_pipe.fdOut);
        }
    }

    // TODO: close useless fd-entry after redirect user_pipe
    for(vector<userPipeInfo>::iterator it = userList[client_fd].userPipeList.begin(); it != userList[client_fd].userPipeList.end(); it++) {    
        if(it->type == USER_PIPE_SEND || it->type == USER_PIPE_RECV) {
            close(it->fdIn);
            close(it->fdOut);
        }
    }
    return 0;
}

int forkProcess(int client_fd, vector<vector<string>> cmdTable, bool &line_end_with_pipeN, string inputLine) {

    pid_t           pid;
    vector<pid_t>   pidList;

    // per-cmd
    for(int i = 0; i < cmdTable.size(); i++) {

        // check create new pipe or re-use old pipe
        bool next_cmd_executable = true;
        for(int j = 0; j < userList[client_fd].pipeList.size(); j++) {
            
            // right behind current cmd
            if(userList[client_fd].pipeList.at(j).cmd_idx == i && !(userList[client_fd].pipeList.at(j).visited)) {
                
                // check whether there are existed pipes that have the same destinaiton as current pipe
                // if it does, then we don't need to create a new one, just share the old pipe
                bool pipe_same_destination = false;
                for(int k = 0; k < j; k++) {
                    if(userList[client_fd].pipeList.at(j).type == 2 || userList[client_fd].pipeList.at(j).type == 3) {
                        if(userList[client_fd].pipeList.at(j).line_distance == userList[client_fd].pipeList.at(k).line_distance) {   
                            pipe_same_destination = true;
                            userList[client_fd].pipeList.at(j).fdIn = userList[client_fd].pipeList.at(k).fdIn;
                            userList[client_fd].pipeList.at(j).fdOut = userList[client_fd].pipeList.at(k).fdOut;
                        }
                    }
                }   

                // CREATE new PIPE
                if(!pipe_same_destination) {
                    int pipe_tmp[2];
                    int pipe_result = pipe(pipe_tmp);

                    if(pipe_result < 0) {
                        cerr << "[ERROR]: Pipe failed" << endl;
                        return 1;
                    }
                    userList[client_fd].pipeList.at(j).fdIn  = pipe_tmp[0];   // for process to read
                    userList[client_fd].pipeList.at(j).fdOut = pipe_tmp[1];   // for process to write
                }
            }
        }


        // 1. CREATE new user_pipe (only for send/senderr type)
        // 2. broadcast pipe semd/recv success msg (for both send/recv type)
        for(vector<userPipeInfo>::iterator user_pipe = userList[client_fd].userPipeList.begin(); 
            user_pipe != userList[client_fd].userPipeList.end(); user_pipe++) {

            // behind current cmd
            if(user_pipe->cmd_idx == i && !(user_pipe->visited)) {
                //  create new user_pipe
                if(user_pipe->type == USER_PIPE_SEND || user_pipe->type == USER_PIPE_SEND_ERR) {
                    int pipe_tmp[2];
                    int pipe_result = pipe(pipe_tmp);

                    if(pipe_result < 0) {
                        cerr << "[ERROR]: Pipe failed" << endl;
                        return 1;
                    }
                    user_pipe->fdIn  = pipe_tmp[0];   // for process to read
                    user_pipe->fdOut = pipe_tmp[1];   // for process to write
                    
                    //  and broadcast user_pipe send success
                    if(user_pipe->type == USER_PIPE_SEND){
                        char buf[500];
                        snprintf(buf, sizeof(buf), 
                            "*** %s (#%d) just piped '%s' to %s (#%d) ***", 
                            userList[client_fd].name.c_str(), user_pipe->src_uid,
                            inputLine.c_str(),
                            userList[user_pipe->dst_ssock].name.c_str(), user_pipe->dst_uid
                        );
                        string sendPipeSuccessMsg = buf;
                        broadcastMsg(client_fd, sendPipeSuccessMsg);
                    }
                }
                // broadcast user_pipe recv success
                else if(user_pipe->type == USER_PIPE_RECV) {
                    char buf[500];
                    snprintf(buf, sizeof(buf), 
                        "*** %s (#%d) just received from %s (#%d) by '%s' ***", 
                        userList[client_fd].name.c_str(), user_pipe->dst_uid,
                        userList[user_pipe->src_ssock].name.c_str(), user_pipe->src_uid,
                        inputLine.c_str()
                    );
                    string recvPipeSuccessMsg = buf;
                    broadcastMsg(client_fd, recvPipeSuccessMsg);
                }
            }
        }

        pid = fork();
        pidList.push_back(pid);
        
        // fork failed
        // => wait for a while, and retry fork for the same cmd later
        if(pid < 0) {

            i--;
            pidList.pop_back();
            usleep(2000);
            continue;
        }

        // child
        else if(pid == 0) {

            // get next cmd, this is for ">"
            // we need the FILENAME to do data stream redirection
            string next_cmd_string;
            if(i < cmdTable.size() - 1) {
                next_cmd_string = cmdTable.at(i+1).at(0);
            } else {
                next_cmd_string = "\0";
            }
            const char* nextCmd = next_cmd_string.c_str();

            // save current cmd and it's arg into a "vector of char*"
            // because execvp() only allows "char* const argv[]"
            vector<char*> singleCmd;
            const char* execCmd = cmdTable.at(i).at(0).c_str(); 

            for(int j = 0; j < cmdTable.at(i).size(); j++) {
                const char* const_arg = cmdTable.at(i).at(j).c_str();
                char* arg = strdup(const_arg);
                singleCmd.push_back(arg);
            }
            singleCmd.push_back(NULL);
            char** cmdArgs = &singleCmd[0];

            // redirect the pipe in/out to 0,1,2 ,
            // and then execcute the cmd
            redirectPipe(client_fd, i, nextCmd, next_cmd_executable);

            if(next_cmd_executable) {
                int err = execvp(execCmd, cmdArgs);
                if(err == -1) {
                    cerr << "Unknown command: [" << execCmd << "]." << endl;
                    exit(EXIT_FAILURE);
                }
            }
            exit(EXIT_SUCCESS);
        }

        // parent
        else {
        }

        // TODO: check read-end of user pipe???
        // 1. Read end of USER_PIPE
        for(vector<userPipeInfo>::iterator user_pipe = userList[client_fd].userPipeList.begin(); 
            user_pipe != userList[client_fd].userPipeList.end(); ) {

            // right behind current cmd
            if(user_pipe->cmd_idx == i && !(user_pipe->visited)) {
                user_pipe->visited = true;

                bool same_sender_receiver = false;
                if(user_pipe->type == USER_PIPE_RECV) {
                    close(user_pipe->fdIn); 
                    close(user_pipe->fdOut);
                    
                    // remove sender&receiver's user_pipe from userPipeList
                    // find sender's and remove
                    for(vector<userPipeInfo>::iterator src_pipe = userList[user_pipe->src_ssock].userPipeList.begin();
                        src_pipe != userList[user_pipe->src_ssock].userPipeList.end(); src_pipe++) {

                        if(src_pipe->src_uid == user_pipe->src_uid && src_pipe->dst_uid == user_pipe->dst_uid) {
                            
                            if(user_pipe->src_uid == user_pipe->dst_uid) { // avoid double delete same user_pipe
                                same_sender_receiver = true;
                            }
                            
                            userList[user_pipe->src_ssock].userPipeList.erase(src_pipe);
                            break;
                        }
                    }
                    
                    if(!same_sender_receiver)
                        user_pipe = userList[client_fd].userPipeList.erase(user_pipe);  //receiver's
                } 
                else {
                    user_pipe++;
                }
            }
            else {
                user_pipe++;
            }
        }

        // 1. Read end of NORMAL PIPE 
        // 2. update cmd_distance
        for(vector<pipeInfo>::iterator cur_pipe = userList[client_fd].pipeList.begin(); cur_pipe != userList[client_fd].pipeList.end(); ) {
            
            // mark current pipe as visited, which means the idx of this pipe is no longer needed
            // so that when processing next line,
            // this pipe_idx won't affect "check create new pipe or re-use old pipe" decision
            if(cur_pipe->cmd_idx == i && !(cur_pipe->visited)) {
                cur_pipe->visited = true;
            }

            // Read end of NORMAL_PIPE
            if(cur_pipe->cmd_distance == 0) {
                if(cur_pipe->type == 1) {
                    close(cur_pipe->fdOut);
                    close(cur_pipe->fdIn);
                }
                cur_pipe = userList[client_fd].pipeList.erase(cur_pipe);
            } 
            // update cmd_distance, if this pipe is visited
            else if(cur_pipe->visited) {
                cur_pipe->cmd_distance--;
                cur_pipe++;
            }
            else {
                cur_pipe++;
            }
        }

        // Read end of NUMBER PIPE
        // => Check 1st cmd of current line
        if(i == 0) {
            // loop through pipeList and check line_distance
            for(vector<pipeInfo>::iterator cur_pipe = userList[client_fd].pipeList.begin(); cur_pipe != userList[client_fd].pipeList.end(); ) {
                
                if(cur_pipe->type == 2 || cur_pipe->type == 3) {
                    if(cur_pipe->line_distance == 0) {
                        close(cur_pipe->fdOut);
                        close(cur_pipe->fdIn);
                        cur_pipe = userList[client_fd].pipeList.erase(cur_pipe);
                    } else {
                        cur_pipe++;
                    }
                } else {
                    cur_pipe++;
                }
            }
        }
    }

    // update line_distance 
    for(vector<pipeInfo>::iterator cur_pipe = userList[client_fd].pipeList.begin(); cur_pipe != userList[client_fd].pipeList.end(); cur_pipe++) {
        if(cur_pipe->type == 2 || cur_pipe->type == 3) {
            if(cur_pipe->line_distance > 0) {
                cur_pipe->line_distance--;
            } 
        }
    } 


    // for prompt order
    if(!line_end_with_pipeN) {
        for(int i = 0; i < pidList.size(); i++) {
            int status;
            waitpid(pidList.at(i), &status, 0);
        }
    }

    return 0;
}


int npshell(int client_fd) {
    bool    should_run = true;

    // read in a LINE every time
    string inputLine(INPUT_LEN, '\0');
    vector<vector<string>> cmdTable;
    vector<string> line_args;
    bool line_end_with_pipeN = true;

    initEnv();
    recoverEnv(client_fd);

    /* Read whole line from stdin */
    inputLine = readInputLine(client_fd);
    while(inputLine.back() == '\r' || inputLine.back() == '\n') {
        inputLine.pop_back();
    }

    /* Split line to args by space */
    line_args = splitLine(inputLine);
    if(line_args.size() == 0) {
        return 0;
    }

    /* Parse line_args into vector of cmd */
    cmdTable = parseLine(client_fd, line_args, line_end_with_pipeN);
    
    // bulit-in command
    string first_cmd_arg = cmdTable.at(0).at(0);
    if(first_cmd_arg == "printenv" || first_cmd_arg == "setenv") {
        builtinCmd(client_fd, cmdTable.at(0), line_args);
    }
    else if(first_cmd_arg == "who" || first_cmd_arg == "name" || first_cmd_arg == "yell" || first_cmd_arg == "tell") {
        builtinCmd(client_fd, cmdTable.at(0), line_args);
    }
    else if(first_cmd_arg == "exit") {
        return 1;
    }

    // real command
    else {
        signal(SIGCHLD, childReaper);
        forkProcess(client_fd, cmdTable, line_end_with_pipeN, inputLine);
    }

    cmdTable.clear();
    
    printf("%% ");
    fflush(stdout);
    
    return 0;
}

int passiveTCP(int port) {

    int qlen = 1;
    struct sockaddr_in sin; 

    bzero((char*)&sin, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(port);

    int s = socket(PF_INET, SOCK_STREAM, 0);
    if(s < 0) {
        cerr << "Cannot create socket!" << endl;
        exit(EXIT_FAILURE);
    }

    int enable = 1;
    if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0){
        perror("setsockopt(SO_REUSEADDR) failed!");
    }
    if (setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int)) < 0) 
        perror("setsockopt(SO_REUSEPORT) failed");

    int err = bind(s, (struct sockaddr*)&sin, sizeof(sin));
    if(err < 0) {
        cerr << "Cannot bind to port " << port << endl;
        exit(EXIT_FAILURE);
    } 

    if(listen(s, qlen) < 0) {
        perror("listen failed");
        // cerr << "Cannot listen to port " << port << endl;
        exit(EXIT_FAILURE);
    }

    return s;
}

int runServer(int port) {

    struct sockaddr_in client_in;
    fd_set rfds;
    fd_set afds;

    int msock = passiveTCP(port);
    int nfds = FD_SETSIZE;
    FD_ZERO(&afds);
    FD_SET(msock, &afds);

    while(1) {
        
        memcpy(&rfds, &afds, sizeof(rfds));
        if( select(nfds, &rfds, NULL, NULL, (struct timeval *)0) < 0) {
            if(errno = EINTR){
                continue;
            } 
            perror("Select failed!");
            continue;
        }

        if(FD_ISSET(msock, &rfds)) {
            socklen_t alen = sizeof(client_in);
            int ssock = accept(msock, (struct sockaddr *)&client_in, &alen);
            if(ssock < 0) {
               perror("Cannot accept connection");
               continue;
            }
            FD_SET(ssock, &afds);

            // user Login
            userLogin(ssock, client_in, msock);

            send(ssock, "% ", 2, 0);
        }
    
        for(int fd = 0; fd < nfds; fd++) {

            // get fd for current client
            if(fd != msock && FD_ISSET(fd, &rfds)) {
                int ssock = fd;
                dup2(ssock, STDIN_FILENO);
                dup2(ssock, STDOUT_FILENO);
                dup2(ssock, STDERR_FILENO);

                // client exit
                if(npshell(ssock) == 1) {
                    FD_CLR(ssock, &afds);

                    userLogout(ssock, msock);
                }
                
            }
        }
    }
    return 0;
}


int main(int argc, char* argv[], char* env[]) {
    
    initEnv();

    if(argc < 2) {
        cerr << "Usage: ./np_simple [PORT]" << endl;
    }
    int port = atoi(argv[1]);

    runServer(port);

    return 0;
}