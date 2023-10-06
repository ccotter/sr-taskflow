# sr-taskflow

A demo of a Sender based TaskFlow inspired API for dynamic graph execution.

## Build instructions

This project relies on a local clone of the stdexec library.

```
git clone https://github.com/ccotter/sr-taskflow && cd sr-taskflow
git clone https://github.com/NVIDIA/stdexec
mkdir build && cd build && cmake -DCMAKE_CXX_STANDARD=20 .. && make
./sr-taskflow
```
