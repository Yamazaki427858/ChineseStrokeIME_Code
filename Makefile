CXX = g++
CXXFLAGS = -std=c++11 -Wall -O2 -mwindows -DUNICODE -D_UNICODE

SRCS = main.cpp ime_core.cpp input_handler.cpp dictionary.cpp dict_updater.cpp \
       buffer_manager.cpp window_manager.cpp config_loader.cpp screen_manager.cpp \
       position_manager.cpp tray_manager.cpp ime_manager.cpp

OBJS = $(SRCS:.cpp=.o)
TARGET = ChineseStrokeIME.exe

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS) -static -static-libgcc -static-libstdc++ \
	-lgdi32 -luser32 -lkernel32 -lshell32 -lcomctl32 -limm32 -lwininet -lcrypt32

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	@if exist *.o del /F /Q *.o 2>nul
	@if exist $(TARGET) del /F /Q $(TARGET) 2>nul
