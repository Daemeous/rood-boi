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
#ifdef __cplusplus
}
#endif
#endif
