#!/usr/bin/python3
"""pkg-patchmsg.py <patch> <topic> <name>: prints a patch's commit metadata for tools/pkg-work.sh:
line 1 author ("Name <mail>"), line 2 date (RFC 2822 or empty), then the commit message (subject,
body, trailers: DEP-3 fields other than the description, and Gbp-Pq Topic/Name). Understands git
format-patch headers (From:/Date:/Subject:) and DEP-3 headers (Description:/Subject:, Author:/From:)."""
import email, email.policy, re, sys

path, topic, name = sys.argv[1:4]
text = open(path, encoding="utf-8", errors="surrogateescape").read()
# the header: everything before the diff (DEP-3 ends it with a line "---"; git with "---" + stat)
m = re.search(r"^(---\s*$|diff --git |Index: |--- a/)", text, re.M)
head = text[: m.start()] if m else text
lines = head.splitlines()
if lines and lines[0].startswith("From ") and re.match(r"From [0-9a-f]{40} ", lines[0]):
    lines = lines[1:]
author, date, subject, body, extra = "", "", "", [], []
if any(l.startswith("Subject:") for l in lines[:10]) and not any(l.startswith("Description:") for l in lines):
    msg = email.message_from_string("\n".join(lines), policy=email.policy.compat32)
    author = str(msg.get("From", "") or "")
    date = str(msg.get("Date", "") or "")
    subject = re.sub(r"\s+", " ", str(msg.get("Subject", ""))).strip()
    subject = re.sub(r"^\[PATCH[^\]]*\]\s*", "", subject)
    payload = msg.get_payload()
    body = [l for l in (payload if isinstance(payload, str) else "").strip("\n").splitlines()]
else:
    field = None
    fields = {}
    for l in lines:
        if re.match(r"^[A-Za-z-]+:", l):
            field, _, v = l.partition(":")
            fields.setdefault(field, []).append(v.strip())
        elif l.startswith((" ", "\t")) and field:
            fields[field].append("" if l.strip() == "." else l.strip())
    desc = fields.pop("Description", None) or fields.pop("Subject", None) or [name]
    subject, body = desc[0], desc[1:]
    author = (fields.pop("Author", None) or fields.pop("From", None) or [""])[0]
    for k, v in fields.items():
        extra.append(f"{k}: {' '.join(x for x in v if x)}")
author = author or "PS3 Linux project"
if "<" not in author:
    author += " <ps3@localhost>"
print(author)
print(date)
print(subject or name)
if body:
    print()
    print("\n".join(body).strip("\n"))
print()
for e in extra:
    print(e)
if topic:
    print(f"Gbp-Pq: Topic {topic}")
print(f"Gbp-Pq: Name {name}")
