"""Explicitly rebuild only the generated x64 PollikFS v2 fixture, never user data."""
from pathlib import Path
import json
import struct
import sys
import re

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from format_pollikfs2 import format_disk, pack_inode, pack_dirent, START

def build(output):
    output = Path(output).resolve()
    assert output in {(ROOT / "build/x86_64/kernel").resolve(), (ROOT / "build/x86_64/selftest").resolve(),(ROOT/'build/x86_64/system').resolve()}
    target = output / "PollikData-test.img"
    pending = output / "PollikData-test.img.pending"
    format_disk(pending, total_size_mb=40)
    image = bytearray(pending.read_bytes())
    hello = (output / "userspace/hello.elf").read_bytes()
    dynamic = bytearray(hello)
    struct.pack_into("<H", dynamic, 16, 3)  # unsupported ET_DYN
    limit=re.search(r'^#define ELF64_MAX_IMAGE\s+\((\d+)\*1024\*1024\)',(ROOT/'kernel/arch/x86_64/elf64.h').read_text(),re.M)
    if not limit:raise ValueError('Cannot derive the real ELF64 input limit for the oversized fixture')
    oversized_bytes=int(limit.group(1))*1024*1024+1
    files = {
        "hello": hello,
        "argvtest": (output / "userspace/argvtest.elf").read_bytes(),
        "spin": (output / "userspace/schedule.elf").read_bytes(),
        "faulttest": (output / "userspace/faulttest.elf").read_bytes(),
        "empty": b"", "tiny": hello[:32], "truncated": hello[:128],
        "invalid": b"bad ELF" + bytes(121), "dynamic": bytes(dynamic),
        # Above the raised loader cap but storable via the double-indirect level.
        "large": bytes(oversized_bytes),
        "runtime": (output / "userspace/runtime.elf").read_bytes(),
        "memtest": (output / "userspace/memtest.elf").read_bytes(),
        "dirtest": (output / "userspace/dirtest.elf").read_bytes(),
        "stattest": (output / "userspace/stattest.elf").read_bytes(),
        "readtest": (output / "userspace/readtest.elf").read_bytes(),
        "hello_c": (output / "userspace/hello_c.elf").read_bytes(),
        "argv_c": (output / "userspace/argv_c.elf").read_bytes(),
        "allocator_test_c": (output / "userspace/allocator_test_c.elf").read_bytes(),
        "filesystem_test_c": (output / "userspace/filesystem_test_c.elf").read_bytes(),
        "runtime_test_c": (output / "userspace/runtime_test_c.elf").read_bytes(),
        "abi_test_o2": (output / "userspace/abi_test_o2.elf").read_bytes(),
        "abi_test_o0": (output / "userspace/abi_test_o0.elf").read_bytes(),
        "stack_guard_c": (output / "userspace/stack_guard_c.elf").read_bytes(),
        "null_fault_c": (output / "userspace/null_fault_c.elf").read_bytes(),
        "spin_c": (output / "userspace/spin_c.elf").read_bytes(),
        "sdk_files": (output / "userspace/sdk_files.elf").read_bytes(),
        "sdk_dirs": (output / "userspace/sdk_dirs.elf").read_bytes(),
        "sdk_memory": (output / "userspace/sdk_memory.elf").read_bytes(),
        "sdk_cwd": (output / "userspace/sdk_cwd.elf").read_bytes(),
        "sdk_time": (output / "userspace/sdk_time.elf").read_bytes(),
        "sdk_errno": (output / "userspace/sdk_errno.elf").read_bytes(),
        "sdk_multifile": (output / "userspace/sdk_multifile.elf").read_bytes(),
        "sdk_library": (output / "userspace/sdk_library.elf").read_bytes(),
        "sdk_write": (output / "userspace/sdk_write.elf").read_bytes(),
        "mutation_c": (output / "userspace/mutation_c.elf").read_bytes(),
        "spawn_child_c": (output / "userspace/spawn_child_c.elf").read_bytes(),
        "spawn_parent_c": (output / "userspace/spawn_parent_c.elf").read_bytes(),
        "stdio_c": (output / "userspace/stdio_c.elf").read_bytes(),
        "libc_c": (output / "userspace/libc_c.elf").read_bytes(),
        "alloc_stress_c": (output / "userspace/alloc_stress_c.elf").read_bytes(),
        "tool_stage_c": (output / "userspace/tool_stage_c.elf").read_bytes(),
        "tool_driver_c": (output / "userspace/tool_driver_c.elf").read_bytes(),
        "big_elf_c": (output / "userspace/big_elf_c.elf").read_bytes(),
        "tcc": (output / "userspace/tcc.elf").read_bytes(),
        "selfhost_driver_c": (output / "userspace/selfhost_driver_c.elf").read_bytes(),
        "pollish": (output / "userspace/pollish.elf").read_bytes(),
        "pipe_nowait.pol": (output / "userspace/pipe_nowait.elf").read_bytes(),
        "terminal.pol": (output / "userspace/terminal.elf").read_bytes(),
        "windowdemo.pol": (output / "userspace/windowdemo.elf").read_bytes(),
        "desktop.pol": (output / "userspace/desktop.elf").read_bytes(),
        "files.pol": (output / "userspace/files.elf").read_bytes(),
        "browser.pol": (output / "userspace/browser.elf").read_bytes(),
        "notes.pol": (output / "userspace/notes.elf").read_bytes(),
        "calculator.pol": (output / "userspace/calculator.elf").read_bytes(),
        "etc/read_test.txt": b"Alpha file data\n",
        "etc/other.txt": b"Bravo file data\n",
    }
    next_block = 43
    def block():
        nonlocal next_block
        result = next_block
        next_block += 1
        image[START+1024+result//8] |= 1 << (result%8)
        return result
    # /bin holds more than one 1024-byte directory block; extend inode 2.
    bin_extra = block()
    bin_extra2 = block()
    bin_extra3 = block()
    image[START+(5+2//17)*1024+(2%17)*60:START+(5+2//17)*1024+(2%17)*60+60] = \
        pack_inode(2, 4096, [37, bin_extra, bin_extra2, bin_extra3])
    manifest = {}
    directory_blocks = {37: [37, bin_extra, bin_extra2, bin_extra3], 40: [40]}
    directory_slots = {37: 0, 40: 0}
    for index, (name, data) in enumerate(files.items()):
        inode = 8+index
        blocks = []
        for offset in range(0, len(data), 1024):
            b = block()
            blocks.append(b)
            chunk = data[offset:offset+1024]
            image[START+b*1024:START+b*1024+len(chunk)] = chunk
        indirect = 0
        double_indirect = 0
        assert len(blocks)-8 <= 256+256*256, f"{name} exceeds the PollikFS double-indirect limit"
        if len(blocks) > 8:
            indirect = block()
            struct.pack_into(f"<{len(blocks[8:8+256])}I", image, START+indirect*1024,
                             *blocks[8:8+256])
        if len(blocks) > 8+256:
            tail = blocks[8+256:]
            groups = [tail[i:i+256] for i in range(0, len(tail), 256)]
            assert len(groups) <= 256, f"{name} exceeds the PollikFS double-indirect limit"
            double_indirect = block()
            middle_blocks = []
            for group in groups:
                middle = block()
                middle_blocks.append(middle)
                struct.pack_into(f"<{len(group)}I", image, START+middle*1024, *group)
            struct.pack_into(f"<{len(middle_blocks)}I", image, START+double_indirect*1024,
                             *middle_blocks)
        inode_offset = START+(5+inode//17)*1024+(inode%17)*60
        image[inode_offset:inode_offset+60] = pack_inode(1, len(data), blocks[:8], indirect,
                                                         double_indirect)
        if name.startswith("etc/"):
            # Synthetic stored legacy tick counters, including high unsigned bits.
            struct.pack_into("<II", image, inode_offset+44, 0x80000001, 0xfedcba98)
        directory = 40 if name.startswith("etc/") else 37
        blocks_for_directory = directory_blocks[directory]
        slot = directory_slots[directory]
        assert slot < len(blocks_for_directory)*16, "fixture directory capacity"
        directory_slots[directory] += 1
        entry_offset = START+blocks_for_directory[slot//16]*1024+(slot%16)*64
        image[entry_offset:entry_offset+64] = pack_dirent(inode, name.split("/")[-1], 1)
        manifest[name] = {"size": len(data), "inode_offset": inode_offset, "blocks": blocks, "indirect": indirect}
    next_inode = 8+len(files)
    def inode(mode, size, blocks):
        nonlocal next_inode
        number = next_inode
        next_inode += 1
        offset = START+(5+number//17)*1024+(number%17)*60
        image[offset:offset+60] = pack_inode(mode, size, blocks)
        return number

    def add_entry(directory_inode, name, target_inode, file_type):
        inode_offset = START+(5+directory_inode//17)*1024+(directory_inode%17)*60
        values = list(struct.unpack_from("<15I", image, inode_offset))
        size, directs = values[1], values[2:10]
        for slot in range((size // 64)):
            target = START+directs[slot//16]*1024+(slot%16)*64
            if struct.unpack_from("<I", image, target)[0] == 0:
                image[target:target+64] = pack_dirent(target_inode, name, file_type)
                return
        used = (size + 1023) // 1024
        assert used < 8, "fixture directory direct-block capacity"
        directs[used] = block()
        size += 1024
        image[inode_offset:inode_offset+60] = pack_inode(2, size, directs)
        target = START+directs[used]*1024
        image[target:target+64] = pack_dirent(target_inode, name, file_type)

    def add_directory(parent_inode, name):
        directory_inode = inode(2, 1024, [block()])
        if parent_inode == 1 and name == "usr":
            target = START+36*1024+6*64
            image[target:target+64] = pack_dirent(directory_inode, name, 2)
        else:
            add_entry(parent_inode, name, directory_inode, 2)
        return directory_inode

    def add_tree_file(parent_inode, name, data):
        blocks = []
        for offset in range(0, len(data), 1024):
            data_block = block()
            blocks.append(data_block)
            chunk = data[offset:offset+1024]
            image[START+data_block*1024:START+data_block*1024+len(chunk)] = chunk
        indirect = 0
        double_indirect = 0
        if len(blocks) > 8:
            indirect = block()
            head = blocks[8:8+256]
            struct.pack_into(f"<{len(head)}I", image, START+indirect*1024, *head)
        if len(blocks) > 264:
            tail = blocks[264:]
            double_indirect = block()
            middles = []
            for start in range(0, len(tail), 256):
                middle = block()
                middles.append(middle)
                group = tail[start:start+256]
                struct.pack_into(f"<{len(group)}I", image, START+middle*1024, *group)
            struct.pack_into(f"<{len(middles)}I", image, START+double_indirect*1024, *middles)
        number = next_inode
        inode_offset = START+(5+number//17)*1024+(number%17)*60
        image[inode_offset:inode_offset+60] = pack_inode(1, len(data), blocks[:8], indirect,
                                                         double_indirect)
        # Advance through the shared inode allocator after writing the richer inode.
        nonlocal_next_inode[0] += 1
        add_entry(parent_inode, name, number, 1)

    # Native compiler sysroot. All files used after boot live in PollikFS.
    nonlocal_next_inode = [0]
    def reserve_tree_file(parent_inode, name, data):
        nonlocal next_inode
        next_inode = nonlocal_next_inode[0]
        add_tree_file(parent_inode, name, data)
        next_inode = nonlocal_next_inode[0]

    usr_inode = add_directory(1, "usr")
    include_inode = add_directory(usr_inode, "include")
    lib_inode = add_directory(usr_inode, "lib")
    share_inode = add_directory(usr_inode, "share")
    wallpapers_inode = add_directory(share_inode, "wallpapers")
    icons_inode = add_directory(share_inode,"icons")
    src_inode = add_directory(usr_inode, "src")
    libc_src_inode = add_directory(src_inode, "libc")
    pollikos_inode = add_directory(include_inode, "pollikos")
    sys_inode = add_directory(include_inode, "sys")
    tcc_inode = add_directory(lib_inode, "tcc")
    tcc_include_inode = add_directory(tcc_inode, "include")
    nonlocal_next_inode[0] = next_inode
    include_root = ROOT / "sdk/include"
    for header in sorted(include_root.glob("*.h")):
        reserve_tree_file(include_inode, header.name, header.read_bytes())
    reserve_tree_file(include_inode, "pollikos_abi.h",
                      (ROOT / "include/pollikos_abi.h").read_bytes())
    for header in sorted((include_root / "pollikos").glob("*.h")):
        reserve_tree_file(pollikos_inode, header.name, header.read_bytes())
    reserve_tree_file(pollikos_inode, "abi_numbers.h",
                      (output / "sdk/include/pollikos/abi_numbers.h").read_bytes())
    reserve_tree_file(pollikos_inode, "minimp3.h",
                      (output / "sdk/include/pollikos/minimp3.h").read_bytes())
    reserve_tree_file(pollikos_inode, "stb_image.h",
                      (output / "sdk/include/pollikos/stb_image.h").read_bytes())
    for header in sorted((include_root / "sys").glob("*.h")):
        reserve_tree_file(sys_inode, header.name, header.read_bytes())
    for header in sorted((ROOT / "third_party/tinycc/include").glob("*.h")):
        if not (include_root / header.name).exists():
            reserve_tree_file(tcc_include_inode, header.name, header.read_bytes())
    for source in sorted((ROOT / "sdk/lib").glob("*.c")):
        reserve_tree_file(libc_src_inode, source.name, source.read_bytes())
    reserve_tree_file(lib_inode, "crt0.o", (output / "sdk/crt0.o").read_bytes())
    reserve_tree_file(lib_inode, "libc.a", (output / "userspace/libc-native.a").read_bytes())
    reserve_tree_file(tcc_inode, "libtcc1.a", (output / "userspace/libtcc1.a").read_bytes())
    reserve_tree_file(wallpapers_inode, "light.png",
                      (ROOT / "assets/Background_LightTheme.png").read_bytes())
    reserve_tree_file(wallpapers_inode, "dark.png",
                      (ROOT / "assets/Background_BlackTheme.png").read_bytes())
    for name in ('welcome','files','terminal','notes','settings','browser','pollikmark','calculator',
                 'folder','folder-blue','file','trash'):
        reserve_tree_file(icons_inode,name+'.png',(ROOT/'assets/system-icons'/(name+'.png')).read_bytes())
    # Interactive shell fixture: the canonical self-host source in /home so the
    # terminal workflow can compile it without redirection support.
    reserve_tree_file(3, "hello.c",
                      b"#include <stdio.h>\n"
                      b"int main(void) {\n"
                      b"    printf(\"Hello from self-hosted PollikOS C!\\n\");\n"
                      b"    return 42;\n"
                      b"}\n")
    reserve_tree_file(3, "notes.txt",
                      b"Welcome to PollikOS Notes\n\n"
                      b"This document is saved on PollikFS in /home/notes.txt.\n"
                      b"Edit freely, then press Ctrl+S to save.\n")
    reserve_tree_file(3, "welcome.html",
                      b"<!doctype html><html><head><title>Pollik Browser</title>"
                      b"<link rel=\"stylesheet\" href=\"welcome.css\"></head><body>"
                      b"<main class=\"page\"><p class=\"eyebrow\">POLLIKOS / NATIVE WEB</p>"
                      b"<h1>Welcome to PollikOS</h1><p id=\"intro\" class=\"lead\">This page is rendered by the native x86_64 browser."
                      b" It is an HTML document styled by a separate CSS file.</p>"
                      b"<button id=\"action\">Run JavaScript action</button><p id=\"result\">Click the button to test page scripts.</p>"
                      b"<section class=\"cards\"><article><h2>HTML</h2><p>Real document parsing and text flow.</p></article>"
                      b"<article><h2>CSS</h2><p>Selectors, colors, spacing and layout.</p></article></section>"
                      b"<p class=\"note\">Use Ctrl+L or F to open another local HTML file.</p></main>"
                      b"<script src=\"welcome.js\"></script></body></html>\n")
    reserve_tree_file(3, "welcome.css",
                      b"body { background-color: #f4f6fa; color: #202939; padding: 22px; }\n"
                      b".page { max-width: 720px; margin: 8px auto; }\n"
                      b".eyebrow { color: #536b93; font-size: 12px; font-weight: 700; }\n"
                      b"h1 { color: #17243a; font-size: 32px; margin-bottom: 8px; }\n"
                      b".lead { color: #4d5b70; font-size: 18px; line-height: 28px; }\n"
                      b".cards { display: flex; gap: 16px; margin-top: 18px; }\n"
                      b"article { background-color: #ffffff; border: 1px solid #d9e0eb; border-radius: 12px; padding: 16px; width: 280px; }\n"
                      b"h2 { color: #315a91; font-size: 20px; }\n"
                      b"button { background-color: #0f766e; color: #ffffff; padding: 8px 14px; border-radius: 8px; }\n"
                      b"#result { color: #64748b; }\n"
                      b".note { color: #64748b; margin-top: 24px; }\n")
    reserve_tree_file(3, "welcome.js",
                      b"let intro = document.getElementById('intro');\n"
                      b"intro.textContent = 'JavaScript executed inside the native browser.';\n"
                      b"intro.style.color = '#315a91';\n"
                      b"document.getElementById('action').addEventListener('click', function(event) {\n"
                      b"  console.log('click listener body ran');\n"
                      b"  let result = document.querySelector('#result');\n"
                      b"  result.textContent = 'The native browser ran this click handler.';\n"
                      b"  result.style.color = '#be123c';\n"
                      b"  console.log(result.textContent);\n"
                      b"});\n"
                      b"document.title = 'Pollik Native Browser';\n")
    directory_blocks = [block(), block()]
    directory_inode = inode(2, 2048, directory_blocks)
    root_entry = START+36*1024+4*64
    image[root_entry:root_entry+64] = pack_dirent(directory_inode, "testdir", 2)
    names = ["alpha.txt", "beta.txt", "subdir", "empty.txt", ".layout", ".trashinfo", "n"*55, "omega.txt"]
    for index, name in enumerate(names):
        kind = 2 if name == "subdir" else 1
        number = inode(kind, 1024 if kind == 2 else 0, [block()] if kind == 2 else [])
        # Leave unused slots before the final entry to exercise block traversal.
        slot = index if index < 7 else 16
        offset = START+directory_blocks[slot//16]*1024+(slot%16)*64
        image[offset:offset+64] = pack_dirent(number, name, kind)
    manifest["testdir"] = {"inode": directory_inode, "blocks": directory_blocks, "names": names}
    # Standard compiler scratch directory /tmp.
    tmp_inode = inode(2, 1024, [block()])
    tmp_entry = START+36*1024+5*64
    image[tmp_entry:tmp_entry+64] = pack_dirent(tmp_inode, "tmp", 2)
    manifest["tmp"] = {"inode": tmp_inode}
    struct.pack_into("<II", image, START+16, 32768-next_block, 512-next_inode)
    pending.write_bytes(image)
    pending.replace(target)
    (output / "data-manifest.json").write_text(json.dumps(manifest, indent=2)+"\n")
    print(f"Installed {len(files)} /bin and /etc fixtures into {target}")

if __name__ == "__main__":
    build(sys.argv[1])
