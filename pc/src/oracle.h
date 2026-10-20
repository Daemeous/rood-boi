/* Optional oracle hooks; standalone component probes need no oracle backend. */
#ifndef PC_ORACLE_H
#define PC_ORACLE_H
#ifdef __cplusplus
extern "C" {
#endif
extern int pc_oracle_classic(void) __attribute__((weak));
extern int pc_shadow_speculative(void) __attribute__((weak));
#ifdef __cplusplus
}
#endif
#endif
