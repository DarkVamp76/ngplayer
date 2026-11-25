# Bakes the Sound Blaster AWE32 card for the player into its own header (same layout as the cards of card3d_data.h, with the generated back side).
# usage: python3 tools/bake_awe32.py DIR_WITH_SB_AWE32.glb > src/awe32_data.h        (needs numpy and Pillow)
# The GLB is in metres with y up; the tool turns it into mm and uses the same bake_ews() as the AdLib Gold and the EWS64, with a classifier for the node names of this model.
import os, re, sys
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
src = sys.argv[1]
glb = os.path.basename(src) if src.endswith('.glb') else 'SB_AWE32.glb'
sys.argv = [sys.argv[0], os.path.dirname(os.path.abspath(src)) if src.endswith('.glb') else src]
import bake_card3d as B
def classify(n):
    if n == 'PCB': return 'board'
    if re.match(r'Finger\d*$', n): return 'finger'
    if n.endswith('_body') or re.match(r'(Cap\d*|Osc\d+|Xtal\d*|Hdr\w*)$', n): return 'part'
    return None
import contextlib
with contextlib.redirect_stdout(sys.stderr):
    s = B.bake_ews('AWE', glb, 320, lines=('SOUND BLASTER', 'AWE32'), auto_outline=True, classify=classify, scale=1000.0, back_pal=B.PAL_AWE)
sys.stdout.write('/* Creative Sound Blaster AWE32, baked from the 3D model by tools/bake_awe32.py. GPL-3.0-or-later */\n#include <stdint.h>\n' + s)
