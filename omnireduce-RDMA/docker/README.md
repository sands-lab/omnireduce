# Docker usage
The docker image includes PyTorch with OmniReduce and some experiments in [this repo](https://github.com/sands-lab/omnireduce-experiments).
To build the docker image, run:

    docker build -t omnireduce/pytorch:exps . -f Dockerfile