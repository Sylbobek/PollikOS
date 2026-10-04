import struct
import sys
from pathlib import Path

POLLIK2_MAGIC = 0x504B4632  # "PKF2"
POLLIK2_BLOCK_SIZE = 1024
POLLIK2_TOTAL_BLOCKS = 32768
POLLIK2_INODE_COUNT = 512
START = 64 * 512  # 32768 bytes

def inspect_filesystem(disk_path):
    p = Path(disk_path)
    if not p.exists() or p.stat().st_size < START + 1024:
        return {"status": "empty", "message": "Image does not exist or is too small"}

    with open(p, "rb") as f:
        f.seek(START)
        sb_bytes = f.read(52)
        if len(sb_bytes) < 52:
            return {"status": "unsupported", "message": "Superblock truncated"}

        fields = struct.unpack("<13I", sb_bytes)
        magic, bsize, tot_blks, ino_cnt, free_blks, free_inos, root_ino, bmap_blk, bmap_cnt, itbl_blk, itbl_cnt, dblk_start, gen = fields

        info = {
            "magic": hex(magic),
            "block_size": bsize,
            "total_blocks": tot_blks,
            "inode_count": ino_cnt,
            "free_blocks": free_blks,
            "free_inodes": free_inos,
            "root_inode": root_ino,
            "bitmap_block": bmap_blk,
            "bitmap_count": bmap_cnt,
            "inode_table_block": itbl_blk,
            "inode_table_count": itbl_cnt,
            "data_blocks_start": dblk_start,
            "generation": gen
        }

        if (magic == POLLIK2_MAGIC and bsize == POLLIK2_BLOCK_SIZE and tot_blks == POLLIK2_TOTAL_BLOCKS and
            ino_cnt == POLLIK2_INODE_COUNT and root_ino == 1 and bmap_blk == 1 and bmap_cnt == 4 and
            itbl_blk == 5 and itbl_cnt == 31 and dblk_start == 36):
            return {"status": "current", "info": info}

        if (magic == POLLIK2_MAGIC and bsize == POLLIK2_BLOCK_SIZE and tot_blks == POLLIK2_TOTAL_BLOCKS and
            ino_cnt == POLLIK2_INODE_COUNT and root_ino == 1 and bmap_blk == 1 and bmap_cnt == 4 and
            itbl_blk == 5 and itbl_cnt == 30 and dblk_start == 35):
            return {"status": "legacy_30_35", "info": info}

        return {"status": "unsupported", "info": info, "message": f"Non-matching geometry: itbl_cnt={itbl_cnt}, dblk_start={dblk_start}, magic={hex(magic)}"}

def migrate_filesystem(disk_path):
    inspection = inspect_filesystem(disk_path)
    status = inspection["status"]

    if status == "current":
        print(f"PollikFS v2 on {disk_path} is already up to date (geometry [31, 36]).")
        return True

    if status != "legacy_30_35":
        print(f"ERROR: Cannot migrate filesystem on {disk_path}: status is '{status}'. {inspection.get('message', '')}")
        return False

    with open(disk_path, "r+b") as f:
        # 1. Read superblock
        f.seek(START)
        sb_data = bytearray(f.read(512))
        fields = list(struct.unpack("<13I", sb_data[:52]))

        # fields: magic, bsize, tot_blks, ino_cnt, free_blks, free_inos, root_ino, bmap_blk, bmap_cnt, itbl_blk, itbl_cnt, dblk_start, gen
        bmap_blk = fields[7]
        bmap_cnt = fields[8]
        itbl_blk = fields[9]
        itbl_cnt = fields[10]

        # 2. Read bitmap
        f.seek(START + bmap_blk * POLLIK2_BLOCK_SIZE)
        bitmap = bytearray(f.read(bmap_cnt * POLLIK2_BLOCK_SIZE))

        def is_block_used(b):
            return bool(bitmap[b // 8] & (1 << (b % 8)))

        def set_block_used(b):
            bitmap[b // 8] |= (1 << (b % 8))

        def find_free_block(start_b=36):
            for b in range(start_b, POLLIK2_TOTAL_BLOCKS):
                if not is_block_used(b):
                    return b
            return None

        # 3. Check all inodes for block 35 usage
        # 30 blocks * 17 inodes per block = 510 inodes (0..509)
        block_35_owner = None
        for ino in range(1, 510):
            tbl_blk = itbl_blk + (ino // 17)
            off = START + tbl_blk * POLLIK2_BLOCK_SIZE + (ino % 17) * 60
            f.seek(off)
            inode_bytes = f.read(60)
            if len(inode_bytes) < 60:
                continue
            imode, isize, *directs, indirect, icreated, imodified, r1, r2 = struct.unpack("<15I", inode_bytes)
            if imode == 0:
                continue

            for d_idx, d_blk in enumerate(directs):
                if d_blk == 35:
                    block_35_owner = (ino, "direct", d_idx, off)
                    break
            if block_35_owner:
                break
            if indirect == 35:
                block_35_owner = (ino, "indirect_ptr", 0, off)
                break
            elif indirect != 0:
                # Read indirect block
                f.seek(START + indirect * POLLIK2_BLOCK_SIZE)
                ind_data = f.read(POLLIK2_BLOCK_SIZE)
                ind_ptrs = struct.unpack(f"<{POLLIK2_BLOCK_SIZE // 4}I", ind_data)
                for ind_idx, ind_blk in enumerate(ind_ptrs):
                    if ind_blk == 35:
                        block_35_owner = (ino, "indirect_blk", (indirect, ind_idx), off)
                        break
            if block_35_owner:
                break

        # If block 35 was used by an inode, relocate it
        if block_35_owner:
            ino, kind, detail, inode_off = block_35_owner
            new_b = find_free_block(36)
            if not new_b:
                print("ERROR: Disk is completely full, cannot relocate block 35 during migration.")
                return False
            # Copy block 35 data to new_b
            f.seek(START + 35 * POLLIK2_BLOCK_SIZE)
            b35_data = f.read(POLLIK2_BLOCK_SIZE)
            f.seek(START + new_b * POLLIK2_BLOCK_SIZE)
            f.write(b35_data)
            set_block_used(new_b)

            # Update inode or indirect block
            f.seek(inode_off)
            inode_fields = list(struct.unpack("<15I", f.read(60)))
            if kind == "direct":
                inode_fields[2 + detail] = new_b
            elif kind == "indirect_ptr":
                inode_fields[10] = new_b
            elif kind == "indirect_blk":
                ind_blk_num, ind_slot = detail
                f.seek(START + ind_blk_num * POLLIK2_BLOCK_SIZE + ind_slot * 4)
                f.write(struct.pack("<I", new_b))

            f.seek(inode_off)
            f.write(struct.pack("<15I", *inode_fields))
            print(f"Relocated user block 35 of inode {ino} to new block {new_b}.")

        # 4. Zero block 35 so it serves as the clean 31st inode table block (inodes 510..526)
        f.seek(START + 35 * POLLIK2_BLOCK_SIZE)
        f.write(b'\x00' * POLLIK2_BLOCK_SIZE)

        # 5. Mark block 35 as allocated in bitmap
        set_block_used(35)
        f.seek(START + bmap_blk * POLLIK2_BLOCK_SIZE)
        f.write(bitmap)

        # 6. Update superblock geometry: itbl_cnt=31, dblk_start=36
        fields[10] = 31
        fields[11] = 36
        sb_data[:52] = struct.pack("<13I", *fields)
        f.seek(START)
        f.write(sb_data)
        f.flush()

    print(f"SUCCESS: Successfully migrated {disk_path} from geometry [30, 35] to [31, 36].")
    return True

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python migrate_pollikfs.py <command> <disk_image>")
        print("Commands: inspect, migrate")
        sys.exit(1)

    cmd = sys.argv[1]
    target = sys.argv[2] if len(sys.argv) > 2 else "build/PollikData.img"

    if cmd == "inspect":
        res = inspect_filesystem(target)
        print(f"STATUS:{res['status']}")
        if "info" in res:
            for k, v in res["info"].items():
                print(f"{k}={v}")
        if "message" in res:
            print(f"MESSAGE:{res['message']}")
        sys.exit(0 if res["status"] in ("current", "legacy_30_35") else 2)

    elif cmd == "migrate":
        ok = migrate_filesystem(target)
        sys.exit(0 if ok else 1)
    else:
        print(f"Unknown command: {cmd}")
        sys.exit(1)
