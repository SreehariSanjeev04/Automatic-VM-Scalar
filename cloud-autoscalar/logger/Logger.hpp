#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>

#define LOG_INFO(fmt, ...)  fprintf(stdout, "[INFO]  %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) fprintf(stderr, "[ERROR] %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)

#endif