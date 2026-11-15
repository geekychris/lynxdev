"""The emulator's own tests (emulator/tests/test_lynxh.py: the raw lynxh
protocol, no lynxdev package needed), collected here so `./lynx selftest`
runs them. Run alone: python3 tools/lynxdev/tests/test_emulator.py"""
import os
import sys
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(_HERE, "..", "..", "..", "emulator", "tests"))

import test_lynxh  # noqa: E402
from test_lynxh import *  # noqa: E402,F401,F403  (the TestCase classes)

setUpModule = test_lynxh.setUpModule

if __name__ == "__main__":
    unittest.main(verbosity=2)
