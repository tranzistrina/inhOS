#!/usr/bin/env python3
"""Convert FB2 to the inhOS book.txt envelope, preserving contents."""
import argparse
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'desktop'))
from inhfs.books import convert
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('input');p.add_argument('output');a=p.parse_args()
convert(a.input,a.output)
