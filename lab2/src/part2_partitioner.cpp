#include <iostream>
#include <fstream>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <string>

using namespace std;

int main(int argc, char **argv)
{
    if(argc != 6)
    {
        cout << "usage: " << argv[0] << " <path-to-file> <pattern> <search-start-position> <search-end-position> <max-chunk-size>\nprovided arguments:\n";
        for(int i = 0; i < argc; i++)
            cout << argv[i] << "\n";
        return -1;
    }
    
    char *file_to_search_in = argv[1];
    char *pattern_to_search_for = argv[2];
    int search_start_position = atoi(argv[3]);
    int search_end_position = atoi(argv[4]);
    int max_chunk_size = atoi(argv[5]);
    
    cout << "[" << getpid() << "] start position = " << search_start_position << " ; end position = " << search_end_position << "\n";
    
    if (search_end_position - search_start_position > max_chunk_size) {
        int mid_point = (search_end_position + search_start_position) / 2;
        
        string mid_str = to_string(mid_point);
        string mid_plus_one_str = to_string(mid_point + 1); 
        
        int pid_left = fork();
        if (pid_left < 0) {
            cerr << "Fork failed!\n";
            return 1;
        } 
        else if (pid_left == 0) {
            char* args[] = {
                argv[0], 
                argv[1], argv[2], argv[3], 
                (char*)mid_str.c_str(), 
                argv[5], 
                NULL
            };
            execv(args[0], args);
            perror("execv failed on left child"); 
            exit(1);
        }
        
        cout << "[" << getpid() << "] forked left child " << pid_left << "\n";

        int pid_right = fork();
        if (pid_right < 0) {
            cerr << "Fork failed!\n";
            return 1;
        } 
        else if (pid_right == 0) {
            char* args[] = {
                argv[0],
                argv[1], argv[2], 
                (char*)mid_plus_one_str.c_str(), 
                argv[4], argv[5], 
                NULL
            };
            execv(args[0], args);
            perror("execv failed on right child"); 
            exit(1);
        }
        
        cout << "[" << getpid() << "] forked right child " << pid_right << "\n";

        waitpid(pid_left, NULL, 0);
        cout << "[" << getpid() << "] left child returned\n";
        
        waitpid(pid_right, NULL, 0);
        cout << "[" << getpid() << "] right child returned\n";
    } 
    else {
        int searcher_pid = fork();
        if (searcher_pid < 0) {
            cerr << "Fork failed!\n";
            return 1;
        }
        else if (searcher_pid == 0) {
            char* args[] = {
                (char*)"./part2_searcher.out", 
                argv[1], argv[2], argv[3], argv[4], 
                NULL
            };
            execv(args[0], args);
            perror("execv failed on searcher");
            exit(1);
        }
        
        cout << "[" << getpid() << "] forked searcher child " << searcher_pid << "\n";
        
        waitpid(searcher_pid, NULL, 0);
        
        cout << "[" << getpid() << "] searcher child returned\n";
    }

    return 0;
}