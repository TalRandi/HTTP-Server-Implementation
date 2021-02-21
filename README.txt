
Exercise Name: EX3 – HTTP Server

Files:

server.c 	   :	 This c file implements the server communication with multiple clients, it gets an HTTP request 
			 from the client and returns a appropriate response.
threadpool.c       :     This file implements the header threadpool file. it let's the server the ability to serve multiple requests,
			 from multiple clients. 
README 		   :	 This file

Remarks: 

This program implements an HTTP server, that gets requests from many clients and send them back a appropriate response. 
This program get the HTTP request from the client and takes only the first request part.


The server could send some error responses to the client in cases of wrong request,
Or try to get some blocked or no-access allowed file requests.
The server allow to clients to show directory contents, and navigate in these folders.
Response returns to clients after few steps - client get a soket, server read the request, and then the server sent the response to user.

Note -	If the client is a browser and it sent the path of directory without '/' at the last char, usually the browser complete the request,
	But if the the user use telnet as client - this case will be 302 found error. 



There are four private function:

handle_request - the main function that handle the client request.
handle_errors - inner function, called by handle_request and handle all error cases.
handle_proper_requests - inner function, called by handle_request and returns the 200 ok case response to client.
dir_content - this function build the directory content of some folder.


