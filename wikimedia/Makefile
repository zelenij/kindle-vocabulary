SOURCES=$(wildcard *.cpp)
OBJECTS:=$(patsubst %.cpp,%.o,$(SOURCES))
EXE=server

ZIP=wikimeda.zip
ZIPFILES=$(SOURCES) Makefile README

#CXX_OPT=-g
CXX_OPT=-O3
CXXFLAGS=$(CXX_OPT) -Wall -std=c++14
LIBS=-lboost_system

.PHONY : clean

all: $(EXE)

$(EXE): $(OBJECTS)
	g++ -o $(EXE) $(OBJECTS) $(LIBS)

$(OBJECTS): %.o: %.cpp
	g++ $(CXXFLAGS) -c -o $@ $<

zip: $(ZIPFILES)
	zip -v $(ZIP) $(ZIPFILES)

clean:
	rm -f $(OBJECTS) $(EXE)
