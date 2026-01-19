################################################################################
##########                  AUDIO OPTIMIZER			                ############
################################################################################

##########                  PLATFORM DETECTION                      ############

COMPILER := g++
COMP_FLAGS := -std=c++23 -Wall -Wconversion -Wpedantic -O2
TARGET_NAME := app
LINKABLE_VENDORS := fftw

UNAME := $(shell uname -s)
UNAME_ARCH := $(shell uname -m)

ifeq ($(UNAME), Linux) # LINUX
    OS := Linux_x86_64
    RPATH := -Wl,-rpath,'$$ORIGIN'
    define GET_LIB_PATTERNS
        lib$(1).so*
    endef
else ifeq ($(UNAME), Darwin) # MACOS
	LLVM_PREFIX := $(shell brew --prefix llvm)
	COMPILER    := $(LLVM_PREFIX)/bin/clang++
	
	# Targets the version we find installed on the machine
	LIBCXX_DIR  := $(LLVM_PREFIX)/lib/c++
	LIBCXX_PATH := $(LIBCXX_DIR)/libc++.dylib
	
	UNAME_ARCH := $(shell uname -m)
	ifeq ($(UNAME_ARCH), arm64)
		OS := MacOs_Arm_64
	else
		OS := MacOs_x86_64
	endif
	
	RPATH := -Wl,-rpath,'@loader_path' -Wl,-rpath,$(LIBCXX_DIR) $(LIBCXX_PATH)
	
define GET_LIB_PATTERNS
lib$(1)*.dylib
endef

endif
  
CXX := $(COMPILER) $(COMP_FLAGS)

##########                      BUILD TYPE                          ############

BUILD_TYPE ?= DEBUG

ifeq ($(BUILD_TYPE), DEBUG)
    APP_FLAGS := -g -O0 -fsanitize=address $(PLATFORM_DEFINES)
else 
    APP_FLAGS := -O3 $(PLATFORM_DEFINES)
endif
 
##########                      COMPILATION                         ############

## Directories 
BUILD_DIR := build/$(BUILD_TYPE)/$(OS)
OBJ_DIR   := $(BUILD_DIR)/obj
BIN_DIR   := $(BUILD_DIR)/bin
SOURCE_PATH := .

## Vendor Pathing
VENDOR_LIB_SRC_PATH := $(SOURCE_PATH)/vendor/lib/$(OS)/fftw
VENDOR_LDFLAGS      := -L$(VENDOR_LIB_SRC_PATH) -lfftw3

## Sources
WIZ_SRC_DIRS := $(SOURCE_PATH) $(SOURCE_PATH)/src $(SOURCE_PATH)/vendor/gist $(SOURCE_PATH)/vendor/kiss_fft130 $(SOURCE_PATH)/vendor/miniaudio

# Find BOTH .cpp and .c files (KissFFT uses .c)
APP_SRC_CPP := $(foreach dir,$(WIZ_SRC_DIRS),$(wildcard $(dir)/*.cpp))
APP_SRC_C   := $(foreach dir,$(WIZ_SRC_DIRS),$(wildcard $(dir)/*.c))

# Create object paths for both
APP_OBJ := $(patsubst %.cpp, $(OBJ_DIR)/%.o, $(notdir $(APP_SRC_CPP))) \
           $(patsubst %.c, $(OBJ_DIR)/%.o, $(notdir $(APP_SRC_C)))

INCLUDE_FLAGS := -DUSE_KISS_FFT -isystem $(SOURCE_PATH)/vendor -I$(SOURCE_PATH)/src -I$(VENDOR_LIB_SRC_PATH)

TARGET     := $(BIN_DIR)/$(TARGET_NAME)
COPY_STAMP := $(BIN_DIR)/.copy_libs

## Rules
# Search paths for both file types
vpath %.cpp $(WIZ_SRC_DIRS)
vpath %.c $(WIZ_SRC_DIRS)

all: $(TARGET)

$(OBJ_DIR) $(BIN_DIR):
	mkdir -p $@

# Rule for C++ files
$(OBJ_DIR)/%.o: %.cpp | $(OBJ_DIR)
	$(CXX) $(APP_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@

# Rule for C files (Required for KissFFT)
$(OBJ_DIR)/%.o: %.c | $(OBJ_DIR)
	$(COMPILER) $(APP_FLAGS) $(INCLUDE_FLAGS) -c $< -o $@

$(COPY_STAMP): | $(BIN_DIR)
	@touch $@

# Link everything
$(TARGET): $(APP_OBJ) $(COPY_STAMP) | $(BIN_DIR)
	$(CXX) $(APP_FLAGS) $(APP_OBJ) -o $@ $(VENDOR_LDFLAGS) $(RPATH)

.PHONY: clean run
clean:
	rm -rf build

run: all
	@./$(TARGET)

write: all
	@./$(TARGET) -write