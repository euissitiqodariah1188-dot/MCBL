#ifndef MCBL_LOG_H
#define MCBL_LOG_H
/*
 * McBL# log.* — Logging
 * ========================
 * log.debug(msg)   → [DEBUG] msg
 * log.info(msg)    → [INFO]  msg
 * log.warn(msg)    → [WARN]  msg
 * log.error(msg)   → [ERROR] msg
 * log.fatal(msg)   → [FATAL] msg + exit(1)
 * log.set_level(l) → set minimum log level
 * log.to_file(p)   → redirect log to file
 * log.timestamp(1) → include timestamp
 */
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum { LOG_DEBUG=0, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_FATAL } McblLogLevel;

void mcbl_log_debug(const char *msg);
void mcbl_log_info (const char *msg);
void mcbl_log_warn (const char *msg);
void mcbl_log_error(const char *msg);
void mcbl_log_fatal(const char *msg);   /* calls exit(1) */
void mcbl_log_set_level(McblLogLevel lvl);
void mcbl_log_to_file(const char *path);
void mcbl_log_timestamp(int enabled);
void mcbl_log_fmt(McblLogLevel lvl, const char *fmt, ...);

/* Convenience macros */
#define MCBL_LOG_DEBUG(msg) mcbl_log_debug(msg)
#define MCBL_LOG_INFO(msg)  mcbl_log_info(msg)
#define MCBL_LOG_WARN(msg)  mcbl_log_warn(msg)
#define MCBL_LOG_ERROR(msg) mcbl_log_error(msg)

#ifdef __cplusplus
}
#endif
#endif /* MCBL_LOG_H */
