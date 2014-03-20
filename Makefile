# wasdwm
# See LICENSE file for copyright and license details.

include config.mk

SRC = wasdwm.c
OBJ = ${SRC:.c=.o}

all: options wasdwm

options:
	@echo wasdwm build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

.c.o:
	@echo CC $<
	@${CC} -c ${CFLAGS} $<

${OBJ}: config.h config.mk

config.h:
	@echo creating $@ from config.def.h
	@cp config.def.h $@

wasdwm: ${OBJ}
	@echo CC -o $@
	@${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	@echo cleaning
	@rm -f wasdwm ${OBJ} wasdwm-${VERSION}.tar.gz

dist: clean
	@echo creating dist tarball
	@mkdir -p wasdwm-${VERSION}
	@cp -R LICENSE Makefile README config.def.h config.mk \
		wasdwm.1 ${SRC} wasdwm-${VERSION}
	@tar -cf wasdwm-${VERSION}.tar wasdwm-${VERSION}
	@gzip wasdwm-${VERSION}.tar
	@rm -rf wasdwm-${VERSION}

install: all
	@echo installing executable file to ${DESTDIR}${PREFIX}/bin
	@mkdir -p ${DESTDIR}${PREFIX}/bin
	@cp -f wasdwm ${DESTDIR}${PREFIX}/bin
	@chmod 755 ${DESTDIR}${PREFIX}/bin/wasdwm
	@echo installing manual page to ${DESTDIR}${MANPREFIX}/man1
	@mkdir -p ${DESTDIR}${MANPREFIX}/man1
	@sed "s/VERSION/${VERSION}/g" < wasdwm.1 > ${DESTDIR}${MANPREFIX}/man1/wasdwm.1
	@chmod 644 ${DESTDIR}${MANPREFIX}/man1/wasdwm.1

uninstall:
	@echo removing executable file from ${DESTDIR}${PREFIX}/bin
	@rm -f ${DESTDIR}${PREFIX}/bin/wasdwm
	@echo removing manual page from ${DESTDIR}${MANPREFIX}/man1
	@rm -f ${DESTDIR}${MANPREFIX}/man1/wasdwm.1

.PHONY: all options clean dist install uninstall
