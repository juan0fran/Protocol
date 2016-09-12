#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <protocol/StopAndWait.h>
#include <util/log.h>
#include <util/socket_utils.h>
#include <interfaces/packet.h>
#include <interfaces/phy.h>

#include <signal.h>
#include <argp.h>

#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

typedef struct arguments_t{
	char 		*phy_socket;
	char 		*net_socket;
	char 		*beacon_socket;
	char 		*ip_addr;
	uint8_t		master;
	uint8_t		slave;
	uint8_t     debug_level;        // Verbose level
    uint8_t     print_long_help;	// Print a long help and exit

}arguments_t;

static arguments_t arguments;

static Control control;
static Status status;

static char doc[] = "Protocol v0.1";
static char args_doc[] = "";
static struct argp_option options[] = {
	{"phy_socket", 'P', "/path/to/file", 0, "Set physical layer unix socket file!"},
	{"net_socket", 'N', "/path/to/file", 0, "Set network layer unix socket file!"},
	{"beacon_socket", 'B', "/path/to/file", 0, "Set beacon layer unix socket file!"},
	{"ip_addr", 'I', "x.x.x.x", 0, "IP address of tunnel"},
	{"master", 'M', "0", 0, "Set as MASTER device"},
	{"slave", 'S', "0", 0, "Set as SLAVE device"},
    {"debug_level", 'D', "#number", 0, "Set debug level from 0 (LOG_DEBUG) to 4 (LOG_CRITICAL)"},
    {"long-help",  'H', 0, 0, "Print a long help and exit"},
    {0},
};
static void init_args(arguments_t *arguments)
// ------------------------------------------------------------------------------------------------
{
    arguments->debug_level = LOG_DEBUG;
    arguments->print_long_help = 0;

    arguments->phy_socket = strdup(".");
    /** If it is not specified, config file at /etc/gsdaemon.conf */
    arguments->net_socket = strdup(".");
    
    arguments->beacon_socket = strdup(".");

    arguments->ip_addr = strdup("10.0.0.1");
    /** The usrp by default will be at /usr/bin/. */
    arguments->master = 0;
    arguments->slave = 0;
}

static int isDirectory(const char *path) {
   struct stat statbuf;
   if (stat(path, &statbuf) != 0)
       return 0;
   return S_ISDIR(statbuf.st_mode);
}

// ------------------------------------------------------------------------------------------------
// Option parser 
static error_t parse_opt (int key, char *arg, struct argp_state *state)
// ------------------------------------------------------------------------------------------------
{
    arguments_t *arguments = state->input;
    char        *end;  // Used to indicate if ASCII to int was successful

    switch (key){
    	case 'P':
    		printf("Physical Layer Socket: %s\n", arg);
        	if (arg[strlen(arg) - 1] == '/'){
        		arg[strlen(arg) - 1] = '\0';
        	}
        	/** If isDirectory != 0 --> it is a directory 
				Otherwise it is a file -> thus ask the user to introduce a path not a filename
        	*/
        	if (isDirectory(arg) != 0){
        		printf("Introduce a FILE not a PATH\n");
				argp_usage(state);
				break;
        	}  
        	arguments->phy_socket = realpath(arg, NULL);
        	if (arguments->phy_socket == NULL){
        		arguments->phy_socket = strdup(arg);
        	}
        	printf("Path: %s\n", arguments->phy_socket);
        	/* needs to be freed */
        	break;	
    	case 'N':
    		printf("Network Layer Socket: %s\n", arg);
        	if (arg[strlen(arg) - 1] == '/'){
        		arg[strlen(arg) - 1] = '\0';
        	}
        	/** If isDirectory != 0 --> it is a directory 
				Otherwise it is a file -> thus ask the user to introduce a path not a filename
        	*/
        	if (isDirectory(arg) != 0){
        		printf("Introduce a FILE not a PATH\n");
				argp_usage(state);
				break;
        	}

        	arguments->net_socket = realpath(arg, NULL);
        	if (arguments->net_socket == NULL){
        		arguments->net_socket = strdup(arg);
        	}
        	printf("Path: %s\n", arguments->net_socket);
        	/* needs to be freed */
        	break;
    	case 'B':
    		printf("Beacon Layer Socket: %s\n", arg);
        	if (arg[strlen(arg) - 1] == '/'){
        		arg[strlen(arg) - 1] = '\0';
        	}
        	/** If isDirectory != 0 --> it is a directory 
				Otherwise it is a file -> thus ask the user to introduce a path not a filename
        	*/
        	if (isDirectory(arg) != 0){
        		printf("Introduce a FILE not a PATH\n");
				argp_usage(state);
				break;
        	}

        	arguments->beacon_socket = realpath(arg, NULL);
        	if (arguments->beacon_socket == NULL){
        		arguments->beacon_socket = strdup(arg);
        	}
        	printf("Path: %s\n", arguments->beacon_socket);
        	/* needs to be freed */
        	break;        	
        case 'I':
        	arguments->ip_addr = strdup(arg);
        	/* needs to be freed */
        	break;
        	// Print long help and exit
        case 'H':
            arguments->print_long_help = 1;
            break;
        case 'M':
        	arguments->master = strtol(arg, &end, 10);
        	break;	
    	case 'S':
    		arguments->slave = strtol(arg, &end, 10);
        	break;	
        case 'D':
            arguments->debug_level = strtol(arg, &end, 10);
            if (*end){
                argp_usage(state);
                break; 
            }
            if (arguments->debug_level < LOG_DEBUG || arguments->debug_level > LOG_CRITICAL){
            	printf("Bad logging level selected!!");
            	argp_usage(state);
            	break;
            }
            break;        	
        default:
            return ARGP_ERR_UNKNOWN;
		break;
    }
    return 0;
}

static struct argp argp = {options, parse_opt, args_doc, doc};

static void delete_args(arguments_t *arguments){
    if (arguments->phy_socket)
    {
        free(arguments->phy_socket);
    }
    if (arguments->net_socket)
    {
        free(arguments->net_socket);
    }   
    if (arguments->beacon_socket)
    {
        free(arguments->beacon_socket);
    }    
    if (arguments->ip_addr)
    {
    	free(arguments->ip_addr);
    }

}

static void terminate(const int signal_) {
// ------------------------------------------------------------------------------------------------
    printf("Terminating with signal %d\n", signal_);
    delete_args(&arguments);
    exit(1);
}


/* This is in charge of the physical -> data -> network link */
void physical_layer_control(){
	/* Physical layer will pass through a Unix Socket to the Data link layer */
	/* Data link layer will pass through a Unix Socket to the Network layer */
	/* Connect to a physical layer -> simulated as we are a socket client */
	/* Connect to a network layer -> simulated as we are a socket client */
	ErrorHandler err;
	while(1){
		if (control.initialised == 0){
			/* If there is any beacon message available -> send it */
			/* We would be always slaves */
			/* If a beacon message is received -> Try to connect to the SLAVE */
			/* Here -> Try to receive a message */
			/* In case some packet has been received but the packet is not a beacon packet -> try to connect as slave */
			control.master_slave_flag = SLAVE;
			/* Until it is not initialised, try to initilise */
			//log_message(LOG_INFO, "Going to initialise the link\n");
			err = protocol_establishment_routine(initialise_link, &control, &status);
			if (err == IO_ERROR){
				log_message(LOG_ERROR, "Error at protocol control: %d\n", err);
				return;
			}
			log_message(LOG_INFO, "Control initialised: %d\n", control.initialised);
		}
		/* The link is initialised now! We have connection ;) */
		err = protocol_establishment_routine(check_link_availability, &control, &status);
		if (err == IO_ERROR){
			log_message(LOG_ERROR, "Error at protocol control: %d\n", err);
			return;
		}
		if (control.initialised == 1){
			/* Beacon does not understand aboud connection states */
			/* Just try to send a beacon if any */
			err = StopAndWait(&control, &status);
			if (err != NO_ERROR){
				log_message(LOG_ERROR, "Error at S&W protocol: %d\n", err);
				return;
			}
			if (control.initialised == 1){
				err = protocol_establishment_routine(check_link_availability, &control, &status);
				if (err == IO_ERROR){
					log_message(LOG_ERROR, "Error at protocol control: %d\n", err);
					return;
				}
			}
		}
	}
}

int protocol_routine(char * sock_data_phy, char * sock_data_net, char * sock_data_beacon, char * ip){
	char syscall[256];
	init_radio(255);
	log_init(arguments.debug_level, NULL, 0);
	while (1){
		log_message(LOG_DEBUG, "Connect sockets\n");
		control.phy_fd = initialise_client_socket(sock_data_phy);
		if (control.phy_fd == -1){
			perror("Openning phyfd: ");
			exit(-1);
		}
	#ifndef __MAC_OS_X__
		/* Before connecting the IFACE, set UP the IP */
		sprintf(syscall, "ip tuntap del dev %s mode tun", sock_data_net);
		system(syscall);
        sprintf(syscall, "ip tuntap add dev %s mode tun", sock_data_net);
        system(syscall);        
  		control.net_fd = tun_alloc(sock_data_net);  /* tun interface */
        sprintf(syscall, "ip link set dev %s up", sock_data_net);
        system(syscall);
        sprintf(syscall, "ifconfig %s %s", sock_data_net, ip);
        system(syscall);
        sprintf(syscall, "ip route add %s/24 dev %s", ip, sock_data_net);
        system(syscall);
        sprintf(syscall, "ifconfig %s %s", sock_data_net, ip);
        system(syscall);        
  	#else
		control.net_fd = initialise_server_socket(sock_data_net);
		if (control.net_fd == -1){
			perror("Opening netfd: ");
			exit(-1);
		}
	#endif
		control.beacon_fd = initialise_server_socket(sock_data_beacon);
		if (control.net_fd == -1){
			perror("Opening beaconfd: ");
			exit(-1);
		}

		control.initialised = 0;
		control.packet_counter = 10;
		/* Ping link time is 10 seconds */
		control.ping_link_time = 10000;
		control.piggy_time = 20;
		control.byte_round_trip_time = 1500;
		control.packet_timeout_time = 1500; /* ms */ /* The channel has a delay of 10 ms, so 100 ms per timeout as an example */
		control.round_trip_time = control.packet_timeout_time;
		control.death_link_time = 60000; /* in ms */ /* after 60 seconds without handshake, test again */
		log_message(LOG_INFO, "The two socket are initialised\n");
		physical_layer_control();
		close(control.phy_fd);
		close(control.net_fd);
	}	
	return 0;
}

int main (int argc, char ** argv) {
	int i;
    // unsolicited termination handling
    struct sigaction sa;
    // Catch all signals possible on process exit!
    for (i = 1; i < 15; i++) 
    {
        // These are uncatchable or harmless or we want a core dump (SEGV) 
        if (i != SIGKILL
            && i != SIGSEGV
            && i != SIGSTOP
            && i != SIGVTALRM
            && i != SIGWINCH
            && i != SIGPROF) 
        {
            memset(&sa, 0, sizeof(sa));
            sa.sa_handler = terminate;
            sigaction(i, &sa, NULL);
        }
    }
    // Set argument defaults
    init_args(&arguments); 
    // Parse arguments 
    argp_parse (&argp, argc, argv, 0, 0, &arguments);

    if (arguments.print_long_help)
    {
        printf("TODO: PRINT Long help HERE\n\n");
        delete_args(&arguments);
        exit(0);
    }
	return (protocol_routine(arguments.phy_socket, arguments.net_socket, arguments.beacon_socket, arguments.ip_addr));
}


















