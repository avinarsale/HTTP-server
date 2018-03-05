#include <stdio.h>
#define _BSD_SOURCE
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <fstream>
#include <sys/stat.h>
#include <dirent.h>
#include <iostream>
#include <map>
#include <sstream>
#include <algorithm>  
#include <time.h>
#include <ctime>
#include <mutex>
#include <netdb.h>
#define MAX_SEND 1000

using namespace std;
mutex myMutex;
map<string, string> mime_types;

static const char* Response_OK =
  "HTTP/1.1 200 OK\n";
 
static const char* Response_Not_Found_p1 = 
  "HTTP/1.1 404 Not Found\n";
static const char* Response_Not_Found_p2 = 
  "Accept-Ranges: bytes\n"
  "Content-Length: 115\n"
  "Content-type: text/html\n"
  "\n"
  "<html>\n"
  " <body>\n"
  "  <h1>Page Not Found</h1>\n"
  "  <p>The requested URL was not found on this server.</p>\n"
  " </body>\n"
  "</html>\n";

static void request_handler (int my_socket_fn, const char* url_page,
			map<string,int> &access_times, struct sockaddr_in client_det)
{
	char buffer_sent[MAX_SEND];
	string header;

	char h_date[32];
	time_t now = time(0);
	struct tm tm = *gmtime(&now);
	strftime(h_date, sizeof h_date, "%a, %d %b %Y %H:%M:%S %Z", &tm);
	header.append("Date: ");
	header += h_date;
	header.append("\nServer: anarsal1 server");

	char full_page_name[100]="./www/";
	struct stat stat_buffer; 
	strcat(full_page_name,url_page+1);
	if(stat (full_page_name, &stat_buffer) == 0){
		string file_name(full_page_name);			
		map<string,int>::iterator itr_access=access_times.find(file_name);
		if(itr_access==access_times.end()){
			access_times.insert(pair<string,int>(file_name,1));
		}else{
			access_times.find(file_name)->second=++access_times.find(file_name)->second;
		}
		cout << file_name.substr(5,file_name.length());
		cout << "|" << inet_ntoa(client_det.sin_addr); 
		cout << "|" << ntohs(client_det.sin_port);
		cout << "|" << access_times.find(file_name)->second << endl;
		
		time_t last_modified = stat_buffer.st_mtime;
		std::tm * ptm = gmtime(&last_modified);
		char h_last_modified[32];
		strftime(h_last_modified, sizeof h_last_modified, "%a, %d %b %Y %H:%M:%S %Z", ptm);
		
		streampos begin,end;
		ifstream myfile (full_page_name, ios::binary);
		begin = myfile.tellg();
		myfile.seekg (0, ios::end);
		end = myfile.tellg();
		myfile.close();
		int content_length=end-begin;
		
		string extention = file_name.substr(file_name.find_last_of(".") + 1);			
		map<string,string>::iterator itr=mime_types.find(extention);
		string mimetype;
		if(itr==mime_types.end())
		{
			mimetype="application/octet-stream"; 
		}else{
			mimetype=mime_types.find(extention)->second;
		}
		
		header.append("\nLast-Modified: ");
		header += h_last_modified;
		header.append("\nAccept-Ranges: bytes");
		header.append("\nContent-Length: ");
		header += to_string(content_length);
		header.append("\nContent-Type: ");
		header +=mimetype;
		header.append("\n\n");
	}else{
		cout << "Page not found." << endl;
		write(my_socket_fn,Response_Not_Found_p1, strlen(Response_Not_Found_p1));
		write(my_socket_fn,header.c_str(), strlen(header.c_str()));
		write(my_socket_fn,Response_Not_Found_p2, strlen(Response_Not_Found_p2));
		return;
	}
	if(send(my_socket_fn, Response_OK, strlen (Response_OK),0)<1){
		cerr << "ERROR: sending data over socket." << endl;
		return;
	}
	if(send(my_socket_fn,header.c_str(), strlen(header.c_str()),0)<1){
		cerr << "ERROR: sending data over socket." << endl;
		return;
	}

	FILE *sendFile = fopen(full_page_name,"r");
	while( !feof(sendFile) ) {
		int total_read = fread(buffer_sent, sizeof(unsigned char), MAX_SEND, sendFile);
		if( total_read < 1 ) break;

		char *buffer_sent_ptr = buffer_sent;
		do{
			int sent_count = send(my_socket_fn, buffer_sent_ptr, total_read, 0);
			if( sent_count < 1 ) {
				cerr << "ERROR: sending data over socket." << endl;
				break;
			}
			total_read -= sent_count;
			buffer_sent_ptr += sent_count;
		}while( total_read > 0 );
	}
}

int main (){
	map<string,int> access_times;
		
	fstream fileStream;
	fileStream.open ("/etc/mime.types", fstream::in);
    string line;
	while (getline(fileStream, line)){
		istringstream is_line(line);
		string key;
		if (getline(is_line, key, '\t')) {
			string value;
			if (key[0] == '#'){
				continue;
			}
			if (getline(is_line, value)) {
				value.erase(remove(value.begin(), value.end(), '\t'), value.end());
				istringstream iss(value);
				string word;
				while(iss >> word) {
					mime_types[word] = key;
				}
			}
		}
	}

    struct sockaddr_in socket_address;
    int server_ds, my_socket, read_count;
    int address_len = sizeof(socket_address);
    int option=1;
    char socket_buffer[1024] = {0};

    if ((server_ds = socket(AF_INET, SOCK_STREAM, 0)) == 0){
        cerr << "ERROR: failed in creating socket" << endl;
        exit(EXIT_FAILURE);
    }
    if (setsockopt(server_ds, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &option, sizeof(option))) {
        cerr << "ERROR: failed setsockopt" << endl;
        exit(EXIT_FAILURE);
    }
    socket_address.sin_family = AF_INET;
    socket_address.sin_addr.s_addr = INADDR_ANY;
    socket_address.sin_port = 0; //htons(8080);

    if (bind(server_ds, (struct sockaddr *)&socket_address, sizeof(socket_address))<0) {
        cerr << "ERROR: binding failed" << endl;
        exit(EXIT_FAILURE);
    }
    if (listen(server_ds, 3) < 0) {
        cerr << "ERROR: failed at listen" << endl;
        exit(EXIT_FAILURE);
    }
	
	char hostname[1024];
	hostname[1023] = '\0';
	gethostname(hostname, 1023);
	struct hostent* host_n;
	host_n = gethostbyname(hostname);
	cout << "Host name:" << host_n->h_name << endl;

	int socket_address_len = sizeof(struct sockaddr);
    struct sockaddr_in socket_address_1;
	getsockname(server_ds, (struct sockaddr *) &socket_address_1, (socklen_t*)&socket_address_len);
 	cout << "Server port:" << ntohs(socket_address_1.sin_port) << endl;
	
	struct stat stat_buffer_dir; 
	if(stat ("./www", &stat_buffer_dir) != 0){
		cerr << "Directory www does not exists. Exiting.." << endl;
		exit(EXIT_FAILURE);
	}

	
	while(1)
	{
		if ((my_socket = accept(server_ds, (struct sockaddr *)&socket_address, (socklen_t*)&address_len))<0) {
			cerr << "ERROR: failed to accept" << endl;
			exit(EXIT_FAILURE);
		}
		
		read_count = read( my_socket, socket_buffer, 1024);
		char method[sizeof (socket_buffer)];
		char url[sizeof (socket_buffer)];
		char protocol[sizeof (socket_buffer)];
		if(read_count>0){
			socket_buffer[read_count] = '\0';
			sscanf (socket_buffer, "%s %s %s", method, url, protocol);
			while (strstr(socket_buffer, "\r\n\r\n") == NULL) {
				read_count = read (my_socket, socket_buffer, sizeof(socket_buffer));
			}
			if (read_count == -1) {
				close(my_socket);
				return -1;
			}
		}
		else if (read_count<0){
			cerr << "ERROR: In reading socket_buffer." << endl;
			return -1;
		}

		myMutex.lock();
		request_handler(my_socket, url, access_times, socket_address);
		myMutex.unlock();
		close (my_socket);
	} // while 1
	return 0;
}