# Native Windows build: the tools pipe text between programs (cpp | python | cc1 | python | as)
# that expect Unix line endings, so Python writes "\n" to stdout and stderr as on Linux.
import sys

for stream in (sys.stdout, sys.stderr):
    try:
        stream.reconfigure(newline="\n")
    except (AttributeError, ValueError):
        pass
