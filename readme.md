<h1 align="center">UDFread<br />
<div align="center">
<a href="https://github.com/qemus/udfread"><img src="https://raw.githubusercontent.com/qemus/udfread/master/.github/logo.png" title="Logo" style="max-width:100%;" width="128" /></a>
</div>
<div align="center">
  
[![Build]][build_url]
[![Version]][release_url]
[![Size]][release_url]

</div>

# UDFread

UDFread is a small command-line interface for the `libudfread` library.

It can inspect and selectively read files from UDF images without mounting the
image or extracting a complete large file first.

Release binaries statically link the corresponding upstream `libudfread`
release, so users do not need to install a separate runtime library package.

## Commands

```text
udfread info IMAGE
udfread ls [-l] [-R] IMAGE [PATH]
udfread stat IMAGE PATH
udfread cat IMAGE PATH
udfread extract IMAGE PATH DESTINATION
udfread range [-o DESTINATION] IMAGE PATH OFFSET [LENGTH]
udfread map IMAGE PATH
udfread blocks IMAGE PATH FILE_BLOCK [COUNT]
```

`range` performs a real logical seek through libudfread, so this reads only the
requested part of a file even when the UDF file uses multiple allocation
extents:

```bash
udfread range -o wim-header.bin windows.iso /sources/install.wim 0 208
udfread range -o install.xml windows.iso /sources/install.wim "$xml_offset" "$xml_size"
```

Numbers may be decimal or hexadecimal with a `0x` prefix.

## Install a release package

Download the package matching the host architecture from the latest GitHub
release and install it with `apt`:

```bash
sudo apt install ./udfread_VERSION_amd64.deb
```

The package has no runtime dependency on a separately installed libudfread
package.

## Build on Debian or Ubuntu

The build automatically retrieves the newest stable upstream libudfread tag,
builds it as a static library, and links it into `udfread`:

```bash
sudo apt-get update
sudo apt-get install -y build-essential git meson ninja-build pkg-config
make
sudo make install
```

To build a specific upstream release instead, remove any previously downloaded
source and provide the desired tag:

```bash
make distclean
make LIBUDFREAD_REF=1.2.0
```

An existing source checkout can also be supplied directly:

```bash
make LIBUDFREAD_SOURCE=/path/to/libudfread LIBUDFREAD_REF=1.2.0
```

The default installation path is `/usr/local/bin/udfread`. To use another
prefix:

```bash
make clean
make
sudo make install PREFIX=/usr
```

Use `make clean` to remove build products while retaining the downloaded
upstream source, or `make distclean` to remove both.


## Stars 🌟
[![Stargazers](https://raw.githubusercontent.com/star-stats/stars/refs/heads/data/charts/qemus-udfread.svg)](https://github.com/qemus/udfread/stargazers)

[build_url]: https://github.com/qemus/udfread/
[release_url]: https://github.com/qemus/udfread/releases/

[Build]: https://github.com/qemus/udfread/actions/workflows/build.yml/badge.svg
[Size]: https://img.shields.io/badge/size-64_KB-steelblue?style=flat&color=066da5
[Version]: https://img.shields.io/github/v/tag/qemus/udfread?label=version&sort=semver&color=066da5
