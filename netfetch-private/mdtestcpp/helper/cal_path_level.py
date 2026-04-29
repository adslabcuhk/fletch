from collections import defaultdict

op_names = {
    0: "MKDIR", 1: "RMDIR", 2: "TOUCH", 3: "LOAD",
    4: "RM", 5: "STAT", 6: "OPEN", 7: "CLOSE",
    8: "FINISH", 9: "WARMUP", 10: "CHMOD", 11: "MV",
    12: "READDIR", 13: "STATDIR", 14: "OBSOLETE"
}

op_levels = defaultdict(list)
file_name = "/home/jz/In-Switch-FS-Metadata/path_level_workloads/linkedin_reqs_mixed.txt"

with open(file_name) as f:
    for line in f:
        line = line.strip()
        if not line:
            continue
        op_str, path = line.split(" ", 1)
        op = int(op_str)
        level = path.count("/") - 2
        op_name = op_names.get(op, f"UNKNOWN({op})")
        op_levels[op_name].append(level)

total = sum(len(levels) for levels in op_levels.values())
for op_name, levels in sorted(op_levels.items()):
    avg = sum(levels) / len(levels)
    print(f"op={op_name:<10}  count={len(levels):>12,}  avg_path_level={avg:.2f}  min={min(levels)}  max={max(levels)}")
print(f"{'total':<14}  count={total:>12,}")