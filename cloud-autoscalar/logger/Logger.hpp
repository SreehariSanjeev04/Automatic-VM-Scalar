#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>

#define LOG_INFO(fmt, ...)  do { fprintf(stdout, "\033[32m[INFO]  %s:%d: " fmt "\033[0m\n", __FILE__, __LINE__, ##__VA_ARGS__); fflush(stdout); } while (false)
#define LOG_ERROR(fmt, ...) do { fprintf(stderr, "\033[31m[ERROR] %s:%d: " fmt "\033[0m\n", __FILE__, __LINE__, ##__VA_ARGS__); fflush(stderr); } while (false)

#endif