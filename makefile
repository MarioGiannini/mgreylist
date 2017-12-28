#
# Simple make file, for simple greylister
#
CC = gcc
CFLAGS = -static
VER=0.1

mgreylistd: mgreylistd.c mgreylistc mgreylistd.8.gz
	$(CC) mgreylistd.c  $(CFLAGS) -o mgreylistd

mgreylistc: mgreylistc.c
	$(CC) mgreylistc.c  $(CFLAGS) -o mgreylistc

mgreylistd.8.gz: mgreylistd.8
	gzip -c mgreylistd.8 > mgreylistd.8.gz

tar:
	@rm *~
	@rm -rf ..mgreylistd-$(VER)
	@mkdir ..mgreylistd-$(VER)
	@cp * ..mgreylistd-$(VER)/ -r
	@tar cf ~/rpm/SOURCES/mgreylistd.tar ../mgreylistd-$(VER) 2> /dev/null


install:
	@cp mgreylistd /usr/sbin
	@cp mgreylistc /usr/bin
	@cp etc/init.d/mgreylistd /etc/init.d/
	@chmod 755 /etc/init.d/mgreylistd
	@mkdir /etc/mgreylistd
	@cp etc/mgreylistd/* /etc/mgreylistd/ -r
	@cp mgreylistd.8.gz /usr/share/man/man8
	@mkdir /var/lib/mgreylistd/

uninstall:
	@rm /etc/init.d/mgreylistd
	@rm /etc/mgreylistd/*
	@rmdir /etc/mgreylistd
	@rm /usr/bin/mgreylistc
	@rm /usr/sbin/mgreylistd
	@rm /usr/share/man/man8/mgreylistd.8.gz
	@rm /var/lib/mgreylistd/*
	@rmdir /var/lib/mgreylistd/

