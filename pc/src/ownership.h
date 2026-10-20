#ifndef PC_OWNERSHIP_H
#define PC_OWNERSHIP_H
#ifdef __cplusplus
extern "C" {
#endif
int pc_owner_count(void);
int pc_owner_get(int subsystem);
int pc_owner_set(int subsystem, int owner);
int pc_owner_find(const char* name);
int pc_owner_parse(const char* label);
int pc_owner_routes(int subsystem);
const char* pc_owner_subsystem(int subsystem);
const char* pc_owner_label(int owner);
/* Owner 3, alternate: per-route flip bits and owned/original call counts. */
int pc_owner_alternate_reset(int subsystem);
int pc_owner_alternate_report(int subsystem, char* out, int size);
#ifdef __cplusplus
}
#endif
#endif
