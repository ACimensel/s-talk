#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "list.h"
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h> 

#define MSG_MAX_LEN 1024

static List* in_list;
static List* out_list;

static int loc_port;
static char* rem_name;
static struct hostent* rem_addr;
static int rem_port;

static struct sockaddr_in sin_local;
static struct sockaddr_in sin_remote;
static int socketDescriptor;

static pthread_mutex_t in_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t out_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wait_keyb_in = PTHREAD_COND_INITIALIZER;
static pthread_cond_t wait_netw_in = PTHREAD_COND_INITIALIZER;

static pthread_t network_in;
static pthread_t screen_out;
static pthread_t keyboard_in;
static pthread_t network_out;

static void* Msg_Recv();
static void* Msg_Print();
static void* Msg_Keyboard();
static void* Msg_Send();

int main(int argCount, char** args)
{
	// Save args in variables
	loc_port = atoi(args[1]);
	rem_name = args[2];
    rem_addr = gethostbyname(rem_name);
	rem_port = atoi(args[3]);

	// Get the remote address by name
    if (rem_addr == NULL) {
   		printf("Error getting remote address by name, value of errno = %d. Exiting the program..\n", errno);
		exit(1);
    }

	// Create lists
	in_list = List_create();
	out_list = List_create();

	// Set-up sockets
    memset(&sin_local, 0, sizeof(sin_local));
    sin_local.sin_family = AF_INET;						// Connection may be from network
	sin_local.sin_addr.s_addr = htonl(INADDR_ANY);    	// Host to Network long
	sin_local.sin_port = htons(loc_port);             	// Host to Network short

    memset(&sin_remote, 0, sizeof(sin_remote));
    sin_remote.sin_family = AF_INET;
    sin_remote.sin_addr = *(struct in_addr*)rem_addr->h_addr_list[0];
	sin_remote.sin_port = htons(rem_port);

	// Create the socket for UDP
	socketDescriptor = socket(PF_INET, SOCK_DGRAM, 0);
	// If socket was not created exit
	if(socketDescriptor == -1){
   		printf("Error creating socket, value of errno = %d. Exiting the program..\n", errno);
		exit(1);
	}
	// Bind the socket to the port (loc_port) that we specify, exit on failure
	if(bind (socketDescriptor, (struct sockaddr*) &sin_local, sizeof(sin_local)) == -1){
   		printf("Error binding socket, value of errno = %d. Exiting the program..\n", errno);
		exit(1);
	}

	// Intro message
	printf("You are listening on Port %d\n", loc_port);
	printf("Connecting to %s at Port %d\n", rem_name, rem_port);

	// Create threads
	// network_in receives remote messages and puts them in the (List*) in_list
    if(pthread_create(&network_in, NULL, Msg_Recv, NULL) != 0){
   		printf("Error creating network_in thread, value of errno = %d. Exiting the program..\n", errno);
		exit(1);
	}
	// screen_out prints messages in the (List*) in_list
    if(pthread_create(&screen_out, NULL, Msg_Print, NULL) != 0){
   		printf("Error creating screen_out thread, value of errno = %d. Exiting the program..\n", errno);
		exit(1);
	}
	// keyboard_in takes in strings from the user keyboard and puts them in the (List*) out_list
    if(pthread_create(&keyboard_in, NULL, Msg_Keyboard, NULL) != 0){
   		printf("Error creating keyboard_in thread, value of errno = %d. Exiting the program..\n", errno);
		exit(1);
	}
	// network_out sends strings in the (List*) out_list to the specified Address and Port of client
    if(pthread_create(&network_out, NULL, Msg_Send, NULL) != 0){
   		printf("Error creating network_out thread, value of errno = %d. Exiting the program..\n", errno);
		exit(1);
	}

	// Join threads
    pthread_join(network_in, NULL);
    pthread_join(screen_out, NULL);
    pthread_join(keyboard_in, NULL);
    pthread_join(network_out, NULL);
	
	// Destroy mutexes
	pthread_mutex_destroy(&in_mtx);
	pthread_mutex_destroy(&out_mtx);

	// Destroy condition variables
	pthread_cond_destroy(&wait_keyb_in);
	pthread_cond_destroy(&wait_netw_in);

	// Close socket
	close(socketDescriptor);

    return 0;
}

void* Msg_Recv()
{
	unsigned int sin_remote_len = sizeof(sin_remote);
	char messageRx[MSG_MAX_LEN];
	
	while (1) {
		// Get the data (blocking)
		int bytesRx = recvfrom(socketDescriptor, messageRx, MSG_MAX_LEN, 0, (struct sockaddr *) &sin_remote, &sin_remote_len);
		// check recvfrom() return value
		if(bytesRx == -1){ printf("Error receiving message, value of errno = %d\n", errno); } 

		else{
			// Make it null terminated (so string functions work):
			int terminateIndex = (bytesRx < MSG_MAX_LEN) ? bytesRx : MSG_MAX_LEN - 1;
			messageRx[terminateIndex] = 0;
		
			pthread_mutex_lock(&in_mtx);

			List_add(in_list, &messageRx);

			pthread_cond_signal(&wait_netw_in);
			pthread_mutex_unlock(&in_mtx);
		}
	}
	return NULL;
}

void* Msg_Print()
{
	while (1) {
		pthread_mutex_lock(&in_mtx);

		if(List_count(in_list) > 0){

			List_first(in_list);
			char* str = List_remove(in_list);
			List_last(in_list);

			fflush(stdout);
			fputs("REMOTE: ", stdout);
			fputs(str, stdout);

			if(strncmp(str, "!\n", 3) == 0){
				pthread_mutex_unlock(&in_mtx);
				break;
			}
		}
		else if (List_count(in_list) == 0){
			pthread_cond_wait(&wait_netw_in, &in_mtx); // go to sleep if 'in_list' is empty
		}

		pthread_mutex_unlock(&in_mtx);
	}

	pthread_cancel(network_in);
	pthread_cancel(keyboard_in);
	pthread_cancel(network_out);
	pthread_cancel(screen_out);
	return NULL;
}

void* Msg_Keyboard()
{
	while (1) {
		char keyb_input[MSG_MAX_LEN];

		fflush(stdin);
		fgets(keyb_input, MSG_MAX_LEN, stdin);

		pthread_mutex_lock(&out_mtx);

		List_add(out_list, keyb_input); 
		pthread_cond_signal(&wait_keyb_in);
		pthread_mutex_unlock(&out_mtx);
	}
	return NULL;
}

void* Msg_Send()
{
	unsigned int sin_local_len = sizeof(sin_local);

	while (1) {
		pthread_mutex_lock(&out_mtx);

		if(List_count(out_list) > 0){
			char* str = List_remove(out_list);

			// Transmit a reply:
			int bytesTx = sendto( socketDescriptor,	str, strlen(str), 0, (struct sockaddr *) &sin_remote, sin_local_len);
			// check sendto() return value
			if(bytesTx == -1){ printf("Error sending message, value of errno = %d\n", errno); }

			if(strncmp(str, "!\n", 3) == 0){
				pthread_mutex_unlock(&out_mtx);
				break;
			}
		}
		else if (List_count(out_list) == 0){
			pthread_cond_wait(&wait_keyb_in, &out_mtx); // go to sleep if 'out_list' is empty
		}

		pthread_mutex_unlock(&out_mtx);
	}

	pthread_cancel(network_in);
	pthread_cancel(screen_out);
	pthread_cancel(keyboard_in);
	pthread_cancel(network_out);
	return NULL;
}