CXX := g++
CPPFLAGS := -O2 -march=native
OBJS := block0.o block1.o block1stevens00.o block1stevens01.o block1stevens10.o block1stevens11.o block1wang.o main.o md5.o timer.o
LIBS :=

.PHONY := clean all

all: md5fastcoll run

md5fastcoll: $(OBJS)
	$(CXX) $(CPPFLAGS) $(LIBS) $^ -o $@

run: run.sh
	cp -f run.sh run
	chmod +x run

clean:
	rm -f $(OBJS)
	rm -f md5fastcoll
	rm -f run

%.o: %.cpp
	$(CXX) $(CPPFLAGS) -c $< -o $@
