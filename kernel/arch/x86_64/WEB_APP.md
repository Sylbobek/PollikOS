# Native x86_64 browser application

`/bin/browser.pol` is a userspace application. It uses the x86_64 window and
read-only file APIs and reads HTML, stylesheets, and scripts from PollikFS. The
HTML parser, CSS cascade, layout, and renderer are shared with the i386 browser.
The JavaScript runtime is Elk, with `document.getElementById`,
`document.querySelector`, text updates, common inline style properties,
`document.title`, `console.log`, and click listeners wired to the parsed page.
Click events bubble through the DOM and expose their type, target, and pointer
coordinates. Inline color changes are applied to inherited text styles.

Enter a PollikFS path such as `/home/welcome.html`, or an `http://` or
`https://` URL, with
Ctrl+L or F, then press Enter. Arrow keys and the mouse wheel scroll the page.
Local pages can load relative CSS and JavaScript files. Remote pages use the
kernel's nonblocking HTTP API and can load linked stylesheets and scripts in
sequence while the window continues to receive input. JavaScript has a
per-script instruction budget and a bounded runtime arena.

The x86_64 kernel now links the PollikOS RTL8139, IPv4, UDP, DNS, and TCP
components; QEMU verified RTL8139 polling, DHCP, and a browser request that
fetches HTML, linked CSS, and JavaScript from a local HTTP server. The current
HTTP API is GET-only, IPv4, one request at a time, and supports `Content-Length`,
chunked, or connection-close-delimited responses. HTTPS uses a nonblocking
BearSSL TLS 1.2 client, the built-in trust anchors, secure RDRAND entropy, and
the RTC date for certificate checks. Page bodies are capped at 96 KiB;
stylesheets and scripts are bounded by their browser runtime limits. Redirects,
forms, cookies, TLS 1.3, and general userspace sockets are not implemented yet.
Page execution stays inside PollikOS and is never delegated to the host.
The HTML, CSS, and JavaScript support is a practical subset; this is not yet a
full standards-compliant browser engine.
