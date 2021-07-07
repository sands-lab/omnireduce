MPICC  := mpicxx
CC  := g++
INCLUDE  :=-I../build/include
LIBPATH  :=-L../build
LDLIBS  := -libverbs -lboost_system -lboost_thread -lboost_chrono -lboost_program_options -lomnireduce 
CXXFLAGS  := -Wall -Wextra -fPIC -O3 -std=c++11 

APPS  :=  worker aggregator
ifeq ($(USE_CUDA),ON)
INCLUDE += -I${CUDA_HOME}/include
LIBPATH += -L${CUDA_HOME}/lib -L${CUDA_HOME}/lib64
APPS += cuda_worker
CXXFLAGS += -DUSE_CUDA
LDLIBS += -lcudart
endif

all: ${APPS}

worker: worker_test.cpp
	${MPICC} ${INCLUDE} ${LIBPATH} ${CXXFLAGS} -o $@ $^ ${LDLIBS} 

cuda_worker: cuda_worker_test.cpp
	${MPICC} ${INCLUDE} ${LIBPATH} ${CXXFLAGS} -o $@ $^ ${LDLIBS}

aggregator: aggregator_test.cpp
	${CC} ${INCLUDE} ${LIBPATH} ${CXXFLAGS} -o $@ $^ ${LDLIBS}

clean:
	rm -f *.o ${APPS} cuda_worker
