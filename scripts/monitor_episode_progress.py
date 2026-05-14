from __future__ import annotations

import argparse
import html
import json
import os
import re
import time
from pathlib import Path


EPISODE_RE = re.compile(r"\[episode\s+(\d+)/(\d+)\]")


def _pid_alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    return True


def _parse_status(log_path: Path) -> dict[str, object]:
    latest_episode = None
    total_episodes = None
    last_line = ""
    recent_lines: list[str] = []
    if log_path.exists():
        for raw_line in log_path.read_text(encoding="utf-8").splitlines():
            line = raw_line.strip()
            if not line:
                continue
            last_line = line
            recent_lines.append(line)
            recent_lines = recent_lines[-12:]
            match = EPISODE_RE.search(line)
            if match:
                latest_episode = int(match.group(1))
                total_episodes = int(match.group(2))
    return {
        "latest_episode": latest_episode,
        "total_episodes": total_episodes,
        "last_line": last_line,
        "recent_lines": recent_lines,
    }


def _checkpoint_dirs(checkpoint_root: Path | None) -> list[str]:
    if checkpoint_root is None or not checkpoint_root.exists():
        return []
    dirs = [path.name for path in checkpoint_root.iterdir() if path.is_dir() and path.name.startswith("budget_")]
    return sorted(dirs)


def _render_html(title: str, payload: dict[str, object], interval_s: float, checkpoints: list[str]) -> str:
    latest_episode = payload.get("latest_episode")
    total_episodes = payload.get("total_episodes")
    progress_pct = 0.0
    if isinstance(latest_episode, int) and isinstance(total_episodes, int) and total_episodes > 0:
        progress_pct = 100.0 * latest_episode / total_episodes
    recent_lines = payload.get("recent_lines") or []
    recent_html = "\n".join(f"<li>{html.escape(str(line))}</li>" for line in recent_lines)
    checkpoint_html = "\n".join(f"<li>{html.escape(name)}</li>" for name in checkpoints) or "<li>none yet</li>"
    updated_at = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(float(payload["updated_at_epoch_s"])))
    latest_text = "unknown" if latest_episode is None or total_episodes is None else f"{latest_episode}/{total_episodes}"
    return f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta http-equiv="refresh" content="{max(int(interval_s), 1)}">
  <title>{html.escape(title)}</title>
  <style>
    body {{ font-family: ui-sans-serif, system-ui, sans-serif; margin: 24px; background: #0b1020; color: #e7edf7; }}
    .grid {{ display: grid; grid-template-columns: repeat(4, minmax(160px, 1fr)); gap: 16px; margin-bottom: 20px; }}
    .card {{ background: #121a2f; border: 1px solid #24304f; border-radius: 14px; padding: 16px; }}
    .label {{ font-size: 12px; text-transform: uppercase; letter-spacing: 0.08em; color: #93a4c7; }}
    .value {{ font-size: 28px; font-weight: 700; margin-top: 6px; }}
    .bar {{ height: 10px; background: #1a2440; border-radius: 999px; overflow: hidden; margin-top: 10px; }}
    .fill {{ height: 100%; width: {progress_pct:.2f}%; background: linear-gradient(90deg, #4dd0e1, #80cbc4); }}
    .panel {{ background: #121a2f; border: 1px solid #24304f; border-radius: 14px; padding: 16px; margin-top: 16px; }}
    ul {{ margin: 12px 0 0 18px; padding: 0; }}
    li {{ margin: 6px 0; line-height: 1.35; }}
    code {{ color: #b5f0ff; }}
    .muted {{ color: #93a4c7; }}
  </style>
</head>
<body>
  <h1>{html.escape(title)}</h1>
  <div class="muted">Updated {html.escape(updated_at)}. Auto-refresh every {int(max(interval_s, 1))}s.</div>
  <div class="grid">
    <div class="card">
      <div class="label">Alive</div>
      <div class="value">{html.escape(str(payload.get("alive")))}</div>
    </div>
    <div class="card">
      <div class="label">Latest Episode</div>
      <div class="value">{html.escape(latest_text)}</div>
    </div>
    <div class="card">
      <div class="label">Progress</div>
      <div class="value">{progress_pct:.1f}%</div>
      <div class="bar"><div class="fill"></div></div>
    </div>
    <div class="card">
      <div class="label">Checkpoint Dirs</div>
      <div class="value">{len(checkpoints)}</div>
    </div>
  </div>
  <div class="panel">
    <div class="label">Latest Line</div>
    <div><code>{html.escape(str(payload.get("last_line", "")))}</code></div>
  </div>
  <div class="panel">
    <div class="label">Saved Checkpoints</div>
    <ul>{checkpoint_html}</ul>
  </div>
  <div class="panel">
    <div class="label">Recent Log Lines</div>
    <ul>{recent_html}</ul>
  </div>
</body>
</html>
"""


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--log-path", required=True)
    parser.add_argument("--pid", type=int, default=0)
    parser.add_argument("--out-json", required=True)
    parser.add_argument("--out-txt", required=True)
    parser.add_argument("--out-html")
    parser.add_argument("--checkpoint-root")
    parser.add_argument("--title", default="Run Progress")
    parser.add_argument("--interval", type=float, default=10.0)
    parser.add_argument("--idle-timeout", type=float, default=1800.0)
    args = parser.parse_args()

    log_path = Path(args.log_path)
    out_json = Path(args.out_json)
    out_txt = Path(args.out_txt)
    out_html = Path(args.out_html) if args.out_html else None
    checkpoint_root = Path(args.checkpoint_root) if args.checkpoint_root else None
    out_json.parent.mkdir(parents=True, exist_ok=True)
    out_txt.parent.mkdir(parents=True, exist_ok=True)
    if out_html is not None:
        out_html.parent.mkdir(parents=True, exist_ok=True)

    last_change_ts = time.time()
    last_log_mtime = log_path.stat().st_mtime if log_path.exists() else 0.0
    while True:
        parsed = _parse_status(log_path)
        current_mtime = log_path.stat().st_mtime if log_path.exists() else last_log_mtime
        if current_mtime != last_log_mtime:
            last_log_mtime = current_mtime
            last_change_ts = time.time()
        alive = True if args.pid <= 0 else _pid_alive(args.pid)
        if args.pid <= 0 and (time.time() - last_change_ts) >= args.idle_timeout:
            alive = False
        payload = {
            "pid": args.pid,
            "alive": alive,
            "latest_episode": parsed["latest_episode"],
            "total_episodes": parsed["total_episodes"],
            "last_line": parsed["last_line"],
            "recent_lines": parsed["recent_lines"],
            "updated_at_epoch_s": time.time(),
        }
        out_json.write_text(json.dumps(payload, indent=2), encoding="utf-8")
        episode = parsed["latest_episode"]
        total = parsed["total_episodes"]
        if episode is None or total is None:
            summary = f"alive={alive} episode=unknown/{total or '?'}"
        else:
            summary = f"alive={alive} episode={episode}/{total}"
        if parsed["last_line"]:
            summary = f"{summary}\n{parsed['last_line']}"
        out_txt.write_text(summary + "\n", encoding="utf-8")
        if out_html is not None:
            out_html.write_text(
                _render_html(args.title, payload, args.interval, _checkpoint_dirs(checkpoint_root)),
                encoding="utf-8",
            )
        if not alive:
            break
        time.sleep(args.interval)


if __name__ == "__main__":
    main()
