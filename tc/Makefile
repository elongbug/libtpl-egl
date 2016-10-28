#######################################################
##### Makefile to build tpl-test using libgtest.a #####
#######################################################

# Following variables will be exported from spec file
#
# GTEST_LIB_TEST
# GTEST_LIB_MOCK
# GTEST_INCLUDE
# GTEST_FLAGS
# BIN_NAME

##### Libraries #####
LD_FLAGS = -lm -lrt -lpthread -ltpl-egl -lwayland-client -lwayland-egl
CPPFLAGS += -I./

##### tpl-test srcs #####
SRCS = $(wildcard src/*.cpp)
SRCS += $(wildcard mocked/*.cpp)
HEADERS = $(wildcard src/*.h)
HEADERS += $(wildcard mocked/*.h)

##### tpl-test test cases srcs #####
TESTS = $(wildcard test/*.cpp)


OBJS      = $(SRCS:%.cpp=%.o) \
			$(TESTS:%.cpp=%.o) \
		    $(NULL)

all : $(BIN_NAME)

clean :
	rm -f $(BIN_NAME)

%.o: %.cpp
	$(CXX) -c -o $@ $< $(GTEST_INCLUDE) $(GTEST_FLAGS) $(CPPFLAGS) $(LD_FLAGS)

$(BIN_NAME) : $(OBJS) $(GTEST_LIB_TEST) $(GTEST_LIB_MOCK)
	$(CXX) -lpthread $^ -o $@ $(GTEST_FLAGS) $(CPPFLAGS) $(LD_FLAGS)

