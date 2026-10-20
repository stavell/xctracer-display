#!/usr/bin/env python3
"""Claude Pet daemon entry point. See host/claude_pet/README.md."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from claude_pet.cli import main  # noqa: E402  (the package of the same name wins over this file)

if __name__ == "__main__":
    main()
