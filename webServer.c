#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <signal.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>
#include <pthread.h>

#ifndef PORT
#define PORT 8008
#endif

// BACKLOG is the maximum number of connections the socket will queue up, 
// each waiting to be accept()’ed
#ifndef BACKLOG
#define BACKLOG 1500
#endif

// THREADS is the size of the thread pool to be created
#ifndef THREADS
#define THREADS 10
#endif

// INTERMEDIATE_BUFFER is the max size of read the server can receive.
#ifndef INTERMEDIATE_BUFFER
#define INTERMEDIATE_BUFFER 6*1024
#endif

// MAX_HOSTNAME is the max size the hostname can be
#ifndef MAX_HOSTNAME
#define MAX_HOSTNAME 2*1024
#endif

// RESOURCE_NAME_BUFFER is the max size of resource name the server can be requested
#ifndef RESOURCE_NAME_BUFFER
#define RESOURCE_NAME_BUFFER 2*1024
#endif

#ifndef HEADER
#define HEADER "HTTP/1.1 200 OK \r\n"
#endif

#ifndef RESPONSE_400
#define RESPONSE_400 "HTTP/1.1 400 Bad Request\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01//EN\" \"http://www.w3.org/TR/html4/strict.dtd\"><html>\r\n<head>\r\n<title> 400 Bad Request </title>\r\n</head>\r\n<body>\r\n<p> 400 Bad Request </p>\r\n</body>\r\n</html>\r\n"
#endif

#ifndef RESPONSE_404
#define RESPONSE_404 "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01//EN\" \"http://www.w3.org/TR/html4/strict.dtd\"><html>\r\n<head>\r\n<title> 404 Not Found </title>\r\n</head>\r\n<body>\r\n<p> 404 Not Found </p>\r\n</body>\r\n</html>\r\n"
#endif

#ifndef RESPONSE_500
#define RESPONSE_500 "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01//EN\" \"http://www.w3.org/TR/html4/strict.dtd\"><html>\r\n<head>\r\n<title> 500 Internal Server Error </title>\r\n</head>\r\n<body>\r\n<p> 500 Internal Server Error </p>\r\n</body>\r\n</html>\r\n"
#endif

/* Used to override the processing of the interrupt signal,
   thus allowing for a more gracefull shutdown of the web server */
void sigproc();

/* Used to create a thread to process a connection */
void *process_Connection(void * args);
/* Splits the full string of the response into an array of strings, one entry for each line */
int split_HTTP_Request(int fd, char * httpRequest, char *delimiter, char *** lines);
/* Looks for a hostname, then checks if it matches current hostname */
int check_Hostname(int fd, char ** httpRequest);
/* Checks the validity of the request before returning the requested resources name */
int check_Resource(int fd, char * httpRequest, char ** addr);
/* Attempts to read in the requested resource */
long int read_In_Resource(int fd, char * filename, char ** variable_To_Fill_With_Resource);
/* Responds to the connection with a 200 good response message */
int respond_200(int fd, int size, char * extension, char * content);
/* Responds to the connection with a 400 Bad request message */
void respond_400(int fd);
/* Responds to the connection with a 404 File Not Found message */
void respond_404(int fd);
/* Responds to the connection with a 500 Internal Server Error message */
void respond_500(int fd);

/* STATE OF PROGRAM */

/* My implementation of the thread pool works as follows:
   
   The backlog acts as my work queue.
   The main loop creates {THREADS} number of threads to act as a thread pool.
   These threads then all start running but then block on accept.
   When a connection comes in, one of the threads accepts the connection while the others continue to block
   Thus the threads will block while the work queue is empty (ie. no new connections) and do work whilst 
   it is not (ie. there are connections to process).
   
   Thus it is my undestanding that I have technically implemented the thread pool design pattern by reusing the
   structures and methods given by the connection/operating system. */

/* There is a memory leak on line ~222 on the variable message which I believe is equal to:
   number_Of_Connections_Open * INTERMEDIATE_BUFFER * sizeof(char *)
   I believe this only occurs when you interrupt the program midway through processing an open connection
   as it remains constant with regards to the above equation. Couldn't figure out why it doesn't free though.*/

// Threads is the only global variable and it helps with a graceful shut down of the web server.
// Array of threads being made
pthread_t threads[THREADS];

int main(){
	
	
	/* Create the socket */
	int fd;
	int type = AF_INET6;
	
	/* SOCK STREAM parameter indicates that a TCP stream socket is desired. Use SOCK DGRAM instead to create a UDP datagram socket */
	fd = socket(type, SOCK_STREAM, 0);
	if (fd == -1) {
		// an error occurred
		fprintf(stderr, "Socket failed to be made\n");
		return -1;
	}
	
	/* Means that if the server crashes you don't have to wait ages
	   to be able to rebind the socket */
	int set=1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set));
	
	/* Bind the socket to a port */
	struct sockaddr_in6 addr;
	
	addr.sin6_addr = in6addr_any;
	// The type of connect, either IPv4/IPv6
	addr.sin6_family = type;
	addr.sin6_port = htons(PORT);
	if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
		// an error occurred
		fprintf(stderr, "Error binding\n");
		return -1;
	}
	
	/* Start listening for connections */
	// Backlog is the maximum number of connections the socket will queue up, each waiting to be accept()’ed
	if (listen(fd, BACKLOG) == -1) {
		// an error occurred
		fprintf(stderr, "Failed listen\n");
		return -1;
	}
	printf("Server Running.\n");

	/* Used to override the processing of the interrupt signal,
       thus allowing for a more gracefull shutdown of the web server */
	signal(SIGINT, sigproc);
	
	/* Creates thread pool to deal with connections */
	int error_Check = 0;
	// THREADS is the size of the thread pool to be created
	int num_Threads = THREADS;
	int i;
	
	// the threads variable is an array of threads being made
	// Create all the threads
	for(i=0; i<num_Threads; i++){
		error_Check = pthread_create(&threads[i], NULL, (void *)process_Connection,  &fd);
	}
	
	for(i=0; i<num_Threads; i++){
		pthread_join(threads[i], NULL);
	}
	close(fd);
	printf("Server shut down gracefully.\n");
	return 0;
}

/* Used to override the processing of the interrupt signal,
   thus allowing for a more gracefull shutdown of the web server */
void sigproc(){
	// signal(SIGINT, sigproc);
	//  NOTE some versions of UNIX will reset signal to default
	// after each call. So for portability reset signal each time 
	
	printf("\nInterrupt signal received. Shutting down web server.\n");	
	int i;
	for(i=0; i<THREADS; i++){
		pthread_cancel(threads[i]);
	}
	signal(SIGINT, SIG_DFL);
}

/* Used to create a thread to process a connection */
void *process_Connection(void * args){
	
	int fd = *(int *)(args);
	
	
	while(1){
		int connfd;
		
		/* Accept a connection	*/
		struct sockaddr_in6 cliaddr;	
		socklen_t cliaddr_len = sizeof(cliaddr);
		
		// The thread will block here while there is nothing on the backlog, a.k.a work queue
		connfd = accept(fd, (struct sockaddr *) &cliaddr, &cliaddr_len);
		if (connfd == -1) {
			// an error occurred
			fprintf(stderr, "Error accepting\n");
			close(connfd);
			// return -1;
			continue;
		}else if(connfd == 0){
			fprintf(stderr, "Connection closed by client\n");
			close(connfd);
			continue;
		}
		
		/* Create buffer to read in data from connection */
		ssize_t rcount = 1;
		
		while(rcount){
			
			int error_Check = 0;
			// request_Size is the length of the request
			int request_Size = INTERMEDIATE_BUFFER;
			// message holds the full string of the request
			char * message = malloc(request_Size * sizeof(char *));
			
			/* Read the request into the buffer until it ends with \r\n\r\n */
			// The size of the message currently read in
			int size_Read = 0;
			while( size_Read<4 || strncmp((message + size_Read - 4), "\r\n\r\n", 4) ){
				
				// INTERMEDIATE_BUFFER is the max size of read the server can receive.
				char buf[INTERMEDIATE_BUFFER];
				
				rcount = read(connfd, buf, INTERMEDIATE_BUFFER);
				if(rcount == -1){
					break;
				}else if(!rcount){
					break;
				}else if(request_Size < size_Read+rcount){
					request_Size *= 2;
					message = (char *) realloc( (void*) message, request_Size * sizeof(char *) );
				}
				memcpy(message + size_Read, buf, rcount);
				size_Read += rcount;
			}
			
			// Error checking
			if (rcount == -1) {
				// An error has occurred
				fprintf(stderr, "Error reading from connection\n");
				respond_500(connfd);
				free(message);
				// return -1;
				break;
			}else if(!rcount){
				fprintf(stderr, "Connection closed\n");
				free(message);
				break;
			}
			
			/* Spilt the request into an array of each line */
			char ** lines;
			int size_Of_Array = 0;
			size_Of_Array = split_HTTP_Request(connfd, message, "\r\n", &lines);
			if (size_Of_Array<0){
				fprintf(stderr, "Error splitting HTTP Request\n");
				free(message);
				// return -1;
				continue;
			}
			
			/* Check the hostname field of the request is valid */
			error_Check = 0;
			error_Check = check_Hostname(connfd, lines);
			if(error_Check<0){
				fprintf(stderr, "Error invalid hostname detected\n");
				free(message);
				free(lines);
				continue;
			}
			
			/* Get the name of the resource requested */
			char * filename;
			error_Check = 0;
			error_Check = check_Resource(connfd, *lines, &filename);
			if(error_Check<0){
				fprintf(stderr, "Error at get resource\n");
				free(message);
				free(lines);
				// return -1;
				continue;
			}
			
			/* Try to read in the resource requested */
			char * resource_Requested;
			long int size = 0;
			size = read_In_Resource(connfd, filename, &resource_Requested);
			if(size<0){
				// An error has occurred
				free(message);
				free(lines);
				free(filename);
				continue;
			}
			
			/* Get the resource extension */
			char * extension;
			char * tokenptr;
			for (	tokenptr = strtok(filename, ".");
			     (tokenptr = strtok(NULL, ".")) != NULL;
			     extension = tokenptr);
			
			/* Generate the HTTP response for resource */
			error_Check = 0;
			error_Check = respond_200(connfd, size, extension, resource_Requested);
			
			// Free allocated resources
			free(message);
			free(lines);
			free(filename);
			free(resource_Requested);
			// Check if response was good
			if(error_Check<0){
				fprintf(stderr, "Error replying\n");
				// return -1;
				continue;
			}
		}
		close(connfd);
	}
	
	pthread_exit(NULL);
}

/* This function will split a request into based on the assumption the terminator symbol is the delimiter twice.*/
int split_HTTP_Request(int fd, char * http_Request, char *delimiter, char ***  lines){
	char * cursor = http_Request;
	int num_Of_Lines = 0;
	int length_Of_Request = strlen(http_Request);
	int length_Of_Delimiter = strlen(delimiter);
	
	// If the length of the request is less than the minimum request length
	if (length_Of_Request<19){
		fprintf(stderr, "Request appears too short\n");
		fprintf(stderr,"Request: %s\n", http_Request);
		respond_400(fd);
		return -1;
	}
	
	// While the delimiter hasn't appeared twice
	while(strncmp(cursor, delimiter, length_Of_Delimiter) || 
	      strncmp((cursor+length_Of_Delimiter), delimiter, length_Of_Delimiter) ){
		if( !strncmp(cursor, delimiter, length_Of_Delimiter) ){
			num_Of_Lines++;
		}
		// If num of times gone around loop > length of request, some error has occurred
		if((cursor-http_Request) > length_Of_Request){
			fprintf(stderr, "Error counting lines\n");
			fprintf(stderr,"Request: %s\n", http_Request);
			respond_500(fd);
			return -1;
		}
		cursor++;
	}
	
	num_Of_Lines++;
	*lines = malloc(num_Of_Lines * sizeof(void *));
	// Fail to malloc
	if(*lines == NULL){
		fprintf(stderr, "Error allocating space for lines array\n");
		respond_500(fd);
		return -1;
	}
	
	cursor = strtok(http_Request, delimiter);
	// If no delimiter found then error
	if (cursor == NULL){
		fprintf(stderr, "No delimiter found in HTTP request\n");
		fprintf(stderr,"Request: %s\n", http_Request);
		free(*lines);
		respond_400(fd);
		return -1;
	}
	/* Keep splitting the line up and filling the array */
	**lines = cursor;
	int i;
	for(i=1; i<num_Of_Lines && cursor != NULL; i++){
		cursor = strtok(NULL, delimiter);
		*(*lines+i) = cursor;
	}
	
	return num_Of_Lines;
}

/* Looks for a hostname, then checks if it matches current hostname */
int check_Hostname(int fd, char ** httpRequest){
	char ** currentLine;
	int found = 0;
	int error_Check = 0;
	
	/* Find if the host name has been given in the request */
	for(currentLine = httpRequest; *currentLine && !found; currentLine++){
		
		/* Convert line to lowercase */
		// Make a copy of the current line to preserve the message from being altered
		char * copy = malloc(strlen(*currentLine)+1);
		if(copy == NULL){
			free(copy);
			fprintf(stderr, "Error copying: %s\n", *currentLine);
			break;
		}
		strcpy ( copy, *currentLine );
		char * p = copy;
		
		// Conversion to lower case.
		// Taken from stack overflow: J.F. Sebastian
		// https://stackoverflow.com/a/2661788
		for ( ; *p; ++p) *p = tolower(*p);
		
		found = ! (strncmp( copy, "host: ", 6));
		free(copy);
	}
	// If hostname not given, request is OK
	if(!found){
		return 0;
	}
	// Points back one line to the correct line
	currentLine--;
	
	/* Get the current servers hostname */
	// Allocates space for hostname
	char * hostname = malloc(MAX_HOSTNAME+6);
	if(hostname==NULL){
		fprintf(stderr, "Error allocating space for hostname\n");
		respond_500(fd);
		return -1;
	}
	// Gets the hostname
	error_Check = 0;
	error_Check = gethostname(hostname, MAX_HOSTNAME);
	if(error_Check<0){
		fprintf(stderr, "Error getting hostname\n");
		free(hostname);
		respond_500(fd);
		return -1;
	}
	
	/* Convert hostname to all lowercase */
	char * p = hostname;	
	// Conversion to lower case.
	// Taken from stack overflow: J.F. Sebastian
	// https://stackoverflow.com/a/2661788
	for ( ; *p; ++p) *p = tolower(*p);
	
	// This is used in the conversion of integer to string. It requires a string to fill.
	char * sPort = malloc(7);
	if(sPort==NULL){
		fprintf(stderr, "Error allocating space for sPort\n");
		free(hostname);
		respond_500(fd);
		return -1;
	}
	
	/* Compare the current hostname with the requested hostname */
	if(!strncmp(*currentLine+6, hostname, MAX_HOSTNAME)){
		free(hostname);
		free(sPort);
		return 0;
	}
	
	/* Check if the hostname has specified a non standard port */
	error_Check = 0;
	// Creates a string of the port number to concatenate
	error_Check = snprintf(sPort, 6, ":%i", PORT);
	if(error_Check<0){
		fprintf(stderr, "Error invalid port for server\n");
		free(hostname);
		free(sPort);
		respond_500(fd);
		return -1;
	}
	// Add port number to the end of the hostname string
	strcat(hostname, sPort);
	if(!strncmp(*currentLine+6, hostname, MAX_HOSTNAME)){
		free(hostname);
		free(sPort);
		return 0;
	}
	
	// Free hostname, no longer needed
	free(hostname);
	
	/* Check if it is a localhost domain */
	char * local = malloc(strlen("localhost") + strlen(sPort) + 1);
	if(local==NULL){
		fprintf(stderr, "Error allocating space for local\n");
		free(sPort);
		respond_500(fd);
		return -1;
	}
	strcpy(local, "localhost");
	if (!strncmp(*currentLine+6, local, MAX_HOSTNAME)){
		free(local);
		free(sPort);
		return 0;
	}
	
	/* Check if it is a localhost with a non standard port */
	strcat(local, sPort);
	if (!strncmp(*currentLine+6, local, MAX_HOSTNAME)){
		free(local);
		free(sPort);
		return 0;
	}
	
	fprintf(stderr, "Error %s does not match hostname given: %s\n", hostname, *currentLine+6);
	free(local);
	free(sPort);
	respond_400(fd);
	return -1;
}

/* Checks the validity of the request before returning the requested resources name */
int check_Resource(int fd, char * httpRequest, char ** addr){
	int num = 0;
	char * request = malloc(4);
	// RESOURCE_NAME_BUFFER is the max size of resource name the server can be requested
	char * resource = malloc(RESOURCE_NAME_BUFFER);
	char * protocol = malloc(9);
	if(request==NULL){
		fprintf(stderr, "Error allocating temporary space for splitting GET request\n");
		free(resource);
		free(protocol);
		respond_500(fd);
		return -1;
	}else if(resource==NULL){
		fprintf(stderr, "Error allocating temporary space for splitting resource\n");
		free(request);
		free(protocol);
		respond_500(fd);
		return -1;
	}else if(protocol==NULL){
		fprintf(stderr, "Error allocating temporary space for splitting protocol\n");
		free(request);
		free(resource);
		respond_500(fd);
		return -1;
	}
	
	num = sscanf(httpRequest, "%3s /%s %8s", request, resource, protocol);
	
	free(protocol);
	// If num is less than 3, an error has occurred
	if(num!=3){
		fprintf(stderr, "Error scanning request. Either one field of first line missing or resource name could be too large (MAX: %i)\n", RESOURCE_NAME_BUFFER);
		free(request);
		free(resource);
		respond_400(fd);
		return -1;
	}
	// If the request is not a GET, generate an error
	if(strncmp(request, "GET", 3)){
		fprintf(stderr, "Request is not get\n");
		free(request);
		free(resource);
		respond_400(fd);
		return -1;
	}
	free(request);	
	
	/* This part of the code copies the name of the resource requested into a smaller more trimmed string variable */
	// Reuse of num variable.
	num = strlen(resource)+1;
	*addr = malloc(num);
	if(*addr == NULL){
		fprintf(stderr, "Error allocating space for trimmed resource\n");
		free(resource);
		respond_500(fd);
		return -1;
	}
	strncpy(*addr, resource, num);
	free(resource);
	return 0;
}

/* Attempts to read in the requested resource */
long int read_In_Resource(int fd, char * filename, char ** variable_To_Fill_With_Resource){
	FILE *file;
	int error_Check = 0;
	long int size = 0;
	struct stat fs;
	
	/* Attempt to open file */
	file = fopen(filename, "rb");
	if (file == NULL) {
	  fprintf(stderr, "Can't find requested file: %s\n", filename);
	  respond_404(fd);
	  return -1;
	}
	
	/* Get the size of the file and allocate the space for it */
	
	if((stat(filename, &fs) == -1)){
		fprintf(stderr, "Error getting size\n");
		fclose(file);
		respond_500(fd);
		return -1;
	}
	
	size = fs.st_size;
	
	*variable_To_Fill_With_Resource =  malloc(size * sizeof(char) + 1);
	if(*variable_To_Fill_With_Resource == NULL){
		fprintf(stderr, "Can't open requested file, too large\n");
		fclose(file);
		respond_500(fd);
		return -1;
	}
	/* Try to read in the file */
	error_Check = 0;
	error_Check = fread ( (void *) *variable_To_Fill_With_Resource, sizeof(char), size, file);
	if (error_Check != size){
		fprintf(stderr, "Error reading in data\n");
		fclose(file);
		respond_500(fd);
		return -1;
	}
	fclose(file);
	return size;
}

/* Responds to the connection with a 200 good response message*/
int respond_200(int fd, int size, char * extension, char * content){
	
	/* Finds the number of digits required to represent size */
	int count = 0;
	int num = size;
	while(num){
		num=num/10;
		count++;
	}
	
	/* Length of "Content length: " is 17
	 Count is the num of digits in size
	 "\r\n\r\n" is of size 4 */
	char * content_Length = malloc((17 + count + 4) * sizeof(char) + 1);
	// Malloc check
	if(content_Length == NULL){
		fprintf(stderr, "Error allocating space for Content-Length field of response\n");
		respond_500(fd);
		return -1;
	}
	
	int error_Check = 0;
	error_Check = sprintf(content_Length, "Content-Length: %i\r\n\r\n", size);
	if(error_Check<0){
		fprintf(stderr, "Error at sprintf\n");
		free(content_Length);
		respond_500(fd);
		return -1;
	}
	
	char * content_Type;
	char * response;
	
	/* Get the correct "Content-Type: " message */
	if(!strncmp(extension, "html", 4)){
		content_Type = "Content-Type: text/html\r\n";
	}else if(!strncmp(extension, "htm", 3)){
		content_Type = "Content-Type: text/html\r\n";
	}else if(!strncmp(extension, "txt", 3)){
		content_Type = "Content-Type: text/plain\r\n";
	}else if(!strncmp(extension, "jpg", 3)){
		content_Type = "Content-Type: image/jpeg\r\n";
	}else if(!strncmp(extension, "jpeg", 4)){
		content_Type = "Content-Type: image/jpeg\r\n";
	}else if(!strncmp(extension, "gif", 3)){
		content_Type = "Content-Type: image/gif\r\n";
	}else{
		content_Type = "Content-Type: application/octet-stream\r\n";
	}
	
	// Allocate space to concatenate the full response together
	response = malloc(strlen(HEADER) + strlen(content_Type) + 
	                  strlen(content_Length) + size + 1);

	// Malloc check
	if(response==NULL){
		fprintf(stderr, "Error allocating space for full response. Requested content probably too big\n");
		free(content_Length);
		respond_500(fd);
		return -1;
	}
	
	/* Join together all the variables that make up the response */
	strcpy(response, HEADER);
	strcat(response, content_Type);
	strcat(response, content_Length);
	
	/* Get the value of the pointer required to 
	append any data to the end of response using memcpy */
	int cursor = strlen(response);
	memcpy(response+cursor, content, size+1);
	
	/* Sends the response back to the client */
	error_Check = write(fd, response, cursor+size);
	
	// Frees content_Length, no longer needed
	free(content_Length);
	
	// Check if the message sent fine
	if (error_Check<0) {
		// An error has occurred
		fprintf(stderr, "Full response to client: \n");
		fprintf(stderr, "%s\n", response);
		fprintf(stderr, "Error writing response: 200\n");
		free(response);
		respond_500(fd);
		return -1;
	}
	// Finally, free response before returning
	free(response);
	return 0;
}

/* Responds to the connection with a 400 Bad request message */
void respond_400(int fd){
	int error_Check;
	error_Check = write(fd, RESPONSE_400, strlen(RESPONSE_400));
	if(error_Check<0){
		fprintf(stderr, "Error writing response: 400\n");
	}
	close(fd);
	return;
}

/* Responds to the connection with a 404 File Not Found message */
void respond_404(int fd){
	int error_Check;
	error_Check = write(fd, RESPONSE_404, strlen(RESPONSE_404));
	if(error_Check<0){
		fprintf(stderr, "Error writing response: 404\n");
	}
	close(fd);
	return;
}

/* Responds to the connection with a 500 Internal Server Error message */
void respond_500(int fd){
	int error_Check;
	error_Check = write(fd, RESPONSE_500, strlen(RESPONSE_500));
	if(error_Check<0){
		fprintf(stderr, "Error writing response: 500\n");
	}
	close(fd);
	return;
}