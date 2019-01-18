/*
** selectserver.c -- a cheezy multiperson chat server
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <fcntl.h>
#include <errno.h>
#include <math.h>

#define PORT "9036"   // port we're listening on
#define N 2457600

typedef struct {
	int 	var_code;
	int		sample_time;
	int 	hr;
	float	hs;
	float	gsr;

} phy_data;
			
void SendHeartDataToPipe (int , char*);

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(int argc, char *argv[])
{
    fd_set master;    // master file descriptor list
    fd_set read_fds;  // temp file descriptor list for select()
	fd_set write_fds;  // temp file descriptor list for select()
    int fdmax;        // maximum file descriptor number

    int listener;     // listening socket descriptor
    int newfd;        // newly accept()ed socket descriptor
    struct sockaddr_storage remoteaddr; // client address
    socklen_t addrlen;

    char buf[256];    // buffer for client data
	char buf_test[256] = "60;\n";    // buffer for client data
    int nbytes;

	char remoteIP[INET6_ADDRSTRLEN];

    int yes=1;        // for setsockopt() SO_REUSEADDR, below
    int i, j, rv;

	struct addrinfo hints, *ai, *p;

	int 	pfds[2];		// Pipe file desc
    char 	pipebuf[30];	// Buffer for a pipe

    pipe(pfds);				// Create a comm pipe w/ desc
	fcntl(pfds[0], F_SETFL, O_NONBLOCK);
	fcntl(pfds[1], F_SETFL, O_NONBLOCK);

    if (!fork()) {
		printf(" CHILD: writing to the pipe\n");
		SendHeartDataToPipe(pfds[1], argv[1]);
        printf(" CHILD: exiting\n");
		close(pfds[1]);
        exit(0);
    } else {
        printf("PARENT: reading from pipe\n");
		int n;
        ssize_t r = read(pfds[0], &n, sizeof(int));
		if (r == -1 && errno == EAGAIN)
			printf("no data yet\n");
		else if (r > 0)
			printf("PARENT: read %d\n", n);
		else
			printf("pipe closed\n");
    }
	
    FD_ZERO(&master);    // clear the master and temp sets
    FD_ZERO(&read_fds);
	FD_ZERO(&write_fds);

	// get us a socket and bind it
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	if ((rv = getaddrinfo(NULL, PORT, &hints, &ai)) != 0) {
		fprintf(stderr, "selectserver: %s\n", gai_strerror(rv));
		exit(1);
	}
	
	for(p = ai; p != NULL; p = p->ai_next) {
    	listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (listener < 0) { 
			continue;
		}
		fcntl(listener, F_SETFL, O_NONBLOCK);
		
		// lose the pesky "address already in use" error message
		setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

		if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
			close(listener);
			continue;
		}

		break;
	}

	// if we got here, it means we didn't get bound
	if (p == NULL) {
		fprintf(stderr, "selectserver: failed to bind\n");
		exit(2);
	}

	freeaddrinfo(ai); // all done with this

    // listen
    if (listen(listener, 10) == -1) {
        perror("listen");
        exit(3);
    }

    // add the listener to the master set
    FD_SET(listener, &master);

    // keep track of the biggest file descriptor
    fdmax = listener; // so far, it's this one

    // main loop
    for(;;) {
        read_fds = master; // copy it
		write_fds = master; // copy it
        if (select(fdmax+1, &read_fds, &write_fds, NULL, NULL) == -1) {
            perror("select");
            exit(4);
        }

        // run through the existing connections looking for data to read
        for(i = 0; i <= fdmax; i++) {
            if (FD_ISSET(i, &read_fds)) { // we got one!!
                if (i == listener) {
                    // handle new connections
                    addrlen = sizeof remoteaddr;
					newfd = accept(listener,
						(struct sockaddr *)&remoteaddr,
						&addrlen);

					if (newfd == -1) {
                        perror("accept");
                    } else {
                        FD_SET(newfd, &master); // add to master set
                        if (newfd > fdmax) {    // keep track of the max
                            fdmax = newfd;
                        }
                        printf("selectserver: new connection from %s on "
                            "socket %d\n",
							inet_ntop(remoteaddr.ss_family,
								get_in_addr((struct sockaddr*)&remoteaddr),
								remoteIP, INET6_ADDRSTRLEN),
							newfd);
                    }
                } else {
                    // handle data from a client
                    if ((nbytes = recv(i, buf, sizeof buf, 0)) <= 0) {
                        // got error or connection closed by client
                        if (nbytes == 0) {
                            // connection closed
                            printf("selectserver: socket %d hung up\n", i);
                        } else {
                            perror("recv");
                        }
                        close(i); // bye!
                        FD_CLR(i, &master); // remove from master set
                    }
                } // END handle data from client 
            } // END got new incoming connection
			
			phy_data data;
			ssize_t r = read(pfds[0], &data, sizeof(phy_data));
			if (r > 0) {
				//printf("t: %f HR: %d SIZE:%lu\n",data.hs, data.hr, r);
				// we got some data from a client
				for(j = 0; j <= fdmax; j++) {
					// send to everyone!
					if (FD_ISSET(j, &master)) {
						// except the listener and ourselves
						if (FD_ISSET(j, &write_fds)) { // we got one to send data to!!
							//snprintf(buf, 256, "%d;", data.hr);
							//printf("HR: %d HS:%f\n",data.hr, data.hs);
							data.sample_time = ntohl(data.sample_time);
							data.var_code = ntohl(data.var_code);
							data.hr = ntohl(data.hr);
							//data.hs = ntohl(data.hs);
							//data.gsr = ntohl(data.gsr);
							//printf("t: %d C: %d HR: %d HS:%f\n",data.sample_time, data.var_code, data.hr, data.hs);
							if (send(j, &data, sizeof(phy_data), MSG_DONTWAIT) == -1) {
								perror("send");
							}
						}
					}
				}
			}
        } // END looping through file descriptors
    } // END for(;;)--and you thought it would never end!
    
	close(pfds[0]);
    return 0;
}

void SendHeartDataToPipe (int pfd, char* filename) {

	// Heart Rate values
	int*		HR_val;
	float*		HR_val_times;
	int			HR_val_counter;

	// Heart Signal values
	float*		HS_val;
	float*		HS_val_times;
	int			HS_val_counter;
	
	// GSR Signal values
	float*		GSR_val;
	float*		GSR_val_times;
	int			GSR_val_counter;

	// File descriptor
	FILE *file;

	// Loop counter
	int i;

	HR_val = (int*)malloc(N * sizeof(int));
	HR_val_times = (float*)malloc(N * sizeof(float));
	HR_val_counter = 0;

	HS_val = (float*)malloc(N * sizeof(float));
	HS_val_times = (float*)malloc(N * sizeof(float));
	HS_val_counter = 0;
	
	GSR_val = (float*)malloc(N * sizeof(float));
	GSR_val_times = (float*)malloc(N * sizeof(float));
	GSR_val_counter = 0;

	file = fopen(filename, "r");
	if ( file )
	{
		char line [ BUFSIZ ];
		while ( fgets(line, sizeof line, file) )
		{
			char substr[32], *ptr = line;
			int n;
			char* pch;
			//fputs(line, stdout);

			// Time offset
			pch = strtok (line,",");
			//printf("%s\t", pch);
			HS_val_times[HS_val_counter] = atof(pch);

			// Heart Signal
			pch = strtok (NULL,",");
			//printf("%s\t", pch);
			HS_val[HS_val_counter] = atof(pch);

			// Heart rate
			pch = strtok (NULL,",");
			HR_val[HR_val_counter] = atoi(pch);
			HR_val_times[HR_val_counter] = HS_val_times[HS_val_counter];
			
			// GSR
			pch = strtok (NULL,",");
			GSR_val[GSR_val_counter] = atoi(pch);
			GSR_val_times[GSR_val_counter] = HS_val_times[HS_val_counter];
			
			HR_val_counter++;
			HS_val_counter++;
			GSR_val_counter++;
			
			/*//printf("%s\t", pch);
			if(atoi(pch) != HR_val[HR_val_counter-1]) {
				HR_val[HR_val_counter] = atoi(pch);
				HR_val_times[HR_val_counter] = HS_val_times[HS_val_counter];
				HR_val_counter++;
			}

			HS_val_counter++;*/
			
			//printf("\n", pch);
		}
	}
	else
	{
		perror(filename);
	}

	fclose(file);
	
	while (1) {
		for (i = 0; i < HR_val_counter - 1; i++) {
		
			int microsec = (HR_val_times[i + 1] - HR_val_times[i]) * 1000000; // length of time to sleep, in miliseconds
			
			phy_data data;
			data.var_code = 3;
			data.sample_time = microsec;
			data.hr = HR_val[i];
			data.hs = HS_val[i];
			data.gsr = GSR_val[i];
			
			//printf("t: %f HR: %d\n",HR_val_times[i], HR_val[i]);
			//write(pfd, &HR_val[i], sizeof(int));
			//printf("t: %f HR: %d SIZE:%lu\n",HR_val_times[i], data.hr, sizeof(phy_data));
			ssize_t r = write(pfd, &data, sizeof(phy_data));
			
			//printf("Sleeping for %f sec > %d microsec\n", HR_val_times[i + 1] - HR_val_times[i], microsec);
			struct timespec req;
			req.tv_sec = 0;
			req.tv_nsec = microsec * 1000L;
			//printf("Ready to sleep %lu\n", req.tv_nsec);
			nanosleep(&req, (struct timespec *)NULL);
		}
	}
}