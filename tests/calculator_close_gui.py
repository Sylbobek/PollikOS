"""Legacy runtime reset on close; actual input/model, disposable GUI fixture."""
import time
from gui_metrics import Guest
from gui_fixture import finish_setup
def main():
    with Guest('1024x768','calculator-close',headless=True,boot_only=True) as g:
        g.wait(lambda:'[TEST] PHASE 2 PASS' in g.log.read_text(),'boot stress baseline',120)
        finish_setup(g.qmp,g.log)
        g.key('f8',lambda:g.words('g_focused_window')[0]==7 and g.window(7)[7],'Calculator open')
        for key in ('2','shift-equal','3','ret'):
            g.hmp('sendkey '+key+' 1');time.sleep(.1)
        g.wait(lambda:'[CALC] 5\n' in g.log.read_text(),'calculation',15)
        for cycle in range(5):
            g.key('alt-f4',lambda:not g.window(7)[7],'Calculator close')
            g.key('f8',lambda:g.words('g_focused_window')[0]==7 and g.window(7)[7],'Calculator reopen')
            raw=g.memory(*g.symbol('model'))
            expression=raw[:128].split(b'\0')[0];result=raw[128:224].split(b'\0')[0]
            print(f'RAW close cycle={cycle} expression={expression!r} result={result!r}',flush=True)
            assert expression==b'' and result==b'0','Calculator retained runtime state after close'
        assert 'PANIC' not in g.log.read_text()
        print('PASS legacy Calculator: close clears runtime; five fresh reopen states',flush=True)
if __name__=='__main__':main()
