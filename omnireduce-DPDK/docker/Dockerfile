FROM nvidia/cuda:10.1-devel-ubuntu18.04
RUN apt-get update && \
    DEBIAN_FRONTEND="noninteractive" apt-get install -qy \
        autotools-dev \
        bison \
        build-essential \
        ca-certificates \
        chrpath \
        coreutils \
        debhelper \
        dh-python \
        dpatch \
        ethtool \
        flex \
        gcc \
        gfortran \
        git \
        graphviz \
        iproute2 \
        kmod \
        libboost-program-options-dev \
        libboost-chrono-dev \
        libboost-system-dev \
        libboost-thread-dev \
        libc6-dev \
        libelf1 \
        libgfortran3 \
        libglib2.0-0 \
        libhiredis-dev \
        libjpeg-dev \
        libltdl-dev \
        libmnl-dev \
        libnl-3-200 \
        libnl-3-dev \
        libnl-route-3-200 \
        libnl-route-3-dev \
        libnuma-dev \
        libnuma1 \
        libpng-dev \
        libpython3-dev \
        libssl1.0.0 \
        linux-headers-$(uname -r) \
        linux-modules-$(uname -r) \
        lsb-release \
        lsof \
        m4 \
        net-tools \
        openssh-client \
        openssh-server \
        pciutils \
        perl \
        pkg-config \
        python3 \
        python3-dev \
        python3-distutils \
        swig \
        tk \
        udev \
        vim \
        wget && rm -rf /var/lib/apt/lists/*


# Allow OpenSSH to talk to containers without asking for confirmation
RUN mkdir -p /var/run/sshd && cat /etc/ssh/ssh_config | grep -v 'StrictHostKeyChecking' > /etc/ssh/ssh_config.new && \
    echo "    StrictHostKeyChecking no" >> /etc/ssh/ssh_config.new && \
    mv /etc/ssh/ssh_config.new /etc/ssh/ssh_config

# MLNX driver
ARG MOFED_VER=5.3-1.0.0.1
RUN mkdir -p /tmp/mofed && cd /tmp/mofed && \
        wget http://content.mellanox.com/ofed/MLNX_OFED-${MOFED_VER}/MLNX_OFED_LINUX-${MOFED_VER}-ubuntu18.04-$(uname -m).tgz && \
        tar -xzvf *.tgz && \
        */mlnxofedinstall --user-space-only --without-fw-update --upstream-libs --dpdk --force && \
        cd /tmp && \
        rm -rf mofed

# mamba
RUN cd ~ && \
    wget -O Mambaforge.sh https://github.com/conda-forge/miniforge/releases/latest/download/Mambaforge-$(uname)-$(uname -m).sh && \
    bash Mambaforge.sh -b && \
    /root/mambaforge/bin/mamba install \
    pip \
    python=3.7.*=*_cpython \
    cudnn=7.6 \
    nccl=2.4 \
    cudatoolkit \
    jupyter \
    matplotlib \
    astunparse numpy ninja pyyaml mkl mkl-include setuptools cmake cffi typing_extensions future six requests dataclasses \
    magma-cuda101 -y -c pytorch && \
    rm Mambaforge.sh

# Install Open MPI
RUN mkdir /tmp/openmpi && \
    cd /tmp/openmpi && \
    wget -q https://www.open-mpi.org/software/ompi/v4.1/downloads/openmpi-4.1.1.tar.gz && \
    tar zxf openmpi-4.1.1.tar.gz && \
    cd openmpi-4.1.1 && \
    ./configure --enable-orterun-prefix-by-default && \
    make -j $(nproc) all && \
    make install && \
    ldconfig && \
    rm -rf /tmp/openmpi

# Create a wrapper for OpenMPI to allow running as root by default
# Configure OpenMPI to run good defaults:
#   --bind-to none --map-by slot --mca btl_tcp_if_exclude lo,docker0
RUN mv /usr/local/bin/mpirun /usr/local/bin/mpirun.real && \
    echo '#!/bin/bash' > /usr/local/bin/mpirun && \
    echo 'mpirun.real --allow-run-as-root "$@"' >> /usr/local/bin/mpirun && \
    chmod a+x /usr/local/bin/mpirun && \
    echo "hwloc_base_binding_policy = none" >> /usr/local/etc/openmpi-mca-params.conf && \
    echo "rmaps_base_mapping_policy = slot" >> /usr/local/etc/openmpi-mca-params.conf && \
    echo "btl_tcp_if_exclude = lo,docker0" >> /usr/local/etc/openmpi-mca-params.conf

SHELL ["/root/mambaforge/bin/conda", "run", "--no-capture-output", "-n", "base", "/bin/bash", "-c"]
ENV PATH="/root/mambaforge/bin:/root/mambaforge/condabin:${PATH}"
ENV CPLUS_INCLUDE_PATH=/root/mambaforge/include LIBRARY_PATH=/root/mambaforge/lib LD_LIBRARY_PATH=/root/mambaforge/lib
ARG TORCH_CUDA_ARCH_LIST
RUN cd ~ && git clone --branch docker --depth 1 https://github.com/ChenYuHo/omnireduce.git && cd omnireduce && ./prepare.sh --depth 1 && \
    ./build_all.sh INSTALL MLX5 TIMERS CONDA OFFLOAD_BITMAP NOSCALING PYTORCH ALGO2

ARG EXPS_BASE_PATH=/root
ARG EXPS_PATH=$EXPS_BASE_PATH/exps
ARG EXPS_GIT_LINK=https://github.com/Phlix1/exps.git

RUN cd $EXPS_BASE_PATH && git clone $EXPS_GIT_LINK

#For benchmark
RUN cd $EXPS_PATH/benchmark && ln -s ~/omnireduce/daiet/example/daiet.cfg daiet.cfg

#For DeepLight
RUN mamba install scikit-learn python=3.7.*=*_cpython -y
RUN cd $EXPS_PATH/models/DeepLight && ln -s ~/omnireduce/daiet/example/daiet.cfg daiet.cfg

#For LSTM
RUN mamba install cython python=3.7.*=*_cpython -y
RUN cd $EXPS_PATH/models/LSTM/lm/log_uniform && make && python setup.py install
RUN cd $EXPS_PATH/models/LSTM && ln -s ~/omnireduce/daiet/example/daiet.cfg daiet.cfg

#For NCF
RUN mamba install numpy-indexed python=3.7.*=*_cpython -y
RUN pip install mlperf_compliance
RUN cd $EXPS_PATH/models/NCF && ln -s ~/omnireduce/daiet/example/daiet.cfg daiet.cfg

#For CNN
RUN mamba install pillow python=3.7.*=*_cpython -y
RUN mamba install torchvision=0.8.0 python=3.7.*=*_cpython -c pytorch --no-deps -y
RUN pip install --extra-index-url https://developer.download.nvidia.com/compute/redist --upgrade nvidia-dali-cuda100
RUN cd /usr/local && git clone https://github.com/NVIDIA/apex && cd apex && git reset --hard a651e2c24ecf97cbf367fd3f330df36760e1c597 && \
    pip install -v --disable-pip-version-check --no-cache-dir --global-option="--cpp_ext" --global-option="--cuda_ext" ./
RUN cd $EXPS_PATH/models/CNN && ln -s ~/omnireduce/daiet/example/daiet.cfg daiet.cfg


#For BERT
RUN pip install nvidia-pyindex 
RUN pip install nvidia-dllogger
RUN mamba install unzip -y
RUN cd $EXPS_PATH/models/BERT/dataset/checkpoint && \
    wget --content-disposition https://api.ngc.nvidia.com/v2/models/nvidia/bert_pyt_ckpt_large_qa_squad11_amp/versions/19.09.0/zip -O bert_pyt_ckpt_large_qa_squad11_amp_19.09.0.zip && \
    unzip bert_pyt_ckpt_large_qa_squad11_amp_19.09.0.zip
RUN cd $EXPS_PATH/models/BERT && ln -s ~/omnireduce/daiet/example/daiet.cfg daiet.cfg && mkdir results

ARG OMNIREDUCE_CONTAINER_PORT=2222
ENV OMNIREDUCE_CONTAINER_PORT ${OMNIREDUCE_CONTAINER_PORT}
