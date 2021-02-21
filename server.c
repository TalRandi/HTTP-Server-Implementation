#include "threadpool.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h> 
#include <sys/types.h> 
#include <sys/stat.h> 
#include <string.h>
#include <unistd.h> 
#include <netinet/in.h>
#include <time.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>  


#define BUFLEN 4000
#define BASIC_RESPONSE_SIZE 98
#define BASIC_DIRECTORY_SIZE 250
#define MAXIMUM_ENTITY_SIZE 500

#define PROTOCOL "HTTP/1.1 "
#define SERVER "Server: webserver/1.0\r\n"
#define DATE "Date: "
#define CONTENT_TYPE "Content-Type: text/html\r\n"
#define CONNECTION_CLOSE "Connection: close\r\n\r\n"
#define LOCATION_FOUND "Location:"

#define BAD_REQUEST_CONTENT_LENGTH "Content-Length: 113\r\n"
#define FOUND_CONTENT_LENGTH "Content-Length: 123\r\n"
#define NOT_FOUND_CONTENT_LENGTH "Content-Length: 112\r\n"
#define NOT_SUPPORTED_CONTENT_LENGTH "Content-Length: 129\r\n"
#define INTERNAL_SERVER_ERROR_CONTENT_LENGTH "Content-Length: 144\r\n"
#define FORBIDDEN_LENGTH "Content-Length: 111\r\n"

#define BAD_REQUEST "<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD><BODY><H4>400 Bad request</H4>Bad Request.</BODY></HTML>"
#define NOT_SUPPORTED "<HTML><HEAD><TITLE>501 Not supported</TITLE></HEAD><BODY><H4>501 Not supported</H4>Method is not supported.</BODY></HTML>"
#define INTERNAL_SERVER_ERROR "<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD><BODY><H4>500 Internal Server Error</H4>Some server side error.</BODY></HTML>"
#define FOUND "<HTML><HEAD><TITLE>302 Found</TITLE></HEAD><BODY><H4>302 Found</H4>Directories must end with a slash.</BODY></HTML>"
#define NOT_FOUND "<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD><BODY><H4>404 Not Found</H4>File not found.</BODY></HTML>"
#define FORBIDDEN "<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD><BODY><H4>403 Forbidden</H4>Access denied.</BODY></HTML>"


#define DIRECTORY_PART1 "<HTML><HEAD><TITLE>Index of "
#define DIRECTORY_PART2 "</TITLE></HEAD><BODY><H4>Index of "
#define DIRECTORY_PART3 "</H4><table CELLSPACING=8><tr><th>Name</th><th>Last Modified</th><th>Size</th></tr>"
#define DIRECTORY_DOWN "</table><HR><ADDRESS>webserver/1.0</ADDRESS></BODY></HTML>"

#define RFC1123FMT "%a, %d %b %Y %H:%M:%S GMT"

int handle_request(void* );
void handle_errors(int , int , int , int, int, int, int, char*);
void handle_proper_requests(char*, char*, char*, char* , int );
void dir_content(char* , char* , char* , int );


//Private function gets a file name and return it's content type
char *get_mime_type(char *name)
{
    char *ext = strrchr(name, '.');
    if (!ext) return NULL;
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) return "text/html";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".gif") == 0) return "image/gif";
    if (strcmp(ext, ".png") == 0) return "image/png";   
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".au") == 0) return "audio/basic";
    if (strcmp(ext, ".wav") == 0) return "audio/wav";
    if (strcmp(ext, ".avi") == 0) return "video/x-msvideo";
    if (strcmp(ext, ".mpeg") == 0 || strcmp(ext, ".mpg") == 0) return "video/mpeg";
    if (strcmp(ext, ".mp3") == 0) return "audio/mpeg";
    return NULL;
}
//Private function that retuens the number of digits in a number
int get_digits_number(int number)
{
    int result = 0;
    while(number > 0)
    {
        number /= 10;
        result++;
    }
    return result;
}

int main(int argc,char* argv[])
{
    if(argc != 4)
    {
        printf("Usage: server <port> <pool-size> <max-number-of-request>\n");
        exit(EXIT_FAILURE);
    }
    if(atoi(argv[3]) < 0 || atoi(argv[1]) <= 0)
    {
        printf("Usage: server <port> <pool-size> <max-number-of-request>\n");
        exit(EXIT_FAILURE);        
    }

    int welcome_socket_fd, newsock_fd;
    struct sockaddr_in server_address;


    //Defines a server and it's welcome socket
    welcome_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(welcome_socket_fd < 0)
    {
        perror("socket failure");
        exit(EXIT_FAILURE);
    }

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(atoi(argv[1]));

    if(bind(welcome_socket_fd,(struct sockaddr *)&server_address,sizeof(server_address)) < 0)
    {
        perror("Error on binding");
        exit(EXIT_FAILURE);
    }
    listen(welcome_socket_fd, 5);

    int* clients_id = (int*)calloc(atoi(argv[3]),sizeof(int));
    if(!clients_id)
    {
        perror("Calloc failed");
        exit(EXIT_FAILURE);
    }
    //Create new threadpool
    threadpool* t = create_threadpool(atoi(argv[2]));
    if(!t)
    {
        free(clients_id);
        exit(EXIT_FAILURE);
    }


    //Accept each client and send a thread to serv him
    for(int i = 0 ; i < atoi(argv[3]) ; i++)
    {
        newsock_fd = accept(welcome_socket_fd, NULL, NULL);
        if(newsock_fd < 0)
            perror("Error on accept");

        clients_id[i] = newsock_fd;
        dispatch(t,handle_request,&clients_id[i]);
    }

    close(welcome_socket_fd);
    destroy_threadpool(t);
    free(clients_id);
    return 0;
}
//This function handle a client request 
int handle_request(void* new_socket)
{
    char buffer[BUFLEN];
    int cli_sock = *(int*)new_socket;
    struct stat fs;
    struct stat fs1;
    struct stat fs2;
    int not_supported_flag = 0; //501
    int bad_request_flag = 0; // 400
    int not_found_flag = 0; // 404
    int found_flag = 0; // 302
    int forbidden_flag = 0; //403
    int internal_server_error_flag = 0; //500
    int i = 0;
    char* path = 0;
    char* my_path = 0;
    char* content_type = 0;
    char* content_length = 0;
    unsigned char* body = 0;
    DIR* dir = 0;
    DIR* dir1 = 0;
    struct dirent* dentry = 0;
    int read_fd = 0;

    read_fd = read(cli_sock,buffer,BUFLEN);

    //Read failed
    if(read_fd < 0)
        internal_server_error_flag = 1;

    //Empty request
    else if(read_fd == 0)
    {
        handle_errors(1,0,0,0,0,0,cli_sock,"");
        close(cli_sock);
        return 0;
    }
    //Read some request
    else
    {
        i = 0; 
        while(buffer[i] != '\r' && i < BUFLEN)
            i++;

        buffer[i] = '\0';
        
        //Returns first token 
        char* token = strtok(buffer, " ");

        if(!token)
            bad_request_flag = 1; 

        //Checks the request                                                                                      
        else
        {
            i = 0;
            if(strcmp(token,"GET") != 0)
                not_supported_flag = 1;
                
            if(bad_request_flag == 0 && internal_server_error_flag == 0)
            {
                while(token) 
                { 
                    if(i == 1)
                        path = token;
                    
                    if(i == 2)
                    {
                        if(strncmp(token,"HTTP/1.1",8) != 0 && strncmp(token,"HTTP/1.0",8) != 0)
                            bad_request_flag = 1;
                    }             
                    token = strtok(NULL, " "); 
                    i++;
                }
            }
            if(i != 3)
                bad_request_flag = 1;   
        }
    }
    //Some input error
    if(not_supported_flag == 1 || bad_request_flag == 1 || internal_server_error_flag == 1)
    {
        handle_errors(bad_request_flag,not_supported_flag,internal_server_error_flag,found_flag,not_found_flag,forbidden_flag,cli_sock,path);
        close(cli_sock);
        return 0;
    }
    //No input errors
    else
    {
        my_path = (char*)calloc(strlen(path) + 2, sizeof(char));
        strcat(my_path,".");
        strcat(my_path,path);
        my_path[strlen(my_path)] = '\0';

        dir = opendir(my_path);
        //dir is not a directory or have no permmisions
        if(!dir)
        {
            char* temp = (char*)calloc(strlen(my_path) + 1,sizeof(char));
            
            int i = 0 ;
            while(i < strlen(my_path))
            {
                while(my_path[i] != '/' && i < strlen(my_path))
                {
                    temp[i] = my_path[i];
                    i++;
                }
                if(stat(temp,&fs2) == -1)
                {
                    not_found_flag = 1;
                    break;
                }
                if(S_ISREG(fs2.st_mode))
                { 
                    if((fs2.st_mode & S_IROTH)){}
                    else
                    {
                        forbidden_flag = 1;
                        break;
                    }
                }
                else if(S_ISDIR(fs2.st_mode))
                {
                    if(fs2.st_mode & S_IXOTH ){}
                    else
                    {
                        forbidden_flag = 1;
                        break;
                    }
                }    
                temp[i] = '/';
                i++;
            }
            //File not found or 403 forbidden error
            if(not_found_flag == 1 || forbidden_flag == 1)
                handle_errors(bad_request_flag, not_supported_flag, internal_server_error_flag, found_flag, not_found_flag, forbidden_flag, cli_sock, path);
           
            //ptr is a file
            else
            {
                //return the file
                stat(my_path,&fs);
                int fd = open(my_path, O_RDONLY);
                int size = (int)fs.st_size;
                // time_t now1;
                char timebuf[128];
                // now1 = time(NULL);
                body = (unsigned char*)calloc(size + 1,sizeof(unsigned char));
                    
                read(fd,body,size);

                content_type = get_mime_type(my_path);
                content_length = (char*)calloc(get_digits_number(size) + 1,sizeof(char));
                sprintf(content_length, "%d", size);
                content_length[get_digits_number(size)] = '\0';

                strftime(timebuf, sizeof(timebuf), RFC1123FMT, localtime(&fs.st_mtime));
                handle_proper_requests("",content_type,content_length,timebuf,cli_sock);
                write(cli_sock,body,size);
                
                free(body);
                free(content_length);    
            }
            free(my_path);
            free(temp);
            close(cli_sock);
            return 0;
        }
        //Directory
        else
        {
            //302
            if(my_path[strlen(my_path) - 1] != '/')
                found_flag = 1;
        
            //Case of dir content or index.html file
            else
            { 
                // time_t now;
                char timebuf[128];
                char dir_last_modified[128];
                // now = time(NULL);   
                stat(my_path,&fs);
                strftime(dir_last_modified, sizeof(dir_last_modified), RFC1123FMT, localtime(&fs.st_mtime));
                
                char* index_html = (char*)calloc(strlen(my_path)+strlen("index.html")+1,sizeof(char));
                strcat(index_html,my_path);
                strcat(index_html,"index.html");
                index_html[strlen(my_path)+strlen("index.html")] = '\0';
                
                //Index.html exists
                if(access(index_html, F_OK) == 0) 
                {
                    stat(index_html,&fs);
                    int fd = open(index_html, O_RDONLY);
                    int size = (int)fs.st_size;
                    body = (unsigned char*)calloc(size + 1,sizeof(char));
                        
                    read(fd,body,size);
                    body[size] = '\0';

                    content_type = "text/html";
                    content_length = (char*)calloc(get_digits_number(size) + 1,sizeof(char));
                    sprintf(content_length, "%d", size);
                    content_length[get_digits_number(size)] = '\0';

                    strftime(timebuf, sizeof(timebuf), RFC1123FMT, localtime(&fs.st_mtime));
                    handle_proper_requests(body,content_type,content_length,timebuf,cli_sock);
                    
                    free(body);
                    free(content_length);
                } 
                //returns the directory content
                else 
                {
                    char* directory_content = 0;
                    int directory_content_size = BASIC_DIRECTORY_SIZE;
                    char* directory_content_size_string = 0;
                    int file_size_integer = 0;

                    while((dentry = readdir(dir)))
                        directory_content_size += MAXIMUM_ENTITY_SIZE;
                          
                    directory_content = (char*)calloc(directory_content_size,sizeof(char));
                    
                    strcat(directory_content,DIRECTORY_PART1);
                    strcat(directory_content,my_path);
                    strcat(directory_content,DIRECTORY_PART2);
                    strcat(directory_content,my_path);
                    strcat(directory_content,DIRECTORY_PART3);
                
                    dir1 = opendir(my_path);
                    while((dentry = readdir(dir1)))
                    {
                        char* buf = (char*)calloc(strlen(my_path)+strlen(dentry->d_name) + 1,sizeof(char));
                        strcat(buf,my_path);
                        strcat(buf,dentry->d_name);

                        stat(buf,&fs1);

                        if(dentry->d_type == DT_REG)
                            file_size_integer = (int)fs1.st_size;

                        strftime(timebuf, sizeof(timebuf), RFC1123FMT, localtime(&fs1.st_mtime));
                        strcat(directory_content,"<tr><td><A HREF= \"");
                        strcat(directory_content,dentry->d_name);
                        strcat(directory_content,"\">");
                        strcat(directory_content,dentry->d_name);
                        strcat(directory_content,"</A></td><td>");
                        strcat(directory_content,timebuf);
                        strcat(directory_content,"</td>");

                        if(dentry->d_type == DT_REG)
                        {
                            strcat(directory_content,"<td>");
                            char* file_size_string = (char*)calloc(get_digits_number(file_size_integer) + 1,sizeof(char));
                            sprintf(file_size_string, "%d", file_size_integer);
                            file_size_string[get_digits_number(file_size_integer)] = '\0';
                            strcat(directory_content,file_size_string);
                            strcat(directory_content,"</td>");
                            free(file_size_string);
                        }                 
                        strcat(directory_content,"</tr>");
                        free(buf);
                    }
                    strcat(directory_content,DIRECTORY_DOWN);
                    directory_content_size_string = (char*)calloc(get_digits_number(directory_content_size) + 1,sizeof(char));
                    sprintf(directory_content_size_string, "%d", directory_content_size);
                    directory_content_size_string[get_digits_number(directory_content_size)] = '\0';
                    dir_content(directory_content,directory_content_size_string,dir_last_modified,cli_sock);
                    closedir(dir1);
                    free(directory_content_size_string);
                    free(directory_content);
                }
                free(index_html);
                free(my_path);
                close(cli_sock);
                closedir(dir);
                return 0;
            }
        }
        closedir(dir);
        free(my_path);
    }
    //handle errors
    if(bad_request_flag == 1 || not_supported_flag == 1 || internal_server_error_flag == 1 || found_flag == 1 || not_found_flag == 1)
        handle_errors(bad_request_flag, not_supported_flag, internal_server_error_flag, found_flag, not_found_flag, forbidden_flag, cli_sock, path);
    
    // free(path);
    close(cli_sock);
    return 0;
}
//This function gets all errors flag and display the relevant error to user.
void handle_errors(int bad_request_flag, int not_supported_flag, int internal_server_error_flag, int found_flag, int not_found_flag,int forbidden_flag, int fd, char* path)
{
    char* response = 0;
    int response_size = BASIC_RESPONSE_SIZE;
    time_t now;
    char timebuf[128];
    now = time(NULL);
    char* code_and_desc = 0;
    char* content_length = 0;
    char* body = 0;

    strftime(timebuf, sizeof(timebuf), RFC1123FMT, localtime(&now));

    //400 bad request
    if(bad_request_flag == 1)
    {
        response_size += strlen(BAD_REQUEST) + strlen("400 Bad Request\r\n")+ strlen(timebuf) + 300;
        code_and_desc = "400 Bad Request\r\n";
        content_length = BAD_REQUEST_CONTENT_LENGTH;
        body = BAD_REQUEST;
    }
    //501 not supported
    else if(not_supported_flag == 1)
    {
        response_size += strlen(NOT_SUPPORTED) + strlen("501 Not supported\r\n")+ strlen(timebuf) + 300;
        code_and_desc = "501 Not supported\r\n";
        content_length = NOT_SUPPORTED_CONTENT_LENGTH;
        body = NOT_SUPPORTED;
    }
    //500 internal server error
    else if(internal_server_error_flag == 1)
    {
        response_size += strlen(INTERNAL_SERVER_ERROR) + strlen("500 Internal Server Error\r\n")+ strlen(timebuf) + 300;
        code_and_desc = "500 Internal Server Error\r\n";
        content_length = INTERNAL_SERVER_ERROR_CONTENT_LENGTH;
        body = INTERNAL_SERVER_ERROR;
    }
    //302 Found
    else if(found_flag == 1)
    {
        response_size += strlen(FOUND) + strlen("302 Found\r\n")+ strlen(timebuf) + 300;
        code_and_desc = "302 Found\r\n";
        content_length = FOUND_CONTENT_LENGTH;
        body = FOUND;
    }
    //404 Not found
    else if(not_found_flag == 1)
    {
        response_size += strlen(NOT_FOUND) + strlen("404 Not Found\r\n")+ strlen(timebuf) + 300;
        code_and_desc = "404 Not Found\r\n";
        content_length = NOT_FOUND_CONTENT_LENGTH;
        body = NOT_FOUND;        
    }
    //403 Forbidden
    else if(forbidden_flag == 1)
    {
        response_size += strlen(FORBIDDEN) + strlen("403 Forbidden\r\n")+ strlen(timebuf) + 300;
        code_and_desc = "403 Forbidden\r\n";
        content_length = FORBIDDEN_LENGTH;
        body = FORBIDDEN;        
    }
    
    response = (char*)calloc(response_size,sizeof(char));
    strcat(response,PROTOCOL);
    strcat(response,code_and_desc);
    strcat(response,SERVER);
    strcat(response,DATE);
    strcat(response,timebuf);
    strcat(response,"\r\n");

    if(found_flag == 1)
    {
        strcat(response,LOCATION_FOUND);
        strcat(response,path);
        strcat(response,"/\r\n");
    }

    strcat(response,CONTENT_TYPE);
    strcat(response,content_length);
    strcat(response,CONNECTION_CLOSE);
    strcat(response,body);
    
    send(fd, response, strlen(response), 0);

    free(response);
}
//This function handle a '200 ok' requests
void handle_proper_requests(char* body, char* content_type, char* content_length, char* last_modified, int fd)
{
    char* response = 0;
    int response_size = BASIC_RESPONSE_SIZE;
    time_t now;
    char timebuf[128];
    now = time(NULL);

    strftime(timebuf, sizeof(timebuf), RFC1123FMT, localtime(&now));

    response_size += strlen(body) + strlen("200 OK\r\n")+ strlen(timebuf) + 300;
    response = (char*)calloc(response_size,sizeof(char));
    strcat(response,PROTOCOL);
    strcat(response,"200 OK\r\n");
    strcat(response,SERVER);
    strcat(response,DATE);
    strcat(response,timebuf);
    strcat(response,"\r\n");
    if(content_type)
    {
        strcat(response,"Content-Type: ");
        strcat(response,content_type);
        strcat(response,"\r\n");
    }
    strcat(response,"Content-Length: ");
    strcat(response,content_length);
    strcat(response,"\r\n");
    strcat(response,"Last-Modified: ");
    strcat(response,last_modified);
    strcat(response,"\r\n");
    strcat(response,CONNECTION_CLOSE);
    strcat(response,body);

    send(fd, response, strlen(response), 0);

    free(response);
}
//This function returns the file as in list format
void dir_content(char* body, char* content_length, char* last_modified, int fd)
{   

    char* response = 0;
    int response_size = BASIC_RESPONSE_SIZE;
    time_t now;
    char timebuf[128];
    now = time(NULL);

    strftime(timebuf, sizeof(timebuf), RFC1123FMT, localtime(&now));

    response_size += strlen(body) + strlen("200 OK\r\n")+ strlen(timebuf) + 300;
    response = (char*)calloc(response_size,sizeof(char));
    strcat(response,PROTOCOL);
    strcat(response,"200 OK\r\n");
    strcat(response,SERVER);
    strcat(response,DATE);
    strcat(response,timebuf);
    strcat(response,"\r\n");
    strcat(response,CONTENT_TYPE);
    strcat(response,"Content-Length: ");
    strcat(response,content_length);
    strcat(response,"\r\n");
    strcat(response,"Last-Modified: ");
    strcat(response,last_modified);
    strcat(response,"\r\n");
    strcat(response,CONNECTION_CLOSE);
    strcat(response,body);

    send(fd, response, strlen(response), 0);

    free(response);  
}

