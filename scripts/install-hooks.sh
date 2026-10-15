#!/bin/sh
# Install a git pre-commit hook that runs scripts/check.sh. Safe to run again.
# Emergency bypass: git commit --no-verify
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HOOK="$(git -C "$ROOT" rev-parse --path-format=absolute --git-path hooks)/pre-commit"
mkdir -p "$(dirname "$HOOK")"
cat > "$HOOK" <<HOOKEOF
#!/bin/sh
# Installed by scripts/install-hooks.sh. Bypass in an emergency: git commit --no-verify
exec "$ROOT/scripts/check.sh"
HOOKEOF
chmod +x "$HOOK"
echo "pre-commit hook installed: $HOOK"
echo "Bypass in an emergency: git commit --no-verify"
