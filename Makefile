
# Executable to build
EXE = copy 

# Sources
PROJECT_HOME = .
OBJ_DIR = $(PROJECT_HOME)/_obj

SRCS = $(PROJECT_HOME)/main.cpp \
       $(PROJECT_HOME)/dirReader.cpp \
       $(PROJECT_HOME)/dirCopy.cpp \
       $(PROJECT_HOME)/fileReader.cpp \
       $(PROJECT_HOME)/fileWriter.cpp

# Include directories
INCS = -I$(PROJECT_HOME)

# Libraries
LIBS = -lstdc++fs

# Objective files to build
OBJS = $(addprefix $(OBJ_DIR)/, $(addsuffix .o, $(basename $(notdir $(SRCS)))))

# Get information about current kernel to distinguish between RedHat6 vs. Redhat7
OS = $(shell uname -r | cut -d. -f4)

# Compiler and linker to use
ifeq "$(OS)" "el6"
  CC = g++4.8.2
  GCC_LIB = /usr/local/geneva/gcc-4.8.2/lib64
else
  #CC = g++
  CC = /usr/local/geneva/packages/devtoolset-7/root/usr/bin/g++
  GCC_LIB = /usr/local/geneva/packages/devtoolset-7/root/usr/lib64
endif

CFLAGS = -std=gnu++17 -Wall -pthread
#CFLAGS = -std=gnu++11 -Wall -pthread -O3 -s -DNDEBUG
LD = $(CC)
LDFLAGS += -Wl,-rpath,$(GCC_LIB) -pthread

# Build executable
$(EXE): $(OBJS)
	LD_LIBRARY_PATH=$(GCC_LIB); $(LD) $(LDFLAGS) -o $(EXE) $(OBJS) $(LIBS)

# Compile source files
# Add -MP to generate dependency list
# Add -MMD to not include system headers
$(OBJ_DIR)/%.o: $(PROJECT_HOME)/%.cpp Makefile
	-mkdir -p $(OBJ_DIR)
	LD_LIBRARY_PATH=$(GCC_LIB); $(CC) -c -MP -MMD $(CFLAGS) $(INCS) -o $(OBJ_DIR)/$*.o $<
	
# Delete all intermediate files
clean: 
#	@echo OBJS = $(OBJS)
	rm -rf $(EXE) $(OBJ_DIR) core

#
# Read the dependency files.
# Note: use '-' prefix to don't display error or warning
# if include file do not exist (just remade it)
#
-include $(OBJS:.o=.d)


