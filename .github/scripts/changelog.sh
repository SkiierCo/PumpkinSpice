#!/bin/bash
# Prints the README's changelog for one version (the lines under **<tag>** in "## Changes") as release text: every
# "- " item becomes its own paragraph with its wrapped lines joined back up (a line ending in "\" keeps its line break),
# and links to README sections point at the repo page, since the release page has no such sections.
# usage: changelog.sh <tag, e.g. v1.5> <README.md> <owner/repo>
set -euo pipefail
tag="$1" readme="$2" repo="$3"
awk -v head="**${tag}**" '
    $0 == head { on = 1; next }
    on && (/^\*\*v/ || /^## /) { exit }
    on { print }
' "$readme" |
awk '
    function flush() { if (p != "") { printf "%s\n\n", p; p = "" } }
    /^- / { flush(); p = substr($0, 3); next }
    /^  +[^ ]/ {
        sub(/^ +/, "")
        if (p ~ /\\$/) p = substr(p, 1, length(p) - 1) "\n" $0
        else p = p " " $0
        next
    }
    NF == 0 { next }
    { flush(); p = $0 }
    END { flush() }
' |
sed "s|](#|](https://github.com/${repo}#|g"
