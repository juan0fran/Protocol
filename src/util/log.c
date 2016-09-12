#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <libgen.h>

#include <util/log.h>

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

static ErrorGrade log_level_min = LOG_DEBUG;

static char LOG_PATH[LOG_BUFF_SIZE];
static int file_flag;
static int log_init_done = 0;

void log_init(ErrorGrade level, const char * path, int _file_flag){
	log_init_done = 1;
    log_level_min = level;
    if (_file_flag == 1){
    	strcpy(LOG_PATH, (const char *) path);
    	file_flag = _file_flag;
	}else{
		file_flag = _file_flag;
	}
	return;
}

/**

    A file will be selected depending on filename parameter. After the time, the function will be also attached 

 */
void _log_message(ErrorGrade type, char *filename, char * function, char* fmt, ...)
{
    va_list args;
    char tmp_msg[LOG_BUFF_SIZE];
    char msg[LOG_BUFF_SIZE];
    char file[LOG_BUFF_SIZE];
    char copy[LOG_BUFF_SIZE];
    char date[LOG_BUFF_SIZE];
    struct timeval epoch_time;
    struct tm gm_time;
    char *ext;
    FILE * fp;

    if (log_init_done == 0){
    	printf("Initialize the LOG functions\n");
    	exit(1);
    }

    if (type >= log_level_min) {
        gettimeofday(&epoch_time, NULL);
        gmtime_r(&epoch_time.tv_sec, &gm_time);
        strftime(date, LOG_BUFF_SIZE, "%Y-%m-%d %H:%M:%S", &gm_time);
        /*sprintf(msg, "[%s] @ %s -> %s: ", function, date, */
        sprintf (msg, "[%s] -> %s: ", function,
        				type == 0 ? "Debug" : type == 1 ? "Info" : type == 2 ? "Warn" : type == 3 ? "Error" : "Critical");
        va_start(args, fmt);
        vsnprintf(tmp_msg, LOG_BUFF_SIZE - 1, fmt, args);
        va_end(args);
        strncat(msg, tmp_msg, LOG_BUFF_SIZE - 1);   

        if (file_flag){
	        ext = strrchr( basename(filename), '.');
	        bzero(copy, sizeof(copy));
	        strncpy(copy, basename(filename), strlen(basename(filename)) - strlen(ext));
	        sprintf(file, "%s/%s.log", LOG_PATH, copy);
    	}
        #ifdef PRINTF_ENABLED
        if (file_flag){
        	printf("Writing to: %s --> ", file);
        }
        if (msg[strlen(msg)-1] == '\n'){
        	printf("%s", msg);
    	}else{
    		printf("%s\n", msg);
    	}
        #endif

        if (file_flag){
	        fp = fopen(file, "a+");
	        if (fp != NULL){
	            fprintf(fp, "%s\n", msg);
	            fclose(fp);
	        }else{
	            printf("Cannot open file: %s ->", file);
	            perror("Error");
	        }
    	}
    }
}