PROG=	pipa
SRCS=	pipa.c	tui.c

LDADD=	-lncurses

.include <bsd.prog.mk>
