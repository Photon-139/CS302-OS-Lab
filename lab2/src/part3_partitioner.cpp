#include <iostream>
#include <fstream>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

using namespace std;

pid_t left_pid = -1, right_pid = -1, searcher_pid;

void handle_sig(int sig){
    cout << "[" << getpid() << "] received SIGTERM\n";
    if(left_pid>0) kill(left_pid, SIGTERM);
    if(right_pid>0) kill(right_pid, SIGTERM);
    if(searcher_pid>0) kill(searcher_pid, SIGTERM);
    exit(0);
}

int main(int argc, char **argv)
{
	if(argc != 6)
	{
		cout <<"usage: ./partitioner.out <path-to-file> <pattern> <search-start-position> <search-end-position> <max-chunk-size>\nprovided arguments:\n";
		for(int i = 0; i < argc; i++)
			cout << argv[i] << "\n";
		return -1;
	}
	signal(SIGTERM, handle_sig);
	char *file_to_search_in = argv[1];
	char *pattern_to_search_for = argv[2];
	int search_start_position = atoi(argv[3]);
	int search_end_position = atoi(argv[4]);
	int max_chunk_size = atoi(argv[5]);
    
    int curr_region = search_end_position-search_start_position+1;
    
    cout << "[" << getpid() << "]" << " start position = " << argv[3] << " ; end position = " << argv[4] << "\n";
    if(curr_region<=max_chunk_size){
        searcher_pid = fork();
        if(searcher_pid==0){
            string start_str = to_string(search_start_position);
            string end_str = to_string(search_end_position);
            execl("./part3_searcher.out", "part3_searcher.out", file_to_search_in, pattern_to_search_for, start_str.c_str(), end_str.c_str(), NULL);
            cout << "Error while calling execl\n";
            exit(-1);
        }
        cout << "[" << getpid() << "]" << " forked searcher child " << to_string(searcher_pid) << "\n";

        int status;
        waitpid(searcher_pid, &status, 0);
        cout << "[" << getpid() << "]" << " searcher child returned\n";
        int found = WIFEXITED(status) && WEXITSTATUS(status)==1;
        return found;
    }

    int mid = (search_start_position+search_end_position)/2;
    
    left_pid = fork();
    if(left_pid==0){
        string left_start_str = to_string(search_start_position);
        string left_end_str = to_string(mid);
        execl("./part3_partitioner.out", "part3_partitioner.out", file_to_search_in, pattern_to_search_for, left_start_str.c_str(), left_end_str.c_str(), argv[5], NULL);
        cout << "Error while calling execl\n";
        exit(-1);
    }
    cout << "[" << getpid() << "]" << " forked left child " << left_pid << "\n";

    right_pid = fork();
    if(right_pid==0){
        string right_start_str = to_string(mid+1);
        string right_end_str = to_string(search_end_position);
        execl("./part3_partitioner.out", "part3_partitioner.out", file_to_search_in, pattern_to_search_for, right_start_str.c_str(), right_end_str.c_str(), argv[5], NULL);
        cout << "Error while calling execl\n";
        exit(-1);
    }
    cout << "[" << getpid() << "]" << " forked right child " << right_pid << "\n";

    int remaining = 2;
    int found = 0;
    while(remaining){
        int status;
        auto child_pid = wait(&status);
        remaining--;
        cout << "[" << getpid() << "]" << (child_pid==left_pid ? " left " : " right ")  << "child returned\n";
        if(WIFEXITED(status) && WEXITSTATUS(status)==1){
            found = 1;
            if(remaining){
                kill(child_pid==left_pid ? right_pid : left_pid, SIGTERM);
            }
        }
    }
    exit(found);
	
	//TODO
	//cout << "[" << my_pid << "] start position = " << search_start_position << " ; end position = " << search_end_position << "\n";
	//cout << "[" << my_pid << "] forked left child " << my_children[0] << "\n";
	//cout << "[" << my_pid << "] forked right child " << my_children[1] << "\n";
	//cout << "[" << my_pid << "] left child returned\n";
	//cout << "[" << my_pid << "] right child returned\n";
	//cout << "[" << my_pid << "] left child returned\n";
	//cout << "[" << my_pid << "] right child returned\n";*/
	//cout << "[" << my_pid << "] forked searcher child " << searcher_pid << "\n";
	//cout << "[" << my_pid << "] searcher child returned \n";
	//cout << "[" << my_pid << "] received SIGTERM\n"; //applicable for Part III of the assignment

	return 0;
}
