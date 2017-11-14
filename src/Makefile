LDFLAGS=-lm -Wl,-z,now -Wl,-z,relro
CC = /opt/codesourcery/bin/arm-linux-gcc -g -Wall -fPIC -O2 -rdynamic
STRIP = /opt/codesourcery/bin/arm-linux-strip
CFLAGS = -I../ -I/opt/codesourcery/arm-none-linux-gnueabi/libc/usr/include -I/home/merge/Asterisk-13/../Share/external/GXV317X/include
CFLAGS += -fPIE -fstack-protector-all -D_FORTIFY_SOURCE=1
LIBS = -L/opt/codesourcery/arm-none-linux-gnueabi/libc/usr/lib -L/home/merge/Asterisk-13/../Share/external/GXV317X/lib -lcurl -ljansson
PROGRAM = mcm-demo

BASIC_OBJS = avs_controller.o

$(PROGRAM):$(BASIC_OBJS)
	$(CC) -o $(PROGRAM) $(CFLAGS) $(BASIC_OBJS) $(LIBS) $(LDFLAGS)

%.o: %.c 
	$(CC) $(CFLAGS) -rdynamic -c $< -o $@

.PHONY : clean objclean
clean : objclean

objclean :
	-rm -f $(PROGRAM)
	-rm -f $(BASIC_OBJS)
