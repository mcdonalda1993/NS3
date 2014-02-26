#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <signal.h>
#include <unistd.h>
#include <ctype.h>

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

// void sigproc(void);
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

// int fd;

/* Things to ask about in the labs */
/* Am I allowed to fprintf any errors? */
/* Is the printf of "Server running" ok leave in? */
/* Is localhost a valid domain name? */

/* There is an issue where, every so often, the browser will seem to send the request
   however will just sit loading constantly. I have tried tracing the problem but can only
   determine that it must be some error with accepting the request because although the browser
   appears to have sent one, my program does not appear to receive it. All the use has to do to
   remedy the situation is to resend the request. That usually works. I should also point out that
   this issue had surfaced in the single threaded version. Unknown if it occurs in the multi-threaded
   version */
 /* Try:  https://ngrok.com/
    to debug later */

/* Need to:
-Multithreading */

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

	// signal(SIGINT, sigproc);

	int connfd;
	
	while(1){
		
		int error_Check = 0;
		
		/* Accept a connection	*/
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
		}else{
			fprintf(stderr, "Connection accepted: %i\n", connfd);
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
		}else{
			fprintf(stderr, "Message: %s\n", buf);
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
		
		/* Check the hostname field of the request is valid */
		error_Check = 0;
		error_Check = check_Hostname(connfd, lines);
		if(error_Check<0){
			fprintf(stderr, "Error invalid hostname detected\n");
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

/* Looks for a hostname, then checks if it matches current hostname */
int check_Hostname(int fd, char ** httpRequest){
	char ** currentLine;
	int found = 0;
	int error_Check = 0;
	
	/* Find if the host name has been given in the request */
	for(currentLine = httpRequest; *currentLine && !found; currentLine++){
		
		/* Convert line to lowercase */
		// Make a copy of the current line to preserve the message from being altered
		char * copy = malloc(strlen(*currentLine));
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
	
	/* Check if it is a localhost domain */
	char * local = malloc(strlen("localhost") + strlen(sPort));
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
	free(hostname);
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
	                  strlen(content_Length) + size);

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

