CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -O2
TARGET = vmsc
OBJS = vMSC.o main.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)

vMSC.o: vMSC.cpp vMSC.h
	$(CXX) $(CXXFLAGS) -c vMSC.cpp

main.o: main.cpp vMSC.h
	$(CXX) $(CXXFLAGS) -c main.cpp

clean:
	rm -f $(OBJS) $(TARGET)

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run
