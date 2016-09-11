#ifndef	__LOG_H_
#define __LOG_H_

#define PRINTF_ENABLED
#define LOG_BUFF_SIZE 256

typedef enum ErrorGrade{
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_CRITICAL,
}ErrorGrade;

/** Messages will be File dependent and Function dependent */

void log_init(ErrorGrade level, const char * path, int _file_flag);
void _log_message(ErrorGrade type, char *filename, char * function, char* fmt, ...);

#define log_message(type, ...) _log_message(type, (char *)__FILE__, (char *)__FUNCTION__, __VA_ARGS__ )

#endif