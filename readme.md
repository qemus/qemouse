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

## Examples

Show information about a UDF image:

```bash
udfread info disc.iso
```

List the root directory or recursively inspect a subdirectory:

```bash
udfread ls -l disc.iso /
udfread ls -l -R disc.iso /documents
```

Show information about a file:

```bash
udfread stat disc.iso /documents/manual.pdf
```

Write a text file to standard output:

```bash
udfread cat disc.iso /README.TXT
```

Extract one file without unpacking the complete image:

```bash
udfread extract disc.iso /images/logo.png ./logo.png
```

Read only a selected byte range from a large file:

```bash
udfread range -o file-header.bin disc.iso /data/archive.bin 0 4096
udfread range disc.iso /data/archive.bin 0x1000 0x200 | sha256sum
```

`range` performs a real logical seek through `libudfread`, so it reads only the
requested part of a file even when the UDF file uses multiple allocation
extents.

Numbers may be decimal or hexadecimal with a `0x` prefix.

Show how the logical blocks of a file map to physical locations in the image:

```bash
udfread map disc.iso /video/movie.m2ts
udfread blocks disc.iso /video/movie.m2ts 0 16
```

## Case-insensitive paths

Use `-i` or `--ignore-case` with any command that accepts a UDF path to resolve
each path component using ASCII case-insensitive matching:

```bash
udfread stat -i disc.iso /DOCUMENTS/MANUAL.PDF
udfread extract --ignore-case disc.iso /IMAGES/LOGO.PNG ./logo.png
```

An exact component match always takes precedence. If no exact match exists and
multiple entries differ only by ASCII letter case, the lookup fails as
ambiguous instead of selecting an entry arbitrarily.

Output from commands such as `stat` and `ls` uses the actual path spelling
stored in the image.

## Install a release package

Download the package matching the host architecture from the latest GitHub
release and install it with `apt`:

```bash
sudo apt install ./udfread_VERSION_amd64.deb
```

The package has no runtime dependency on a separately installed `libudfread`
package.

## Build on Debian or Ubuntu

The build automatically retrieves the newest stable upstream `libudfread` tag,
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
