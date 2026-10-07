"""Real DOM/JS binding regression; optional reconstruction in a copy only."""
from pathlib import Path
import argparse,subprocess
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();group=parser.add_mutually_exclusive_group()
group.add_argument('--before-sequencing',action='store_true');group.add_argument('--before-release',action='store_true');opt=parser.parse_args()
flags=[]
if opt.before_sequencing or opt.before_release:
    original=ROOT/'sdk/apps/browser_js.c';text=original.read_text()
    if opt.before_sequencing:
        text=text.replace('jsval_t body=wrap_node(dom_query_selector(document,"body"));\n    js_set(runtime,doc,"body",body);','js_set(runtime,doc,"body",wrap_node(dom_query_selector(document,"body")));')
        text=text.replace('jsval_t target=wrap_node(node);\n    js_set(runtime,event,"target",target);\n    js_set(runtime,event,"currentTarget",target);','js_set(runtime,event,"target",wrap_node(node));\n    js_set(runtime,event,"currentTarget",wrap_node(node));')
    else:
        text=text.replace('for(unsigned i=0;i<binding_count;i++)if(stale[i]){\n        js_root_release(runtime,&object_roots[i]);js_root_release(runtime,&style_roots[i]);\n        bindings[i].node=0;\n    }','for(unsigned i=0;i<binding_count;i++)if(stale[i])bindings[i].node=0;')
    for rel in ('../../kernel/browser/browser.h','../../third_party/elk/elk.h'):
        text=text.replace('"'+rel+'"','"'+(original.parent/rel).resolve().as_posix()+'"')
    copy=ROOT/'build/browser-js-before-sequencing.c';copy.write_text(text)
    flags=['-DBROWSER_JS_SOURCE="'+copy.as_posix()+'"']
command=['clang','-DPOLLIK_BROWSER_STANDALONE=1','-O2','-g','-Wall','-Wextra','-Werror','-fuse-ld=lld',*flags,
         'tests/browser_js_bindings_native.c','kernel/browser/html_parser.c','kernel/browser/css_engine.c',
         '-o','build/browser_js_bindings_native.exe']
print('COMMAND '+subprocess.list2cmdline(command),flush=True)
subprocess.run(command,cwd=ROOT,check=True)
subprocess.run([str(ROOT/'build/browser_js_bindings_native.exe')],cwd=ROOT,check=True)
