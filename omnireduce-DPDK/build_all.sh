#!/bin/bash

# FLAGS: INSTALL MLX5 COLOCATED LATENCIES TIMESTAMPS TIMERS DEBUG CONDA OFFLOAD_BITMAP NOSCALING PYTORCH ALGO2 COUNTERS NO_FILL_STORE
set -e
set -x

CWD="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
DPDK_ARGS='-fPIC '
DAIET_ARGS=''
EXP_ARGS=''
PS_ARGS=''
GLOO_CMAKE_ARGS=''

if [[ $@ == *'CONDA'* ]]; then
  echo "will install libraries to ${CONDA_PREFIX:-'/'}"
  THIS_TIME=`date`
  echo "build_all.sh invoked at ${THIS_TIME} with $@" > ${CONDA_PREFIX}/build-info.txt
fi

if [[ $@ == *'MLX5'* ]]; then
  echo 'MLX5 SUPPORT'
  EXP_ARGS+='-DUSE_MLX5=1 '
fi
if [[ $@ == *'MLX4'* ]]; then
  echo 'MLX4 SUPPORT'
  EXP_ARGS+='-DUSE_MLX4=1 '
fi
if [[ $@ == *'COLOCATED'* ]]; then
  echo 'COLOCATED SET'
  DAIET_ARGS+='COLOCATED=ON '
fi
if [[ $@ == *'LATENCIES'* ]]; then
  echo 'LATENCIES SET'
  DAIET_ARGS+='LATENCIES=ON '
fi
if [[ $@ == *'TIMESTAMPS'* ]]; then
  echo 'TIMESTAMPS SET'
  DAIET_ARGS+='TIMESTAMPS=ON '
fi
if [[ $@ == *'COUNTERS'* ]]; then
  echo 'COUNTERS SET'
  DAIET_ARGS+='COUNTERS=ON '
fi
if [[ $@ == *'ALGO2'* ]]; then
  echo 'ALGO2 SET'
  DAIET_ARGS+='ALGO2=ON '
  PS_ARGS+='ALGO2=ON '
fi
if [[ $@ == *'TIMERS'* ]]; then
  echo 'TIMERS SET'
  DAIET_ARGS+='TIMERS=ON '
  PS_ARGS+='TIMERS=ON '
fi
if [[ $@ == *'NO_FILL_STORE'* ]]; then
  echo 'NO_FILL_STORE SET'
  DAIET_ARGS+='NO_FILL_STORE=ON '
fi
if [[ $@ == *'DEBUG'* ]]; then
  echo 'DEBUG SET'
  DAIET_ARGS+='DEBUG=ON COUNTERS=ON '
  DPDK_ARGS+='-g -O0 '
  PS_ARGS+='DEBUG=ON '
  EXP_ARGS+='-DDEBUG=1 '
fi
if [[ $@ == *'CONDA'* ]]; then
  GLOO_CMAKE_ARGS+='-DCMAKE_CXX_FLAGS="-D_GLIBCXX_USE_CXX11_ABI=1" '
  GLOO_CMAKE_ARGS+="-DCMAKE_INSTALL_PREFIX=${CONDA_PREFIX} "
  EXP_ARGS+='-DCMAKE_CXX_FLAGS="-D_GLIBCXX_USE_CXX11_ABI=1"'
  DAIET_EXTRA_CXX_FLAGS+="-I${CONDA_PREFIX}/include -L${CONDA_PREFIX}/lib "
fi
if [[ $@ == *'OFFLOAD_BITMAP'* ]]; then
  echo 'OFFLOAD_BITMAP SET'
  DAIET_ARGS+='OFFLOAD_BITMAP=ON '
  OFFLOAD_BITMAP=1
else
  OFFLOAD_BITMAP=0
fi
if [[ $@ == *'NOSCALING'* ]]; then
  echo 'NOSCALING SET'
  DAIET_ARGS+='NOSCALING=ON '
  PS_ARGS+='NOSCALING=ON '
fi

# Build DPDK
cd $CWD/daiet/lib/dpdk/

if [[ $@ != *'SKIP_DPDK'* ]]; then
  rm -rf build

  if [[ $@ == *'MLX5'* ]]; then
    sed -i 's/CONFIG_RTE_LIBRTE_MLX5_PMD=n/CONFIG_RTE_LIBRTE_MLX5_PMD=y/' config/common_base
  else
    sed -i 's/CONFIG_RTE_LIBRTE_MLX5_PMD=y/CONFIG_RTE_LIBRTE_MLX5_PMD=n/' config/common_base
  fi
  if [[ $@ == *'MLX4'* ]]; then
    sed -i 's/CONFIG_RTE_LIBRTE_MLX4_PMD=n/CONFIG_RTE_LIBRTE_MLX4_PMD=y/' config/common_base
  else
    sed -i 's/CONFIG_RTE_LIBRTE_MLX4_PMD=y/CONFIG_RTE_LIBRTE_MLX4_PMD=n/' config/common_base
  fi

  make defconfig T=x86_64-native-linuxapp-gcc
  make EXTRA_CFLAGS="${DPDK_ARGS}" -j

  if [[ $@ == *'INSTALL'* ]]; then
    if [[ $@ == *'CONDA'* ]]; then
      make install-sdk install-runtime prefix=${CONDA_PREFIX}
    else
      make install
    fi
  fi
fi


if [[ $@ != *'SKIP_DAIET'* ]]; then
  cd $CWD/daiet
  # Build DAIET
  make clean
  rm -rf build
  EXTRA_CXX_FLAGS=${DAIET_EXTRA_CXX_FLAGS} make ${DAIET_ARGS} -j
  if [[ $@ == *'INSTALL'* ]]; then
    if [[ $@ == *'CONDA'* ]]; then
      make libinstall PREFIX=${CONDA_PREFIX}
    else
      make libinstall
    fi
  fi
fi

if [[ $@ != *'SKIP_GLOO'* ]]; then
  cd $CWD/gloo
  # Build Gloo
  rm -rf build
  mkdir build
  cd build

  if [[ $@ == *'DEBUG'* ]]; then
    CXXFLAGS='-g -O0' cmake -DUSE_DAIET=1 -DUSE_REDIS=1 -DUSE_AVX=1 -DUSE_MPI=1 $GLOO_CMAKE_ARGS ..
  else
    cmake -DBUILD_TEST=OFF -DBUILD_BENCHMARK=OFF -DUSE_DAIET=1 -DUSE_REDIS=1 -DUSE_AVX=1 -DUSE_MPI=1 $GLOO_CMAKE_ARGS ..
  fi

  make -j
  if [[ $@ == *'INSTALL'* ]]; then
    cd $CWD/gloo/build
    if [[ $@ == *'CONDA'* ]]; then
      cmake -DCMAKE_INSTALL_PREFIX=${CONDA_PREFIX} ..
    fi
    make install
  fi
fi


# Build experiments
if [[ $@ != *'SKIP_EXPS'* ]]; then
  cd $CWD/daiet/experiments/exp1/
  mkdir -p build
  cd build
  find . ! -name 'daiet.cfg'   ! -name '.'  ! -name '..' -exec rm -rf {} +
  cmake ${EXP_ARGS} ..
  make -j
fi

if [[ $@ != *'SKIP_EXPS'* ]]; then
  cd $CWD/daiet/experiments/exp2/
  mkdir -p build
  cd build
  find . ! -name 'daiet.cfg'   ! -name '.'  ! -name '..' -exec rm -rf {} +
  cmake ${EXP_ARGS} ..
  make -j
fi

# Build example
if [[ $@ != *'SKIP_EXAMPLE'* ]]; then
  cd $CWD/daiet/example
  mkdir -p build
  cd build
  find . ! -name 'daiet.cfg'   ! -name '.'  ! -name '..' -exec rm -rf {} +
  cmake ${EXP_ARGS} ..
  make -j
fi

# Build dedicated PS
if [[ $@ != *'SKIP_PS'* ]]; then
  cd $CWD/daiet/ps
  make clean
  make ${PS_ARGS} -j
fi

# Build PyTorch
if [[ $@ == *'PYTORCH'* ]]; then
  cd $CWD/pytorch
  OFFLOAD_BITMAP=$OFFLOAD_BITMAP BUILD_TEST=0 BUILD_CAFFE2=0 USE_SYSTEM_NCCL=1 NCCL_INCLUDE_DIR=${CONDA_PREFIX}/include NCCL_LIB_DIR=${CONDA_PREFIX}/lib ${CONDA_PREFIX}/bin/python setup.py install --prefix=${CONDA_PREFIX} --record=`basename ${CONDA_PREFIX}`_files.txt
fi
