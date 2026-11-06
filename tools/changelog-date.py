#!/usr/bin/env python3
"""changelog-date.py CHANGELOG DATE: put DATE on the trailer line of the newest changelog entry.
The entry's date is the commit's, so the same checkout gives the same SOURCE_DATE_EPOCH."""
import re
import sys

path, date = sys.argv[1:]
with open(path) as f:
    text = f.read()
text = re.sub(r"^( -- .*>)  .*$", lambda m: m.group(1) + "  " + date, text, count=1, flags=re.M)
with open(path, "w") as f:
    f.write(text)
