#ifndef POLLIK_BROWSER_START_H
#define POLLIK_BROWSER_START_H
static const char browser_start_page[]=
"<!doctype html><html><head><meta charset='utf-8'><title>Pollik Web</title><style>"
"body{margin:0;background:#f5f7fb;color:#202b43;font-family:sans-serif;}"
".home{padding:40px 36px;max-width:800px;margin:0 auto;}"
"h1{font-size:40px;color:#202b43;margin:24px 0 12px;}"
"p{font-size:18px;color:#62718b;margin:12px 0;}"
".brand{color:#6e58c8;font-size:18px;}"
"input{width:600px;height:42px;padding:8px;border:1px solid #c7cfe0;background:white;font-size:18px;}"
"button{height:42px;padding:8px 18px;background:#6e58c8;color:white;border:1px solid #6e58c8;}"
".links{display:flex;gap:18px;margin:28px 0;}"
".tile{width:200px;padding:18px;background:white;border:1px solid #dce2ee;}"
"a{color:#5c47b1;font-size:18px;}small{color:#6c7992;}"
"</style></head><body><main class='home'><p class='brand'>POLLIK WEB</p>"
"<h1>A new place to explore.</h1><p>Search the web, visit a website, or open your bookmarks.</p>"
"<form action='https://lite.duckduckgo.com/lite/' method='get'><input name='q' type='text' placeholder='Search the web'>"
"<button type='submit'>Search</button></form><div class='links'>"
"<div class='tile'><a href='https://example.org'>Example</a><p>A simple place to start.</p></div>"
"<div class='tile'><a href='https://en.wikipedia.org/wiki/Main_Page'>Wikipedia</a><p>Learn something new.</p></div>"
"<div class='tile'><a href='https://news.ycombinator.com'>Hacker News</a><p>Technology and ideas.</p></div>"
"</div><small>Ctrl+L address | Ctrl+D bookmark | Ctrl+S save page | Alt+Left back</small></main></body></html>";
#endif
