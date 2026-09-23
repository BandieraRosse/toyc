"""Unthresholded RGB comparison for native BMP and CPU PPM, using only stdlib."""
import sys, json, hashlib, struct, zlib
from pathlib import Path

def read(path):
    data = path.read_bytes()
    if data[:2] == b'P6':
        header, offset = [], 0
        while len(header) < 4:
            while data[offset:offset+1].isspace(): offset += 1
            if data[offset:offset+1] == b'#':
                offset = data.index(b'\n', offset) + 1
                continue
            end = offset
            while not data[end:end+1].isspace(): end += 1
            header.append(data[offset:end]); offset = end
        _, w, h, depth = header
        if depth != b'255': raise ValueError('unsupported PPM depth')
        w, h = int(w), int(h)
        rgb = data[offset+1:]
    elif data[:2] == b'BM':
        offset = struct.unpack_from('<I', data, 10)[0]
        w, height = struct.unpack_from('<ii', data, 18)
        bits = struct.unpack_from('<H', data, 28)[0]
        compression = struct.unpack_from('<I', data, 30)[0]
        if w <= 0 or bits not in (24, 32) or compression != 0:
            raise ValueError('unsupported BMP format')
        h = abs(height); stride = ((w*bits+31)//32)*4
        rgb = bytearray(w*h*3)
        for y in range(h):
            row = offset + (h-1-y if height > 0 else y)*stride
            for x in range(w):
                at = row+x*(bits//8); dest=(y*w+x)*3
                rgb[dest:dest+3] = data[at:at+3][::-1]
    else: raise ValueError('unknown format')
    if len(rgb) != w*h*3: raise ValueError('invalid pixel length')
    return w, h, rgb

def png(path, w, h, rgb):
    def chunk(kind, payload):
        return struct.pack('>I',len(payload))+kind+payload+struct.pack('>I',zlib.crc32(kind+payload))
    raw = b''.join(b'\0'+rgb[y*w*3:(y+1)*w*3] for y in range(h))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b''))

def main(root):
    results=[]
    for cpu in sorted(root.glob('*-cpu.ppm')):
        name=cpu.name.removesuffix('-cpu.ppm'); gpu=root/(name+'-gpu.bmp')
        w,h,a=read(cpu); gw,gh,b=read(gpu)
        if (w,h)!=(gw,gh): raise ValueError('extent mismatch')
        d=bytes(abs(x-y) for x,y in zip(a,b))
        for suffix,pixels in [('cpu',a),('gpu',b),('diff',d)]: png(root/(name+'-'+suffix+'.png'),w,h,pixels)
        changed=sum(d[i:i+3]!=b'\0\0\0' for i in range(0,len(d),3))
        results.append(dict(view=name,extent=[w,h],changed_pixels=changed,
            mean_absolute_rgb=[sum(d[c::3])/(w*h) for c in range(3)],maximum_rgb=[max(d[c::3]) for c in range(3)],
            cpu_sha256=hashlib.sha256(cpu.read_bytes()).hexdigest(),gpu_sha256=hashlib.sha256(gpu.read_bytes()).hexdigest()))
    (root/'diff.json').write_text(json.dumps(results,indent=2)+'\n')
    print(json.dumps(results,indent=2))
if __name__=='__main__': main(Path(sys.argv[1]))
