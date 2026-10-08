"""Build a Qin2 Pro 4.14 DTB from the already merged source DT.

This is an offline artifact generator.  It does not touch the kernel source
tree and does not flash a device.
"""
from pathlib import Path
import re
import subprocess

HERE = Path(__file__).resolve().parent
INPUT = HERE.parent / "logs_20260928" / "kernel414_attempt1" / "merged-sp9863a-1h10.dts"
OUTPUT = HERE / "merged-qin414.dts"
DTB = HERE / "merged-qin414.dtb"
DTC = "/home/kevin/los/out/host/linux-x86/bin/dtc"

BOOTARGS = (
    "earlycon=sprd_serial,0x70100000,115200n8 "
    "console=ttyS1,115200n8 loglevel=8 ignore_loglevel "
    "init=/init root=/dev/mmcblk0p30 rootfstype=ext4 ro rootwait "
    "androidboot.hardware=s9863a1h10 androidboot.dtbo_idx=0 swiotlb=1 "
    "androidboot.selinux=enforcing androidboot.serialno=Qin2Pro1911010597 "
    "androidboot.verifiedbootstate=orange androidboot.flash.locked=0 "
    "panic=10 log_buf_len=4M "
    "lcd_id=ID67191 lcd_name=lcd_rm67191_edo_mipi_fhd "
    "lcd_base=9d654000 lcd_size=1440x576 pixel_clock=67000000"
)

PANEL = r'''

		lcd_rm67191_edo_mipi_fhd {
			phandle = <0x190>;
			sprd,dsi-work-mode = <0x01>;
			sprd,dsi-lane-number = <0x04>;
			sprd,dsi-color-format = "rgb888";
			sprd,width-mm = <0x44>;
			sprd,height-mm = <0x79>;
			sprd,esd-check-enable = <0x00>;
			sprd,esd-check-mode = <0x00>;
			sprd,initial-command = [13 78 00 01 11 13 0a 00 01 29];
			sprd,sleep-in-command = [13 0a 00 01 28 13 78 00 01 10 39 00 00 03 41 5a 0e 23 00 00 02 80 70 39 00 00 03 41 00 2f];
			sprd,sleep-out-command = [13 78 00 01 11 13 64 00 01 29];

			display-timings {
				native-mode = <0x191>;

				timing0 {
					phandle = <0x191>;
					clock-frequency = <0x03fcae40>;
					hactive = <0x240>;
					vactive = <0x5a0>;
					hback-porch = <0x19>;
					hfront-porch = <0x2d>;
					vback-porch = <0x32>;
					vfront-porch = <0xb4>;
					hsync-len = <0x0e>;
					vsync-len = <0x06>;
				};
			};
		};
'''

def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one occurrence, got {count}")
    return text.replace(old, new)

text = INPUT.read_text(encoding="utf-8")

# The source DPU already has logo-memory, but assert this so a future input
# cannot silently lose the logo buffer reference.
if 'sprd,logo-memory = <0x161>;' not in text:
    raise RuntimeError("4.14 DPU is missing sprd,logo-memory")

old = 'bootargs = "earlycon=sprd_serial,0x70100000,115200n8 console=ttyS1,115200n8 loglevel=1 init=/init root=/dev/ram0 rw printk.devkmsg=on androidboot.boot_devices=soc/soc:ap-ahb/20600000.sdio swiotlb=1";'
text = replace_once(text, old, f'bootargs = "{BOOTARGS}";', "chosen bootargs")

# Match the official U-Boot/Linux hand-off addresses used by the stock DT.
text = replace_once(
    text,
    'framebuffer@fe769000 {\n\t\t\tphandle = <0x15f>;\n\t\t\treg = <0x00 0xfe769000 0x00 0x1817000>;\n\t\t};',
    'framebuffer@9d000000 {\n\t\t\tphandle = <0x15f>;\n\t\t\treg = <0x00 0x9d000000 0x00 0x2000000>;\n\t\t};',
    "framebuffer reserved memory",
)
text = replace_once(
    text,
    'logobuffer@9e000000 {\n\t\t\tphandle = <0x161>;\n\t\t\treg = <0x00 0x9e000000 0x00 0x7e9000>;\n\t\t};',
    'logobuffer@9d654000 {\n\t\t\tphandle = <0x161>;\n\t\t\treg = <0x00 0x9d654000 0x00 0x7e9000>;\n\t\t};',
    "logo reserved memory",
)
text = replace_once(
    text,
    'ramoops@fff80000 {\n\t\t\tcompatible = "ramoops";\n\t\t\treg = <0x00 0xfff80000 0x00 0x40000>;',
    'ramoops@877c0000 {\n\t\t\tcompatible = "ramoops";\n\t\t\treg = <0x00 0x877c0000 0x00 0x40000>;',
    "pstore reserved memory",
)

# Fix aliases that still refer to the pre-patch reserved-memory names.  The
# decompiled DTS can contain either a literal ``\\0`` terminator (from some
# dtc versions) or no terminator at all, so accept both forms.  The old
# generator only handled the first spelling; the current source uses the
# second, leaving pstore_reserved pointing at the nonexistent 0xfff80000 node.
for old, new in (
    (
        'fb_reserved = "/reserved-memory/framebuffer@fe769000\\0";',
        'fb_reserved = "/reserved-memory/framebuffer@9d000000\\0";',
    ),
    (
        'fb_reserved = "/reserved-memory/framebuffer@fe769000";',
        'fb_reserved = "/reserved-memory/framebuffer@9d000000";',
    ),
    (
        'logo_reserved = "/reserved-memory/logobuffer@9e000000\\0";',
        'logo_reserved = "/reserved-memory/logobuffer@9d654000\\0";',
    ),
    (
        'logo_reserved = "/reserved-memory/logobuffer@9e000000";',
        'logo_reserved = "/reserved-memory/logobuffer@9d654000";',
    ),
    (
        'pstore_reserved = "/reserved-memory/ramoops@fff80000\\0";',
        'pstore_reserved = "/reserved-memory/ramoops@877c0000\\0";',
    ),
    (
        'pstore_reserved = "/reserved-memory/ramoops@fff80000";',
        'pstore_reserved = "/reserved-memory/ramoops@877c0000";',
    ),
):
    text = text.replace(old, new)

# The 4.14 panel parser uses /lcds/<lcd_name> and does not understand the
# stock 4.4 lcd67191@67191 properties.  Add the exact node it expects.
text = replace_once(text, '\t};\n\n\taliases {', PANEL + '\t};\n\n\taliases {', "RM67191 panel insertion")
text = replace_once(
    text,
    '\t\tlcd_nt35695_truly_mipi_fhd = "/lcds/lcd_nt35695_truly_mipi_fhd\\0";',
    '\t\tlcd_nt35695_truly_mipi_fhd = "/lcds/lcd_nt35695_truly_mipi_fhd\\0";\n\t\tlcd_rm67191_edo_mipi_fhd = "/lcds/lcd_rm67191_edo_mipi_fhd\\0";',
    "RM67191 alias",
)

# LOS first-stage init needs an fstab node in the DT (firmware/android/fstab).
# This build merges vendor+product content into system.img (both are real dirs
# under /system and /vendor,/product are symlinks), so there must be NO fstab
# entries that mount the stock vendor/product partitions -- doing so would
# shadow the merged trees.  Keep the fstab node present but empty.
FSTAB = '''
			fstab {
				compatible = "android,fstab";
			};
'''
# LOS first-stage init waits for a block device for EVERY name in vbmeta parts
# when any fstab entry has the avb flag.  socko/odmko don't exist on this device,
# so restrict parts to the partitions actually chained in our vbmeta image.
_vbmeta_anchor = 'parts = "socko,odmko,boot,recovery,dtbo,l_modem,pm_sys,l_ldsp,l_gdsp";'
if _vbmeta_anchor in text:
    text = text.replace(
        _vbmeta_anchor,
        'parts = "boot,system,vendor,product,dtbo,recovery,l_modem,l_ldsp,l_gdsp,pm_sys,dtb";')
_idx = text.find('parts = ')
assert _idx >= 0, "vbmeta parts anchor not found"
# find the closing '\n\t\t\t};' of the vbmeta node after the anchor
_close = text.find('\n\t\t\t};', _idx)
assert _close >= 0, "vbmeta node close not found"
_insert_at = _close + len('\n\t\t\t};')
if 'android,fstab' not in text:
    text = text[:_insert_at] + '\n' + FSTAB.rstrip('\n') + text[_insert_at:]

OUTPUT.write_text(text, encoding="utf-8", newline="\n")

def wsl_path(path: Path) -> str:
    # This workspace is on E:, which WSL exposes as /mnt/e.
    return "/mnt/" + path.drive[0].lower() + path.as_posix()[2:]

cmd = [
    "wsl.exe", "-d", "Ubuntu-20.04", "--", DTC, "-@", "-I", "dts",
    "-O", "dtb", "-o", wsl_path(DTB), wsl_path(OUTPUT),
]
print("running:", " ".join(cmd))
subprocess.run(cmd, check=True)
print(f"wrote {OUTPUT} ({OUTPUT.stat().st_size} bytes)")
print(f"wrote {DTB} ({DTB.stat().st_size} bytes)")
