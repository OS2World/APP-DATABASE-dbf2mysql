# Makefile fopr the dbf2mysql-utility
# Maarten Boekhold (boekhold@cindy.et.tudelft.nl) 1995

# Set this to your C-compiler
CC=gcc

# set this to your install-program (what does Solaris have
# in /usr/sbin/install? SYSV install?)
INSTALL=copy

#AR=/usr/bin/ar
AR=ar

# Set this to whatever your compiler accepts. Nothing special is needed
CFLAGS=-O5 -Wall -Zexe

# Set this to your Minerva (mSQL) installation-path
MYSQLINC=-I/mysql2/include
MYSQLLIB=-L/mysql2/lib

# Set this to where you want the binary (no man-page yet, don't know
# how to write them)
INSTALLDIR=/mysql2/bin

# Set this if your system needs extra libraries
#
# For Solaris use:
#EXTRALIBS= -lmysys -lmystrings -lmysqlclient  -lm
EXTRALIBS= -llibmysqlclient -lsocket -lm

# You should not have to change this unless your system doesn't have gzip
# or doesn't have it in the standard place (/usr/local/bin for ex.).
# Anyways, it is not needed for just a simple compile and install
RM=/bin/rm -f
GZIP=/bin/gzip
TAR=/bin/tar

VERSION=1.10d

OBJS=dbf.o endian.o libdbf.a dbf2mysql.o mysql2dbf.o

all: dbf2mysql mysql2dbf

libdbf.a: dbf.o endian.o
	$(AR) rcs libdbf.a dbf.o endian.o

dbf2mysql: dbf2mysql.o libdbf.a
	$(CC) $(CFLAGS) -s -L. $(MYSQLLIB) -o $@ dbf2mysql.o -llibdbf $(EXTRALIBS)
	
mysql2dbf: mysql2dbf.o libdbf.a
	$(CC) $(CFLAGS) -s -L. $(MYSQLLIB) -o $@ mysql2dbf.o -llibdbf $(EXTRALIBS)

dbf.o: dbf.c dbf.h
	$(CC) $(CFLAGS) -c -o $@ dbf.c

endian.o: endian.c
	$(CC) $(CFLAGS) -c -o $@ endian.c

dbf2mysql.o: dbf2mysql.c dbf.h
	$(CC) $(CFLAGS) -DVERSION=\"$(VERSION)\" $(MYSQLINC) -c -o $@ dbf2mysql.c

mysql2dbf.o: mysql2dbf.c dbf.h
	$(CC) $(CFLAGS) -DVERSION=\"$(VERSION)\" $(MYSQLINC) -c -o $@ mysql2dbf.c

install: dbf2mysql
	$(INSTALL) -m 0755 -s dbf2mysql $(INSTALLDIR)
	$(INSTALL) -m 0755 -s mysql2dbf $(INSTALLDIR)

clean:
	$(RM) $(OBJS) dbf2mysql mysql2dbf

# the 'expand' is just for me, I use a tabstop of 4 for my editor, which
# makes lines in the README very long and ugly for people using 8, so
# I just expand them to spaces

dist:
#	-expand -4 README.tab > README
	(cd .. ; $(TAR) cf dbf2mysql-$(VERSION).tar dbf2mysql-$(VERSION)/*.[ch] \
	dbf2mysql-$(VERSION)/Makefile dbf2mysql-$(VERSION)/README \
        dbf2mysql-$(VERSION)/kbl2win.cvt ; \
	$(GZIP) -f9 dbf2mysql-$(VERSION).tar)
