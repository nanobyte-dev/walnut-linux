#!/usr/bin/env python3
"""Tests for system/installer/walnut-install (docs/release/ALPHA.md M5).

Unit tests with no root permissions, no real block devices, and no real network.
"""

import hashlib
from importlib.machinery import SourceFileLoader
import importlib.util
import io
import os
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch

# Import walnut-install by path
INSTALLER_DIR = Path(__file__).resolve().parent
INSTALLER_PATH = INSTALLER_DIR / "walnut-install"
loader = SourceFileLoader("walnut_install", str(INSTALLER_PATH))
spec = importlib.util.spec_from_loader("walnut_install", loader)
walnut_install = importlib.util.module_from_spec(spec)
loader.exec_module(walnut_install)


def make_tar_xz(path: str, filenames_and_contents: dict[str, bytes]) -> None:
    """Create a minimal valid .tar.xz archive."""
    with tarfile.open(path, "w:xz") as tf:
        for fname, data in filenames_and_contents.items():
            ti = tarfile.TarInfo(fname)
            ti.size = len(data)
            ti.mtime = 0
            tf.addfile(ti, io.BytesIO(data))


def create_release_fixtures(release_dir: str, release: str = "6.12.111+ps3.648") -> dict[str, str]:
    """Populate release_dir with valid release files and matching SHA256SUMS."""
    rootfs_path = os.path.join(release_dir, "rootfs.tar.xz")
    make_tar_xz(rootfs_path, {"test.txt": b"walnut alpha rootfs dummy\n"})

    kernel_path = os.path.join(release_dir, f"vmlinux-{release}")
    with open(kernel_path, "wb") as f:
        f.write(b"\x7fELFppc64dummykernel")

    initrd_path = os.path.join(release_dir, "initrd-walnut.img")
    with open(initrd_path, "wb") as f:
        f.write(b"initrdwalnutgeneric")

    args_path = os.path.join(release_dir, f"{release}.args")
    with open(args_path, "w", encoding="utf-8") as f:
        f.write("quiet splash\n")

    files = [rootfs_path, kernel_path, initrd_path, args_path]
    sums = []
    for fp in files:
        h = hashlib.sha256()
        with open(fp, "rb") as f:
            h.update(f.read())
        sums.append(f"{h.hexdigest()}  {os.path.basename(fp)}\n")

    sha256sums_path = os.path.join(release_dir, "SHA256SUMS")
    with open(sha256sums_path, "w", encoding="utf-8") as f:
        f.writelines(sums)

    return {
        "release_dir": release_dir,
        "rootfs": rootfs_path,
        "kernel": kernel_path,
        "initrd": initrd_path,
        "args_file": args_path,
        "sha256sums": sha256sums_path,
    }


class TestChecksumVerification(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.fixtures = create_release_fixtures(self.tmp.name)
        with open(self.fixtures["sha256sums"]) as f:
            self.sums_all = f.readlines()

    def tearDown(self):
        self.tmp.cleanup()

    def test_checksum_good(self):
        info = walnut_install.verify_release_dir(self.tmp.name)
        self.assertEqual(info["release"], "6.12.111+ps3.648")
        self.assertEqual(info["rootfs"], self.fixtures["rootfs"])
        self.assertEqual(info["kernel"], self.fixtures["kernel"])
        self.assertEqual(info["initrd"], self.fixtures["initrd"])
        self.assertEqual(info["args_file"], self.fixtures["args_file"])
        self.assertEqual(len(info["verified_files"]), 4)

    def test_archive_layout(self):
        """the release archive, laid out like the two disks (stamp.py prepare); sums with paths"""
        rel = "6.12.111+ps3.726"
        d = os.path.join(self.tmp.name, "walnut-0.1.0")
        boot, inst = os.path.join(d, "dev_hdd1/linux/boot"), os.path.join(d, "dev_hdd1/linux/install")
        os.makedirs(boot)
        os.makedirs(inst)
        os.makedirs(os.path.join(d, "dev_hdd0/linux"))
        make_tar_xz(os.path.join(inst, "rootfs.tar.xz"), {"etc/hostname": b"walnut\n"})
        files = {f"dev_hdd1/linux/boot/vmlinux-{rel}": b"k", "dev_hdd1/linux/boot/initrd-walnut.img": b"i",
                 f"dev_hdd1/linux/boot/{rel}.args": b"quiet\n", "dev_hdd1/kboot.conf": b"walnut='...'\n",
                 "dev_hdd1/linux/install/install.conf": b"walnut-install 1\n", "dev_hdd0/linux/walnut.img.xz": b"xz"}
        for name, data in files.items():
            with open(os.path.join(d, name), "wb") as f:
                f.write(data)
        with open(os.path.join(d, "SHA256SUMS"), "w") as f:
            for name in ["dev_hdd1/linux/install/rootfs.tar.xz", *files]:
                with open(os.path.join(d, name), "rb") as b:
                    f.write(f"{hashlib.sha256(b.read()).hexdigest()}  {name}\n")
        info = walnut_install.verify_release_dir(d)
        self.assertEqual(info["release"], rel)
        self.assertEqual(info["kernel"], os.path.join(boot, f"vmlinux-{rel}"))
        self.assertEqual(info["initrd"], os.path.join(boot, "initrd-walnut.img"))
        self.assertEqual(info["args_file"], os.path.join(boot, f"{rel}.args"))
        self.assertEqual(info["rootfs"], os.path.join(inst, "rootfs.tar.xz"))
        self.assertEqual(len(info["verified_files"]), 7)

    def test_checksum_tampered(self):
        # Tamper with kernel file
        with open(self.fixtures["kernel"], "ab") as f:
            f.write(b"tampered")

        with self.assertRaises(ValueError) as ctx:
            walnut_install.verify_release_dir(self.tmp.name)
        self.assertIn("Checksum mismatch", str(ctx.exception))

    def test_checksum_missing_file(self):
        # Delete rootfs
        os.remove(self.fixtures["rootfs"])

        with self.assertRaises(ValueError) as ctx:
            walnut_install.verify_release_dir(self.tmp.name)
        self.assertIn("Missing file listed in SHA256SUMS", str(ctx.exception))

    def test_empty_sha256sums_fails(self):
        for text in ("", "\n", "# nothing\n"):
            with open(self.fixtures["sha256sums"], "w") as f:
                f.write(text)
            with self.assertRaises(ValueError) as ctx:
                walnut_install.verify_release_dir(self.tmp.name)
            self.assertIn("lists no files", str(ctx.exception))

    def test_garbled_sha256sums_fails(self):
        good = "".join(self.read_sums())
        for bad in ("garbage\n", "zz" * 32 + "  vmlinux-6.12.111+ps3.648\n", "abcd  rootfs.tar.xz\n",
                    "0" * 64 + "  ../escape\n", "0" * 64 + "  /etc/passwd\n"):
            with open(self.fixtures["sha256sums"], "w") as f:
                f.write(good + bad)
            with self.assertRaises(ValueError, msg=bad):
                walnut_install.verify_release_dir(self.tmp.name)

    def read_sums(self):
        with open(self.fixtures["sha256sums"]) as f:
            return f.readlines()

    def test_files_come_from_the_verified_set(self):
        """a kernel, initrd or args file that SHA256SUMS does not list is never picked"""
        d = self.tmp.name
        # SHA256SUMS lists only the rootfs and kernel; the initrd and args file sit next to them unlisted
        lines = [l for l in self.read_sums() if "initrd" not in l and ".args" not in l]
        with open(self.fixtures["sha256sums"], "w") as f:
            f.writelines(lines)
        with self.assertRaises(ValueError) as ctx:
            walnut_install.verify_release_dir(d)
        self.assertIn("No initrd", str(ctx.exception))
        # an extra unlisted kernel next to the listed ones is ignored, not "multiple kernels"
        with open(os.path.join(d, "vmlinux-9.9.9"), "wb") as f:
            f.write(b"unlisted")
        with open(self.fixtures["sha256sums"], "w") as f:
            f.writelines(self.sums_all)
        info = walnut_install.verify_release_dir(d)
        self.assertEqual(info["kernel"], self.fixtures["kernel"])
        self.assertEqual(info["release"], "6.12.111+ps3.648")

    def test_unlisted_rootfs_is_not_used(self):
        d = self.tmp.name
        with open(os.path.join(d, "evil.tar.xz"), "wb") as f:
            f.write(b"x")
        os.remove(self.fixtures["rootfs"])
        lines = [l for l in self.read_sums() if "tar.xz" not in l]
        with open(self.fixtures["sha256sums"], "w") as f:
            f.writelines(lines)
        with self.assertRaises(ValueError) as ctx:
            walnut_install.verify_release_dir(d)
        self.assertIn("No rootfs", str(ctx.exception))

    def test_checksum_missing_sha256sums(self):
        os.remove(self.fixtures["sha256sums"])

        with self.assertRaises(ValueError) as ctx:
            walnut_install.verify_release_dir(self.tmp.name)
        self.assertIn("SHA256SUMS not found", str(ctx.exception))


class TestUSBDiskRefusals(unittest.TestCase):
    def test_refusal_not_usb(self):
        def fake_runner(cmd):
            # lsblk -dno TRAN /dev/sda -> sata
            if "TRAN" in cmd:
                return "sata\n"
            if "TYPE" in cmd:
                return "disk\n"
            return ""

        with self.assertRaises(ValueError) as ctx:
            walnut_install.check_usb_disk("/dev/sda", cmd_runner=fake_runner, mounts_text="")
        self.assertIn("not 'usb'", str(ctx.exception))

    def test_refusal_partition_instead_of_disk(self):
        def fake_runner(cmd):
            if "TYPE" in cmd:
                return "part\n"
            if "TRAN" in cmd:
                return "usb\n"
            return ""

        with self.assertRaises(ValueError) as ctx:
            walnut_install.check_usb_disk("/dev/sdb1", cmd_runner=fake_runner, mounts_text="")
        self.assertIn("not a whole disk", str(ctx.exception))

    def test_refusal_mounted_lsblk(self):
        def fake_runner(cmd):
            if "TYPE" in cmd:
                return "disk\n"
            if "TRAN" in cmd:
                return "usb\n"
            if "MOUNTPOINT" in cmd:
                return "/media/usbdisk\n"
            return ""

        with self.assertRaises(ValueError) as ctx:
            walnut_install.check_usb_disk("/dev/sdb", cmd_runner=fake_runner, mounts_text="")
        self.assertIn("currently mounted", str(ctx.exception))

    def test_refusal_mounted_proc_mounts(self):
        def fake_runner(cmd):
            if "TYPE" in cmd:
                return "disk\n"
            if "TRAN" in cmd:
                return "usb\n"
            if "MOUNTPOINT" in cmd:
                return "\n"
            return ""

        mounts = "/dev/sdb1 /mnt/data ext4 rw 0 0\n"
        with self.assertRaises(ValueError) as ctx:
            walnut_install.check_usb_disk("/dev/sdb", cmd_runner=fake_runner, mounts_text=mounts)
        self.assertIn("mounted at /mnt/data", str(ctx.exception))

    def test_refusal_root_disk(self):
        def fake_runner(cmd):
            if "TYPE" in cmd:
                return "disk\n"
            if "TRAN" in cmd:
                return "usb\n"
            if "MOUNTPOINT" in cmd:
                return "\n"
            if "PKNAME" in cmd:
                return "sdb\n"
            return ""

        mounts = "/dev/sdb2 / ext4 rw,relatime 0 0\n"
        with self.assertRaises(ValueError) as ctx:
            walnut_install.check_usb_disk("/dev/sdb", cmd_runner=fake_runner, mounts_text=mounts)
        self.assertIn("PC root", str(ctx.exception))


class TestSfdiskAndMkfsGeneration(unittest.TestCase):
    def test_sfdisk_script(self):
        script = walnut_install.generate_sfdisk_script("a1b2c3d4", swap_gib=4)
        lines = script.strip().splitlines()
        self.assertEqual(lines[0], "label: dos")
        self.assertEqual(lines[1], "label-id: 0xa1b2c3d4")
        self.assertEqual(lines[2], "size=4GiB, type=82")
        self.assertEqual(lines[3], "type=83")

    def test_sfdisk_script_custom_swap(self):
        script = walnut_install.generate_sfdisk_script("12345678", swap_gib=2)
        self.assertIn("size=2GiB, type=82", script)

    def test_mkfs_args(self):
        args = walnut_install.generate_mkfs_args("/dev/sdb2")
        self.assertEqual(args[0], "mkfs.ext4")
        self.assertIn("-L", args)
        self.assertEqual(args[args.index("-L") + 1], "ps3root")
        self.assertIn("-O", args)
        self.assertEqual(args[args.index("-O") + 1], "^metadata_csum,^metadata_csum_seed,^64bit,^orphan_file")
        self.assertEqual(args[-1], "/dev/sdb2")

    def test_mkswap_args(self):
        args = walnut_install.generate_mkswap_args("/dev/sdb1")
        self.assertEqual(args, ["mkswap", "-L", "ps3swap", "/dev/sdb1"])

    def test_dry_run_output(self):
        tmp = tempfile.TemporaryDirectory()
        try:
            create_release_fixtures(tmp.name)
            fake_disk_info = {
                "disk": "/dev/sdb",
                "model": "TEST_USB_DRIVE",
                "serial": "12345678",
                "size": "64G",
            }
            with patch.object(walnut_install, "check_usb_disk", return_value=fake_disk_info):
                with patch("sys.stdout", new=io.StringIO()) as fake_out:
                    walnut_install.main(["usb", "--release-dir", tmp.name, "--disk", "/dev/sdb", "--dry-run"])
                    output = fake_out.getvalue()

            # Verify sfdisk script in dry run output
            self.assertIn("label: dos", output)
            self.assertIn("label-id: 0x", output)
            self.assertIn("size=4GiB, type=82", output)
            self.assertIn("type=83", output)

            # Verify mkfs arguments in dry run output
            self.assertIn("mkfs.ext4", output)
            self.assertIn("-L ps3root", output)
            self.assertIn("^metadata_csum,^metadata_csum_seed,^64bit,^orphan_file", output)
            self.assertIn("mkswap -L ps3swap", output)
        finally:
            tmp.cleanup()


class TestRootConfigurationFiles(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root_dir = self.tmp.name

    def tearDown(self):
        self.tmp.cleanup()

    def test_write_fstab(self):
        walnut_install.write_fstab(self.root_dir, "a1b2c3d4")
        fstab_path = os.path.join(self.root_dir, "etc", "fstab")
        self.assertTrue(os.path.isfile(fstab_path))
        with open(fstab_path, "r", encoding="utf-8") as f:
            content = f.read()

        self.assertIn("PARTUUID=a1b2c3d4-02  /     ext4   defaults,noatime  0 1", content)
        self.assertIn("PARTUUID=a1b2c3d4-01  none  swap   sw,pri=200,noauto 0 0", content)
        self.assertIn("tmpfs                 /tmp  tmpfs  defaults,size=64M 0 0", content)

    def test_update_ps3_boot_new(self):
        walnut_install.update_ps3_boot(self.root_dir, "a1b2c3d4")
        conf_path = os.path.join(self.root_dir, "etc", "default", "ps3-boot")
        self.assertTrue(os.path.isfile(conf_path))
        with open(conf_path, "r", encoding="utf-8") as f:
            content = f.read()

        self.assertEqual(
            content.strip(),
            'PS3_ROOT_ARGS="root=PARTUUID=a1b2c3d4-02 rootfstype=ext4 ro rootwait"',
        )

    def test_update_ps3_boot_existing(self):
        conf_path = os.path.join(self.root_dir, "etc", "default", "ps3-boot")
        os.makedirs(os.path.dirname(conf_path), exist_ok=True)
        with open(conf_path, "w", encoding="utf-8") as f:
            f.write('# config\nPS3_ROOT_ARGS="root=/dev/sda2 ro"\nPS3_DEFAULT=release\n')

        walnut_install.update_ps3_boot(self.root_dir, "a1b2c3d4")
        with open(conf_path, "r", encoding="utf-8") as f:
            content = f.read()

        self.assertIn('PS3_ROOT_ARGS="root=PARTUUID=a1b2c3d4-02 rootfstype=ext4 ro rootwait"', content)
        self.assertIn("PS3_DEFAULT=release", content)
        self.assertNotIn("/dev/sda2", content)

    def test_write_kboot_conf(self):
        walnut_install.write_kboot_conf(self.root_dir, "6.12.111+ps3.648", "a1b2c3d4", "quiet splash")
        kboot_path = os.path.join(self.root_dir, "etc", "kboot.conf")
        self.assertTrue(os.path.isfile(kboot_path))
        with open(kboot_path, "r", encoding="utf-8") as f:
            content = f.read()

        expected = "walnut-usb='/boot/vmlinux-6.12.111+ps3.648 root=PARTUUID=a1b2c3d4-02 rootfstype=ext4 ro rootwait quiet splash'\n"
        self.assertIn(expected, content)

    def test_write_kboot_conf_no_args(self):
        walnut_install.write_kboot_conf(self.root_dir, "6.12.111+ps3.648", "a1b2c3d4", "")
        kboot_path = os.path.join(self.root_dir, "etc", "kboot.conf")
        with open(kboot_path, "r", encoding="utf-8") as f:
            content = f.read()

        expected = "walnut-usb='/boot/vmlinux-6.12.111+ps3.648 root=PARTUUID=a1b2c3d4-02 rootfstype=ext4 ro rootwait'\n"
        self.assertIn(expected, content)


class TestInternalDryRun(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.fixtures = create_release_fixtures(self.tmp.name)
        self.stamp_mod = walnut_install.get_stamp_module()

    def tearDown(self):
        self.tmp.cleanup()

    def test_internal_dry_run_calls_stamp_with_right_arguments(self):
        with patch.object(self.stamp_mod, "install") as mock_install:
            with patch("sys.stdout", new=io.StringIO()):
                walnut_install.main([
                    "internal",
                    "--release-dir", self.tmp.name,
                    "--size", "16G",
                    "--swap-mib", "1024",
                    "--host", "192.0.2.1",
                    "--dry-run",
                ])

            mock_install.assert_called_once()
            call_args = mock_install.call_args[0][0]
            self.assertEqual(call_args.size, "16G")
            self.assertEqual(call_args.swap_mib, 1024)
            self.assertEqual(call_args.host, "192.0.2.1")
            self.assertEqual(call_args.release, "6.12.111+ps3.648")
            self.assertEqual(call_args.image, "/linux/walnut.img")
            self.assertEqual(call_args.rootfs, self.fixtures["rootfs"])
            self.assertEqual(call_args.kernel, self.fixtures["kernel"])
            self.assertEqual(call_args.initrd, self.fixtures["initrd"])
            self.assertEqual(call_args.args_file, self.fixtures["args_file"])

    def test_internal_dry_run_executes_with_mock_ftp(self):
        # Runs end-to-end through stamp_mod.install with dry-run FTP mock
        with patch("sys.stdout", new=io.StringIO()) as fake_out:
            rc = walnut_install.main([
                "internal",
                "--release-dir", self.tmp.name,
                "--size", "16G",
                "--swap-mib", "1024",
                "--host", "192.0.2.1",
                "--dry-run",
            ])
            output = fake_out.getvalue()

        self.assertEqual(rc, 0)
        # Check printed pre-steps and post-steps
        self.assertIn("Walnut Internal Install", output)
        self.assertIn("dev_hdd0/linux/walnut.img", output)
        self.assertIn("What to do next on your PS3", output)
        self.assertIn("set the boot flag to OtherOS", output)
        self.assertIn("select 'walnut'", output)




class TestReviewFixes(unittest.TestCase):
    def test_root_disk_found_through_lvm(self):
        """the root is /dev/mapper/vg-root on /dev/sdb2: /dev/sdb is refused even if it looked like USB"""
        m = walnut_install
        def runner(cmd):
            if cmd[:3] == ["lsblk", "-dno", "TYPE"]: return "disk"
            if cmd[:3] == ["lsblk", "-dno", "TRAN"]: return "usb"
            if cmd[:3] == ["lsblk", "-no", "MOUNTPOINT"]: return ""
            if cmd[:3] == ["lsblk", "-nso", "NAME"]: return "vg-root\n`-sdb2\n  `-sdb\n"
            return "X"
        mounts = "/dev/mapper/vg-root / ext4 rw 0 0\n"
        with self.assertRaises(ValueError):
            m.check_usb_disk("/dev/sdb", cmd_runner=runner, mounts_text=mounts)

    def test_sudo_rerun_carries_no_confirmation(self):
        """the typed confirmation happens once, in the root process: the re-run has no flag that skips it"""
        src = INSTALLER_PATH.read_text()
        self.assertNotIn('"--yes"', src)
        self.assertNotIn("--confirmed", src)


class TestTypedConfirmation(unittest.TestCase):
    """Q3: nothing but the typed model name confirms a live install"""
    INFO = {"disk": "/dev/sdz", "model": "Cruzer Blade", "serial": "S1", "size": "16G"}

    def confirm(self, typed, info=None):
        def fake_input(prompt):
            if isinstance(typed, BaseException):
                raise typed
            return typed
        with patch("builtins.input", fake_input), patch("sys.stdout", io.StringIO()):
            return walnut_install.confirm_usb_disk(info or self.INFO)

    def test_exact_model_confirms(self):
        self.assertTrue(self.confirm("Cruzer Blade"))
        self.assertTrue(self.confirm("  Cruzer Blade \n"))

    def test_wrong_or_empty_input_aborts(self):
        for typed in ("cruzer blade", "yes", "y", "", "/dev/sdz", EOFError()):
            with self.assertRaises(SystemExit, msg=repr(typed)):
                self.confirm(typed)

    def test_unknown_model_is_not_confirmed_by_end_of_input(self):
        for model in ("", "   ", None):
            with self.assertRaises(SystemExit, msg=repr(model)):
                self.confirm(EOFError(), dict(self.INFO, model=model))
            with self.assertRaises(SystemExit):
                self.confirm("", dict(self.INFO, model=model))
        self.assertTrue(self.confirm("UNKNOWN", dict(self.INFO, model="")))

    def test_dry_run_asks_nothing(self):
        def boom(prompt):
            raise AssertionError("asked")
        with patch("builtins.input", boom), patch("sys.stdout", io.StringIO()):
            self.assertTrue(walnut_install.confirm_usb_disk(self.INFO, dry_run=True))

    def test_no_option_skips_it(self):
        for flag in ("--yes", "--confirmed", "-y", "--force"):
            with patch("sys.stderr", io.StringIO()), self.assertRaises(SystemExit) as cm:
                walnut_install.main(["usb", "--release-dir", "/nonexistent", "--disk", "/dev/sdz", flag])
            self.assertEqual(cm.exception.code, 2, flag)

    def test_live_usb_run_prompts_before_any_write(self):
        """as root, a live run reaches the prompt and stops at a wrong answer: no mkfs, no sfdisk"""
        calls = []
        with tempfile.TemporaryDirectory() as d:
            create_release_fixtures(d)
            with patch.object(walnut_install, "check_usb_disk", lambda disk: dict(self.INFO)), \
                 patch("os.geteuid", lambda: 0), \
                 patch("subprocess.run", lambda *a, **k: calls.append(a)), \
                 patch("builtins.input", lambda p: "nope"), \
                 patch("sys.stdout", io.StringIO()), \
                 self.assertRaises(SystemExit):
                walnut_install.main(["usb", "--release-dir", d, "--disk", "/dev/sdz"])
        self.assertEqual(calls, [])

    def test_sudo_rerun_goes_before_the_prompt_and_carries_no_skip(self):
        """not root: the process execs sudo (which asks the model itself), never prompting first"""
        execs = []
        def fake_exec(f, argv):
            execs.append(argv)
            raise SystemExit(0)
        with tempfile.TemporaryDirectory() as d:
            create_release_fixtures(d)
            with patch.object(walnut_install, "check_usb_disk", lambda disk: dict(self.INFO)), \
                 patch("os.geteuid", lambda: 1000), \
                 patch("os.execvp", fake_exec), \
                 patch("builtins.input", lambda p: self.fail("asked before sudo")), \
                 patch("sys.stdout", io.StringIO()), \
                 self.assertRaises(SystemExit):
                walnut_install.main(["usb", "--release-dir", d, "--disk", "/dev/sdz"])
        self.assertEqual(len(execs), 1)
        self.assertEqual(execs[0][0], "sudo")
        self.assertFalse({"--yes", "--confirmed", "-y"} & set(execs[0]))


if __name__ == "__main__":
    unittest.main()
