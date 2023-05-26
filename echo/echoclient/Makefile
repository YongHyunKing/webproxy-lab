TARGET = echoclient
OBJS = 	echoclient.o csapp.o
%.o: %.c
	gcc -c $<
$(TARGET) : $(OBJS)
	gcc -o $(TARGET) $(OBJS)
clean:
	rm -f *.o
	rm -f $(TARGET)
# CC = gcc
# CFLAGS = -g -Wall
# LDFLAGS = -lpthread

# csapp.o: csapp.c csapp.h
# 	$(CC) $(CFLAGS) -c csapp.c

# echoclient.o: echoclient.c csapp.h
# 	$(CC) $(CFLAGS) -c echoclient.c

# echoclient : echoclient.o csapp.o:
# 	$(CC) $(CFLAGS) echoclient.o csapp.o -o echoclient $(LDFLAGS)

# clean:
# 	rm -f *~ *.o echoclient echoserveri