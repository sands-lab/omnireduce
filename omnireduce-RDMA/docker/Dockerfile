FROM nvidia/cuda:10.1-devel-ubuntu18.04

ARG https_proxy
ARG http_proxy

ARG OMNIREDUCE_BASE_PATH=/usr/local
ARG OMNIREDUCE_PATH=$OMNIREDUCE_BASE_PATH/omnireduce
ARG OMNIREDUCE_GIT_LINK=https://github.com/Phlix1/omnireduce.git
ARG OMNIREDUCE_BRANCH=master

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update
RUN apt-get install -y --allow-downgrades --allow-change-held-packages --no-install-recommends \
        build-essential \
        tzdata \
        ca-certificates \
        git \
        curl \
        wget \
        vim \
        cmake \
        lsb-release \
        libcudnn7=7.6.0.64-1+cuda10.1 \
        libnuma-dev \
        ibverbs-providers \
        librdmacm-dev \
        ibverbs-utils \
        rdmacm-utils \
        libibverbs-dev \
        python3 \
        python3-dev \
        python3-pip \
        python3-setuptools \
        libnccl2=2.4.7-1+cuda10.1 \
        libnccl-dev=2.4.7-1+cuda10.1 \
        iputils-ping \
        net-tools \
        perftest 

RUN apt-get install -y --no-install-recommends openssh-client openssh-server && \
    mkdir -p /var/run/sshd

RUN apt-get install -y --no-install-recommends libboost-all-dev=1.65.1.0ubuntu1

RUN cd /usr/local && \
    wget https://repo.continuum.io/miniconda/Miniconda3-latest-Linux-x86_64.sh -O ./miniconda.sh && \
    bash miniconda.sh -b -p /usr/local/conda && \
    rm miniconda.sh
ENV PATH $PATH:/usr/local/conda/bin

RUN conda install -y -c conda-forge -c defaults -c pytorch magma-cuda101 mkl mkl-include ninja numpy=1.20.1 pyyaml scipy setuptools six=1.15.0 cffi typing_extensions future requests dataclasses

RUN cd $OMNIREDUCE_BASE_PATH && git clone $OMNIREDUCE_GIT_LINK && cd $OMNIREDUCE_PATH && make USE_CUDA=ON && cp $OMNIREDUCE_PATH/build/libomnireduce.so /usr/lib/x86_64-linux-gnu/ && \
    cp -r $OMNIREDUCE_PATH/build/include/omnireduce /usr/include/ && cd $OMNIREDUCE_PATH/example && CUDA_HOME=/usr/local/cuda/ make USE_CUDA=ON

RUN cd $OMNIREDUCE_BASE_PATH && git clone --recursive https://github.com/pytorch/pytorch && cd $OMNIREDUCE_BASE_PATH/pytorch && git checkout 57bffc3a8e4fee0cce31e1ff1f662ccf7b16db57 && \
    git submodule sync && git submodule update --init --recursive && git apply $OMNIREDUCE_PATH/frameworks_integration/pytorch_patch/omnireduce-pytorch.patch && \
    USE_SYSTEM_NCCL=0 /usr/local/conda/bin/python setup.py install

##experiments

ARG EXPS_BASE_PATH=/home
ARG EXPS_PATH=$EXPS_BASE_PATH/exps
ARG EXPS_GIT_LINK=https://github.com/Phlix1/exps.git

RUN cd /usr/bin && rm python

RUN cd $EXPS_BASE_PATH && git clone $EXPS_GIT_LINK

#For benchmark
RUN cd $EXPS_PATH/benchmark && ln -s $OMNIREDUCE_PATH/example/omnireduce.cfg omnireduce.cfg

#For DeepLight
RUN pip install -U scikit-learn
RUN cd $EXPS_PATH/models/DeepLight && ln -s $OMNIREDUCE_PATH/example/omnireduce.cfg omnireduce.cfg

#For LSTM
RUN conda install -y -c conda-forge -c defaults -c pytorch cython
RUN cd $EXPS_PATH/models/LSTM/lm/log_uniform && make && python setup.py install
RUN cd $EXPS_PATH/models/LSTM && ln -s $OMNIREDUCE_PATH/example/omnireduce.cfg omnireduce.cfg

#For NCF
RUN conda install -y -c conda-forge numpy-indexed
RUN pip install mlperf_compliance
RUN cd $EXPS_PATH/models/NCF && ln -s $OMNIREDUCE_PATH/example/omnireduce.cfg omnireduce.cfg

#For CNN
RUN pip install Pillow
RUN pip install torchvision===0.8.0 --no-dependencies
RUN pip install --extra-index-url https://developer.download.nvidia.com/compute/redist --upgrade nvidia-dali-cuda100
RUN cd /usr/local && git clone https://github.com/NVIDIA/apex && cd apex && git reset --hard a651e2c24ecf97cbf367fd3f330df36760e1c597 && \
    pip install -v --disable-pip-version-check --no-cache-dir --global-option="--cpp_ext" --global-option="--cuda_ext" ./
RUN cd $EXPS_PATH/models/CNN && ln -s $OMNIREDUCE_PATH/example/omnireduce.cfg omnireduce.cfg

#For BERT
RUN pip install nvidia-pyindex
RUN pip install nvidia-dllogger
RUN conda install -y unzip
RUN cd $EXPS_PATH/models/BERT/dataset/checkpoint && \
    wget --content-disposition https://api.ngc.nvidia.com/v2/models/nvidia/bert_pyt_ckpt_large_qa_squad11_amp/versions/19.09.0/zip -O bert_pyt_ckpt_large_qa_squad11_amp_19.09.0.zip && \
    unzip bert_pyt_ckpt_large_qa_squad11_amp_19.09.0.zip
RUN cd $EXPS_PATH/models/BERT && ln -s $OMNIREDUCE_PATH/example/omnireduce.cfg omnireduce.cfg && mkdir results

RUN pip install jupyter
RUN pip install matplotlib
RUN sed -i 's/#PermitRootLogin prohibit-password/PermitRootLogin yes/' /etc/ssh/sshd_config
RUN sed -i 's/#   StrictHostKeyChecking ask/    StrictHostKeyChecking no/' /etc/ssh/ssh_config
ENTRYPOINT /usr/sbin/sshd -p 2222 && /bin/bash
