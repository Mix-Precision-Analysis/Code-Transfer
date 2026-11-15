include Makefile.config

# Default plugin to build; extend PLUGINS if you want other *.cpp -> *.so
PLUGINS := TransformType CleanupUnused

SRCS    := $(addsuffix .cpp,$(PLUGINS))
OBJS    := $(SRCS:.cpp=.o)
SONAMES := $(SRCS:.cpp=.so)

.PHONY: all clean

all: $(SONAMES)

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

%.so: %.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(SHARED_LIB_OPT) -o $@ $<

clean:
	$(RM) $(OBJS) $(SONAMES)
# python /Users/aleilei/Documents/PerciLLM/20260114/Code-Transfer-Bench/plugins/cleanup-unused.py \
#   /Users/aleilei/Documents/PerciLLM/20260114/Code-Transfer-Bench/plugins/test/mix_11.c \
#   /Users/aleilei/Documents/PerciLLM/20260114/Code-Transfer-Bench/plugins/out \
#   -I<你的包含路径...>

# python3 ../plugins/cleanup-unused.py ./out/mix_11.c ./out/ -I/home/lilei/software/gsl/include
# python3 ../plugins/cleanup-unused.py ./out/mix_11.c ./out/ -I/home/lilei/software/gsl/include --no-inplace


