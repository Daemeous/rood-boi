/* Explicit native versions of the original room-effect callback tables: the callbacks are Zig
 * (game/battle/room_callbacks.zig). The table words are checked against the private disc data by
 * build_boot.py. */
int pc_cb_80091C6C(vs_field_SwingEffectRecord* p);
int pc_cb_80091E10(vs_field_SwingEffectRecord* p);
int pc_cb_80091FE8(vs_field_SwingEffectRecord* p);
int pc_cb_800923F8(vs_field_SwingEffectRecord* p);
int pc_cb_80092540(vs_field_SwingEffectRecord* p);
int pc_cb_80092C68(func_80092F74_t* a, vs_field_RoomEffectCommandHeader* b);
int pc_cb_80092E14(func_80092F74_t* a, vs_field_RoomEffectCommandHeader* b);
int pc_cb_80092F6C(func_80092F74_t* a, vs_field_RoomEffectCommandHeader* b);
int pc_cb_80092F74(func_80092F74_t* a, vs_field_RoomEffectCommandHeader* b);
int pc_cb_80092F98(func_80092F74_t* a, vs_field_RoomEffectCommandHeader* b);
int pc_cb_80092FBC(func_80092F74_t* a, vs_field_RoomEffectCommandHeader* b);
int pc_cb_8009352C(func_80092F74_t* a, vs_field_RoomEffectCommandHeader* b);
int pc_cb_800935B8(func_80092F74_t* a, vs_field_RoomEffectCommandHeader* b);
int pc_cb_80092E7C(func_80092F74_t* a, vs_field_RoomEffectCommandHeader* b);
int pc_cb_800935F8(func_80092F74_t* a, vs_field_RoomEffectCommandHeader* b);
int pc_cb_8009361C(func_80092F74_t* a, vs_field_RoomEffectCommandHeader* b);
int pc_cb_800936F0(func_80092F74_t* a, vs_field_RoomEffectCommandHeader* b);
int pc_cb_80093640(func_80092F74_t* a, vs_field_RoomEffectCommandHeader* b);
int (*vs_field_textureEffectHandlers[])(vs_field_SwingEffectRecord*) = {
    pc_cb_80091C6C,
    pc_cb_80091FE8,
    pc_cb_80091E10,
    pc_cb_80092540,
    pc_cb_80092540,
    pc_cb_80092540,
    pc_cb_800923F8,
};
int (*vs_field_roomEffectOpcodeHandlers[])(func_80092F74_t*, vs_field_RoomEffectCommandHeader*) = {
    pc_cb_80092C68,
    pc_cb_80092E14,
    pc_cb_80092F6C,
    pc_cb_80092F74,
    pc_cb_80092F98,
    pc_cb_80092FBC,
    pc_cb_8009352C,
    pc_cb_800935B8,
    pc_cb_80092E7C,
    pc_cb_800935F8,
    pc_cb_8009361C,
    pc_cb_800936F0,
    pc_cb_80093640,
};
