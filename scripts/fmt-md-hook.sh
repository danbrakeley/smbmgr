#!/usr/bin/env bash
# Claude Code PostToolUse hook (.claude/settings.json): re-wrap the markdown
# file Claude just wrote, using the width in deno.jsonc. Hook input is JSON on
# stdin.
#
# `deno fmt <path>` ignores deno.jsonc's include/exclude for a path given
# explicitly, so this script has to decide what is in scope itself: tracked-style
# docs inside the repo only. Gitignored paths (build/, local/, qt/) are skipped
# via git; the vendored Qt skills are the one exclude repeated from deno.jsonc.
set -u

if ! command -v deno >/dev/null 2>&1; then
  echo "fmt-md-hook: deno not on PATH, markdown left unwrapped" >&2
  exit 1
fi

file=$(deno eval 'const p = JSON.parse(await new Response(Deno.stdin.readable).text());
console.log(p.tool_response?.filePath ?? p.tool_input?.file_path ?? "");')

case "$file" in
  *.md) ;;
  *) exit 0 ;;
esac

cd "${CLAUDE_PROJECT_DIR:-.}" || exit 0
root=$(pwd)
# Git Bash on Windows: the tool reports E:\..., pwd reports /e/...
if command -v cygpath >/dev/null 2>&1; then
  file=$(cygpath -u "$file")
fi

case "$file" in
  "$root"/.claude/skills/qt-*) exit 0 ;;
  "$root"/*) rel=${file#"$root"/} ;;
  *) exit 0 ;;
esac

git check-ignore -q "$rel" 2>/dev/null && exit 0

deno fmt --quiet "$rel"
