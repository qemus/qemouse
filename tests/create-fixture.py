#!/usr/bin/env python3

import lzma
import struct
from pathlib import Path

BLOCK_SIZE = 2048
TOTAL_BLOCKS = 320
PARTITION_START = 270

ROOT = Path(__file__).resolve().parent
FIXTURE = ROOT / "fixture.udf.xz"
EXPECTED = ROOT / "expected"

HELLO_CONTENT = b"Hello from the UDF fixture.\n"
NESTED_CONTENT = bytes(((index * 37 + 11) & 0xFF) for index in range(6144))


def put_u16(buffer: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<H", buffer, offset, value)


def put_u32(buffer: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<I", buffer, offset, value)


def put_u64(buffer: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<Q", buffer, offset, value)


def add_tag(buffer: bytearray, tag_id: int, location: int = 0) -> None:
    put_u16(buffer, 0, tag_id)
    put_u16(buffer, 2, 2)
    buffer[4] = 0
    buffer[5] = 0
    put_u16(buffer, 6, 1)
    put_u16(buffer, 8, 0)
    put_u16(buffer, 10, 0)
    put_u32(buffer, 12, location)
    buffer[4] = (sum(buffer[:4]) + sum(buffer[5:16])) & 0xFF


def encode_cs0(text: str) -> bytes:
    return b"\x08" + text.encode("ascii")


def encode_dstring(field_size: int, text: str) -> bytes:
    output = bytearray(field_size)
    encoded = encode_cs0(text)

    if len(encoded) > field_size - 1:
        raise ValueError(f"String does not fit in dstring field: {text}")

    output[:len(encoded)] = encoded
    output[-1] = len(encoded)
    return bytes(output)


def create_long_ad(length: int, lba: int, partition: int = 0) -> bytes:
    output = bytearray(16)
    put_u32(output, 0, length)
    put_u32(output, 4, lba)
    put_u16(output, 8, partition)
    return bytes(output)


def create_fid(name: str, file_entry_lba: int, is_directory: bool = False) -> bytes:
    encoded_name = encode_cs0(name)
    size = (38 + len(encoded_name) + 3) & ~3
    output = bytearray(size)

    output[18] = 0x02 if is_directory else 0
    output[19] = len(encoded_name)
    output[20:36] = create_long_ad(BLOCK_SIZE, file_entry_lba)
    put_u16(output, 36, 0)
    output[38:38 + len(encoded_name)] = encoded_name
    add_tag(output, 257)

    return bytes(output)


def create_file_entry(
    file_type: int,
    length: int,
    content: bytes | None = None,
    data_lba: int | None = None,
) -> bytes:
    output = bytearray(BLOCK_SIZE)
    add_tag(output, 261)
    put_u16(output, 20, 4)
    output[27] = file_type
    put_u64(output, 56, length)
    put_u32(output, 168, 0)

    if content is not None:
        put_u16(output, 34, 3)
        put_u32(output, 172, len(content))
        output[176:176 + len(content)] = content
    else:
        if data_lba is None:
            raise ValueError("data_lba is required for external file content")

        put_u16(output, 34, 0)
        put_u32(output, 172, 8)
        put_u32(output, 176, length)
        put_u32(output, 180, data_lba)

    return bytes(output)


def create_descriptor(tag_id: int, lba: int) -> bytearray:
    output = bytearray(BLOCK_SIZE)
    add_tag(output, tag_id, lba)
    return output


def create_image() -> bytes:
    image = bytearray(TOTAL_BLOCKS * BLOCK_SIZE)

    def write_block(block: int, data: bytes) -> None:
        if len(data) > BLOCK_SIZE:
            raise ValueError(f"Block {block} contains too much data")

        offset = block * BLOCK_SIZE
        image[offset:offset + len(data)] = data

    for block, identifier in (
        (16, b"BEA01"),
        (17, b"NSR02"),
        (18, b"TEA01"),
    ):
        descriptor = bytearray(BLOCK_SIZE)
        descriptor[1:6] = identifier
        descriptor[6] = 1
        write_block(block, descriptor)

    anchor = create_descriptor(2, 256)
    put_u32(anchor, 16, 4 * BLOCK_SIZE)
    put_u32(anchor, 20, 257)
    write_block(256, anchor)

    primary = create_descriptor(1, 257)
    primary[24:56] = encode_dstring(32, "UDFREAD_TEST")
    primary[72:200] = encode_dstring(128, "UDFREAD_TEST_SET")
    write_block(257, primary)

    partition = create_descriptor(5, 258)
    put_u16(partition, 22, 0)
    put_u32(partition, 188, PARTITION_START)
    put_u32(partition, 192, TOTAL_BLOCKS - PARTITION_START)
    write_block(258, partition)

    logical = create_descriptor(6, 259)
    put_u32(logical, 212, BLOCK_SIZE)
    logical[216] = 0
    domain = b"*OSTA UDF Compliant"
    logical[217:217 + len(domain)] = domain
    put_u16(logical, 240, 0x0102)
    logical[248:264] = create_long_ad(BLOCK_SIZE, 0)
    put_u32(logical, 264, 6)
    put_u32(logical, 268, 1)
    logical[440] = 1
    logical[441] = 6
    put_u16(logical, 442, 1)
    put_u16(logical, 444, 0)
    write_block(259, logical)

    write_block(260, create_descriptor(8, 260))

    file_set = create_descriptor(256, 0)
    file_set[400:416] = create_long_ad(BLOCK_SIZE, 1)
    write_block(PARTITION_START, file_set)

    root_content = b"".join((
        create_fid("hello.txt", 2),
        create_fid("empty.txt", 3),
        create_fid("nested", 4, is_directory=True),
    ))
    write_block(
        PARTITION_START + 1,
        create_file_entry(4, len(root_content), root_content),
    )
    write_block(
        PARTITION_START + 2,
        create_file_entry(5, len(HELLO_CONTENT), HELLO_CONTENT),
    )
    write_block(
        PARTITION_START + 3,
        create_file_entry(5, 0, b""),
    )

    nested_directory = create_fid("data.bin", 5)
    write_block(
        PARTITION_START + 4,
        create_file_entry(4, len(nested_directory), nested_directory),
    )
    write_block(
        PARTITION_START + 5,
        create_file_entry(5, len(NESTED_CONTENT), data_lba=10),
    )

    data_offset = (PARTITION_START + 10) * BLOCK_SIZE
    image[data_offset:data_offset + len(NESTED_CONTENT)] = NESTED_CONTENT

    return bytes(image)


def main() -> None:
    EXPECTED.mkdir(parents=True, exist_ok=True)
    EXPECTED.joinpath("hello.txt").write_bytes(HELLO_CONTENT)
    EXPECTED.joinpath("nested.bin").write_bytes(NESTED_CONTENT)

    compressed = lzma.compress(
        create_image(),
        format=lzma.FORMAT_XZ,
        check=lzma.CHECK_CRC32,
        preset=9 | lzma.PRESET_EXTREME,
    )
    FIXTURE.write_bytes(compressed)

    print(f"Created {FIXTURE.relative_to(ROOT.parent)} ({len(compressed)} bytes)")


if __name__ == "__main__":
    main()
