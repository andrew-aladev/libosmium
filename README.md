# Osmium Library

http://osmcode.org/libosmium

A fast and flexible C++ library for working with OpenStreetMap data.

NOTE: This is a beta version of the next-generation Osmium. For production
use, see the Osmium version at https://github.com/joto/osmium .

There are a few applications that use the Osmium library in the examples
directory. See the [osmium-contrib](http://github.com/osmcode/osmium-contrib)
repository for more example code.

[![Build Status](https://secure.travis-ci.org/osmcode/libosmium.png)](http://travis-ci.org/osmcode/libosmium)
[![Build status](https://ci.appveyor.com/api/projects/status/mkbg6e6stdgq7c1b?svg=true)](https://ci.appveyor.com/project/Mapbox/libosmium)

Libosmium is developed on Linux, but also works on OSX and Windows (with some
limitations).

## Prerequisites

Because Osmium uses many C++11 features you need a modern compiler and standard
C++ library. Osmium needs at least GCC 4.8 or clang (LLVM) 3.4. (Some parts may
work with older versions.)

Different parts of Osmium (and the applications built on top of it) need
different libraries. You DO NOT NEED to install all of them, just install those
you need for the programs you need.

See the [list of dependencies](https://github.com/osmcode/libosmium/wiki/Libosmium-dependencies).


## Directories

* include: C/C++ include files. All of Osmium is in those header files which
  are needed for building Osmium applications.

* examples: Osmium example applications.

* test: Tests (see below).

* doc: Config for documentation.


## Building

Osmium is a header-only library, so there is nothing to build for the
library itself.

But there are some tests and examples that can be build. Libosmium uses
cmake:

    mkdir build
    cd build
    cmake ..
    make

This will build the examples and tests. Call `ctest` to run the tests.

For more see the
[Libosmium Wiki](https://github.com/osmcode/libosmium/wiki/Building-Libosmium).


## Testing

See the
[Libosmium Wiki](https://github.com/osmcode/libosmium/wiki/Testing-Libosmium)
for instructions.


## Osmium on 32bit Machines

Osmium works well on 64 bit machines, but on 32 bit machines there are some
problems. Be aware that not everything will work on 32 bit architectures.
This is mostly due to the 64 bit needed for node IDs. Also Osmium hasn't been
tested well on 32 bit systems. Here are some issues you might run into:

* Google Sparsehash does not work on 32 bit machines in our use case.
* The `mmap` system call is called with a `size_t` argument, so it can't
  give you more than 4GByte of memory on 32 bit systems. This might be a
  problem.

Please report any issues you have and we might be able to solve them.


## Switching from the old Osmium

See the
[Libosmium Wiki](https://github.com/osmcode/libosmium/wiki/Changes-from-old-versions-of-Osmium).


## License

The Osmium Library is available under the Boost Software License. See
LICENSE.txt.


## Authors

The Osmium Library was mainly written and is maintained by Jochen Topf
(jochen@topf.org).

Other authors:
* Peter Körner (github@mazdermind.de) (PBF writer, ...)

