import sys,glob
from PIL import Image
d,out=sys.argv[1],sys.argv[2]; sel=sys.argv[3:]
fs=sorted(glob.glob(d+'/*.png'))
if sel: fs=[f for f in fs if any(s in f for s in sel)]
cols=3
W=Image.new('L',(cols*404,((len(fs)+cols-1)//cols)*304),255)
for i,f in enumerate(fs): W.paste(Image.open(f),((i%cols)*404,(i//cols)*304))
W.save(out); print(len(fs))
