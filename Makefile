PROG=	pipa
SRCS=	pipa.c	tui.c
BINDIR=	/usr/local/bin
SCRIPT=	pipa-run

LDADD=	-lncurses	-lm

afterinstall:
	${INSTALL} ${INSTALL_COPY} -m ${BINMODE} ${SCRIPT} ${DESTDIR}${BINDIR}/${SCRIPT}

.include <bsd.prog.mk>
