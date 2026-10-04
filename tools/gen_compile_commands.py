#!/usr/bin/env python3
import json
import glob
import os

def generate():
    root = os.path.abspath(os.path.dirname(os.path.dirname(__file__)))
    entries = []

    # Kernel modules
    kernel_modules = [
        'kernel','desktop','compositor','graphics','gfx_device','soft3d',
        'input_dispatch','wm','hw','mem','pmm','vmm','klog','ahci','storage',
        'pollikfs','vfs','process','syscall','elf','network','framebuffer',
        'ui','trash','ui_animation','desktop_items','auth','installer'
    ]
    for m in kernel_modules:
        entries.append({
            'directory': root,
            'file': os.path.join(root, 'kernel', f'{m}.c'),
            'arguments': [
                'clang', '--target=i386-none-elf', '-m32', '-march=i386',
                '-ffreestanding', '-fno-pic', '-fno-pie', '-fno-stack-protector',
                '-mno-sse', '-mno-mmx', '-Os', '-Wall', '-Wextra',
                '-Ikernel/include', '-Ikernel', '-c', f'kernel/{m}.c'
            ]
        })

    # Network modules
    net_modules = ['net_util','rtl8139','wifi_if','arp','ipv4','icmp','udp','dhcp','dns','tcp','tls','http','net_manager']
    for m in net_modules:
        entries.append({
            'directory': root,
            'file': os.path.join(root, 'kernel', 'net', f'{m}.c'),
            'arguments': [
                'clang', '--target=i386-none-elf', '-m32', '-march=i386',
                '-ffreestanding', '-fno-pic', '-fno-pie', '-fno-stack-protector',
                '-mno-sse', '-mno-mmx', '-Os', '-Wall', '-Wextra',
                '-Ikernel/include', '-Ikernel', '-c', f'kernel/net/{m}.c'
            ]
        })

    # Browser modules
    browser_modules = ['browser_app','html_parser','css_engine','layout','render','js_engine','js_compat','images']
    for m in browser_modules:
        entries.append({
            'directory': root,
            'file': os.path.join(root, 'kernel', 'browser', f'{m}.c'),
            'arguments': [
                'clang', '--target=i386-none-elf', '-m32', '-march=i386',
                '-ffreestanding', '-fno-pic', '-fno-pie', '-fno-stack-protector',
                '-mno-sse', '-mno-mmx', '-Os', '-Wall', '-Wextra',
                '-Ikernel/include', '-Ikernel', '-c', f'kernel/browser/{m}.c'
            ]
        })

    # GUI modules
    gui_modules = ['apps','app_edit','welcome','files','notes','terminal','settings','browser_client','pollikmark']
    for m in gui_modules:
        entries.append({
            'directory': root,
            'file': os.path.join(root, 'kernel', 'gui', f'{m}.c'),
            'arguments': [
                'clang', '--target=i386-none-elf', '-m32', '-march=i386',
                '-ffreestanding', '-fno-pic', '-fno-pie', '-fno-stack-protector',
                '-mno-sse', '-mno-mmx', '-Os', '-Wall', '-Wextra',
                '-Ikernel/include', '-Ikernel', '-c', f'kernel/gui/{m}.c'
            ]
        })

    # Third-party Elk
    entries.append({
        'directory': root,
        'file': os.path.join(root, 'third_party', 'elk', 'elk.c'),
        'arguments': [
            'clang', '--target=i386-none-elf', '-march=i386', '-ffreestanding',
            '-fno-pic', '-fno-stack-protector', '-mno-sse', '-mno-mmx', '-Os',
            '-DJS_OPT', '-Ikernel/include', '-c', 'third_party/elk/elk.c'
        ]
    })

    # BearSSL
    for f in glob.glob(os.path.join(root, 'third_party', 'bearssl', 'src', '**', '*.c'), recursive=True):
        rel = os.path.relpath(f, root).replace('\\', '/')
        entries.append({
            'directory': root,
            'file': f,
            'arguments': [
                'clang', '--target=i386-none-elf', '-march=i386', '-ffreestanding',
                '-fno-pic', '-fno-stack-protector', '-mno-sse', '-mno-mmx', '-Os',
                '-DBR_AES_X86NI=0', '-DBR_SSE2=0', '-DBR_RDRAND=0', '-DBR_USE_URANDOM=0',
                '-DBR_USE_WIN32_RAND=0', '-DBR_USE_UNIX_TIME=0', '-DBR_USE_WIN32_TIME=0',
                '-Ikernel/include', '-Ithird_party/bearssl/inc', '-Ithird_party/bearssl/src',
                '-c', rel
            ]
        })

    # Userspace apps
    for app in ['hello', 'fault_test', 'fault_kernel', 'fault_stack']:
        entries.append({
            'directory': root,
            'file': os.path.join(root, 'apps', app, f'{app}.c'),
            'arguments': [
                'clang', '--target=i386-none-elf', '-m32', '-march=i386',
                '-ffreestanding', '-fno-pic', '-fno-pie', '-fno-stack-protector',
                '-mno-sse', '-mno-mmx', '-O2', '-Iinclude', '-c', f'apps/{app}/{app}.c'
            ]
        })

    out_path = os.path.join(root, 'compile_commands.json')
    with open(out_path, 'w', encoding='utf-8') as f:
        json.dump(entries, f, indent=2)
    print(f'Generated compile_commands.json with {len(entries)} entries at {out_path}')

if __name__ == '__main__':
    generate()
