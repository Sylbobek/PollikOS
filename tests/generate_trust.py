"""Regenerate BearSSL trust anchors from reviewed DER certificates. Requires cryptography."""
from pathlib import Path
from cryptography import x509
from cryptography.hazmat.primitives.asymmetric import rsa, ec
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat
from cryptography.hazmat.primitives import hashes
root=Path(__file__).resolve().parents[1]/'kernel'/'certs'
parts=[];anchors=[];manifest=[]
def arr(name,data):
    parts.append('static unsigned char '+name+'[] = {'+','.join(hex(b) for b in data)+'};')
for i,p in enumerate(sorted(root.glob('*.der'))):
    cert=x509.load_der_x509_certificate(p.read_bytes());name='ca'+str(i)
    arr(name+'dn',cert.subject.public_bytes());key=cert.public_key();k=key.public_numbers()
    if isinstance(key,rsa.RSAPublicKey):
        for suffix,v in [('n',k.n),('e',k.e)]:arr(name+suffix,v.to_bytes((v.bit_length()+7)//8,'big'))
        pk='{BR_KEYTYPE_RSA,{.rsa={'+name+'n,sizeof('+name+'n),'+name+'e,sizeof('+name+'e)}}}'
    elif isinstance(key,ec.EllipticCurvePublicKey):
        curve={'secp256r1':'BR_EC_secp256r1','secp384r1':'BR_EC_secp384r1','secp521r1':'BR_EC_secp521r1'}[key.curve.name]
        arr(name+'q',key.public_bytes(Encoding.X962,PublicFormat.UncompressedPoint))
        pk='{BR_KEYTYPE_EC,{.ec={'+curve+','+name+'q,sizeof('+name+'q)}}}'
    else:raise ValueError('Unsupported CA key')
    anchors.append('{{'+name+'dn,sizeof('+name+'dn)},BR_X509_TA_CA,'+pk+'}')
    manifest.append(p.name+'\n'+cert.subject.rfc4514_string()+'\nSHA256 '+cert.fingerprint(hashes.SHA256()).hex()+'\n')
parts.append('static const br_x509_trust_anchor TRUST_ANCHORS[] = {'+','.join(anchors)+'};')
(root/'anchors.h').write_text('\n'.join(parts)+'\n')
(root/'fingerprints.txt').write_text('\n'.join(manifest))
