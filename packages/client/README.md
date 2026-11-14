# devotchi

Command-line client for [Devotchi](https://github.com/hussamkoman/devotchi), a
desk pet for AI coding agents. It approves Claude Code permission requests on
a small device (or the local dashboard), shows what the agent is doing and
reflects the project's mood.

This package contains the CLI, the MCP server, the Claude Code hook setup,
the local dashboard and an embedded Devotchi server for `--local` mode.
Requires Node.js 22.13 or newer.

```bash
devotchi start --local --lan      # server + dashboard on this machine, reachable by the device
devotchi setup                    # install the Claude Code hooks into .claude/settings.local.json
devotchi pair <CODE>              # pair the device showing <CODE>
claude mcp add devotchi -- devotchi mcp
devotchi doctor                   # check server, token, hooks and devices
devotchi --help                   # every command
```

Devotchi never allows or denies a request on its own. A request nobody
answers on a device falls back to Claude Code's own terminal prompt after the
hold time (30 s by default).

See the repository README for the full documentation.
