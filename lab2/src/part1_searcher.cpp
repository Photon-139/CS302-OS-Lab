#include <iostream>
#include <fstream>
#include <cstring>
#include <unistd.h>
#include <signal.h>

using namespace std;

int main(int argc, char **argv)
{
	if(argc != 5)
	{
		cout <<"usage: ./partitioner.out <path-to-file> <pattern> <search-start-position> <search-end-position>\nprovided arguments:\n";
		for(int i = 0; i < argc; i++)
			cout << argv[i] << "\n";
		return -1;
	}
	
	char *file_to_search_in = argv[1];
	char *pattern_to_search_for = argv[2];
	int search_start_position = atoi(argv[3]);
	int search_end_position = atoi(argv[4]);

	int pattern_length = strlen(pattern_to_search_for);
	int chunk_size = search_end_position - search_start_position + 1;

	if (chunk_size >= pattern_length && search_start_position >= 0)
	{
		ifstream file(file_to_search_in, ios::binary);
		if (file.is_open())
		{
			file.seekg(search_start_position, ios::beg);
			string buffer(chunk_size, '\0');
			file.read(&buffer[0], chunk_size);
			size_t bytes_read = file.gcount();
			buffer.resize(bytes_read);
			file.close();

			size_t pos = buffer.find(pattern_to_search_for);
			if (pos != string::npos)
			{
				cout << "[" << getpid() << "] found at [" << (search_start_position + (int)pos) << "]\n";
				return 0;
			}
		}
	}

	cout << "[" << getpid() << "] didn't find\n";
	return 0;
}
