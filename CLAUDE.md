@AGENTS.md

## Claude Code specifics
- `/hw-test` runs the full hardware loop and reports the verdict.
- After `make test`, Read `build/hw/images/selftest_last.jpg` to see the frame.
- Permissions in `.claude/settings.json` deny eFuse / secure-boot commands — do not try to work around them.
