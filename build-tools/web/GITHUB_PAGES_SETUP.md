# GitHub Pages Setup for yetty + JSLinux

This guide explains how to deploy yetty with JSLinux terminal to GitHub Pages.

## The Challenge

JSLinux needs the Alpine Linux filesystem from vfsync.org (~1.6GB), but:
- vfsync.org has CORS restrictions (only allows bellard.org)
- GitHub Pages can't run server-side code
- 1.6GB is too large to bundle in the repo

## Solution: Cloudflare Workers Proxy

Use a free Cloudflare Worker to proxy vfsync.org with CORS headers.

### Step 1: Create Cloudflare Worker

1. Create account at https://dash.cloudflare.com
2. Go to **Workers & Pages** → **Create Worker**
3. Paste the contents of `cloudflare-vfsync-proxy.js`
4. Click **Deploy**
5. Note your worker URL (e.g., `https://vfsync-proxy.yourname.workers.dev`)

### Step 2: Update Config

Edit `jslinux/alpine-x86_64.cfg`:

```javascript
{
    version: 1,
    machine: "pc",
    memory_size: 256,
    kernel: "kernel-x86_64.bin",
    cmdline: "loglevel=3 console=hvc0 root=root rootfstype=9p rootflags=trans=virtio ro TZ=${TZ}",
    fs0: { file: "https://vfsync-proxy.yourname.workers.dev/u/os/alpine-x86_64" },
    eth0: { driver: "user" },
}
```

### Step 3: Deploy to GitHub Pages

```bash
# Build yetty for web
cmake --preset webasm-dawn-release
cmake --build build-webasm-dawn-release

# Copy to docs/ for GitHub Pages (or use gh-pages branch)
cp -r build-webasm-dawn-release/* docs/

# Commit and push
git add docs/
git commit -m "Deploy to GitHub Pages"
git push

# Enable GitHub Pages in repo settings -> Pages -> Source: /docs
```

## Adding Demo Files

Demo files can be injected into the VM after it boots using `fs_import_file`.

### Method 1: Via JavaScript

Include `file-import.js` and call:

```javascript
// After VM boots (shows shell prompt)
await importFileToVM(ptyId, '/home/demo.sh', '#!/bin/sh\necho Hello World');

// Or import from URLs
await importFilesFromURLs(ptyId, {
    '/home/demo.sh': '/demo/demo.sh',
    '/usr/local/bin/mytool': '/tools/mytool'
});
```

### Method 2: Via postMessage

Send directly to the VM iframe:

```javascript
const iframe = document.getElementById('jslinux-pty-1');
iframe.contentWindow.postMessage({
    type: 'import-file',
    path: '/home/demo.sh',
    data: new TextEncoder().encode('#!/bin/sh\necho Hello')
}, '*');
```

## Files to Deploy

Minimum files needed:
```
index.html
terminal.html
yetty.js
yetty.wasm
yetty.data
jslinux/
  alpine-x86_64.cfg    # Updated with your proxy URL
  kernel-x86_64.bin
  vm-bridge.html
  term-bridge.js
  x86_64emu-wasm.js
  x86_64emu-wasm.wasm
  file-import.js       # Optional: for demo file injection
demo/                  # Your demo files
tools/                 # Your tools
```

## Cloudflare Worker Limits (Free Tier)

- 100,000 requests/day
- 10ms CPU time per request
- Sufficient for demo/development use

## Troubleshooting

### CORS Errors
- Check worker URL is correct in config
- Verify worker is deployed and running
- Check browser console for specific error

### VM Won't Boot
- Check kernel file exists
- Check config file syntax (no trailing commas in JSON-like format)
- Check browser console for errors

### Files Not Importing
- Wait for shell prompt before importing
- Check path starts with /
- Check file-import.js is loaded
