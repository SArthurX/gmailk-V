TARGET=main


ifeq (,$(TOOLCHAIN_PREFIX))
$(error TOOLCHAIN_PREFIX is not set)
endif

ifeq ($(CHIP), CV181X)
CFLAGS += -DCV181X -D__CV181X__
LDFLAGS += -lcvi_ive
else ifeq ($(CHIP), CV180X)
CFLAGS += -DCV180X -D__CV180X__
CFLAGS += -DUSE_TPU_IVE
LDFLAGS += -lcvi_ive_tpu
else
$(error CHIP is not supported)
endif

CXX=$(TOOLCHAIN_PREFIX)g++
CC=$(TOOLCHAIN_PREFIX)gcc
CFLAGS +=  -fsigned-char -Wno-format-truncation -fdiagnostics-color=always -s -lpthread -latomic
CXXFLAGS = $(CFLAGS) -std=c++11 -I./include -I$(COMMON_DIR)/../include/tdl

LDFLAGS += -lini -lsns_full -lsample -lisp -lvdec -lvenc -lawb \
		   -lae -laf -lcvi_bin -lcvi_bin_isp -lmisc -lisp_algo \
		   -lsys  -lvi -lvo -lvpss -lrgn -lgdc
LDFLAGS += -lcvi_tdl
LDFLAGS += -lopencv_core -lopencv_imgproc -lopencv_imgcodecs
LDFLAGS += -lcvikernel -lcvimath -lcviruntime
LDFLAGS += -lcvi_rtsp
LDFLAGS += -lwiringx

COMMON_SRC = $(COMMON_DIR)/middleware_utils.c
COMMON_OBJ = $(COMMON_SRC:.c=.o)

CPP_SOURCES = $(wildcard src/*.cpp)
CPP_OBJS = $(CPP_SOURCES:.cpp=.o)

C_SOURCES = $(wildcard src/*.c)
C_OBJS = $(C_SOURCES:.c=.o)

SOURCE = main.cpp
OBJS = $(SOURCE:.cpp=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS) $(CPP_OBJS) $(C_OBJS) $(COMMON_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(COMMON_OBJ) $(CPP_OBJS) $(C_OBJS) $(OBJS) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -o $@ -c $<

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

.PHONY: clean
clean:
	@rm -rf *.o src/*.o
	@rm -rf $(COMMON_OBJ)
	@rm -rf $(CPP_OBJS) $(C_OBJS) $(OBJS)
	@rm -rf $(TARGET)
