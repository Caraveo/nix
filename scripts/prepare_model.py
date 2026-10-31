#!/usr/bin/env python3
"""Convert pinned SmolLM2 BF16 weights and tokenizer to Core's Q8 bundle.
Optional preparation dependencies: numpy, tokenizers. No torch or pickle loading.
"""
import argparse, hashlib, json, pathlib, struct
import numpy as np
from tokenizers import Tokenizer

ROOT=pathlib.Path(__file__).resolve().parents[1]
REVISION='12fd25f77366fa6b3b4b768ec3050bf629380bac'
SOURCE='HuggingFaceTB/SmolLM2-135M-Instruct'
MAGIC=0x4149584e


def byte_map():
    values=list(range(33,127))+list(range(161,173))+list(range(174,256))
    chars=values.copy(); extra=0
    for byte in range(256):
        if byte not in values:
            values.append(byte);chars.append(256+extra);extra+=1
    return dict(zip(values,map(chr,chars)))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory',type=pathlib.Path,default=ROOT/'models')
    args=parser.parse_args();d=args.directory
    cfg=json.loads((d/'config.json').read_text())
    assert [cfg[k] for k in ['hidden_size','intermediate_size','num_hidden_layers','num_attention_heads','num_key_value_heads','vocab_size']]==[576,1536,30,9,3,49152]
    assert cfg['tie_word_embeddings'] and not cfg.get('attention_bias')
    source=d/'model.safetensors'
    with source.open('rb') as f:
        length,=struct.unpack('<Q',f.read(8)); tensors=json.loads(f.read(length))
    mapping=np.memmap(source,mode='r',dtype=np.uint8)
    def tensor(name):
        item=tensors[name];assert item['dtype']=='BF16',name
        lo,hi=item['data_offsets']
        bits=np.frombuffer(mapping,dtype='<u2',count=(hi-lo)//2,offset=8+length+lo)
        return (bits.astype(np.uint32)<<16).view(np.float32).reshape(item['shape'])
    tokjson=json.loads((d/'tokenizer.json').read_text());vocab=tokjson['model']['vocab']
    decoder={v:k for k,v in byte_map().items()}
    ordered=sorted(vocab,key=vocab.get);assert len(ordered)==49152
    pieces=[bytes(decoder[ch] for ch in token) for token in ordered]
    offsets=[0]
    for piece in pieces:offsets.append(offsets[-1]+len(piece))
    merges=np.zeros((131072,4),dtype='<u4');merges[:,0]=0xffffffff
    for rank,pair in enumerate(tokjson['model']['merges']):
        left,right=pair.split(' ');a,b=vocab[left],vocab[right]
        result=vocab[left+right];slot=((a*0x9e3779b1)^(b*0x85ebca6b))&131071
        while merges[slot,0]!=0xffffffff:slot=(slot+1)&131071
        merges[slot]=[a,b,result,rank]
    tokenizer=Tokenizer.from_file(str(d/'tokenizer.json'))
    prefix=tokenizer.encode('<|im_start|>system\nYou are NIX. Answer briefly. Never claim to run actions.<|im_end|>\n<|im_start|>user\n',add_special_tokens=False).ids
    suffix=tokenizer.encode('<|im_end|>\n<|im_start|>assistant\n',add_special_tokens=False).ids
    path=d/'smollm2-135m-core-q8.bin';h=[0]*32
    h[:10]=[MAGIC,1,0,576,1536,30,9,3,49152,256]
    with path.open('wb') as f:
        f.write(bytes(128))
        def align():f.write(bytes((-f.tell())%16))
        def array(index,data):
            align();h[index]=f.tell();f.write(data)
        array(10,np.array(offsets,dtype='<u4').tobytes())
        array(11,b''.join(pieces))
        array(12,np.array([vocab.get(byte_map()[i],vocab['?']) for i in range(256)],dtype='<u4').tobytes())
        array(13,merges.tobytes())
        array(14,np.array(prefix,dtype='<u4').tobytes());h[15]=len(prefix)
        array(16,np.array(suffix,dtype='<u4').tobytes());h[17]=len(suffix)
        align();h[18]=f.tell()
        f.write(np.array([cfg['rope_theta']**(-2*i/64) for i in range(32)],dtype='<f4').tobytes())
        def matrix(name):
            a=tensor(name);assert a.ndim==2 and np.isfinite(a).all(),name
            scale=np.maximum(np.max(np.abs(a),axis=1)/127,1e-20).astype('<f4')
            quant=np.clip(np.rint(a/scale[:,None]),-127,127).astype(np.int8)
            f.write(struct.pack('<II',*a.shape));f.write(scale.tobytes());f.write(quant.tobytes());align()
        def vector(name):
            a=tensor(name);assert a.shape==(576,) and np.isfinite(a).all(),name
            f.write(a.astype('<f4').tobytes())
        matrix('model.embed_tokens.weight')
        for layer in range(30):
            p=f'model.layers.{layer}.'
            vector(p+'input_layernorm.weight')
            for name in ['q_proj','k_proj','v_proj','o_proj']:matrix(p+'self_attn.'+name+'.weight')
            vector(p+'post_attention_layernorm.weight')
            for name in ['gate_proj','up_proj','down_proj']:matrix(p+'mlp.'+name+'.weight')
        vector('model.norm.weight')
        h[2]=f.tell();f.seek(0);f.write(struct.pack('<32I',*h))
    manifest={'model':SOURCE,'revision':REVISION,'license':'Apache-2.0','format':'NIXAI Q8 per-row symmetric v1','context_tokens':256,
              'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
              'bundle_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'bundle_bytes':path.stat().st_size,
              'tokenizer_sha256':hashlib.sha256((d/'tokenizer.json').read_bytes()).hexdigest()}
    (d/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps(manifest,indent=2))


if __name__=='__main__':main()
