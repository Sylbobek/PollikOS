"""Bounded x86_64 selftest checkpoint using the existing guest assertions."""
from x86_64_boot import boot,fresh_data
boot('selftest','qemu64',64,['[X64] SELFTEST PASS'],
     data_image=fresh_data('selftest','-ci'),mutable_data=True,suffix='-ci',deadline=900)
