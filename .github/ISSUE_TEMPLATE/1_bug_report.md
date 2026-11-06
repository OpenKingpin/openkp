---
name: 'Report a bug'
about: 'Report an issue or crash with OpenKP'
title: ''
labels: 'bug'
assignees: ''

---

### Before reporting bugs

Please make sure the issue is reproducible with the latest `openkp` build.

### Environment & Setup

Provide the following information:
- **OpenKP version / commit hash:** (e.g. from `openkp --version` or console)
- **Operating System:** (e.g. Windows 10/11 64-bit, Ubuntu 24.04, Arch)
- **GPU and Driver:**
- **Server connected to:** (IP address, port, and mod e.g. vanilla KP / Monkey Mod)
- **Map name:** (e.g. `kptd1`, `team_flub`, etc.)

### Describe the Bug

A clear and concise description of what happened.

### Steps to Reproduce

1. Launch `openkp`
2. Connect to server `...`
3. Perform action `...`
4. Observe issue

### Expected Behavior

A clear and concise description of what you expected to happen.

### Screenshots or Video

If applicable, add screenshots or video to help explain the problem.

### Log File or Crash Dump

Provide console output or log file created by launching:
```bash
openkp +set developer 1 +set logfile 1
```
On Windows, provide the crash dialog details if applicable. On Linux, provide the `gdb` backtrace if a crash occurred.
