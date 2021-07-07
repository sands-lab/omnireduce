# OmniReduce-DPDK

## prepare submodules
```bash
./prepare.sh [--depth=10] # optional --depth shallow copys submodules
```

## create conda environment
```bash
conda env create --prefix ../env --file environment.yml
```

## build
```bash
conda activate ../env
./build_all.sh MLX5 CONDA INSTALL NOSCALING PYTORCH HOROVOD
```

## offload bitmap (only supports PyTorch)
```bash
conda activate ../env
./build_all.sh MLX5 CONDA INSTALL OFFLOAD_BITMAP NOSCALING PYTORCH
```
