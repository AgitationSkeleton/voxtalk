"""Write a 64x64 PNG favicon using the same artwork as the Windows icon."""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools'))
from make_icon import render, png_entry
open(sys.argv[1], 'wb').write(png_entry(render(64)))
