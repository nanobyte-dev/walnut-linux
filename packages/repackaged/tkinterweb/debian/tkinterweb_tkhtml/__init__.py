"""
tkinterweb_tkhtml for Debian (Walnut): TkinterWeb's loader for the Tkhtml widget, pointed at the
system's Tkhtml (package tk-html3, /usr/lib/tcltk/<triplet>/tkhtml3.0) instead of the binaries
TkinterWeb-Tkhtml 2.3.0 bundles for x86 and arm. The interface is that module's, which tkinterweb
calls (get_tkhtml_file, load_tkhtml_file, load_tkhtml, get_loaded_tkhtml_version, the TKHTML_*
names); the functions are upstream's, less the bundled platform directories.

Copyright (c) 2026 Andrew Clarke (TkinterWeb-Tkhtml, MIT)
"""

import os
import sysconfig

from tkinter import TclVersion

__title__ = 'TkinterWeb-Tkhtml'
__author__ = "Andrew Clarke"
__copyright__ = "Copyright (c) 2026 Andrew Clarke"
__license__ = "MIT"
__version__ = '2.3.0'

TKHTML_ROOT_DIR = os.path.join("/usr/lib/tcltk", sysconfig.get_config_var("MULTIARCH") or "", "tkhtml3.0")
# no Cairo-powered Tkhtml 3.1/3.2 (TkinterWeb-Tkhtml-Extras) is packaged
TKHTML_EXTRAS_ROOT_DIR = None
TKHTML_EXTRAS_VERSION = None

try:
    ALL_TKHTML_BINARIES = [[TKHTML_ROOT_DIR, file] for file in os.listdir(TKHTML_ROOT_DIR) if "libTkhtml" in file]
except OSError:
    ALL_TKHTML_BINARIES = []

if TclVersion >= 9:
    TKHTML_BINARIES = [[loc, file] for loc, file in ALL_TKHTML_BINARIES if "TclTk9" in file]
else:
    TKHTML_BINARIES = [[loc, file] for loc, file in ALL_TKHTML_BINARIES if "TclTk9" not in file]

HELP_MESSAGE = f"Install the tk-html3 package (Tkhtml 3.0 for Tcl/Tk 8.6, in {TKHTML_ROOT_DIR})."
HELP_MESSAGE_EXP = "Experimental Tkhtml releases are not packaged."


def get_tkhtml_file(version=None, index=-1, experimental=False):
    "Get the location of the system's Tkhtml binary"
    if not TKHTML_BINARIES:
        if ALL_TKHTML_BINARIES:
            raise OSError(f"No Tkhtml versions could be found for Tcl/Tk {int(TclVersion)}. {HELP_MESSAGE}")
        else:
            raise OSError(f"No Tkhtml versions could be found for your system. {HELP_MESSAGE}")

    if isinstance(version, float):
        version = str(version)
    if version:
        for loc, file in TKHTML_BINARIES:
            if version in file:
                if "exp" in file:
                    if not experimental:
                        raise OSError(f"Tkhtml version {version} is an experimental release but experimental mode is disabled. {HELP_MESSAGE_EXP}")
                    experimental = True
                else:
                    if experimental == True:
                        raise OSError(f"Tkhtml version {version} is not an experimental release but experimental mode is enabled. {HELP_MESSAGE_EXP}")
                    experimental = False
                return os.path.join(loc, file), version, experimental
        raise OSError(f"Tkhtml version {version} either does not exist or is unsupported on your system. {HELP_MESSAGE}")
    else:
        if experimental == True:
            files = [k for k in TKHTML_BINARIES if 'exp' in k[1]]
            if not files:
                raise OSError(f"No experimental Tkhtml versions could be found on your system. {HELP_MESSAGE_EXP}")
        elif not experimental:
            files = [k for k in TKHTML_BINARIES if 'exp' not in k[1]]
        else:
            files = TKHTML_BINARIES
        loc, file = sorted(files)[index]
        experimental = "exp" in file
        version = file.replace("libTkhtml", "").replace("exp", "").replace("TclTk9", "")
        version = version[:version.rfind(".")]
        return os.path.join(loc, file), version, experimental


def get_loaded_tkhtml_version(master):
    """Get the version of the loaded Tkhtml instance.
    This will raise a TclError if Tkhtml is not loaded."""
    return master.tk.call("package", "present", "Tkhtml")


def load_tkhtml_file(master, file):
    "Load Tkhtml into the current Tcl/Tk instance"
    master.tk.call("load", file)


def load_tkhtml(master):
    "Load Tkhtml into the current Tcl/Tk instance"
    master.tk.call("package", "require", "Tkhtml")
