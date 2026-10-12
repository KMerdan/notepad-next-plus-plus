#!/usr/bin/env python3
"""Refresh CodeGraph with tracked and non-ignored untracked source files.

Run with the same Python environment/version as the CodeGraph MCP server. The current
CodeGraph full-build collector uses git ls-files and excludes untracked work.
These process-local extensions change no installed package, Git index or
repository history. The usual JavaScript parser handles CommonJS files, which
2.3.9 otherwise omits without a shebang. The ignore policy and postprocessing
are retained. Use this entry point until upstream includes .cjs and new files
have been committed.
"""
from pathlib import Path
import os
import subprocess
import sys


def main() -> int:
    from code_review_graph import incremental, parser
    import code_review_graph
    from code_review_graph.cli import main as graph_main

    root = Path(__file__).resolve().parent.parent
    version = tuple(int(part) for part in code_review_graph.__version__.split(".")[:3])
    if version < (2, 3, 9):
        raise SystemExit("Use CodeGraph >=2.3.9 from the same environment as the MCP server.")
    tracked = incremental.get_all_tracked_files

    def working_files(repo_root, recurse_submodules=None):
        existing = tracked(repo_root, recurse_submodules)
        result = subprocess.run(
            ["git", "ls-files", "--others", "--exclude-standard", "-z"],
            cwd=repo_root, check=True, capture_output=True, timeout=30,
        )
        additions = [os.fsdecode(item) for item in result.stdout.split(b"\0") if item]
        return sorted(set(existing + additions))

    incremental.get_all_tracked_files = working_files
    parser.EXTENSION_TO_LANGUAGE.setdefault(".cjs", "javascript")
    os.environ["CRG_SERIAL_PARSE"] = "1"
    sys.argv = ["code-review-graph", "build", "--repo", str(root)]
    return graph_main() or 0


if __name__ == "__main__":
    raise SystemExit(main())
