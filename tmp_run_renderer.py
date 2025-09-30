import os
import sys
sys.path.insert(0, r"C:\Users\AX-6\Documents\GitHub\yup\python\build\lib.win-amd64-cpython-311")
os.add_dll_directory(r"C:\Users\AX-6\Documents\GitHub\yup\python\build\lib.win-amd64-cpython-311")
os.add_dll_directory(r"C:\Users\AX-6\Documents\GitHub\yup\python\build\temp.win-amd64-cpython-311\Release\python\Release")
from yup_rive_renderer import RiveOffscreenRenderer
print("creating renderer...")
try:
    renderer = RiveOffscreenRenderer(1280, 720, staging_buffer_count=2, enable_presentation=True)
    print("renderer created", renderer.is_valid())
except Exception as exc:
    print("exception:", exc)
