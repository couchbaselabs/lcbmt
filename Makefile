LCB_ROOT=/sources/libcouchbase/inst
CPPFLAGS=-Wall -pthread -g -I$(LCB_ROOT)/include -Iinclude -O0
LDFLAGS=-Wl,-rpath=$(LCB_ROOT)/lib -L$(LCB_ROOT)/lib \
		-Wl,-rpath='$$ORIGIN' -L. -rdynamic \
		-lcouchbase -lpthread

SO=libcouchbase-mt.so
OBJS=src/lcbmt.o src/sockinit.o src/unix.o src/token.o src/cbwrap.o

all: $(SO) mt89

%.o: %.c
	$(CC) -c $(CPPFLAGS) -fPIC -o $@ $^

$(SO): $(OBJS)
	$(CC) $(CPPFLAGS) -shared -o $@ $^

mt89: $(SO) examples/mt-c89.c examples/cliopts.c
	$(CC) $(CPPFLAGS) -o $@ $^ $(LDFLAGS) -rdynamic -lcouchbase-mt
