# @(#)$Header$

BASEDIR= /usr/ih
BINDIR = $(BASEDIR)/bin
LIBDIR = $(BASEDIR)/lib
INCDIR = $(BASEDIR)/include
LIB    = $(LIBDIR)/libocal.a

CC     = gcc
CFLAGS = -O3 -DMD5 -I$(INCDIR)
MKDIR  = mkdir -p
CP     = cp -p
RM     = rm -f

all:		canlink

canlink:	canlink.o
		$(CC) $(CFLAGS) -o canlink canlink.o -L$(LIBDIR) -local

canlink.o:	canlink.c

install:	canlink
		if [ ! -d $(BINDIR) ]; then $(MKDIR) $(BINDIR); fi;
		$(RM)          $(BINDIR)/canlink
		$(CP) canlink  $(BINDIR)/canlink

files:
		@echo canlink.c Makefile | tr " " "\012"

clean:
		rm -f *.o core

clobber:	clean
		rm -f canlink

