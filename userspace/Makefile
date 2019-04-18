TARGET = userspace
CFLAGS = -Wall -g
OBJS = userspace.o

all: $(TARGET)

$(TARGET):$(OBJS)
	$(CC) -o $(TARGET) $(OBJS)
clean:
	rm -f $(OBJS) $(TARGET)
