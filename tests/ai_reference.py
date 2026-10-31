#!/usr/bin/env python3
"""Check Core's tokenizer and transformer against independent NumPy operations."""
import json, pathlib, struct, subprocess
import numpy as np
from tokenizers import Tokenizer
ROOT=pathlib.Path(__file__).resolve().parents[1]
bundle=ROOT/'models/smollm2-135m-core-q8.bin'
exe=ROOT/'build/ai-native'
tokenizer=Tokenizer.from_file(str(ROOT/'models/tokenizer.json'))
subprocess.run(['clang','-O3','-msse2','-ffp-contract=off','-Wall','-Wextra','-Werror',str(ROOT/'tests/ai_native.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe),str(bundle),'dot-check'],check=True)
for text in ['Hello world!', 'What is the capital of France?', "I can't write 123 books.", '  spaces   matter ', 'NIX, Ghost & Memories.', "It's working. We're here.", 'How does a computer save a file?', '2 + 2 = 4', 'A\nnew line.', "she'll know; they've arrived"]:
    actual=json.loads(subprocess.check_output([str(exe),str(bundle),'tokenize',text]))
    expected=tokenizer.encode(text,add_special_tokens=False).ids
    assert actual==expected,(text,actual,expected)
print('PASS: byte-level BPE agrees with upstream tokenizer')
query='What is the capital of France?'
subprocess.run([str(exe),str(bundle),query,'logits'],check=True,cwd=ROOT)
b=np.memmap(bundle,mode='r',dtype=np.uint8);h=np.frombuffer(b,dtype='<u4',count=32)
offset=int(h[18])+128

def vector(n):
    global offset
    result=np.frombuffer(b,dtype='<f4',count=n,offset=offset).copy();offset+=n*4;return result

class Matrix:
    def __init__(self,q,scale):self.q,self.scale=q,scale
    def __getitem__(self,index):return self.q[index].astype(np.float32)*self.scale[index]

def matrix():
    global offset
    rows,cols=struct.unpack_from('<II',b,offset);offset+=8
    scale=vector(rows)
    result=Matrix(np.frombuffer(b,dtype=np.int8,count=rows*cols,offset=offset).reshape(rows,cols).copy(),scale)
    offset=(offset+rows*cols+15)&~15
    return result

embedding=matrix();layers=[]
for _ in range(30):
    norm=vector(576);q,k,v,o=[matrix() for _ in range(4)];post=vector(576);gate,up,down=[matrix() for _ in range(3)]
    layers.append((norm,q,k,v,o,post,gate,up,down))
final=vector(576)
prefix=np.frombuffer(b,dtype='<u4',count=int(h[15]),offset=int(h[14])).tolist()
suffix=np.frombuffer(b,dtype='<u4',count=int(h[17]),offset=int(h[16])).tolist()
tokens=prefix+tokenizer.encode(query,add_special_tokens=False).ids+suffix
keys=np.zeros((30,len(tokens),192),np.float32);values=np.zeros_like(keys)
freq=np.float32(100000)**(-np.arange(0,64,2,dtype=np.float32)/64)

def mv(weight,x):
    scale=np.maximum(np.max(np.abs(x))/np.float32(127),np.float32(1e-30))
    q=x*(np.float32(1)/scale)
    q=np.clip(np.trunc(q+np.where(q<0,-.5,.5)),-127,127).astype(np.int8)
    sums=np.einsum('ij,j->i',weight.q,q,dtype=np.int32,casting='unsafe',optimize=False)
    return sums.astype(np.float32)*weight.scale*scale

def rms(x,w):return w*x/np.sqrt(np.mean(x*x)+np.float32(1e-5))
def rope(x,p):
    x=x.reshape(-1,64);a=x[:,:32].copy();c=np.cos(p*freq);s=np.sin(p*freq)
    return np.concatenate((a*c-x[:,32:]*s,x[:,32:]*c+a*s),axis=1).reshape(-1)

for pos,token in enumerate(tokens):
    x=embedding[token].copy()
    for li,(norm,wq,wk,wv,wo,post,wg,wu,wd) in enumerate(layers):
        a=rms(x,norm);q=rope(mv(wq,a),pos);keys[li,pos]=rope(mv(wk,a),pos);values[li,pos]=mv(wv,a)
        att=np.empty(576,np.float32)
        for head in range(9):
            base=(head//3)*64
            scores=keys[li,:pos+1,base:base+64]@q[head*64:head*64+64]/8
            p=np.exp(scores-np.max(scores));p/=p.sum()
            att[head*64:head*64+64]=p@values[li,:pos+1,base:base+64]
        x+=mv(wo,att);a=rms(x,post);gate=mv(wg,a)
        x+=mv(wd,(gate/(1+np.exp(np.clip(-gate,-80,80))))*mv(wu,a))
logits=mv(embedding,rms(x,final))
actual=np.fromfile(ROOT/'build/ai-logits.bin',dtype='<f4')
error=float(np.max(np.abs(actual-logits)))
assert np.argmax(actual)==np.argmax(logits),(np.argmax(actual),np.argmax(logits))
# W8A8 introduces discontinuities at activation rounding boundaries. Across
# independently accumulated float operations, compare distribution shape and
# the leading token rather than demanding bit-identical transformer logits.
a=actual-actual.mean();r=logits-logits.mean()
correlation=float(np.dot(a,r)/(np.linalg.norm(a)*np.linalg.norm(r)))
assert correlation>0.95,(correlation,error)
print(f'PASS: independent NumPy transformer agrees on first generated token; centered logit correlation {correlation:.6f}, max difference {error:.4f}')
