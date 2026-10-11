#ifndef	TYPES_H
#define	TYPES_H

#include <stdint.h>
#include <stddef.h>

#if !defined(__APPLE__)
/* major part of a device */
#define	major(x)	((int)(((unsigned)(x)>>8)&0377))

/* minor part of a device */
#define	minor(x)	((int)((x)&0377))

/* make a device number */
#define	makedev(x,y)	((dev_t)(((x)<<8) | (y)))

#endif

#ifndef _UCHAR_T
#define _UCHAR_T
typedef	unsigned char	u_char;
#endif
#ifndef _USHORT_T
#define _USHORT_T
typedef	unsigned short	u_short;
#endif
#ifndef _UINT_T
#define _UINT_T
typedef	unsigned int	u_int;
#endif
#ifndef _ULONG_T
#define _ULONG_T
#ifdef ROOD_PC_LP64 /* rood-boi: the PS1's 32-bit u_long on LP64 */
typedef	uint32_t	u_long;
#else
typedef	unsigned long	u_long;
#endif
#endif
#ifndef _SYSIII_USHORT
#define _SYSIII_USHORT
typedef	unsigned short	ushort;		/* sys III compat */
#endif
#ifndef __psx__
#ifndef _SYSV_UINT
#define _SYSV_UINT
typedef	unsigned int	uint;		/* sys V compat */
#endif
#ifndef _SYSV_ULONG
#define _SYSV_ULONG
#ifdef ROOD_PC_LP64
typedef	uint32_t	ulong;
#else
typedef	unsigned long	ulong;		/* sys V compat */
#endif
#endif
#endif /* ! __psx__ */

#define	NBBY	8

#endif
