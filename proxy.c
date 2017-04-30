/*
 * proxy.c - CS:APP Web proxy
 *
 * TEAM MEMBERS:  put your name(s) and e-mail addresses here
 *     Howard the Duck, howie@duck.sewanee.edu
 *     James Q. Pleebus, pleebles@q.sewanee.edu
 *
 * IMPORTANT: Give a high level description of your code here. You
 * must also provide a header comment at the beginning of each
 * function that describes what that function does.
 */

#include "csapp.h"
#include "string.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

struct reqData {
  ssize_t len;
  int ishtml;
};

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3";

/*
 * Function prototypes
 */
int parse_uri(char *uri, char *target_addr, char *path, char  *port);
void format_log_entry(char *logstring, char* ipstr, char *uri, int size);
int send_data(rio_t rios, int fd, int clientfd, char *newRequest);
int startsWith(const char *pre, const char *str);
void fetch(int fd,  char* ipstr)  ;
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

/*
 * main - Main routine for the proxy program
 */
int main(int argc, char **argv)
{
  int listenfd, connfd;
  socklen_t clientlen;
  char hostname[MAXLINE], port[MAXLINE];
    
  struct sockaddr_storage clientaddr;/* Enough space for any address */

  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(0);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);

    connfd = Accept(listenfd, (SA*)&clientaddr, &clientlen);
    Getnameinfo((SA*)&clientaddr, clientlen, hostname, MAXLINE,
                      port, MAXLINE, NI_NUMERICHOST);
    fetch(connfd, hostname);
    Close(connfd);
  }
}


int startsWith(const char *pre, const char *str)
{
  size_t lenpre = strlen(pre),
    lenstr = strlen(str);
  return lenstr < lenpre ? 0 : strncmp(pre, str, lenpre) == 0;
}

/*
 * send_header sends the header info to the client and returns the package size
 */
int send_data(rio_t rios, int fd, int clientfd, char *newRequest)
{
  struct reqData *data = (struct reqData *)malloc(sizeof(struct reqData));
  char content[MAXLINE];
  ssize_t len = 0;

  data->ishtml = 0;

  int bytesRead = 0;
    
  while (rio_readlineb(&rios, content, MAXLINE)) {
    rio_writen(fd, content, strlen(content)); //send it to client
    if(startsWith("Content-Type: text/html",content)){
      data->ishtml = 1;
    }
    if(startsWith("Content-Length:",content)){
      char *tmp = strchr(content,' ');
      data->len = atoi(tmp);
      bytesRead += data->len; //bytes for text
    }
    if(strcmp(content,"\r\n")==0)
      break;
  }

  if(data->ishtml){
    while (rio_readlineb(&rios, content, MAXLINE))
      rio_writen(fd, content, strlen(content)); //send it to client
  }
  else{
    while (rio_readnb(&rios, content, MAXLINE) && data->len > 0) {
      ssize_t tmp = rio_writen(fd, content, MAXLINE); //send it to client
      len -= tmp;
      bytesRead += tmp; //bytes for binary data
    }
  }

  return bytesRead;
}

void fetch(int fd, char* ipaddr){
    FILE* logFile;
    char* logstring = (char*)malloc(sizeof(char)*MAXLINE);
    
    //struct stat sbuf;
    char request[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], pathname[MAXLINE];
    //int port;
    char* port = (char*)malloc(sizeof(char)*20);
    rio_t rioc; //for client
    int clientfd; //for this proxy to connect to web server

    /* Read request line and headers */
    rio_readinitb(&rioc, fd);
    if (!rio_readlineb(&rioc, request, MAXLINE)) //read request
        return;

    //printf("%s", request); // buf holds HTTP request

    sscanf(request, "%s %s %s", method, uri, version);   //parsing request
    if (strcasecmp(method, "GET")) {                 //checks method
        clienterror(fd, method, "501", "Not Implemented","Proxy does not support this request");
        return;
    }
    //read_requesthdrs(&rio);

    int stat = parse_uri(uri,hostname,pathname,port); //get hostname and pathname from uri
    if(stat!=0){ //returns -1 if problem
        clienterror(fd, uri, "505", "??????",".....");
        return;
    }

    //printf("%s\n", request)

    char newRequest[MAXBUF];
    char *v = "HTTP/1.0";

    sprintf(newRequest,"%s /%s %s\r\n"
                       "Host: %s\r\n"
                       "User-Agent: %s\r\n"
                       "Connection: close\r\n"
                       "Proxy-Connection:close\r\n\r\n",
                       method,pathname,v,hostname,user_agent_hdr);

    //now need to make connection with web server
    clientfd = open_clientfd(hostname, port);

    rio_t rios;
    rio_writen(clientfd, newRequest, strlen(newRequest)); //send request
    rio_readinitb(&rios, clientfd);

    int bytes = send_data(rios,fd,clientfd,newRequest);  //need to return num of bytes read

    //logging file
    if((logFile = fopen("proxy.log","aw"))== NULL){
        printf("Cannot open log file\n");
    }
    format_log_entry(logstring, ipaddr, hostname, bytes);
    fprintf(logFile, "%s", logstring);
    fclose(logFile);
    
    free(logstring);      
    free(port);
    Close(clientfd);
}

/*
 * parse_uri - URI parser
 *
 * Given a URI from an HTTP proxy GET request (i.e., a URL), extract
 * the host name, path name, and port.  The memory for hostname and
 * pathname must already be allocated and should be at least MAXLINE
 * bytes. Return -1 if there are any problems.
 */
int parse_uri(char *uri, char *hostname, char *pathname, char *port)
{
    char *hostbegin;
    char *hostend;
    char *pathbegin;
    int len;

    if (strncasecmp(uri, "http://", 7) != 0) {
        hostname[0] = '\0';
        return -1;
    }

    /* Extract the host name */
    hostbegin = uri + 7;
    hostend = strpbrk(hostbegin, " :/\r\n\0");
    len = hostend - hostbegin;
    strncpy(hostname, hostbegin, len);
    hostname[len] = '\0';

    /* Extract the port number */
    if (*hostend == ':')
        strcpy(port,(hostend + 1));
    else
        strcpy(port,"80");/* default */

    /* Extract the path */
    pathbegin = strchr(hostbegin, '/');
    if (pathbegin == NULL) {
        pathname[0] = '\0';
    }
    else {
        pathbegin++;
        strcpy(pathname, pathbegin);
    }


    return 0;
}

/*
 * format_log_entry - Create a formatted log entry in logstring. 
 * 
 * The inputs are the socket address of the requesting client
 * (sockaddr), the URI from the request (uri), and the size in bytes
 * of the response from the server (size).
 */
void format_log_entry(char *logstring, char* ipaddr, char *uri, int size)
{
    time_t now;
    char time_str[MAXLINE];
    //unsigned long host;
    //unsigned char a, b, c, d;
    //struct in_addr inaddr;
    //char addr[MAXBUF];

    /* Get a formatted time string */
    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    //storing the formatted log entry string in logstring
    sprintf(logstring, "%s %s %s %d\n", time_str, ipaddr, uri, size);
}

/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg)
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""9b4949"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p style=""color:red;font-size:50"">%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
/* $end clienterror */
