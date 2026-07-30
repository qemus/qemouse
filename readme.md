<div align="center">
<a href="https://github.com/qemus/udfread"><img src="https://raw.githubusercontent.com/qemus/udfread/master/.github/logo.png" title="Logo" style="max-width:100%;" width="128" /></a>
</div>
<div align="center">
  
[![Build]][build_url]
[![Version]][release_url]
[![Size]][release_url]

</div>

# UDFread

UDFread is a small command-line interface for VideoLAN's read-only
`libudfread` library. The installed binary is named `udfread`.

It can inspect and selectively read files from UDF images without mounting the
image or extracting a complete large file first.

## Commands

```text
udfread info IMAGE
udfread ls \[-l] \[-R] IMAGE \[PATH]
udfread stat IMAGE PATH
udfread cat IMAGE PATH
udfread extract IMAGE PATH DESTINATION
udfread range \[-o DESTINATION] IMAGE PATH OFFSET \[LENGTH]
udfread map IMAGE PATH
udfread blocks IMAGE PATH FILE\_BLOCK \[COUNT]
```

`range` performs a real logical seek through libudfread, so this reads only the
requested part of a file even when the UDF file uses multiple allocation
extents:

```bash
udfread range -o wim-header.bin windows.iso /sources/install.wim 0 208
udfread range -o install.xml windows.iso /sources/install.wim "$xml\_offset" "$xml\_size"
```

Numbers may be decimal or hexadecimal with a `0x` prefix.

## Build on Debian or Ubuntu

```bash
sudo apt-get update
sudo apt-get install -y build-essential pkg-config libudfread-dev
make
sudo make install
```

The default installation path is `/usr/local/bin/udfread`. To use another
prefix:

```bash
make clean
make
sudo make install PREFIX=/usr
```

## Build on Fedora

```bash
sudo dnf install gcc make pkgconf-pkg-config libudfread-devel
make
sudo make install
```

## Build on Alpine

```bash
sudo apk add build-base pkgconf libudfread-dev
make
sudo make install
```

## Design notes

The CLI covers the useful filesystem-facing parts of libudfread:

* volume identifiers;
* directory traversal;
* file opening and sizing;
* seekable byte-stream reads;
* logical block reads;
* logical-file-block to physical-LBA mapping;
* library version reporting.

`udfread\_open\_input()` is intentionally not represented as a command because it
is an embedding API for applications that provide their own block backend.
There is also no write support because libudfread itself is read-only.

The `blocks` command writes complete 2048-byte logical blocks. The last block
can therefore include bytes beyond the logical EOF. Use `range` when exact byte
length matters.

## Stars 🌟
[![Stargazers](https://raw.githubusercontent.com/star-stats/stars/refs/heads/data/charts/qemus-udfread.svg)](https://github.com/qemus/udfread/stargazers)

[build_url]: https://github.com/qemus/udfread/
[release_url]: https://github.com/qemus/udfread/releases/

[Build]: https://github.com/qemus/udfread/actions/workflows/build.yml/badge.svg
[Size]: https://img.shields.io/badge/size-712_KB-steelblue?style=flat&color=066da5
[Version]: https://img.shields.io/github/v/tag/qemus/udfread?label=version&sort=semver&color=066da5
