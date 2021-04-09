#include<stdlib.h>
#include<iostream>
#include<sstream>
#include<string>
#include<vector>
#include<unistd.h>
#include<fcntl.h>
#include<signal.h>
#include<sys/wait.h>
#include<string.h>
#include<pwd.h>
#include<fstream>
using namespace std;

#define INPUT_LEN 15000
#define CMD_LEN 256

struct pipeInfo {
    int     fdIn;
    int     fdOut;
    int     idx;                 // i-th pipe of that line
    int     line_distance;       // d line left to NUMBER_PIPE's read end
    int     cmd_distance;        // c cmd left to  NORMAL_PIPE's read end

    int     type;
    bool    visited;             // TRUE means this pipe is processed, therefore it's idx is no longer needed
};

vector<pipeInfo> pipeList;

int updatePipeInfo(int, int, int, int);
int sourceShell(string);

void sigChildHandler(int signo) {
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

void initEnv() {
    clearenv();
    setenv("PATH", "bin:.", true);
}

void printEnv(string env) {
    char* envValue = getenv(env.c_str());
    if(envValue != NULL) {
        printf("%s\n", envValue);
    }
}

void setEnv(string env, string envValue) {
    setenv(env.c_str(), envValue.c_str(), true);
}

void unsetEnv(string env) {
    unsetenv(env.c_str());
}

string homeDir() {
    return getpwuid(getuid())->pw_dir;
}

void updateHistory(string inputLine) {

    inputLine = inputLine + "\n";
    string history_filename = homeDir() + "/.npshell_history";

    int line_history_fd = open(history_filename.c_str(), O_RDWR|O_CREAT|O_APPEND, S_IRWXU|S_IRWXG|S_IRWXO);
    write(line_history_fd, inputLine.c_str(), strlen(inputLine.c_str()));
}

int builtinCmd(vector<string> cmd) {
    string cmd_type = cmd.at(0);

    if(cmd_type == "exit") {
        exit(EXIT_SUCCESS);
    }
    else if(cmd_type == "printenv") {
        if(cmd.size() != 2) {
            cerr << "Usage: ​printenv [variable name]" << endl;
            return 1;
        }
        printEnv(cmd.at(1));
    }
    else if(cmd_type == "setenv") {
        if(cmd.size() != 3) {
            cerr << "Usage: setenv [variable name] [value to assign]" << endl;
            return 1;
        }
        setEnv(cmd.at(1), cmd.at(2));
    }
    else if(cmd_type == "unsetenv") {
        if(cmd.size() != 2) {
            cerr << "Usage: unsetenv [variable name]" << endl;
            return 1;
        }
        unsetEnv(cmd.at(1));
    }
    else if(cmd_type == "source") {
        if(cmd.size() != 2) {
            cerr << "Usage: source [file]" << endl;
            return 1;
        }
        sourceShell(cmd.at(1));
    }
    return 0;
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

vector< vector<string> > parseLine(vector<string> &line_args, bool &line_end_with_pipeN) {
    vector< vector<string> > allCmds;    // all cmds of current line
    vector<string> singleCmd;
    bool add_cmd_to_table = false;

    int pipe_cnt = 0;
    for(vector<string>::iterator it = line_args.begin(); it != line_args.end(); it++) {

        string single_arg = *it;
        
        int pipe_type = -1;
        int pipe_distance = 0;
        int pipe_cmd_distance = 0;
        add_cmd_to_table = false;

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
            // for(string::iterator it2 = single_arg.begin(); it2 != single_arg.end(); it2++) {
            //     if(isdigit(*it2)) {
            //         int current_digit = *it2 - '0';
            //         pipe_distance = pipe_distance*10 + current_digit;
            //     }
            // }

            int pipe_dis1 = 0;
            int pipe_dis2 = 0;
            bool op_flag = false;
            for(string::iterator it2 = single_arg.begin(); it2 != single_arg.end(); it2++) {
                if(!op_flag && isdigit(*it2)) {
                    int current_digit = *it2 - '0';
                    pipe_dis1 = pipe_dis1*10 + current_digit;
                }
                else if(!op_flag && *it2 == '+') {
                    op_flag = true;
                }
                else if(op_flag && isdigit(*it2)) {
                    int current_digit = *it2 - '0';
                    pipe_dis2 = pipe_dis2*10 + current_digit;
                }
            }
            pipe_distance = pipe_dis1 + pipe_dis2;


            if(single_arg.at(0) == '|') {
                pipe_type = 2;
            }
            else if(single_arg.at(0) == '!') {
                pipe_type = 3;
            }
        }
        
        else if(single_arg == ">>") {
            pipe_type = 5;
            pipe_distance = -1;
            pipe_cmd_distance = 1;
        }
        else if(single_arg == "<") {
            pipe_type = 6;
            pipe_distance = -1;
            pipe_cmd_distance = 1;
        }
        // normal cmd_arg, just push to singleCmd table
        else {
            singleCmd.push_back(single_arg);
        }
        
        // update current pipe info to pipeList
        if(pipe_type != -1) {
            add_cmd_to_table = true;
            allCmds.push_back(singleCmd);
            singleCmd.clear();
            
            if(updatePipeInfo(pipe_type, pipe_distance, pipe_cnt, pipe_cmd_distance) == 0) {
                pipe_cnt++;
            }
        }
    }

    //  this line is end with a COMMAND, not a number pipe
    if(!add_cmd_to_table) {
        allCmds.push_back(singleCmd);
        singleCmd.clear();
        line_end_with_pipeN = false;
    }
    
    return allCmds;
}

int updatePipeInfo(int pipe_type, int pipe_distance, int pipe_cnt, int pipe_cmd_distance) {

    // number pipe or normal pipe
    struct pipeInfo pipe_info;
    if(pipe_type < 4) {
        pipe_info.fdIn = -1;
        pipe_info.fdOut = -1;
    }

    pipe_info.type = pipe_type;
    pipe_info.line_distance = pipe_distance;
    pipe_info.idx = pipe_cnt;
    pipe_info.visited = false;
    pipe_info.cmd_distance = pipe_cmd_distance;

    pipeList.push_back(pipe_info);

    return 0;
}

int redirectPipe(int cmd_table_idx, const char* filename, bool &next_cmd_executable) {

    for(vector<pipeInfo>::iterator it = pipeList.begin(); it != pipeList.end(); it++) {
        
        pipeInfo current_pipe = *it;
        if(current_pipe.idx == cmd_table_idx && !current_pipe.visited) {
            
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
            else if(current_pipe.type == 5) {
                int opened_file_fd = open(filename,  O_RDWR|O_CREAT|O_APPEND, S_IRWXU|S_IRWXG|S_IRWXO);
                dup2(opened_file_fd, STDOUT_FILENO);
            }
            else if(current_pipe.type == 6) {
                int opened_file_fd = open(filename,  O_RDONLY, S_IRWXU|S_IRWXG|S_IRWXO);
                dup2(opened_file_fd, STDIN_FILENO);
            }
        }    
        
        // these are current_pipe's read side
        // new logic
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
        else if(current_pipe.type == 5 && current_pipe.cmd_distance == 0) {
            next_cmd_executable = false;
        }
        else if(current_pipe.type == 6 && current_pipe.cmd_distance == 0) {
            next_cmd_executable = false;
        }
    }

    // clean up all useless fd-entry
    for(vector<pipeInfo>::iterator it = pipeList.begin(); it != pipeList.end(); it++) {    
        
        pipeInfo cur_pipe = *it;
        if(cur_pipe.type >= 1) {
            close(cur_pipe.fdIn);
            close(cur_pipe.fdOut);
        }
    }

    return 0;
}

int forkProcess(vector<vector<string>> cmdTable, bool &line_end_with_pipeN, int &total_cmd_cnt, int &last_cmd_return) {

    pid_t           pid;
    vector<pid_t>   pidList;
    int current_cmd_cnt = total_cmd_cnt;

    // per-cmd
    for(int i = 0; i < cmdTable.size(); i++) {
        // cout << cmdTable.at(i).at(0) << endl;

        // check create new pipe or re-use old pipe
        bool next_cmd_executable = true;
        for(int j = 0; j < pipeList.size(); j++) {
            
            // right behind current cmd
            if(pipeList.at(j).idx == i && !(pipeList.at(j).visited)) {
                
                // check whether there are existed pipes that have the same destinaiton as current pipe
                // if it does, then we don't need to create a new one, just share the old pipe
                bool pipe_same_destination = false;
                for(int k = 0; k < j; k++) {
                    if(pipeList.at(j).type == 2 || pipeList.at(j).type == 3) {
                        if(pipeList.at(j).line_distance == pipeList.at(k).line_distance) {   
                            pipe_same_destination = true;
                            pipeList.at(j).fdIn = pipeList.at(k).fdIn;
                            pipeList.at(j).fdOut = pipeList.at(k).fdOut;
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
                    pipeList.at(j).fdIn  = pipe_tmp[0];   // for process to read
                    pipeList.at(j).fdOut = pipe_tmp[1];   // for process to write
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
            redirectPipe(i, nextCmd, next_cmd_executable);
            if(next_cmd_executable) {
                int err = execvp(execCmd, cmdArgs);
                if(err == -1) {
                    cout << "Unknown command: [" << execCmd << "]." << endl;
                    exit(EXIT_FAILURE);
                }
            }
            exit(EXIT_SUCCESS);
        }

        // parent
        else {
        }
        
        // only parent or isFile(child) can arrive here
        for(vector<pipeInfo>::iterator cur_pipe = pipeList.begin(); cur_pipe != pipeList.end(); ) {
            
            // mark current pipe as visited, which means the idx of this pipe is no longer needed
            // so that when processing next line,
            // this pipe_idx won't affect "check create new pipe or re-use old pipe" decision
            if(cur_pipe->idx == i && !(cur_pipe->visited)) {
                cur_pipe->visited = true;
            }

            // Read end of NORMAL_PIPE
            if(cur_pipe->cmd_distance == 0) {
                if(cur_pipe->type == 1) {
                    close(cur_pipe->fdOut);
                    close(cur_pipe->fdIn);
                }
                cur_pipe = pipeList.erase(cur_pipe);
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
            // loop through pipeList and check line_distance?
            for(vector<pipeInfo>::iterator cur_pipe = pipeList.begin(); cur_pipe != pipeList.end(); ) {
                
                if(cur_pipe->type == 2 || cur_pipe->type == 3) {
                    if(cur_pipe->line_distance == 0) {
                        close(cur_pipe->fdOut);
                        close(cur_pipe->fdIn);
                        cur_pipe = pipeList.erase(cur_pipe);
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
    for(vector<pipeInfo>::iterator cur_pipe = pipeList.begin(); cur_pipe != pipeList.end(); cur_pipe++) {
        
        if(cur_pipe->type == 2 || cur_pipe->type == 3) {
            if(cur_pipe->line_distance > 0) {
                cur_pipe->line_distance--;
            } 
        }
    } 


    // WTF
    if(!line_end_with_pipeN) {
        for(int i = 0; i < pidList.size(); i++) {
            int status;
            waitpid(pidList.at(i), &status, 0);
            last_cmd_return = WEXITSTATUS(status);
        }
    }

    return 0;
}

int sourceShell(string input_file) {
    
    ifstream infile;
    string inputLine(INPUT_LEN, '\0');
    
    infile.open(input_file.c_str());    
    while(getline(infile, inputLine)) {
        vector< vector<string> > cmdTable;
        vector<string> line_args;
        bool line_end_with_pipeN = true;
        int total_cmd_cnt = 0;
        int last_cmd_return = 0;

        /* Read whole line from stdin */
        // getline(std::cin, inputLine);

        /* Split line to args by space */
        line_args = splitLine(inputLine);
        if(line_args.size() == 0) {
            continue;
        }

        /* Parse line_args into vector of cmd */
        cmdTable = parseLine(line_args, line_end_with_pipeN);
        
        // bulit-in command
        string first_cmd_arg = cmdTable.at(0).at(0);
        if(first_cmd_arg == "exit" || first_cmd_arg == "printenv" || first_cmd_arg == "setenv" || first_cmd_arg == "unsetenv" || first_cmd_arg == "source") {
            builtinCmd(cmdTable.at(0));
        }
        // real command
        else {
            signal(SIGCHLD, sigChildHandler);
            forkProcess(cmdTable, line_end_with_pipeN, total_cmd_cnt, last_cmd_return);
        }

        cmdTable.clear();
    }
    return 0;
}

int main() {

    bool    should_run = true;
    int     total_cmd_cnt = 0;
    int     last_cmd_return = 0;

    initEnv();

    // read in a LINE every time
    while(should_run) {
        string inputLine(INPUT_LEN, '\0');
        vector< vector<string> > cmdTable;
        vector<string> line_args;
        bool line_end_with_pipeN = true;

        printf("%% [%d] ", last_cmd_return);
        fflush(stdout);

        /* Read whole line from stdin */
        getline(std::cin, inputLine);

        /* Split line to args by space */
        line_args = splitLine(inputLine);
        if(line_args.size() == 0) {
            continue;
        }

        /* Parse line_args into vector of cmd */
        cmdTable = parseLine(line_args, line_end_with_pipeN);
        
        // bulit-in command
        string first_cmd_arg = cmdTable.at(0).at(0);
        if(first_cmd_arg == "exit" || first_cmd_arg == "printenv" || first_cmd_arg == "setenv" || first_cmd_arg == "unsetenv" || first_cmd_arg == "source") {
            builtinCmd(cmdTable.at(0));
        }
        // real command
        else {
            signal(SIGCHLD, sigChildHandler);
            forkProcess(cmdTable, line_end_with_pipeN, total_cmd_cnt, last_cmd_return);
        }

        cmdTable.clear();

        updateHistory(inputLine);
    }

    return 0;
}