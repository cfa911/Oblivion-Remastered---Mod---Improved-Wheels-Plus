"""Where the source files read by the Python regressions live, so moving a file changes only this one."""
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
KEYBOARD = ROOT / "src/input/Keyboard.cpp"
