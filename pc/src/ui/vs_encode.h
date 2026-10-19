#ifndef PC_VS_ENCODE_H
#define PC_VS_ENCODE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Encode UTF-8 using ui_text.c's menu charset; space is canonically 0x8F.
 * Returns bytes written INCLUDING 0xE7. Reserves its byte when truncating.
 * cap <= 0 or out == NULL writes nothing and returns 0. NULL utf8 is empty.
 * bad (optional) counts replacements in the consumed prefix only. Unsupported
 * Unicode scalars become 0xA5 ('?'); malformed UTF-8 consumes one byte per '?'.
 * Whitespace/control decoding is lossy: no attempt is made to recover controls.
 */
int vs_encode(const char* utf8, unsigned char* out, int cap, int* bad);

#ifdef __cplusplus
}
#endif
#endif
