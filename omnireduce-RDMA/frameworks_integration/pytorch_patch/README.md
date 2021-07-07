# Frameworks Integration
By changing a few lines of code in PyTorch we are able to delegate allreduce SUM operations to OmniReduce.

We take advantage of PyTorch's gloo backend and customize it so that it uses OmniReduce instead of Gloo for operations and data types that OmniReduce supports.
If a job is not supported by OmniReduce then PyTorch automatically fallsback to using gloo.

For OmniReduce to take over from Gloo the following conditions must be met:
- The all reduce operation must be a summation
- The data type must be float or int32
- Each node/host produces 1 tensor or in other words each node/host uses 1 GPU.

## 1. Install OmniReduce
Build OmniReduce and copy the omnireduce `dynami library` and `header files` in the `build` folder to the system `library` and `include` path.

    cp ./build/libomnireduce.so SYSTEM_INCLUDE_PATH
    cp -r ./build/include/omnireduce SYSTEM_LIBRARY_PATH

## 2. Download PyTorch
The patch applies to a specific commit which we must checkout to.
The PyTorch patch applies to a specific commit which we must checkout to.
  
    git clone https://github.com/pytorch/pytorch.git
    cd pytorch
    git checkout 57bffc3 # The 1.7.1 version
    git submodule sync
    git submodule update --init --recursive 
    
This will also take a good while to clone and checkout all submodules.
## 3. Apply patch to PyTorch 

    cd pytorch
    git apply omnireduce-pytorch.patch
    
## 4. Build PyTorch
Install Boost C++ library with below command:

    apt-get install -y libboost-all-dev=1.65.1.0ubuntu1
    
Install PyTorch dependencies and build PyTorch according to the official [repository](https://github.com/pytorch/pytorch#installation).