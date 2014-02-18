#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <signal.h>
#include <unistd.h>

#ifndef PORT
#define PORT 8008
#endif

// BACKLOG is the maximum number of connections the socket will queue up, 
// each waiting to be accept()’ed
#ifndef BACKLOG
#define BACKLOG 1500
#endif

// FULL_MESSAGE_BUFFER is the max size of message the server can receive
#ifndef FULL_MESSAGE_BUFFER
#define FULL_MESSAGE_BUFFER 16*1024
#endif

// RESOURCE_NAME_BUFFER is the max size of resource name the server can be requested
#ifndef RESOURCE_NAME_BUFFER
#define RESOURCE_NAME_BUFFER 2*1024
#endif

#ifndef HEADER
#define HEADER "HTTP/1.1 200 OK \r\n"
#endif

#ifndef RESPONSE_400
#define RESPONSE_400 "Connection: close \r\n"
#endif

#ifndef RESPONSE_404
#define RESPONSE_404 "Connection: close \r\n"
#endif

#ifndef RESPONSE_500
#define RESPONSE_500 "Connection: close \r\n"
#endif

// void sigproc(void);
/* Splits the full string of the response into an array of strings, one entry for each line */
int split_HTTP_Request(int fd, char * httpRequest, char *delimiter, char *** lines);
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

// int fd;

/* Things to ask about in the labs */
/* What does in6addr_any mean? */
/* Am I allowed to fprintf any errors? */
/* Is this printf of server running ok? */
/* Should I close the program when I error or just continue the loop? */
/* If you interrupt the program or it errors, it won't release the port 
	straight away. Any idea how to fix that? */
/* What is the difference between close and fclose and which should I use? */
/* Is it ok that I generate the content length regardless of if the connection is closing */

/* Need to:
-Get it to check hostname
-Multithreading */

int main(){
	
	/* Create the socket */
	int fd;
	int type = AF_INET6;
	
	// SOCK STREAM parameter indicates that a TCP stream socket is desired;
	// use SOCK DGRAM to create a UDP datagram socket
	fd = socket(type, SOCK_STREAM, 0);
	if (fd == -1) {
		// an error occurred
		fprintf(stderr, "Socket failed to be made\n");
		return -1;
	}
	
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
	}
	printf("Server Running.\n");

	// signal(SIGINT, sigproc);

	while(1){
		
		int error_Check = 0;
		
		/* Accept a connection	*/
		int connfd;
		struct sockaddr_in6 cliaddr;	
		socklen_t cliaddr_len = sizeof(cliaddr);
		
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
		ssize_t rcount;
		// FULL_MESSAGE_BUFFER is the max size of message the server can receive
		char buf[FULL_MESSAGE_BUFFER];
		// Read the request into the buffer
		rcount = read(connfd, buf, FULL_MESSAGE_BUFFER);
		// Error checking
		if (rcount == -1) {
			// An error has occurred
			fprintf(stderr, "Error reading from connection\n");
			respond_500(connfd);
			// return -1;
			continue;
		}else if (rcount == FULL_MESSAGE_BUFFER){
			fprintf(stderr, "Error, message limit reached (MAX: %i)\n", FULL_MESSAGE_BUFFER);
			respond_500(connfd);
			// return -1;
			continue;
		}
		
		/* Spilt the request into an array of each line */
		char ** lines;
		int size_Of_Array = 0;
		size_Of_Array = split_HTTP_Request(connfd, buf, "\r\n", &lines);
		if (size_Of_Array<0){
			fprintf(stderr, "Error splitting HTTP Request\n");
			// return -1;
			continue;
		}
		
		/* Get the name of the resource requested */
		char * filename;
		error_Check = 0;
		error_Check = check_Resource(connfd, *lines, &filename);
		if(error_Check<0){
			fprintf(stderr, "Error at get resource\n");
			// return -1;
			continue;
		}
		
		/* Try to read in the resource requested */
		char * resource_Requested;
		long int size = 0;
		size = read_In_Resource(connfd, filename, &resource_Requested);
		if(size<0){
			// An error has occurred
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
		// Check if response was good
		if(error_Check<0){
			fprintf(stderr, "Error replying\n");
			// return -1;
			continue;
		}
	}
	close(fd);
	return 0;
}

// void sigproc(){
// 	// signal(SIGINT, sigproc); /*  */
// 	//  NOTE some versions of UNIX will reset signal to default
// 	// after each call. So for portability reset signal each time 
	
// 	printf(" Interrupt sent\n");	
// 	close(fd);
// 	signal(SIGINT, SIG_DFL);
// }

/* This function will split a request into based on the assumption the terminator symbol is the delimiter twice.*/
int split_HTTP_Request(int fd, char * http_Request, char *delimiter, char ***  lines){
	static int times_Ran = 0;
	times_Ran++;
	printf("%i\n", times_Ran);
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
	
	// printf("Before loop\n");
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
		// printf("Loops - length: %li\n", ((cursor-http_Request) - length_Of_Request));
		cursor++;
	}
	// printf("After loop\n");

	// This was too presumptuous of good data always coming from a HTTP request (Looking at you Chrome)
	// if(length_Of_Request-(cursor-http_Request) != (length_Of_Delimiter*2)){
	// 	return -1;
	// }
	
	num_Of_Lines++;
	*lines = malloc(num_Of_Lines * sizeof(void *));
	// Fail to malloc
	if(*lines == NULL){
		fprintf(stderr, "Error allocating space for lines array\n");
		free(*lines);
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

/* Checks the validity of the request before returning the requested resources name */
int check_Resource(int fd, char * httpRequest, char ** addr){
	int num = 0;
	char * request = malloc(4);
	// RESOURCE_NAME_BUFFER is the max size of resource name the server can be requested
	char * resource = malloc(RESOURCE_NAME_BUFFER);
	char * protocol = malloc(9);
	if(request==NULL || resource==NULL || protocol==NULL){
		fprintf(stderr, "Error allocating temporary space for splitting GET request\n");
		respond_500(fd);
		return -1;
	}
	num = sscanf(httpRequest, "%s /%s %s", request, resource, protocol);
	
	free(protocol);
	// If num is less than 3, an error has occurred
	if(num<3){
		fprintf(stderr, "Error scanning request. Resource name could be too large (MAX: %i)\n", RESOURCE_NAME_BUFFER);
		free(request);
		free(resource);
		respond_500(fd);
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
	
	/* Attempt to open file */
	file = fopen(filename, "rb");
	if (file == NULL) {
	  fprintf(stderr, "Can't find requested file: %s\n", filename);
	  respond_404(fd);
	  return -1;
	}
	
	/* Get the size of the file and allocate the space for it */
	if(fseek(file, 0, SEEK_END)){
	  fprintf(stderr, "Error seeking to end\n");
	  fclose(file);
	  respond_500(fd);
	  return -1;
	}
	size = ftell(file);
	if(size<0){
		fprintf(stderr, "Error getting size\n");
		fclose(file);
		respond_500(fd);
		return -1;
	}
	rewind(file);
	
	*variable_To_Fill_With_Resource =  malloc(size * sizeof(char));
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
	int error_Check = 0;
	
	/* Length of "Content length: " is 17
	 Count is the num of digits in size
	 "\r\n\r\n" is of size 4 */
	char * content_Length = malloc((17 + count + 4) * sizeof(char));
	// Malloc check
	if(content_Length == NULL){
		fprintf(stderr, "Error allocating space for Content-Length field of response\n");
		respond_500(fd);
		return -1;
	}
	
	error_Check = sprintf(content_Length, "Content-Length: %i\r\n\r\n", size);
	if(error_Check<0){
		fprintf(stderr, "Error at sprintf\n");
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
	                  strlen(content_Length) + size);

	// Malloc check
	if(response==NULL){
		fprintf(stderr, "Error allocating space for full response. Requested content probably too big\n");
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
	if (error_Check == -1) {
		// An error has occurred
		fprintf(stderr, "Full response to client: \n");
		fprintf(stderr, "%s\n", response);
		free(response);
		fprintf(stderr, "Error writing response: 200\n");
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

