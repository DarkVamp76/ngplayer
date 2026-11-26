# Bakes the Sound Blaster AWE32 card for the player into its own header (same layout as the cards of card3d_data.h, with the generated back side).
# usage: python3 tools/bake_awe32.py DIR_WITH_awe32_photo.glb > src/awe32_data.h        (needs numpy and Pillow)
# The GLB is made from the maintainer's photo of the card by tools/make_awe32_photo.py; the bake is the same bake() as for the Ultrasound PnP, with a green back side.
import os, sys, contextlib
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
src = sys.argv[1]
sys.argv = [sys.argv[0], os.path.dirname(os.path.abspath(src)) if src.endswith('.glb') else src]
import bake_card3d as B
with contextlib.redirect_stdout(sys.stderr):
    s = B.bake('AWE', 'awe32_photo.glb', 640)
sys.stdout.write('/* Creative Sound Blaster AWE32, baked from a photo of the card by tools/make_awe32_photo.py and tools/bake_awe32.py. GPL-3.0-or-later */\n#include <stdint.h>\n' + s)
