# Security review checklist

The review a fresh Opus subagent (one that didn't write the change) does at plan time or before the
commit, for the change types listed in [CLAUDE.md](../CLAUDE.md) "Security review". This is the
generic checklist; a review's findings go to `~/Documents/walnut/` (private), never into the
repository. Report each finding with a severity (low, medium, high, critical), the code it is in
and how to trigger it; high and critical are fixed, or brought to the user, before the change ships.

Start from who controls each input: an unprivileged local user, a game, a package, a network
peer, an upload. Then read the code as that party.

## Kernel and driver ioctls
- Every size, count, offset and index from user space is bounded before use; sums and products are
  checked for overflow (`check_add_overflow`, `array_size`), and a length is never trusted twice
  without a copy (no double fetch).
- Unused ABI is rejected: unknown flags, reserved fields that are not zero, unknown commands, a
  struct size that doesn't match.
- Object lifetimes: every reference taken is dropped on every exit path; an object found by handle
  is looked up under the lock that frees it; nothing is used after its file is closed or its
  owner exits.
- Memory handed to user space or to the device is cleared first (new pages, reused buffers, padding in
  structs copied out).
- Waits are bounded (timeouts, interruptible), and a stuck device doesn't hold a lock the rest of
  the system needs.
- Access is checked at the entry point (device permissions, capabilities), not assumed from the
  caller.

## Command validators and allowlists
- The validator and the code that executes the stream see the same bytes (copy first, validate
  the copy, run the copy).
- Anything not on the allowlist is refused; a new opcode, method or register is added to the list
  on purpose, with a reason.
- Addresses, lengths and offsets a stream names are checked against what the caller owns, with
  overflow-safe arithmetic.
- Failure leaves the device in a known state; a refused stream can't leave half of its effects
  behind that the next client inherits.

## Kernel configuration and boot
- Hardening options stay on unless a measured reason says otherwise; each cut is written down with
  its reason.
- No debug interfaces, test seams or relaxed checks in the release configuration.
- Module and device nodes have the narrowest permissions that work.

## Mesa and patched libraries
- Sizes from the application or from files (shaders, textures, formats) are bounded before
  allocation; multiplications are checked; failed allocations are handled and don't leave
  dangling state.
- Buffers are cleared before they are reused for another client or returned to the application.
- A patch that adds an environment variable or a debug path must not widen what an untrusted
  process can do to a privileged one (setuid programs, other users).
- Fast paths (vector code, CPU fallbacks) are tested against the plain version on edge sizes and
  under a sanitizer.
- Patches keep their upstream credit and don't weaken a check upstream relies on.

## Root helpers, sudoers, maintainer scripts, units and rules
- A fixed environment and PATH for anything that runs as root: no inherited `PATH`, `LD_*`,
  `PYTHON*`, locale or `IFS`; absolute paths for programs.
- No user-controlled path is opened, written, moved or removed as root without being resolved
  and checked (symlinks, `..`, races between check and use); prefer file descriptors, `O_NOFOLLOW`
  and creating files in a directory only root can write.
- Arguments from the caller are validated against an allowlist, not filtered against a denylist;
  nothing is passed through a shell.
- Temporary files are created safely (`mktemp`, a private directory), never at a predictable name in
  a shared directory.
- sudoers rules name the exact command and arguments, and the target can't be replaced by the
  caller; no wildcards that let an argument add options.
- Maintainer scripts are idempotent, don't trust the state of the system they run on (a symlink in
  place of a directory) and fail closed.
- Units, polkit and udev rules grant the least that works: users, groups, capabilities, sandboxing
  options, device permissions; no rule matches more devices or users than intended.
- Secrets are never in a command line, an environment variable visible to others, a world-readable
  file or a log.

## Installer and update path
- What is downloaded is verified (signature or pinned hash) before it is used, over a channel that
  can't be downgraded.
- The target device or directory is confirmed from what the system reports, never taken from
  text the user or a file supplies, and a destructive step has a check that it is the right one.
- A failed or interrupted run leaves a recoverable system.

## CI, the publisher and the server
- Checks run in the trusted job; code from a pull request or an upload is never run with the
  credentials or the write permission that publishing needs.
- Workflow permissions are the minimum per job; secrets reach only the steps that need them and are
  not echoed.
- Actions are pinned by commit SHA, tools and base images by version or digest.
- Inputs from uploads (names, versions, archive members, sizes) are validated; archives can't write
  outside their directory; sizes and counts are bounded; a hostile file can't make the publisher
  publish something else than what was checked.
- The privacy guards fail closed: a file that is not classified is treated as private.
- No test seams in production: environment switches, hidden arguments and default credentials that
  exist only for tests are absent from the shipped code, or can't be set by the party they would
  help.
- Logs and error messages don't leak private paths, credentials or user data.

## Reporting
- One finding per issue: severity, location, the input that triggers it, the effect, and a fix.
- A fix comes with a test that fails without it, when the code can be tested on the host.
- Re-review the fix when it is more than a few lines.
