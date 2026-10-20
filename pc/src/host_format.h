/* printf-style formatting and a log line for the game's own translation units
 * (their headers have no host stdio; pc/src/host_format.c). */
#ifndef PC_HOST_FORMAT_H
#define PC_HOST_FORMAT_H
#ifdef __cplusplus
extern "C" {
#endif

/* vsnprintf into out (size bytes, always terminated); returns the length it wanted. */
int pc_format(char* out, int size, const char* format, ...) __attribute__((format(printf, 3, 4)));
/* One line to stderr. */
void pc_log_line(const char* text);
/* A monotonic clock in seconds (real time). */
double pc_host_seconds(void);

#ifdef __cplusplus
}
#endif
#endif
