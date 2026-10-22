"""Compatibility entry point; reusable project material has one shared owner."""
from pathlib import Path
import runpy
SHARED=Path(__file__).resolve().parents[4]/'Instruments/Nixie/build_ue_nixie.py'
build=runpy.run_path(str(SHARED))['build']
