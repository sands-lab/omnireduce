#!/bin/bash
set -e
SCRIPTPATH="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
git submodule update --init "$@" --recursive
cd $SCRIPTPATH/pytorch
git apply $SCRIPTPATH/pytorch.patch
git rm third_party/gloo
cd third_party/protobuf
git fetch --unshallow
git checkout 09745575a923640154bcf307fba8aedff47f240a
cd $SCRIPTPATH/gloo
git apply $SCRIPTPATH/gloo.patch
