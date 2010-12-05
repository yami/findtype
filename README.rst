Introduction
============

Findtype is a home-brew gdb command. Sometimes when we inspect a raw memory,
we might know the size of its real type and the types of some of its data members.
So how do you find the type of the raw memory? One method might be searching the
source code, which has following disadvantages and limitations:
  - the search result, which is usually from grep, is noisy.
  - the indirect data members are not listed, such as data members inherited from
    base class, or data members in the immediate data members.
  - macros, templates, third party data types complicates the source code searching.

With findtype, one can simply type following command in gdb to find the type

::

    (gdb) findtype size=64 member='FILE*;struct Foo'

which means find a type whose size is 64 bytes and at least one data member is of type 'FILE*' and one is of type 'struct Foo'.

Compile
=======

It is easy to compile it into official gdb.
  - download .. _gdb: http://www.gnu.org/software/gdb/download/
  - untar gdb and copy findtype.c to gdb-top-dir/gdb
  - Modify the Makefile. You can see how typeprint.c is used in
    Makefile.
  - cd gdb-top-dir; ./configure; make;

Usage
=====

Just type

::

    (gdb) help findtype

to show the help message.
