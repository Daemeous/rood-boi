#pragma once

#ifdef ROOD_PC
#include <inline_c.h>
#define gte_SetTransVector2(v) gte_SetTransVector(v)
#define gte_rtv0tr2() gte_rtv0tr()
#define gte_rtps2() gte_rtps()
#define gte_zrtr() do { CTC2(0, 5); CTC2(0, 6); CTC2(0, 7); } while (0)
#define gte_nclip2() gte_nclip()
#else

#define gte_SetTransVector2(r0) __asm__ volatile (		\
	"lw	$12, 0( %0 );"					\
	"lw	$13, 4( %0 );"					\
	"ctc2	$12, $5;"					\
	"lw	$14, 8( %0 );"					\
	"ctc2	$13, $6;"					\
	"ctc2	$14, $7"					\
	:							\
	: "r"( r0 )						\
	: "$12", "$13", "$14" )

#define gte_rtv0tr2() __asm__ volatile ( \
    "nop;" \
    "nop;" \
    "rtv0tr")

#define gte_rtps2() __asm__ volatile ( \
    "nop;" \
    "nop;" \
    "rtps")

#define gte_zrtr() __asm__ volatile (		\
	"ctc2	$0, $5;"					\
	"ctc2	$0, $6;"					\
	"ctc2	$0, $7"						\
)

#define gte_nclip2() __asm__ volatile ( \
    "nop;" \
    "nop;" \
    "nclip")

#endif
